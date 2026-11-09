#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Penumbra {

	class FileSystem
	{
	public:
		static std::optional<std::string> ReadTextFile(const std::filesystem::path& path);
		static std::optional<std::vector<uint8_t>> ReadBinaryFile(const std::filesystem::path& path);

		// Writes atomically: data goes to a temporary file which then replaces the target,
		// so a crash mid-write never leaves a truncated scene or project file behind.
		static bool WriteTextFile(const std::filesystem::path& path, std::string_view text);
		static bool WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size);

		static std::filesystem::path GetExecutableDirectory();

		// Normalizes a path to forward slashes, relative to base when possible.
		static std::string MakeRelativeGeneric(const std::filesystem::path& path, const std::filesystem::path& base);
	};

}
