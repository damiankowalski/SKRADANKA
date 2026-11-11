#pragma once

#include "Penumbra/Automation/CommandProcessor.h"
#include "Penumbra/Automation/CommandServer.h"
#include "Penumbra/Core/Base.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <istream>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace Penumbra {

	// Where MCP tool calls are executed.
	enum class McpBackend
	{
		Auto,		// the running editor if it answers on the first call, otherwise headless
		Headless,	// the in-process CommandProcessor (no window)
		Editor		// the running editor's command server (changes appear live)
	};

	std::optional<McpBackend> McpBackendFromString(std::string_view name);
	const char* McpBackendToString(McpBackend backend);

	struct McpServerSpecification
	{
		McpBackend Backend = McpBackend::Auto;
		std::string EditorHost = "127.0.0.1";
		uint16_t EditorPort = 7777;
		// Folder with ScriptingAPI.md etc., exposed as resources. Empty: search upwards from the executable.
		std::filesystem::path DocsDirectory;
	};

	// Model Context Protocol server (JSON-RPC 2.0 over newline-delimited stdio, spec 2025-11-25).
	// Every automation command becomes a tool ("entity.create" -> "entity_create") whose input
	// schema is derived from the command table; render_screenshot returns the PNG as image
	// content. Resources expose the component list, the open scene and the API docs.
	class McpServer
	{
	public:
		// `headless` executes calls on the Headless backend and provides the command table.
		McpServer(CommandProcessor& headless, McpServerSpecification specification = {});

		McpServer(const McpServer&) = delete;
		McpServer& operator=(const McpServer&) = delete;

		// Handles one parsed JSON-RPC message (or batch); nullopt when nothing is sent back (notifications).
		std::optional<nlohmann::json> HandleMessage(const nlohmann::json& message);
		// Handles one line of the stdio transport; returns the response line (without newline) or "".
		std::string HandleLine(std::string_view line);

		// Serves messages from `input` until end of stream; each response is passed to `write`.
		void Run(std::istream& input, const std::function<void(std::string_view)>& write);
		// Reserves stdout for protocol messages: duplicates it and points fd 1 / STD_OUTPUT_HANDLE at
		// stderr, so logs and anything a library prints never corrupt the JSON-RPC stream. Call it
		// before anything is logged. Returns the protocol file descriptor, or -1.
		static int ReserveStdout();
		// Serves stdin and the reserved stdout until stdin closes. Returns the process exit code.
		static int RunStdio(McpServer& server, int protocolFd);

		// Backend actually in use (Auto resolves on the first call that needs one).
		McpBackend GetActiveBackend() const { return m_Active; }

		static std::string ToolNameForCommand(std::string_view command);

	private:
		nlohmann::json HandleRequest(const std::string& method, const nlohmann::json& params);
		nlohmann::json Initialize(const nlohmann::json& params);
		nlohmann::json ListTools() const;
		nlohmann::json CallTool(const nlohmann::json& params);
		nlohmann::json CallBackendTool(const nlohmann::json& arguments);
		nlohmann::json CallScreenshot(nlohmann::json request);
		nlohmann::json ListResources() const;
		nlohmann::json ReadResource(const nlohmann::json& params);

		// Executes an automation request on the active backend; returns { ok, result | error }.
		nlohmann::json Execute(const nlohmann::json& request);
		McpBackend ResolveBackend();
		std::string DescribeBackend() const;

	private:
		CommandProcessor& m_Headless;
		McpServerSpecification m_Specification;
		McpBackend m_Active;
		CommandClient m_Editor;
		std::map<std::string, std::string> m_ToolToCommand;
		std::map<std::string, std::filesystem::path> m_Docs; // resource URI -> file
	};

}
