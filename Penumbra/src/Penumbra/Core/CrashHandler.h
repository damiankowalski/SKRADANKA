#pragma once

#include <filesystem>

namespace Penumbra {

	// Installs a process-wide handler for unhandled exceptions (access violations etc.).
	// On a crash it logs the exception and a symbolized call stack, and writes a minidump
	// (<directory>/<name>_<timestamp>.dmp) that can be opened in Visual Studio.
	class CrashHandler
	{
	public:
		static void Install(const std::filesystem::path& dumpDirectory, const std::string& applicationName);
	};

}
