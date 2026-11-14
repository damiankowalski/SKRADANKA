#pragma once

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

}
