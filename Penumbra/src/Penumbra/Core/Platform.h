#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Penumbra::Platform {

	// ".exe" on Windows, "" elsewhere: PenumbraRuntime, an exported game.
#ifdef _WIN32
	inline constexpr const char* ExecutableSuffix = ".exe";
#else
	inline constexpr const char* ExecutableSuffix = "";
#endif

	// Appends the platform's executable suffix to a file name without one.
	inline std::string ExecutableName(const std::string& name) { return name + ExecutableSuffix; }

	bool IsDebuggerAttached();
	void DebugBreak();

	// Opens a file with the application the operating system associates with it (Explorer / Finder).
	bool OpenWithDefaultApplication(const std::filesystem::path& path);

	// What the process costs the machine (the runtime's performance monitor).
	struct ProcessUsage
	{
		double CpuSeconds = 0.0;        // user + kernel time of every thread since the process started
		uint64_t ResidentBytes = 0;     // physical memory in use now (working set)
		uint64_t PeakResidentBytes = 0; // the most it has used
	};
	ProcessUsage GetProcessUsage();
	uint32_t GetLogicalCoreCount();
	uint64_t GetPhysicalMemoryBytes(); // installed RAM; 0 when unknown

}
