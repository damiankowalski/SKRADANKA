#include "pnpch.h"
#include "Penumbra/Renderer/GpuResourceCache.h"

namespace Penumbra {

	namespace {

		uint64_t TextureKey(const TextureData* texture, bool srgb)
		{
			return (reinterpret_cast<uint64_t>(texture) << 1) | (srgb ? 1u : 0u);
		}

		uint32_t MipCount(uint32_t width, uint32_t height)
		{
			uint32_t count = 1;
			while ((width | height) > 1)
			{
				width = std::max(width / 2, 1u);
				height = std::max(height / 2, 1u);
				count++;
			}
			return count;
		}

		float SRGBToLinear(uint8_t value)
		{
			const float c = static_cast<float>(value) / 255.0f;
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		}

		uint8_t LinearToSRGB(float c)
		{
			const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
			return static_cast<uint8_t>(std::clamp(s * 255.0f + 0.5f, 0.0f, 255.0f));
		}

		// 2x2 box filter; colour textures are filtered in linear space so mips do not darken.
		std::vector<uint8_t> Downsample(const std::vector<uint8_t>& source, uint32_t width, uint32_t height, bool srgb)
		{
			const uint32_t outWidth = std::max(width / 2, 1u);
			const uint32_t outHeight = std::max(height / 2, 1u);
			std::vector<uint8_t> result(static_cast<size_t>(outWidth) * outHeight * 4);
			for (uint32_t y = 0; y < outHeight; y++)
			{
				for (uint32_t x = 0; x < outWidth; x++)
				{
					for (uint32_t c = 0; c < 4; c++)
					{
						float sum = 0.0f;
						for (uint32_t dy = 0; dy < 2; dy++)
						{
							for (uint32_t dx = 0; dx < 2; dx++)
							{
								const uint32_t sx = std::min(x * 2 + dx, width - 1);
								const uint32_t sy = std::min(y * 2 + dy, height - 1);
								const uint8_t value = source[(static_cast<size_t>(sy) * width + sx) * 4 + c];
								sum += (srgb && c < 3) ? SRGBToLinear(value) : static_cast<float>(value) / 255.0f;
							}
						}
						const float average = sum * 0.25f;
						result[(static_cast<size_t>(y) * outWidth + x) * 4 + c] =
							(srgb && c < 3) ? LinearToSRGB(average) : static_cast<uint8_t>(std::clamp(average * 255.0f + 0.5f, 0.0f, 255.0f));
					}
				}
			}
			return result;
		}

		nvrhi::TextureHandle CreateSolid(nvrhi::IDevice* device, nvrhi::ICommandList* commandList, const char* name, uint32_t rgba,
			nvrhi::TextureDimension dimension = nvrhi::TextureDimension::Texture2D, uint32_t arraySize = 1)
		{
			nvrhi::TextureDesc desc;
			desc.width = 1;
			desc.height = 1;
			desc.arraySize = arraySize;
			desc.dimension = dimension;
			desc.format = nvrhi::Format::RGBA8_UNORM;
			desc.debugName = name;
			desc.initialState = nvrhi::ResourceStates::ShaderResource;
			desc.keepInitialState = true;
			nvrhi::TextureHandle texture = device->createTexture(desc);
			for (uint32_t slice = 0; slice < arraySize; slice++)
				commandList->writeTexture(texture, slice, 0, &rgba, 4);
			return texture;
		}

	}

	GpuResourceCache::GpuResourceCache(nvrhi::IDevice* device)
		: m_Device(device)
	{
	}

	void GpuResourceCache::CreateDefaults(nvrhi::ICommandList* commandList)
	{
		m_White = CreateSolid(m_Device, commandList, "White", 0xFFFFFFFFu);
		m_Black = CreateSolid(m_Device, commandList, "Black", 0xFF000000u);
		m_FlatNormal = CreateSolid(m_Device, commandList, "FlatNormal", 0xFFFF8080u);
		m_BlackCube = CreateSolid(m_Device, commandList, "BlackCube", 0xFF000000u, nvrhi::TextureDimension::TextureCube, 6);
	}

	nvrhi::TextureHandle GpuResourceCache::Upload(const TextureData& texture, bool srgb, nvrhi::ICommandList* commandList)
	{
		const bool hdr = texture.Format == TextureFormat::RGBA32F;
		nvrhi::TextureDesc desc;
		desc.width = texture.Width;
		desc.height = texture.Height;
		desc.mipLevels = hdr ? 1 : MipCount(texture.Width, texture.Height);
		desc.format = hdr ? nvrhi::Format::RGBA32_FLOAT : (srgb ? nvrhi::Format::SRGBA8_UNORM : nvrhi::Format::RGBA8_UNORM);
		desc.debugName = "AssetTexture";
		desc.initialState = nvrhi::ResourceStates::ShaderResource;
		desc.keepInitialState = true;
		nvrhi::TextureHandle handle = m_Device->createTexture(desc);
		if (!handle)
			return nullptr;

		commandList->writeTexture(handle, 0, 0, texture.Pixels.data(), static_cast<size_t>(texture.Width) * texture.GetBytesPerPixel());
		if (!hdr)
		{
			std::vector<uint8_t> level = texture.Pixels;
			uint32_t width = texture.Width, height = texture.Height;
			for (uint32_t mip = 1; mip < desc.mipLevels; mip++)
			{
				level = Downsample(level, width, height, srgb);
				width = std::max(width / 2, 1u);
				height = std::max(height / 2, 1u);
				commandList->writeTexture(handle, 0, mip, level.data(), static_cast<size_t>(width) * 4);
			}
		}
		return handle;
	}

	nvrhi::ITexture* GpuResourceCache::GetTexture(const TextureData* texture, bool srgb, nvrhi::ICommandList* commandList)
	{
		if (!texture || !texture->IsValid())
			return nullptr;
		const uint64_t key = TextureKey(texture, srgb);
		if (const auto it = m_Textures.find(key); it != m_Textures.end())
			return it->second.Texture;
		nvrhi::TextureHandle handle = Upload(*texture, srgb, commandList);
		if (!handle)
			return nullptr;
		m_Textures[key] = { handle, nullptr };
		return handle;
	}

	nvrhi::ITexture* GpuResourceCache::GetTexture(const std::shared_ptr<const TextureData>& texture, bool srgb, nvrhi::ICommandList* commandList)
	{
		if (!texture || !texture->IsValid())
			return nullptr;
		const uint64_t key = TextureKey(texture.get(), srgb);
		if (const auto it = m_Textures.find(key); it != m_Textures.end())
			return it->second.Texture;
		nvrhi::TextureHandle handle = Upload(*texture, srgb, commandList);
		if (!handle)
			return nullptr;
		m_Textures[key] = { handle, texture };
		return handle;
	}

	const GpuMesh* GpuResourceCache::GetMesh(const std::shared_ptr<const MeshData>& mesh, nvrhi::ICommandList* commandList)
	{
		if (!mesh || mesh->Vertices.empty() || mesh->Indices.empty())
			return nullptr;
		if (const auto it = m_Meshes.find(mesh.get()); it != m_Meshes.end())
			return &it->second;

		GpuMesh gpu;
		gpu.Source = mesh;

		nvrhi::BufferDesc vertexDesc;
		vertexDesc.byteSize = mesh->Vertices.size() * sizeof(Vertex);
		vertexDesc.isVertexBuffer = true;
		const bool skinned = mesh->IsSkinned();
		if (skinned)
			vertexDesc.structStride = sizeof(Vertex);
		vertexDesc.debugName = mesh->Name + ".Vertices";
		vertexDesc.initialState = nvrhi::ResourceStates::VertexBuffer;
		vertexDesc.keepInitialState = true;
		gpu.VertexBuffer = m_Device->createBuffer(vertexDesc);

		nvrhi::BufferDesc indexDesc;
		indexDesc.byteSize = mesh->Indices.size() * sizeof(uint32_t);
		indexDesc.isIndexBuffer = true;
		indexDesc.debugName = mesh->Name + ".Indices";
		indexDesc.initialState = nvrhi::ResourceStates::IndexBuffer;
		indexDesc.keepInitialState = true;
		gpu.IndexBuffer = m_Device->createBuffer(indexDesc);

		if (skinned)
		{
			nvrhi::BufferDesc skinDesc;
			skinDesc.byteSize = mesh->SkinVertices.size() * sizeof(SkinVertex);
			skinDesc.structStride = sizeof(SkinVertex);
			skinDesc.debugName = mesh->Name + ".Skin";
			skinDesc.initialState = nvrhi::ResourceStates::ShaderResource;
			skinDesc.keepInitialState = true;
			gpu.SkinBuffer = m_Device->createBuffer(skinDesc);
			if (!gpu.SkinBuffer)
				return nullptr;
		}

		if (!gpu.VertexBuffer || !gpu.IndexBuffer)
			return nullptr;
		commandList->writeBuffer(gpu.VertexBuffer, mesh->Vertices.data(), vertexDesc.byteSize);
		commandList->writeBuffer(gpu.IndexBuffer, mesh->Indices.data(), indexDesc.byteSize);
		if (gpu.SkinBuffer)
			commandList->writeBuffer(gpu.SkinBuffer, mesh->SkinVertices.data(), mesh->SkinVertices.size() * sizeof(SkinVertex));
		return &m_Meshes.emplace(mesh.get(), std::move(gpu)).first->second;
	}

	void GpuResourceCache::CollectGarbage()
	{
		for (auto it = m_Meshes.begin(); it != m_Meshes.end();)
		{
			// A mesh's textures are shared pointers: the sweep below drops them once nothing else holds them.
			if (it->second.Source.use_count() == 1)
				it = m_Meshes.erase(it);
			else
				++it;
		}
		std::erase_if(m_Textures, [](const auto& entry) { return entry.second.Owner && entry.second.Owner.use_count() == 1; });
	}

	void GpuResourceCache::Clear()
	{
		m_Meshes.clear();
		m_Textures.clear();
	}

}
