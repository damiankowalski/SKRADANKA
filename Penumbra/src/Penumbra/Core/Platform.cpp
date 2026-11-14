#include "pnpch.h"
#include "Penumbra/Core/Platform.h"

#ifdef _WIN32
	#include <Windows.h>
	#include <shellapi.h>
#else
	#include <csignal>
	#include <spawn.h>
	#include <sys/wait.h>
	#include <unistd.h>
	#ifdef __APPLE__
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

}
