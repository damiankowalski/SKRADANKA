#pragma once

#include <filesystem>
#include <string>

namespace Penumbra {

	struct ProjectConfig
	{
		std::string Name = "Untitled";
		std::string StartScene = "Scenes/Main.pscene"; // relative to the asset directory
		std::string AssetDirectory = "Assets";        // relative to the project directory
		uint32_t WindowWidth = 1600;
		uint32_t WindowHeight = 900;
	};

	// A project is a folder with a project.json file and an asset directory.
	// Exactly one project is active at a time; all asset paths are relative to it.
	class Project
	{
	public:
		static constexpr const char* FileName = "project.json";

		static bool Load(const std::filesystem::path& projectFile, std::string& error);
		static bool Create(const std::filesystem::path& directory, const ProjectConfig& config, std::string& error);
		static bool SaveActive();
		static void Unload();
		static bool IsLoaded();

		static ProjectConfig& GetConfig();
		static const std::filesystem::path& GetProjectDirectory();
		static std::filesystem::path GetAssetDirectory();

		// Absolute paths pass through; relative paths resolve against the asset directory
		// (or the working directory when no project is loaded).
		static std::filesystem::path ResolveAssetPath(const std::filesystem::path& path);
		// Converts an absolute path into the project-relative form stored in scenes.
		static std::string MakeAssetRelative(const std::filesystem::path& path);
	};

}
