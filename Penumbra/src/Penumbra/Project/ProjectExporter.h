#pragma once

#include <filesystem>
#include <string>

namespace Penumbra {

	struct ExportResult
	{
		bool Success = false;
		std::string Error;
		std::filesystem::path Executable;
		size_t FilesCopied = 0;
	};

	// Produces a distributable game folder:
	//   <output>/<ProjectName>.exe   (the PenumbraRuntime executable, renamed)
	//   <output>/project.json
	//   <output>/<AssetDirectory>/...
	// The runtime finds project.json next to itself, so the folder can be zipped and shipped.
	class ProjectExporter
	{
	public:
		static ExportResult Export(const std::filesystem::path& projectDirectory, const std::filesystem::path& outputDirectory,
			const std::filesystem::path& runtimeExecutable);
	};

}
