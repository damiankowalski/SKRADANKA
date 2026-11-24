#include "pnpch.h"
#include "Penumbra/Core/FileSystem.h"

#if defined(_WIN32)
	#include <Windows.h>
#elif defined(__APPLE__)
	#include <mach-o/dyld.h>
#endif

namespace Penumbra {

	std::optional<std::string> FileSystem::ReadTextFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary);
		if (!stream)
			return std::nullopt;
		std::ostringstream contents;
		contents << stream.rdbuf();
		return contents.str();
	}

	std::optional<std::vector<uint8_t>> FileSystem::ReadBinaryFile(const std::filesystem::path& path)
	{
		std::ifstream stream(path, std::ios::in | std::ios::binary | std::ios::ate);
		if (!stream)
			return std::nullopt;
		const std::streamsize size = stream.tellg();
		if (size < 0)
			return std::nullopt;
		std::vector<uint8_t> data(static_cast<size_t>(size));
		stream.seekg(0, std::ios::beg);
		if (size > 0 && !stream.read(reinterpret_cast<char*>(data.data()), size))
			return std::nullopt;
		return data;
	}

	bool FileSystem::WriteTextFile(const std::filesystem::path& path, std::string_view text)
	{
		return WriteBinaryFile(path, text.data(), text.size());
	}

	bool FileSystem::WriteBinaryFile(const std::filesystem::path& path, const void* data, size_t size)
	{
		std::error_code error;
		if (path.has_parent_path())
			std::filesystem::create_directories(path.parent_path(), error);

		std::filesystem::path temporary = path;
		temporary += ".tmp";
		{
			std::ofstream stream(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
			if (!stream)
				return false;
			if (size > 0)
				stream.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
			if (!stream)
				return false;
		}

		std::filesystem::rename(temporary, path, error);
		if (error)
		{
			std::filesystem::remove(temporary, error);
			return false;
		}
		return true;
	}

	std::filesystem::path FileSystem::GetExecutableDirectory()
	{
#if defined(_WIN32)
		std::wstring buffer(MAX_PATH, L'\0');
		for (;;)
		{
			const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (length == 0)
				return std::filesystem::current_path();
			if (length < buffer.size())
			{
				buffer.resize(length);
				break;
			}
			buffer.resize(buffer.size() * 2);
		}
		return std::filesystem::path(buffer).parent_path();
#elif defined(__APPLE__)
		uint32_t size = 0;
		_NSGetExecutablePath(nullptr, &size);
		std::string buffer(size, '\0');
		if (_NSGetExecutablePath(buffer.data(), &size) != 0)
			return std::filesystem::current_path();
		buffer.resize(std::strlen(buffer.c_str()));
		std::error_code error;
		const std::filesystem::path resolved = std::filesystem::canonical(buffer, error);
		return (error ? std::filesystem::path(buffer) : resolved).parent_path();
#else
		std::error_code error;
		const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
		return error ? std::filesystem::current_path() : executable.parent_path();
#endif
	}

	std::filesystem::path FileSystem::GetUserDataDirectory()
	{
		auto environment = [](const char* name) -> std::optional<std::filesystem::path> {
			const char* value = std::getenv(name);
			if (!value || !*value)
				return std::nullopt;
			return std::filesystem::path(value);
		};
		if (std::optional<std::filesystem::path> overridden = environment("PENUMBRA_USER_DIR"))
			return *overridden;
#if defined(_WIN32)
		if (std::optional<std::filesystem::path> appData = environment("APPDATA"))
			return *appData / "Penumbra";
#elif defined(__APPLE__)
		if (std::optional<std::filesystem::path> home = environment("HOME"))
			return *home / "Library" / "Application Support" / "Penumbra";
#else
		if (std::optional<std::filesystem::path> config = environment("XDG_CONFIG_HOME"))
			return *config / "Penumbra";
		if (std::optional<std::filesystem::path> home = environment("HOME"))
			return *home / ".config" / "Penumbra";
#endif
		return GetExecutableDirectory() / "UserData";
	}

	std::string FileSystem::MakeRelativeGeneric(const std::filesystem::path& path, const std::filesystem::path& base)
	{
		std::error_code error;
		std::filesystem::path relative = std::filesystem::relative(path, base, error);
		if (error || relative.empty() || *relative.begin() == "..")
			return path.generic_string();
		return relative.generic_string();
	}

}
