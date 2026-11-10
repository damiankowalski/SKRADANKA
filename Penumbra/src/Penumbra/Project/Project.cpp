#include "pnpch.h"
#include "Penumbra/Project/Project.h"

#include "Penumbra/Core/FileSystem.h"

#include <nlohmann/json.hpp>

namespace Penumbra {

	namespace {
		struct ProjectState
		{
			bool Loaded = false;
			ProjectConfig Config;
			std::filesystem::path Directory;
		};

		ProjectState s_Project;

		bool ParseConfig(const nlohmann::json& document, ProjectConfig& config, std::string& error)
		{
			if (!document.is_object())
			{
				error = "project file must be a JSON object";
				return false;
			}
			auto readString = [&](const char* key, std::string& target) {
				const auto it = document.find(key);
				if (it == document.end())
					return true;
				if (!it->is_string())
				{
					error = std::format("'{}' must be a string", key);
					return false;
				}
				target = it->get<std::string>();
				return true;
			};
			auto readUInt = [&](const char* key, uint32_t& target) {
				const auto it = document.find(key);
				if (it == document.end())
					return true;
				if (!it->is_number_unsigned() || it->get<uint32_t>() == 0)
				{
					error = std::format("'{}' must be a positive integer", key);
					return false;
				}
				target = it->get<uint32_t>();
				return true;
			};
			return readString("Name", config.Name) && readString("StartScene", config.StartScene) &&
				   readString("AssetDirectory", config.AssetDirectory) && readUInt("WindowWidth", config.WindowWidth) &&
				   readUInt("WindowHeight", config.WindowHeight);
		}

		nlohmann::json ConfigToJson(const ProjectConfig& config)
		{
			return nlohmann::json{ { "Name", config.Name }, { "StartScene", config.StartScene }, { "AssetDirectory", config.AssetDirectory },
				{ "WindowWidth", config.WindowWidth }, { "WindowHeight", config.WindowHeight } };
		}
	}

	namespace {
		bool IsProject(const std::filesystem::path& path)
		{
			std::error_code error;
			if (std::filesystem::is_directory(path, error))
				return std::filesystem::is_regular_file(path / Project::FileName, error);
			return path.filename() == Project::FileName && std::filesystem::is_regular_file(path, error);
		}
	}

	std::filesystem::path Project::Resolve(const std::filesystem::path& path, const std::filesystem::path& searchRoot)
	{
		if (path.empty() || path.is_absolute())
			return path;
		std::error_code error;
		const std::filesystem::path fromWorkingDirectory = std::filesystem::absolute(path, error).lexically_normal();
		if (IsProject(fromWorkingDirectory))
			return fromWorkingDirectory;
		std::filesystem::path directory = searchRoot.empty() ? FileSystem::GetExecutableDirectory() : searchRoot;
		while (!directory.empty())
		{
			const std::filesystem::path candidate = (directory / path).lexically_normal();
			if (IsProject(candidate))
				return candidate;
			const std::filesystem::path parent = directory.parent_path();
			if (parent == directory)
				break;
			directory = parent;
		}
		return fromWorkingDirectory;
	}

	bool Project::Load(const std::filesystem::path& projectFile, std::string& error)
	{
		std::filesystem::path file = projectFile;
		if (std::filesystem::is_directory(file))
			file /= FileName;

		const std::optional<std::string> text = FileSystem::ReadTextFile(file);
		if (!text)
		{
			std::error_code ignored;
			error = std::format("no project found at '{}' (expected a folder containing {} or the file itself)",
				std::filesystem::absolute(projectFile, ignored).lexically_normal().string(), FileName);
			return false;
		}
		const nlohmann::json document = nlohmann::json::parse(*text, nullptr, false);
		if (document.is_discarded())
		{
			error = std::format("'{}' is not valid JSON", file.string());
			return false;
		}
		ProjectConfig config;
		if (!ParseConfig(document, config, error))
			return false;

		s_Project.Loaded = true;
		s_Project.Config = config;
		s_Project.Directory = std::filesystem::absolute(file).parent_path().lexically_normal();
		PN_CORE_INFO("Loaded project '{}' from {}", config.Name, s_Project.Directory.string());
		return true;
	}

	bool Project::Create(const std::filesystem::path& directory, const ProjectConfig& config, std::string& error)
	{
		std::error_code fsError;
		std::filesystem::create_directories(directory / config.AssetDirectory / "Scenes", fsError);
		std::filesystem::create_directories(directory / config.AssetDirectory / "Scripts", fsError);
		std::filesystem::create_directories(directory / config.AssetDirectory / "Prefabs", fsError);
		if (fsError)
		{
			error = std::format("cannot create project directories: {}", fsError.message());
			return false;
		}
		if (!FileSystem::WriteTextFile(directory / FileName, ConfigToJson(config).dump(2) + "\n"))
		{
			error = "cannot write project file";
			return false;
		}
		return Load(directory / FileName, error);
	}

	bool Project::SaveActive()
	{
		if (!s_Project.Loaded)
			return false;
		return FileSystem::WriteTextFile(s_Project.Directory / FileName, ConfigToJson(s_Project.Config).dump(2) + "\n");
	}

	void Project::Unload() { s_Project = ProjectState{}; }
	bool Project::IsLoaded() { return s_Project.Loaded; }
	ProjectConfig& Project::GetConfig() { return s_Project.Config; }
	const std::filesystem::path& Project::GetProjectDirectory() { return s_Project.Directory; }

	std::filesystem::path Project::GetAssetDirectory()
	{
		if (!s_Project.Loaded)
			return std::filesystem::current_path();
		return s_Project.Directory / s_Project.Config.AssetDirectory;
	}

	std::filesystem::path Project::ResolveAssetPath(const std::filesystem::path& path)
	{
		if (path.is_absolute())
			return path;
		return (GetAssetDirectory() / path).lexically_normal();
	}

	std::string Project::MakeAssetRelative(const std::filesystem::path& path)
	{
		return FileSystem::MakeRelativeGeneric(path, GetAssetDirectory());
	}

}
