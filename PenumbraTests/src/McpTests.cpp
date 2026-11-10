#include "TestHelpers.h"

#include "Penumbra/Automation/CommandSchema.h"
#include "Penumbra/Automation/CommandServer.h"
#include "Penumbra/Automation/McpServer.h"
#include "Penumbra/Scene/Scene.h"

#include <regex>
#include <set>
#include <thread>

using namespace Penumbra;
using json = nlohmann::json;

namespace {

	McpServerSpecification HeadlessSpecification()
	{
		McpServerSpecification specification;
		specification.Backend = McpBackend::Headless;
		specification.DocsDirectory = PENUMBRA_DOCS_DIR;
		return specification;
	}

	// Drives an McpServer the way a client does, one JSON-RPC message at a time.
	class McpClient
	{
	public:
		explicit McpClient(McpServer& server) : m_Server(server) {}

		json Request(const std::string& method, const json& params = json::object())
		{
			const json message = { { "jsonrpc", "2.0" }, { "id", m_NextId++ }, { "method", method }, { "params", params } };
			const std::optional<json> response = m_Server.HandleMessage(message);
			REQUIRE(response.has_value());
			CHECK((*response)["jsonrpc"] == "2.0");
			CHECK((*response)["id"] == message["id"]);
			return *response;
		}

		json Result(const std::string& method, const json& params = json::object())
		{
			const json response = Request(method, params);
			INFO(response.dump());
			REQUIRE(response.contains("result"));
			return response["result"];
		}

		int ErrorCode(const std::string& method, const json& params = json::object())
		{
			const json response = Request(method, params);
			INFO(response.dump());
			REQUIRE(response.contains("error"));
			return response["error"]["code"].get<int>();
		}

		json Call(const std::string& tool, const json& arguments = json::object())
		{
			return Result("tools/call", { { "name", tool }, { "arguments", arguments } });
		}

		// Calls a tool that must succeed and returns its text output.
		std::string CallOk(const std::string& tool, const json& arguments = json::object())
		{
			const json result = Call(tool, arguments);
			INFO(tool << " " << result.dump());
			REQUIRE(result["isError"] == false);
			return Text(result);
		}

		// Calls a tool that must fail and returns the error text.
		std::string CallError(const std::string& tool, const json& arguments = json::object())
		{
			const json result = Call(tool, arguments);
			INFO(tool << " " << result.dump());
			REQUIRE(result["isError"] == true);
			return Text(result);
		}

		static std::string Text(const json& result)
		{
			for (const json& item : result["content"])
			{
				if (item["type"] == "text")
					return item["text"].get<std::string>();
			}
			return {};
		}

	private:
		McpServer& m_Server;
		int m_NextId = 1;
	};

	struct McpFixture
	{
		std::filesystem::path Directory = std::filesystem::temp_directory_path() / "PenumbraTests" / std::to_string(static_cast<uint64_t>(UUID()));
		HeadlessAutomationHost Host;
		CommandProcessor Commands{ Host };
		McpServer Server{ Commands, HeadlessSpecification() };
		McpClient Client{ Server };

		McpFixture() { Host.SetUseWarp(true); }

		~McpFixture()
		{
			Host.StopPlay();
			Project::Unload();
			std::error_code ignored;
			std::filesystem::remove_all(Directory, ignored);
		}
	};

	std::vector<uint8_t> Base64Decode(const std::string& text)
	{
		auto value = [](char c) -> int {
			if (c >= 'A' && c <= 'Z')
				return c - 'A';
			if (c >= 'a' && c <= 'z')
				return c - 'a' + 26;
			if (c >= '0' && c <= '9')
				return c - '0' + 52;
			return c == '+' ? 62 : c == '/' ? 63 : -1;
		};
		std::vector<uint8_t> bytes;
		uint32_t buffer = 0;
		int bits = 0;
		for (const char c : text)
		{
			const int v = value(c);
			if (v < 0)
				break; // padding
			buffer = (buffer << 6) | static_cast<uint32_t>(v);
			bits += 6;
			if (bits >= 8)
			{
				bits -= 8;
				bytes.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
			}
		}
		return bytes;
	}

	// Starts a command server on a free loopback port and serves it from a background thread,
	// standing in for a running editor.
	class FakeEditor
	{
	public:
		FakeEditor()
		{
			for (uint16_t port = 47731; port < 47771 && !m_Server.IsRunning(); port++)
				m_Server.Start(port);
			REQUIRE(m_Server.IsRunning());
			m_Pump = std::thread([this] {
				while (!m_Stop)
				{
					m_Server.ProcessPending([this](const json& request) { return Commands.Execute(request); });
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			});
		}

		~FakeEditor() { Stop(); }

		void Stop()
		{
			m_Stop = true;
			if (m_Pump.joinable())
				m_Pump.join();
			m_Server.Stop();
		}

		uint16_t GetPort() const { return m_Server.GetPort(); }

		HeadlessAutomationHost Host;
		CommandProcessor Commands{ Host };

	private:
		CommandServer m_Server;
		std::atomic<bool> m_Stop = false;
		std::thread m_Pump;
	};

}

TEST_SUITE("Mcp")
{
	TEST_CASE("Command argument strings are parsed")
	{
		CHECK(ParseCommandArguments("").empty());

		const std::vector<CommandArgument> arguments = ParseCommandArguments("id|name, parent|parentName (null to unparent), keepWorld?");
		REQUIRE(arguments.size() == 5);
		CHECK(arguments[0].Name == "id");
		CHECK_FALSE(arguments[0].Required);
		CHECK(arguments[0].Alternatives == std::vector<std::string>{ "name" });
		CHECK(arguments[3].Name == "parentName");
		CHECK(arguments[3].Note == "null to unparent");
		CHECK(arguments[4].Name == "keepWorld");
		CHECK_FALSE(arguments[4].Required);

		const std::vector<CommandArgument> screenshot = ParseCommandArguments("path, width?, height?");
		REQUIRE(screenshot.size() == 3);
		CHECK(screenshot[0].Required);
		CHECK_FALSE(screenshot[1].Required);

		const json schema = BuildCommandInputSchema("path, width?, position?");
		CHECK(schema["type"] == "object");
		CHECK(schema["required"] == json::array({ "path" }));
		CHECK(schema["properties"]["width"]["type"] == "integer");
		CHECK(schema["properties"]["position"]["type"] == "array");
		CHECK(schema["properties"]["position"]["minItems"] == 3);
	}

	TEST_CASE("Every command argument has a typed schema")
	{
		HeadlessAutomationHost host;
		CommandProcessor commands(host);
		for (const auto& [name, info] : commands.GetCommands())
		{
			for (const CommandArgument& argument : ParseCommandArguments(info.Arguments))
				CHECK_MESSAGE(IsKnownCommandArgument(argument.Name), name << ": add '" << argument.Name << "' to CommandSchema.cpp");
		}
	}

	TEST_CASE("Initialize negotiates the protocol version")
	{
		McpFixture f;
		const json result = f.Client.Result("initialize",
			{ { "protocolVersion", "2025-06-18" }, { "capabilities", json::object() }, { "clientInfo", { { "name", "doctest" }, { "version", "1" } } } });
		CHECK(result["protocolVersion"] == "2025-06-18");
		CHECK(result["serverInfo"]["name"] == "penumbra");
		CHECK(result["capabilities"].contains("tools"));
		CHECK(result["capabilities"].contains("resources"));
		CHECK(result["instructions"].is_string());

		// An unknown version is answered with the latest one the server speaks.
		CHECK(f.Client.Result("initialize", { { "protocolVersion", "1999-01-01" } })["protocolVersion"] == "2025-11-25");
		CHECK(f.Client.Result("ping").empty());
	}

	TEST_CASE("Tools are derived from the command table")
	{
		McpFixture f;
		const json tools = f.Client.Result("tools/list")["tools"];
		std::map<std::string, json> byName;
		const std::regex validName("^[a-zA-Z0-9_-]{1,64}$");
		for (const json& tool : tools)
		{
			const std::string name = tool["name"].get<std::string>();
			CHECK_MESSAGE(std::regex_match(name, validName), name);
			CHECK(tool["inputSchema"]["type"] == "object");
			CHECK(tool["description"].is_string());
			byName[name] = tool;
		}

		for (const auto& [command, info] : f.Commands.GetCommands())
		{
			if (command != "help")
				CHECK_MESSAGE(byName.contains(McpServer::ToolNameForCommand(command)), command);
		}
		CHECK(byName.contains("session_backend"));

		const json& create = byName.at("entity_create")["inputSchema"];
		CHECK(create["properties"]["components"]["type"] == "object");
		CHECK(create["properties"]["parent"]["type"] == json::array({ "string", "null" }));
		CHECK_FALSE(create.contains("required"));
		CHECK(byName.at("project_new")["inputSchema"]["required"] == json::array({ "path", "name" }));
		CHECK(byName.at("entity_get")["inputSchema"]["properties"]["id"]["description"].get<std::string>().find("Alternative to name") != std::string::npos);
		// Over MCP the screenshot comes back as an image, so the file path is optional.
		CHECK_FALSE(byName.at("render_screenshot")["inputSchema"].contains("required"));
		CHECK(byName.at("scene_list")["annotations"]["readOnlyHint"] == true);
		CHECK_FALSE(byName.at("entity_destroy").contains("annotations"));
	}

	TEST_CASE("Tool calls run commands and report failures as tool errors")
	{
		McpFixture f;
		f.Client.CallOk("project_new", { { "path", (f.Directory / "Game").string() }, { "name", "Mcp Game" } });
		const json created = json::parse(f.Client.CallOk("entity_create",
			{ { "name", "Torch" }, { "components", { { "PointLight", { { "Intensity", 3.0 } } } } } }));
		REQUIRE(created.contains("id"));

		const json described = json::parse(f.Client.CallOk("entity_get", { { "name", "Torch" } }));
		CHECK(described["id"] == created["id"]);
		CHECK(described["components"].contains("PointLight"));
		CHECK(f.Host.GetEditScene()->FindEntityByName("Torch"));

		// Command failures are tool results with isError, so the model sees the message.
		CHECK(f.Client.CallError("entity_get", { { "name", "Missing" } }).find("no entity named 'Missing'") != std::string::npos);
		CHECK(f.Client.CallError("entity_set", { { "name", "Torch" }, { "components", { { "NoSuchComponent", json::object() } } } }).find("NoSuchComponent") !=
			std::string::npos);

		// Text assets come back verbatim rather than as a JSON string.
		f.Client.CallOk("asset_write", { { "path", "Scripts/Hello.lua" }, { "content", "return \"hi\"\n" } });
		CHECK(f.Client.CallOk("asset_read", { { "path", "Scripts/Hello.lua" } }) == "return \"hi\"\n");

		// Protocol-level mistakes are JSON-RPC errors.
		CHECK(f.Client.ErrorCode("tools/call", { { "name", "no_such_tool" } }) == -32602);
		CHECK(f.Client.ErrorCode("tools/call", { { "name", "scene_list" }, { "arguments", json::array() } }) == -32602);
		CHECK(f.Client.ErrorCode("tools/call", json::object()) == -32602);
	}

	TEST_CASE("Malformed messages get JSON-RPC errors")
	{
		McpFixture f;
		const json parseError = json::parse(f.Server.HandleLine("{not json"));
		CHECK(parseError["error"]["code"] == -32700);
		CHECK(parseError["id"].is_null());

		CHECK(f.Client.ErrorCode("no/such/method") == -32601);
		CHECK(json::parse(f.Server.HandleLine("42"))["error"]["code"] == -32600);
		CHECK(json::parse(f.Server.HandleLine(R"({"jsonrpc":"2.0","id":7})"))["error"]["code"] == -32600);

		// Notifications and responses to (non-existent) server requests are never answered.
		CHECK(f.Server.HandleLine(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
		CHECK(f.Server.HandleLine(R"({"jsonrpc":"2.0","method":"no/such/notification"})").empty());
		CHECK(f.Server.HandleLine(R"({"jsonrpc":"2.0","id":3,"result":{}})").empty());

		// Batches answer every request in them.
		const json batch = json::parse(f.Server.HandleLine(R"([{"jsonrpc":"2.0","id":"a","method":"ping"},{"jsonrpc":"2.0","method":"notifications/initialized"}])"));
		REQUIRE(batch.is_array());
		REQUIRE(batch.size() == 1);
		CHECK(batch[0]["id"] == "a");
	}

	TEST_CASE("Run serves newline-delimited messages over a stream")
	{
		McpFixture f;
		std::istringstream input(
			R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"t","version":"1"}}})"
			"\n"
			R"({"jsonrpc":"2.0","method":"notifications/initialized"})"
			"\n\n"
			R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"entity_create","arguments":{"name":"Piped"}}})"
			"\r\n"
			R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})");
		std::vector<std::string> output;
		f.Server.Run(input, [&output](std::string_view message) { output.emplace_back(message); });

		REQUIRE(output.size() == 3);
		for (const std::string& line : output)
			CHECK(line.find('\n') == std::string::npos);
		CHECK(json::parse(output[0])["id"] == 1);
		CHECK(json::parse(output[1])["result"]["isError"] == false);
		CHECK(json::parse(output[2])["id"] == 3);
		CHECK(f.Host.GetEditScene()->FindEntityByName("Piped"));
	}

	TEST_CASE("render_screenshot returns the image")
	{
		McpFixture f;
		f.Client.CallOk("entity_create", { { "name", "Box" }, { "components", { { "Mesh", { { "Mesh", "builtin:cube" } } } } } });
		const json result = f.Client.Call("render_screenshot", { { "width", 64 }, { "height", 48 } });
		INFO(result.dump().substr(0, 400));
		REQUIRE(result["isError"] == false);
		const json& image = result["content"][0];
		CHECK(image["type"] == "image");
		CHECK(image["mimeType"] == "image/png");
		const std::vector<uint8_t> png = Base64Decode(image["data"].get<std::string>());
		REQUIRE(png.size() > 8);
		CHECK(png[0] == 0x89);
		CHECK(png[1] == 'P');
		CHECK(png[2] == 'N');
		CHECK(png[3] == 'G');
		// Without a path the temporary file is removed and not reported.
		CHECK_FALSE(json::parse(McpClient::Text(result)).contains("path"));

		// With a path the PNG is also kept on disk.
		const std::filesystem::path path = f.Directory / "shot.png";
		const json saved = f.Client.Call("render_screenshot", { { "path", path.string() }, { "width", 32 }, { "height", 32 } });
		REQUIRE(saved["isError"] == false);
		CHECK(saved["content"][0]["type"] == "image");
		CHECK(std::filesystem::exists(path));
	}

	TEST_CASE("Resources expose components, the scene and the docs")
	{
		McpFixture f;
		std::set<std::string> uris;
		const json resources = f.Client.Result("resources/list")["resources"];
		for (const json& resource : resources)
			uris.insert(resource["uri"].get<std::string>());
		CHECK(uris.contains("penumbra://components"));
		CHECK(uris.contains("penumbra://scene"));
		CHECK(uris.contains("penumbra://docs/ScriptingAPI.md"));
		CHECK(uris.contains("penumbra://docs/Automation.md"));

		const json components = f.Client.Result("resources/read", { { "uri", "penumbra://components" } })["contents"][0];
		CHECK(components["mimeType"] == "application/json");
		CHECK(json::parse(components["text"].get<std::string>()).contains("Transform"));

		f.Client.CallOk("entity_create", { { "name", "InScene" } });
		const json scene = f.Client.Result("resources/read", { { "uri", "penumbra://scene" } })["contents"][0];
		CHECK(scene["text"].get<std::string>().find("InScene") != std::string::npos);

		const json docs = f.Client.Result("resources/read", { { "uri", "penumbra://docs/ScriptingAPI.md" } })["contents"][0];
		CHECK(docs["mimeType"] == "text/markdown");
		CHECK_FALSE(docs["text"].get<std::string>().empty());

		CHECK(f.Client.ErrorCode("resources/read", { { "uri", "penumbra://nothing" } }) == -32002);
		CHECK(f.Client.Result("resources/templates/list")["resourceTemplates"].empty());
	}

	TEST_CASE("The editor backend forwards tool calls to the editor's command server")
	{
		McpFixture f;
		FakeEditor editor;
		McpServerSpecification specification = HeadlessSpecification();
		specification.Backend = McpBackend::Auto;
		specification.EditorPort = editor.GetPort();
		McpServer server(f.Commands, specification);
		McpClient client(server);

		// Auto picks the running editor; the entity appears in the editor's scene, not the headless one.
		client.CallOk("entity_create", { { "name", "Live" } });
		CHECK(server.GetActiveBackend() == McpBackend::Editor);
		CHECK(editor.Host.GetEditScene()->FindEntityByName("Live"));
		CHECK_FALSE(f.Host.GetEditScene()->FindEntityByName("Live"));
		CHECK(json::parse(client.CallOk("entity_get", { { "name", "Live" } }))["name"] == "Live");
		CHECK(client.CallError("entity_get", { { "name", "Missing" } }).find("no entity named") != std::string::npos);
		CHECK(json::parse(client.CallOk("session_backend"))["backend"] == "editor");

		// Switching to headless runs against the in-process scene.
		client.CallOk("session_backend", { { "mode", "headless" } });
		CHECK(client.CallError("entity_get", { { "name", "Live" } }).find("no entity named") != std::string::npos);
		client.CallOk("session_backend", { { "mode", "editor" } });

		// When the editor goes away, calls fail with a hint instead of hanging.
		editor.Stop();
		CHECK(client.CallError("scene_list").find("session_backend") != std::string::npos);
		CHECK(client.CallError("session_backend", { { "mode", "editor" } }).find("no editor") != std::string::npos);
		CHECK(client.CallError("session_backend", { { "mode", "sideways" } }).find("mode must be") != std::string::npos);

		// Auto without an editor falls back to headless.
		McpServer fallback(f.Commands, specification);
		McpClient fallbackClient(fallback);
		CHECK(json::parse(fallbackClient.CallOk("session_backend"))["backend"] == "headless");
		CHECK(fallback.GetActiveBackend() == McpBackend::Headless);
		fallbackClient.CallOk("scene_list");
	}
}
