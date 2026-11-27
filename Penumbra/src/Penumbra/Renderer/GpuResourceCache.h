#pragma once

#include "Penumbra/Asset/MeshData.h"

#include <nvrhi/nvrhi.h>

#include <memory>
#include <unordered_map>

namespace Penumbra {

	struct GpuMesh
	{
		nvrhi::BufferHandle VertexBuffer;
		nvrhi::BufferHandle IndexBuffer;
		// Skinned meshes only: per-vertex joints/weights. Their VertexBuffer is also readable as a
		// structured buffer by the skinning compute shader.
		nvrhi::BufferHandle SkinBuffer;
		std::shared_ptr<const MeshData> Source; // keeps the CPU data alive while the GPU copy exists
	};

	// Uploads CPU assets to the GPU on first use and caches the results by asset identity.
	// Uploads are recorded into the command list passed to the getters.
	class GpuResourceCache
	{
	public:
		explicit GpuResourceCache(nvrhi::IDevice* device);

		const GpuMesh* GetMesh(const std::shared_ptr<const MeshData>& mesh, nvrhi::ICommandList* commandList);
		// srgb: colour data (albedo, emissive) vs. linear data (normals, metallic/roughness).
		nvrhi::ITexture* GetTexture(const TextureData* texture, bool srgb, nvrhi::ICommandList* commandList);
		// Uploads a texture whose lifetime the caller guarantees for as long as it is cached.
		nvrhi::ITexture* GetTexture(const std::shared_ptr<const TextureData>& texture, bool srgb, nvrhi::ICommandList* commandList);

		nvrhi::ITexture* GetWhiteTexture() const { return m_White; }
		nvrhi::ITexture* GetBlackTexture() const { return m_Black; }
		nvrhi::ITexture* GetFlatNormalTexture() const { return m_FlatNormal; }
		nvrhi::ITexture* GetBlackCubeTexture() const { return m_BlackCube; }

		// Drops entries whose CPU asset is no longer referenced anywhere else.
		void CollectGarbage();
		void Clear();

		// Creates and fills the small constant textures (needs a command list once).
		void CreateDefaults(nvrhi::ICommandList* commandList);

	private:
		nvrhi::TextureHandle Upload(const TextureData& texture, bool srgb, nvrhi::ICommandList* commandList);

	private:
		nvrhi::IDevice* m_Device;
		std::unordered_map<const MeshData*, GpuMesh> m_Meshes;

		struct TextureEntry
		{
			nvrhi::TextureHandle Texture;
			std::shared_ptr<const TextureData> Owner; // empty for textures passed by address (kept alive by their caller)
		};
		std::unordered_map<uint64_t, TextureEntry> m_Textures;

		nvrhi::TextureHandle m_White;
		nvrhi::TextureHandle m_Black;
		nvrhi::TextureHandle m_FlatNormal;
		nvrhi::TextureHandle m_BlackCube;
	};

}
