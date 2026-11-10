#include "pnpch.h"
#include "Penumbra/Automation/McpServer.h"

#include "Penumbra/Automation/CommandSchema.h"
#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Core/UUID.h"

#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <iostream>

#ifndef PN_VERSION_STRING
	#define PN_VERSION_STRING "0.0.0"
#endif

namespace Penumbra {

	using json = nlohmann::json;

	namespace {

		constexpr const char* LatestProtocolVersion = "2025-11-25";
		constexpr std::array SupportedProtocolVersions = { "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05" };

		// JSON-RPC 2.0 / MCP error codes.
		constexpr int ParseError = -32700;
		constexpr int InvalidRequest = -32600;
		constexpr int MethodNotFound = -32601;
		constexpr int InvalidParams = -32602;
		constexpr int InternalError = -32603;
		constexpr int ResourceNotFound = -32002;

		constexpr const char* BackendTool = "session_backend";
		constexpr const char* ComponentsUri = "penumbra://components";
		constexpr const char* SceneUri = "penumbra://scene";
		constexpr const char* DocsUriPrefix = "penumbra://docs/";
		constexpr std::array DocFiles = { "Automation.md", "ScriptingAPI.md", "Components.md", "Perception.md" };

		// Commands that never change anything: annotated readOnlyHint so clients can auto-approve them.
		const std::unordered_set<std::string> ReadOnlyCommands = { "log.tail", "project.info", "scene.get", "scene.list", "entity.get",
			"component.list", "asset.list", "asset.read", "script.errors" };

		constexpr const char* Instructions =
			"Penumbra game engine automation. Tools mirror the JSON command API (penumbra://docs/Automation.md): entity ids are "
			"decimal strings, asset paths are relative to the project's asset folder, rotations are Euler degrees. Calls run in the "
			"open PenumbraEditor when it listens on its command port (changes appear live and are undoable there), otherwise in a "
			"headless in-process engine; session_backend shows or switches this. Typical loop: project_open or project_new, "
			"component_list, entity_create / prefab_instantiate, asset_write for Lua gameplay (penumbra://docs/ScriptingAPI.md), "
			"play + simulate or test_run to verify, render_screenshot to look at the result, scene_save.";

		struct RpcError
		{
			int Code;
			std::string Message;
		};

		std::string Dump(const json& value, int indent = -1) { return value.dump(indent, ' ', false, json::error_handler_t::replace); }

		std::string Base64Encode(const std::vector<uint8_t>& data)
		{
			static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string encoded;
			encoded.reserve((data.size() + 2) / 3 * 4);
			size_t i = 0;
			for (; i + 2 < data.size(); i += 3)
			{
				const uint32_t triple = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
				encoded += Alphabet[(triple >> 18) & 63];
				encoded += Alphabet[(triple >> 12) & 63];
				encoded += Alphabet[(triple >> 6) & 63];
				encoded += Alphabet[triple & 63];
			}
			if (const size_t remaining = data.size() - i; remaining > 0)
			{
				const uint32_t triple = (uint32_t(data[i]) << 16) | (remaining == 2 ? uint32_t(data[i + 1]) << 8 : 0u);
				encoded += Alphabet[(triple >> 18) & 63];
				encoded += Alphabet[(triple >> 12) & 63];
				encoded += remaining == 2 ? Alphabet[(triple >> 6) & 63] : '=';
				encoded += '=';
			}
			return encoded;
		}

		json TextContent(std::string text) { return { { "type", "text" }, { "text", std::move(text) } }; }

		json ToolError(const std::string& message) { return { { "content", json::array({ TextContent(message) }) }, { "isError", true } }; }

		// Converts an automation response ({ok, result|error}) into a tools/call result.
		json ToolResult(const json& response)
		{
			if (!response.value("ok", false))
				return ToolError(response.value("error", std::string("command failed")));
			const json& result = response.contains("result") ? response["result"] : json();
			// Plain strings (asset.read) are returned verbatim so scripts stay readable.
			return { { "content", json::array({ TextContent(result.is_string() ? result.get<std::string>() : Dump(result)) }) }, { "isError", false } };
		}

		std::filesystem::path FindDocsDirectory()
		{
			std::filesystem::path directory = FileSystem::GetExecutableDirectory();
			while (!directory.empty())
			{
				std::error_code error;
				if (std::filesystem::exists(directory / "docs" / "ScriptingAPI.md", error))
					return directory / "docs";
				const std::filesystem::path parent = directory.parent_path();
				if (parent == directory)
					break;
				directory = parent;
			}
			return {};
		}

	}

	std::optional<McpBackend> McpBackendFromString(std::string_view name)
	{
		if (name == "auto")
			return McpBackend::Auto;
		if (name == "headless")
			return McpBackend::Headless;
		if (name == "editor")
			return McpBackend::Editor;
		return std::nullopt;
	}

	const char* McpBackendToString(McpBackend backend)
	{
		switch (backend)
		{
			case McpBackend::Auto: return "auto";
			case McpBackend::Headless: return "headless";
			case McpBackend::Editor: return "editor";
		}
		return "auto";
	}

	McpServer::McpServer(CommandProcessor& headless, McpServerSpecification specification)
		: m_Headless(headless), m_Specification(std::move(specification)), m_Active(m_Specification.Backend),
		  m_Editor(m_Specification.EditorHost, m_Specification.EditorPort)
	{
		for (const auto& [name, info] : m_Headless.GetCommands())
		{
			if (name != "help") // tools/list already is the command list
				m_ToolToCommand[ToolNameForCommand(name)] = name;
		}

		const std::filesystem::path docs = m_Specification.DocsDirectory.empty() ? FindDocsDirectory() : m_Specification.DocsDirectory;
		for (const char* file : DocFiles)
		{
			std::error_code error;
			if (!docs.empty() && std::filesystem::exists(docs / file, error))
				m_Docs[std::string(DocsUriPrefix) + file] = docs / file;
		}
	}

	std::string McpServer::ToolNameForCommand(std::string_view command)
	{
		// Tool names must match ^[a-zA-Z0-9_-]{1,64}$ for Claude: "entity.create" -> "entity_create".
		std::string name(command);
		std::replace(name.begin(), name.end(), '.', '_');
		return name;
	}

	// ------------------------------------------------------------------------------------------
	// JSON-RPC
	// ------------------------------------------------------------------------------------------

	std::string McpServer::HandleLine(std::string_view line)
	{
		const json message = json::parse(line, nullptr, false);
		if (message.is_discarded())
			return Dump({ { "jsonrpc", "2.0" }, { "id", nullptr }, { "error", { { "code", ParseError }, { "message", "Parse error: invalid JSON" } } } });
		const std::optional<json> response = HandleMessage(message);
		return response ? Dump(*response) : std::string();
	}

	std::optional<json> McpServer::HandleMessage(const json& message)
	{
		auto errorResponse = [](const json& id, int code, const std::string& text) {
			return json{ { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", text } } } };
		};

		if (message.is_array())
		{
			if (message.empty())
				return errorResponse(nullptr, InvalidRequest, "Invalid Request: empty batch");
			json responses = json::array();
			for (const json& item : message)
			{
				if (std::optional<json> response = HandleMessage(item))
					responses.push_back(std::move(*response));
			}
			return responses.empty() ? std::nullopt : std::optional<json>(std::move(responses));
		}

		if (!message.is_object())
			return errorResponse(nullptr, InvalidRequest, "Invalid Request: expected a JSON object");
		const auto idIt = message.find("id");
		const bool isNotification = idIt == message.end();
		const json id = isNotification ? json() : *idIt;
		const auto methodIt = message.find("method");
		if (methodIt == message.end())
		{
			// A response to a server-initiated request (this server sends none) or garbage.
			if (message.contains("result") || message.contains("error"))
				return std::nullopt;
			return errorResponse(id, InvalidRequest, "Invalid Request: missing 'method'");
		}
		if (!methodIt->is_string() || (!isNotification && !id.is_string() && !id.is_number_integer()))
			return isNotification ? std::nullopt : std::optional<json>(errorResponse(id, InvalidRequest, "Invalid Request"));

		const json params = message.contains("params") ? message["params"] : json::object();
		try
		{
			json result = HandleRequest(methodIt->get<std::string>(), params);
			if (isNotification)
				return std::nullopt;
			return json{ { "jsonrpc", "2.0" }, { "id", id }, { "result", std::move(result) } };
		}
		catch (const RpcError& error)
		{
			if (isNotification)
				return std::nullopt;
			return errorResponse(id, error.Code, error.Message);
		}
		catch (const std::exception& exception)
		{
			PN_CORE_ERROR("MCP: {} failed: {}", methodIt->get<std::string>(), exception.what());
			if (isNotification)
				return std::nullopt;
			return errorResponse(id, InternalError, std::format("Internal error: {}", exception.what()));
		}
	}

	json McpServer::HandleRequest(const std::string& method, const json& params)
	{
		if (!params.is_object())
			throw RpcError{ InvalidParams, "params must be an object" };
		if (method == "initialize")
			return Initialize(params);
		if (method == "ping")
			return json::object();
		if (method == "tools/list")
			return ListTools();
		if (method == "tools/call")
			return CallTool(params);
		if (method == "resources/list")
			return ListResources();
		if (method == "resources/read")
			return ReadResource(params);
		if (method == "resources/templates/list")
			return { { "resourceTemplates", json::array() } };
		if (method.starts_with("notifications/"))
			return json::object(); // initialized, cancelled, ...: nothing to do
		throw RpcError{ MethodNotFound, std::format("Method not found: {}", method) };
	}

	json McpServer::Initialize(const json& params)
	{
		const std::string requested = params.value("protocolVersion", std::string());
		const bool supported = std::find(SupportedProtocolVersions.begin(), SupportedProtocolVersions.end(), requested) != SupportedProtocolVersions.end();
		PN_CORE_INFO("MCP: client '{}' connected (protocol {}), backend {}",
			params.contains("clientInfo") ? params["clientInfo"].value("name", std::string("?")) : std::string("?"), requested, DescribeBackend());
		return {
			{ "protocolVersion", supported ? requested : std::string(LatestProtocolVersion) },
			{ "capabilities", { { "tools", { { "listChanged", false } } }, { "resources", { { "subscribe", false }, { "listChanged", false } } } } },
			{ "serverInfo", { { "name", "penumbra" }, { "title", "Penumbra Engine" }, { "version", PN_VERSION_STRING } } },
			{ "instructions", Instructions },
		};
	}

	// ------------------------------------------------------------------------------------------
	// Tools
	// ------------------------------------------------------------------------------------------

	json McpServer::ListTools() const
	{
		json tools = json::array();
		const auto& commands = m_Headless.GetCommands();
		for (const auto& [toolName, command] : m_ToolToCommand)
		{
			const CommandProcessor::CommandInfo& info = commands.at(command);
			json schema = BuildCommandInputSchema(info.Arguments);
			std::string description = info.Description;
			if (command == "render.screenshot")
			{
				// Over MCP the image is returned directly, so a file path is optional.
				schema["properties"]["path"]["description"] = "Also save the PNG here (absolute, or relative to the MCP server's working directory). "
															  "Omit to only receive the image.";
				schema.erase("required");
				description += " Returns the image.";
			}
			json tool = { { "name", toolName }, { "title", command }, { "description", description }, { "inputSchema", std::move(schema) } };
			if (ReadOnlyCommands.contains(command))
				tool["annotations"] = { { "readOnlyHint", true } };
			tools.push_back(std::move(tool));
		}

		tools.push_back({ { "name", BackendTool }, { "title", "session.backend" },
			{ "description",
				"Show or switch where tools run: 'editor' = the running PenumbraEditor (changes appear live), 'headless' = an in-process "
				"engine without a window, 'auto' = the editor if it is running, otherwise headless. Call without arguments to see the current one." },
			{ "inputSchema",
				{ { "type", "object" },
					{ "properties", { { "mode", { { "type", "string" }, { "enum", json::array({ "auto", "headless", "editor" }) } } } } } } } });
		return { { "tools", std::move(tools) } };
	}

	json McpServer::CallTool(const json& params)
	{
		const auto nameIt = params.find("name");
		if (nameIt == params.end() || !nameIt->is_string())
			throw RpcError{ InvalidParams, "tools/call needs a string 'name'" };
		const std::string name = nameIt->get<std::string>();
		const json arguments = params.contains("arguments") && !params["arguments"].is_null() ? params["arguments"] : json::object();
		if (!arguments.is_object())
			throw RpcError{ InvalidParams, "'arguments' must be an object" };

		if (name == BackendTool)
			return CallBackendTool(arguments);

		const auto it = m_ToolToCommand.find(name);
		if (it == m_ToolToCommand.end())
			throw RpcError{ InvalidParams, std::format("Unknown tool: {}", name) };

		json request = arguments;
		request["cmd"] = it->second;
		if (it->second == "render.screenshot")
			return CallScreenshot(std::move(request));
		return ToolResult(Execute(request));
	}

	json McpServer::CallBackendTool(const json& arguments)
	{
		if (const auto modeIt = arguments.find("mode"); modeIt != arguments.end() && !modeIt->is_null())
		{
			const std::optional<McpBackend> mode = modeIt->is_string() ? McpBackendFromString(modeIt->get<std::string>()) : std::nullopt;
			if (!mode)
				return ToolError("mode must be 'auto', 'headless' or 'editor'");
			if (*mode == McpBackend::Editor && !m_Editor.IsReachable())
				return ToolError(std::format("no editor is listening on {}:{}: start PenumbraEditor (its command server is on by default)",
					m_Editor.GetHost(), m_Editor.GetPort()));
			m_Active = *mode;
			PN_CORE_INFO("MCP: backend set to {}", McpBackendToString(m_Active));
		}
		ResolveBackend(); // report where the next call runs, re-probing the editor for "auto"
		const json status = { { "backend", McpBackendToString(m_Active) }, { "description", DescribeBackend() },
			{ "editor", std::format("{}:{}", m_Editor.GetHost(), m_Editor.GetPort()) } };
		return { { "content", json::array({ TextContent(Dump(status)) }) }, { "isError", false } };
	}

	json McpServer::CallScreenshot(json request)
	{
		// The editor runs in its own working directory: always hand it an absolute path.
		bool temporary = false;
		std::error_code error;
		if (!request.contains("path") || !request["path"].is_string() || request["path"].get<std::string>().empty())
		{
			const std::filesystem::path directory = std::filesystem::temp_directory_path(error) / "PenumbraMcp";
			std::filesystem::create_directories(directory, error);
			request["path"] = (directory / std::format("screenshot-{}.png", static_cast<uint64_t>(UUID()))).string();
			temporary = true;
		}
		else
		{
			request["path"] = std::filesystem::absolute(request["path"].get<std::string>(), error).string();
		}

		const json response = Execute(request);
		if (!response.value("ok", false))
			return ToolResult(response);
		json result = response.value("result", json::object());
		const std::filesystem::path file = result.value("path", request["path"].get<std::string>());
		const std::optional<std::vector<uint8_t>> png = FileSystem::ReadBinaryFile(file);
		if (temporary)
		{
			std::filesystem::remove(file, error);
			result.erase("path");
		}
		if (!png)
			return ToolError(std::format("the screenshot was rendered but '{}' cannot be read", file.string()));
		return { { "content",
					 json::array({ { { "type", "image" }, { "data", Base64Encode(*png) }, { "mimeType", "image/png" } }, TextContent(Dump(result)) }) },
			{ "isError", false } };
	}

	// ------------------------------------------------------------------------------------------
	// Resources
	// ------------------------------------------------------------------------------------------

	json McpServer::ListResources() const
	{
		json resources = json::array();
		resources.push_back({ { "uri", ComponentsUri }, { "name", "components" }, { "title", "Component types" },
			{ "description", "Every component type with its fields and default values (component.list)." }, { "mimeType", "application/json" } });
		resources.push_back({ { "uri", SceneUri }, { "name", "scene" }, { "title", "Open scene" },
			{ "description", "The active scene as a scene document (scene.get)." }, { "mimeType", "application/json" } });
		for (const auto& [uri, path] : m_Docs)
		{
			const std::string file = path.filename().string();
			resources.push_back({ { "uri", uri }, { "name", file }, { "title", std::format("Penumbra docs: {}", file) }, { "mimeType", "text/markdown" } });
		}
		return { { "resources", std::move(resources) } };
	}

	json McpServer::ReadResource(const json& params)
	{
		const std::string uri = params.value("uri", std::string());
		auto contents = [&uri](const char* mimeType, std::string text) {
			return json{ { "contents", json::array({ { { "uri", uri }, { "mimeType", mimeType }, { "text", std::move(text) } } }) } };
		};

		if (uri == ComponentsUri || uri == SceneUri)
		{
			const json response = Execute({ { "cmd", uri == ComponentsUri ? "component.list" : "scene.get" } });
			if (!response.value("ok", false))
				throw RpcError{ InternalError, response.value("error", std::string("command failed")) };
			return contents("application/json", Dump(response["result"], 1));
		}
		if (const auto it = m_Docs.find(uri); it != m_Docs.end())
		{
			const std::optional<std::string> text = FileSystem::ReadTextFile(it->second);
			if (!text)
				throw RpcError{ InternalError, std::format("cannot read {}", it->second.string()) };
			return contents("text/markdown", *text);
		}
		throw RpcError{ ResourceNotFound, std::format("Resource not found: {}", uri) };
	}

	// ------------------------------------------------------------------------------------------
	// Backends
	// ------------------------------------------------------------------------------------------

	McpBackend McpServer::ResolveBackend()
	{
		if (m_Active == McpBackend::Auto)
		{
			m_Active = m_Editor.IsReachable() ? McpBackend::Editor : McpBackend::Headless;
			PN_CORE_INFO("MCP: using {}", DescribeBackend());
		}
		return m_Active;
	}

	std::string McpServer::DescribeBackend() const
	{
		switch (m_Active)
		{
			case McpBackend::Auto: return "auto (the editor if it is running, otherwise headless)";
			case McpBackend::Headless: return "headless in-process engine";
			case McpBackend::Editor: return std::format("editor at {}:{}", m_Editor.GetHost(), m_Editor.GetPort());
		}
		return {};
	}

	json McpServer::Execute(const json& request)
	{
		if (ResolveBackend() == McpBackend::Headless)
			return m_Headless.Execute(request);
		std::string error;
		std::optional<json> response = m_Editor.Send(request, error);
		if (!response)
			return { { "ok", false }, { "error", error + ". Start the editor, or call session_backend with mode 'headless'." } };
		return std::move(*response);
	}

	// ------------------------------------------------------------------------------------------
	// Transport
	// ------------------------------------------------------------------------------------------

	void McpServer::Run(std::istream& input, const std::function<void(std::string_view)>& write)
	{
		std::string line;
		while (std::getline(input, line))
		{
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.find_first_not_of(" \t") == std::string::npos)
				continue;
			const std::string response = HandleLine(line);
			if (!response.empty())
				write(response);
		}
	}

	int McpServer::ReserveStdout()
	{
		std::fflush(stdout);
		const int protocol = _dup(_fileno(stdout));
		if (protocol < 0)
			return -1;
		_dup2(_fileno(stderr), _fileno(stdout));
		SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE));
		_setmode(protocol, _O_BINARY);
		return protocol;
	}

	int McpServer::RunStdio(McpServer& server, int protocol)
	{
		if (protocol < 0)
		{
			PN_CORE_CRITICAL("MCP: stdout is not available for the protocol");
			return 1;
		}
		_setmode(_fileno(stdin), _O_BINARY);

		PN_CORE_INFO("MCP server ready on stdio (backend {})", server.DescribeBackend());
		bool connected = true;
		server.Run(std::cin, [&connected, protocol](std::string_view message) {
			if (!connected)
				return;
			std::string line(message);
			line += '\n';
			size_t offset = 0;
			while (offset < line.size())
			{
				const int written = _write(protocol, line.data() + offset, static_cast<unsigned>(std::min<size_t>(line.size() - offset, 1 << 20)));
				if (written <= 0)
				{
					connected = false; // client went away; keep draining stdin until it closes
					return;
				}
				offset += static_cast<size_t>(written);
			}
		});
		_close(protocol);
		PN_CORE_INFO("MCP server: stdin closed, exiting");
		return 0;
	}

}
