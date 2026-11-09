#pragma once

#include "Penumbra/Asset/MeshData.h"

#include <filesystem>
#include <memory>
#include <string>

namespace Penumbra {

	// Caches CPU-side assets by their project-relative path. Thread-safe.
	// Missing or broken assets are logged once and return nullptr; callers fall back gracefully.
	class AssetManager
	{
	public:
		// "builtin:*" names return the shared primitive meshes.
		static std::shared_ptr<const MeshData> GetMesh(const std::string& path);
		static std::shared_ptr<const TextureData> GetTexture(const std::string& path);

		// Drops cached entries whose files changed on disk (hot reload). Returns the number reloaded.
		static size_t ReloadModified();
		static void Clear();

		// Monotonic counter bumped whenever cached content changes, so GPU caches know to refresh.
		static uint64_t GetGeneration();
	};

}
