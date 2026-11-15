#include "pnpch.h"
#include "Penumbra/Core/Platform.h"

#include <algorithm>
#include <cstdio>
#include <thread>

#ifdef _WIN32
	#include <Windows.h>
	#include <psapi.h>
	#include <shellapi.h>
#else
	#include <csignal>
	#include <spawn.h>
	#include <sys/resource.h>
	#include <sys/wait.h>
	#include <unistd.h>
	#ifdef __APPLE__
		#include <mach/mach.h>
		#include <sys/sysctl.h>
	#endif
extern char** environ;
#endif

namespace Penumbra::Platform {

	bool IsDebuggerAttached()
	{
#if defined(_WIN32)
		return IsDebuggerPresent() != FALSE;
#elif defined(__APPLE__)
		int name[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid() };
		kinfo_proc info{};
		size_t size = sizeof(info);
		if (sysctl(name, 4, &info, &size, nullptr, 0) != 0)
			return false;
		return (info.kp_proc.p_flag & P_TRACED) != 0;
#else
		return false;
#endif
	}

	void DebugBreak()
	{
#if defined(_WIN32)
		__debugbreak();
#else
		std::raise(SIGTRAP);
#endif
	}

	bool OpenWithDefaultApplication(const std::filesystem::path& path)
	{
#if defined(_WIN32)
		const auto result = reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL));
		return result > 32;
#else
	#ifdef __APPLE__
		const char* opener = "open";
	#else
		const char* opener = "xdg-open";
	#endif
		const std::string file = path.string();
		char* arguments[] = { const_cast<char*>(opener), const_cast<char*>(file.c_str()), nullptr };
		pid_t child = 0;
		if (posix_spawnp(&child, opener, nullptr, nullptr, arguments, environ) != 0)
			return false;
		int status = 0;
		waitpid(child, &status, 0);
		return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
	}

	ProcessUsage GetProcessUsage()
	{
		ProcessUsage usage;
#if defined(_WIN32)
		FILETIME creation, exitTime, kernel, user;
		if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user))
		{
			auto seconds = [](const FILETIME& time) {
				return static_cast<double>((static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime) * 1e-7;
			};
			usage.CpuSeconds = seconds(kernel) + seconds(user);
		}
		PROCESS_MEMORY_COUNTERS counters{};
		if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
		{
			usage.ResidentBytes = counters.WorkingSetSize;
			usage.PeakResidentBytes = counters.PeakWorkingSetSize;
		}
#else
		rusage self{};
		if (getrusage(RUSAGE_SELF, &self) == 0)
		{
			usage.CpuSeconds = static_cast<double>(self.ru_utime.tv_sec) + static_cast<double>(self.ru_utime.tv_usec) * 1e-6 +
							   static_cast<double>(self.ru_stime.tv_sec) + static_cast<double>(self.ru_stime.tv_usec) * 1e-6;
	#ifdef __APPLE__
			usage.PeakResidentBytes = static_cast<uint64_t>(self.ru_maxrss); // bytes on macOS
	#else
			usage.PeakResidentBytes = static_cast<uint64_t>(self.ru_maxrss) * 1024; // kilobytes on Linux
	#endif
		}
	#ifdef __APPLE__
		mach_task_basic_info info{};
		mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
		if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
		{
			usage.ResidentBytes = info.resident_size;
			usage.PeakResidentBytes = std::max<uint64_t>(usage.PeakResidentBytes, info.resident_size_max);
		}
	#else
		if (FILE* statm = std::fopen("/proc/self/statm", "r"))
		{
			unsigned long size = 0, resident = 0;
			if (std::fscanf(statm, "%lu %lu", &size, &resident) == 2)
				usage.ResidentBytes = static_cast<uint64_t>(resident) * static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
			std::fclose(statm);
		}
	#endif
#endif
		usage.PeakResidentBytes = std::max(usage.PeakResidentBytes, usage.ResidentBytes);
		return usage;
	}

	uint32_t GetLogicalCoreCount() { return std::max(1u, std::thread::hardware_concurrency()); }

	uint64_t GetPhysicalMemoryBytes()
	{
#if defined(_WIN32)
		MEMORYSTATUSEX status{};
		status.dwLength = sizeof(status);
		return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#elif defined(__APPLE__)
		uint64_t bytes = 0;
		size_t size = sizeof(bytes);
		return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0;
#else
		const long pages = sysconf(_SC_PHYS_PAGES);
		const long pageSize = sysconf(_SC_PAGESIZE);
		return pages > 0 && pageSize > 0 ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSize) : 0;
#endif
	}

}
