#include "pnpch.h"
#include "Penumbra/Core/Log.h"

#include <cstdio>
#include <iostream>

#include <Windows.h>

namespace Penumbra {

	namespace {

		struct LogState
		{
			std::mutex Mutex;
			std::ofstream File;
			std::atomic<LogLevel> MinimumLevel = LogLevel::Trace;
			bool ConsoleEnabled = true;
			Log::SinkHandle NextHandle = 1;
			std::vector<std::pair<Log::SinkHandle, Log::SinkCallback>> Sinks;
		};

		LogState& GetState()
		{
			static LogState s_State;
			return s_State;
		}

		WORD ConsoleColorFor(LogLevel level)
		{
			switch (level)
			{
				case LogLevel::Trace:    return FOREGROUND_INTENSITY;
				case LogLevel::Info:     return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
				case LogLevel::Warn:     return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
				case LogLevel::Error:    return FOREGROUND_RED | FOREGROUND_INTENSITY;
				case LogLevel::Critical: return BACKGROUND_RED | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
			}
			return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
		}

		std::string Timestamp()
		{
			const auto now = std::chrono::system_clock::now();
			const auto seconds = std::chrono::floor<std::chrono::seconds>(now);
			const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now - seconds).count();
			const std::time_t time = std::chrono::system_clock::to_time_t(now);
			std::tm local{};
			localtime_s(&local, &time);
			return std::format("{:02}:{:02}:{:02}.{:03}", local.tm_hour, local.tm_min, local.tm_sec, millis);
		}

	}

	const char* LogLevelToString(LogLevel level)
	{
		switch (level)
		{
			case LogLevel::Trace:    return "trace";
			case LogLevel::Info:     return "info";
			case LogLevel::Warn:     return "warn";
			case LogLevel::Error:    return "error";
			case LogLevel::Critical: return "critical";
		}
		return "unknown";
	}

	void Log::Init(const std::string& logFilePath)
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		if (!logFilePath.empty())
		{
			state.File.close();
			state.File.open(logFilePath, std::ios::out | std::ios::trunc);
		}
	}

	void Log::Shutdown()
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		state.Sinks.clear();
		if (state.File.is_open())
			state.File.close();
	}

	void Log::SetMinimumLevel(LogLevel level) { GetState().MinimumLevel = level; }
	LogLevel Log::GetMinimumLevel() { return GetState().MinimumLevel; }

	void Log::SetConsoleOutputEnabled(bool enabled)
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		state.ConsoleEnabled = enabled;
	}

	Log::SinkHandle Log::AddSink(SinkCallback callback)
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		const SinkHandle handle = state.NextHandle++;
		state.Sinks.emplace_back(handle, std::move(callback));
		return handle;
	}

	void Log::RemoveSink(SinkHandle handle)
	{
		LogState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		std::erase_if(state.Sinks, [handle](const auto& sink) { return sink.first == handle; });
	}

	void Log::Write(LogLevel level, std::string_view logger, std::string_view text)
	{
		LogState& state = GetState();
		if (level < state.MinimumLevel)
			return;

		const std::string line = std::format("[{}] [{}] {}: {}", Timestamp(), LogLevelToString(level), logger, text);

		std::scoped_lock lock(state.Mutex);
		if (state.ConsoleEnabled)
		{
			HANDLE console = GetStdHandle(level >= LogLevel::Error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
			CONSOLE_SCREEN_BUFFER_INFO info{};
			const bool isConsole = GetConsoleScreenBufferInfo(console, &info) != 0;
			if (isConsole)
				SetConsoleTextAttribute(console, ConsoleColorFor(level));
			std::FILE* stream = level >= LogLevel::Error ? stderr : stdout;
			std::fputs(line.c_str(), stream);
			if (isConsole)
				SetConsoleTextAttribute(console, info.wAttributes);
			std::fputc('\n', stream);
			std::fflush(stream);
		}

		if (state.File.is_open())
		{
			state.File << line << '\n';
			state.File.flush();
		}

		if (!state.Sinks.empty())
		{
			const LogMessage message{ level, std::string(logger), std::string(text) };
			for (const auto& [handle, sink] : state.Sinks)
				sink(message);
		}
	}

	namespace Detail {

		void AssertionFailed(const char* expression, const char* message, const char* file, int line)
		{
			Log::Write(LogLevel::Critical, "Assert", std::string_view(std::format("{} ({}) at {}:{}", message, expression, file, line)));
			if (IsDebuggerPresent())
				__debugbreak();
			std::abort();
		}

	}

}
