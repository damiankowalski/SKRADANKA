#pragma once

#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace Penumbra {

	enum class LogLevel : uint8_t
	{
		Trace = 0,
		Info,
		Warn,
		Error,
		Critical
	};

	const char* LogLevelToString(LogLevel level);

	struct LogMessage
	{
		LogLevel Level = LogLevel::Info;
		std::string Logger;
		std::string Text;
	};

	// Thread-safe logger with pluggable sinks. The console sink is always active;
	// a file sink and any number of callback sinks (editor console, command server) can be added.
	class Log
	{
	public:
		using SinkCallback = std::function<void(const LogMessage&)>;
		using SinkHandle = uint32_t;

		static void Init(const std::string& logFilePath = {});
		static void Shutdown();

		static void SetMinimumLevel(LogLevel level);
		static LogLevel GetMinimumLevel();
		static void SetConsoleOutputEnabled(bool enabled);

		static SinkHandle AddSink(SinkCallback callback);
		static void RemoveSink(SinkHandle handle);

		static void Write(LogLevel level, std::string_view logger, std::string_view text);

		template<typename... Args>
		static void Write(LogLevel level, std::string_view logger, std::format_string<Args...> fmt, Args&&... args)
		{
			if (level < GetMinimumLevel())
				return;
			Write(level, logger, std::string_view(std::format(fmt, std::forward<Args>(args)...)));
		}
	};

}

#define PN_CORE_TRACE(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Trace, "Penumbra", __VA_ARGS__)
#define PN_CORE_INFO(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Info, "Penumbra", __VA_ARGS__)
#define PN_CORE_WARN(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Warn, "Penumbra", __VA_ARGS__)
#define PN_CORE_ERROR(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Error, "Penumbra", __VA_ARGS__)
#define PN_CORE_CRITICAL(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Critical, "Penumbra", __VA_ARGS__)

#define PN_TRACE(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Trace, "App", __VA_ARGS__)
#define PN_INFO(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Info, "App", __VA_ARGS__)
#define PN_WARN(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Warn, "App", __VA_ARGS__)
#define PN_ERROR(...) ::Penumbra::Log::Write(::Penumbra::LogLevel::Error, "App", __VA_ARGS__)
