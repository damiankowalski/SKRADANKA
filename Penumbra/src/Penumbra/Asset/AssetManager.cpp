#include "pnpch.h"
#include "Penumbra/Asset/AssetManager.h"

#include "Penumbra/Asset/Importers.h"
#include "Penumbra/Asset/Primitives.h"
#include "Penumbra/Project/Project.h"

namespace Penumbra {

	namespace {

		template<typename T>
		struct CacheEntry
		{
			std::shared_ptr<const T> Asset; // nullptr when loading failed
			std::filesystem::path ResolvedPath;
			std::filesystem::file_time_type WriteTime;
		};

		struct AssetState
		{
			std::mutex Mutex;
			std::unordered_map<std::string, CacheEntry<MeshData>> Meshes;
			std::unordered_map<std::string, CacheEntry<TextureData>> Textures;
			std::atomic<uint64_t> Generation = 1;
		};

		AssetState& GetState()
		{
			static AssetState s_State;
			return s_State;
		}

		std::filesystem::file_time_type WriteTimeOf(const std::filesystem::path& path)
		{
			std::error_code error;
			const auto time = std::filesystem::last_write_time(path, error);
			return error ? std::filesystem::file_time_type{} : time;
		}

		std::shared_ptr<const MeshData> LoadMesh(const std::filesystem::path& resolved, const std::string& path)
		{
			std::string error;
			const std::string extension = resolved.extension().string();
			if (extension != ".gltf" && extension != ".glb")
			{
				PN_CORE_ERROR("Mesh '{}': unsupported format (use .gltf or .glb)", path);
				return nullptr;
			}
			// Image files inside the project come from the texture cache: props sharing a material sheet share
			// one copy of it, in memory and on the GPU.
			const auto loadExternal = [](const std::filesystem::path& file, std::string& imageError) -> std::shared_ptr<const TextureData> {
				std::error_code ignored;
				const std::filesystem::path relative = std::filesystem::relative(file, Project::GetAssetDirectory(), ignored);
				const std::string assetPath = relative.generic_string();
				if (!Project::GetAssetDirectory().empty() && !relative.empty() && !assetPath.starts_with(".."))
				{
					std::shared_ptr<const TextureData> shared = AssetManager::GetTexture(assetPath);
					if (!shared)
						imageError = "cannot load " + assetPath;
					return shared;
				}
				std::optional<TextureData> texture = TextureImporter::LoadFromFile(file, imageError);
				return texture ? std::make_shared<const TextureData>(std::move(*texture)) : nullptr;
			};
			std::optional<MeshData> mesh = GltfImporter::Import(resolved, error, loadExternal);
			if (!mesh)
			{
				PN_CORE_ERROR("Mesh '{}': {}", path, error);
				return nullptr;
			}
			PN_CORE_INFO("Loaded mesh '{}' ({} vertices, {} submeshes)", path, mesh->Vertices.size(), mesh->Submeshes.size());
			return std::make_shared<const MeshData>(std::move(*mesh));
		}

		std::shared_ptr<const TextureData> LoadTexture(const std::filesystem::path& resolved, const std::string& path)
		{
			std::string error;
			std::optional<TextureData> texture = TextureImporter::LoadFromFile(resolved, error);
			if (!texture)
			{
				PN_CORE_ERROR("Texture '{}': {}", path, error);
				return nullptr;
			}
			return std::make_shared<const TextureData>(std::move(*texture));
		}

	}

	std::shared_ptr<const MeshData> AssetManager::GetMesh(const std::string& path)
	{
		if (path.empty())
			return nullptr;
		if (path.starts_with(Primitives::BuiltinPrefix))
		{
			std::shared_ptr<const MeshData> builtin = Primitives::GetBuiltin(path);
			if (!builtin)
				PN_CORE_ERROR("Unknown built-in mesh '{}'", path);
			return builtin;
		}

		AssetState& state = GetState();
		{
			std::scoped_lock lock(state.Mutex);
			if (const auto it = state.Meshes.find(path); it != state.Meshes.end())
				return it->second.Asset;
		}

		// Load outside the lock: imports can be slow.
		const std::filesystem::path resolved = Project::ResolveAssetPath(path);
		CacheEntry<MeshData> entry{ LoadMesh(resolved, path), resolved, WriteTimeOf(resolved) };

		std::scoped_lock lock(state.Mutex);
		auto [it, inserted] = state.Meshes.try_emplace(path, std::move(entry));
		return it->second.Asset;
	}

	std::shared_ptr<const TextureData> AssetManager::GetTexture(const std::string& path)
	{
		if (path.empty())
			return nullptr;

		AssetState& state = GetState();
		{
			std::scoped_lock lock(state.Mutex);
			if (const auto it = state.Textures.find(path); it != state.Textures.end())
				return it->second.Asset;
		}

		const std::filesystem::path resolved = Project::ResolveAssetPath(path);
		CacheEntry<TextureData> entry{ LoadTexture(resolved, path), resolved, WriteTimeOf(resolved) };

		std::scoped_lock lock(state.Mutex);
		auto [it, inserted] = state.Textures.try_emplace(path, std::move(entry));
		return it->second.Asset;
	}

	size_t AssetManager::ReloadModified()
	{
		AssetState& state = GetState();
		size_t reloaded = 0;
		auto sweep = [&](auto& cache) {
			for (auto it = cache.begin(); it != cache.end();)
			{
				if (WriteTimeOf(it->second.ResolvedPath) != it->second.WriteTime)
				{
					it = cache.erase(it);
					reloaded++;
				}
				else
				{
					++it;
				}
			}
		};

		std::scoped_lock lock(state.Mutex);
		sweep(state.Meshes);
		sweep(state.Textures);
		if (reloaded > 0)
			state.Generation++;
		return reloaded;
	}

	void AssetManager::Clear()
	{
		AssetState& state = GetState();
		std::scoped_lock lock(state.Mutex);
		state.Meshes.clear();
		state.Textures.clear();
		state.Generation++;
	}

	uint64_t AssetManager::GetGeneration() { return GetState().Generation; }

}
