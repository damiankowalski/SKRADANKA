#include "pnpch.h"
#include "Penumbra/Project/ProjectExporter.h"

#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Core/Platform.h"
#include "Penumbra/Project/Project.h"

#include <nlohmann/json.hpp>

namespace Penumbra {

	namespace {
		std::string SanitizeFileName(const std::string& name)
		{
			std::string result;
			for (char c : name)
			{
				const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ';
				result += allowed ? c : '_';
			}
			return result.empty() ? std::string("Game") : result;
		}
	}

	ExportResult ProjectExporter::Export(const std::filesystem::path& projectDirectory, const std::filesystem::path& outputDirectory,
		const std::filesystem::path& runtimeExecutable)
	{
		ExportResult result;
		std::error_code error;

		const std::filesystem::path projectFile = projectDirectory / Project::FileName;
		const std::optional<std::string> projectText = FileSystem::ReadTextFile(projectFile);
		if (!projectText)
		{
			result.Error = std::format("no project at '{}'", projectDirectory.string());
			return result;
		}
		const nlohmann::json project = nlohmann::json::parse(*projectText, nullptr, false);
		if (project.is_discarded() || !project.is_object())
		{
			result.Error = "project.json is not valid JSON";
			return result;
		}
		if (!std::filesystem::is_regular_file(runtimeExecutable, error))
		{
			result.Error = std::format("runtime executable not found at '{}' (build PenumbraRuntime first)", runtimeExecutable.string());
			return result;
		}

		const std::string assetDirectory = project.value("AssetDirectory", std::string("Assets"));
		const std::filesystem::path sourceAssets = projectDirectory / assetDirectory;
		if (!std::filesystem::is_directory(sourceAssets, error))
		{
			result.Error = std::format("asset directory '{}' does not exist", sourceAssets.string());
			return result;
		}

		const std::filesystem::path output = std::filesystem::weakly_canonical(outputDirectory, error);
		const std::filesystem::path assets = std::filesystem::weakly_canonical(sourceAssets, error);
		const auto [assetsEnd, unused] = std::mismatch(assets.begin(), assets.end(), output.begin(), output.end());
		if (assetsEnd == assets.end())
		{
			result.Error = "the export directory must not be inside the asset directory";
			return result;
		}

		std::filesystem::create_directories(outputDirectory, error);
		if (error)
		{
			result.Error = std::format("cannot create '{}': {}", outputDirectory.string(), error.message());
			return result;
		}

		// Replace the previous export's assets entirely so deleted files do not linger.
		std::filesystem::remove_all(outputDirectory / assetDirectory, error);
		std::filesystem::copy(sourceAssets, outputDirectory / assetDirectory,
			std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
		if (error)
		{
			result.Error = std::format("copying assets failed: {}", error.message());
			return result;
		}
		for (const auto& entry : std::filesystem::recursive_directory_iterator(outputDirectory / assetDirectory, error))
		{
			if (entry.is_regular_file())
				result.FilesCopied++;
		}

		if (!FileSystem::WriteTextFile(outputDirectory / Project::FileName, *projectText))
		{
			result.Error = "cannot write project.json";
			return result;
		}

		result.Executable = outputDirectory / Platform::ExecutableName(SanitizeFileName(project.value("Name", std::string("Game"))));
		std::filesystem::copy_file(runtimeExecutable, result.Executable, std::filesystem::copy_options::overwrite_existing, error);
		if (error)
		{
			result.Error = std::format("copying the runtime failed: {}", error.message());
			return result;
		}
		result.FilesCopied += 2;
		result.Success = true;
		PN_CORE_INFO("Exported '{}' to {} ({} files)", project.value("Name", std::string("Game")), outputDirectory.string(), result.FilesCopied);
		return result;
	}

}
