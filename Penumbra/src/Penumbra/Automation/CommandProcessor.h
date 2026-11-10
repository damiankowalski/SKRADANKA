#pragma once

#include "Penumbra/Automation/AutomationHost.h"
#include "Penumbra/Core/Log.h"

#include <nlohmann/json.hpp>

#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>

namespace Penumbra {

	// JSON command API that lets tools and AI agents drive the engine/editor end to end:
	// projects, scenes, entities, components, prefabs, assets, play mode, input, scripts,
	// screenshots, scripted tests and export.
	//
	// Request:  { "cmd": "entity.create", "name": "Torch", "components": { ... } }
	// Response: { "ok": true, "result": ... }  or  { "ok": false, "error": "..." }
	//
	// "help" lists every command with its arguments. See docs/Automation.md.
	class CommandProcessor
	{
	public:
		explicit CommandProcessor(AutomationHost& host);
		~CommandProcessor();

		CommandProcessor(const CommandProcessor&) = delete;
		CommandProcessor& operator=(const CommandProcessor&) = delete;

		nlohmann::json Execute(const nlohmann::json& request);
		// Executes a JSON array of requests, or JSON Lines text. Stops at the first failure
		// unless continueOnError is set. Returns the array of responses.
		nlohmann::json ExecuteBatch(const std::string& text, bool continueOnError = false);

		struct CommandInfo
		{
			std::string Arguments;
			std::string Description;
			std::function<nlohmann::json(const nlohmann::json&)> Handler;
		};
		const std::map<std::string, CommandInfo>& GetCommands() const { return m_Commands; }

	private:
		void Register(const std::string& name, const std::string& arguments, const std::string& description,
			std::function<nlohmann::json(const nlohmann::json&)> handler);
		void RegisterAll();

	private:
		AutomationHost& m_Host;
		std::map<std::string, CommandInfo> m_Commands;

		std::mutex m_LogMutex;
		std::deque<LogMessage> m_RecentLog;
		Log::SinkHandle m_LogSink = 0;
	};

}
