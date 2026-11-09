#pragma once

#include "Penumbra/Asset/MeshData.h"

#include <filesystem>
#include <optional>
#include <string>

namespace Penumbra {

	class TextureImporter
	{
	public:
		// LDR images (png, jpg, tga, bmp) load as RGBA8; .hdr loads as RGBA32F.
		static std::optional<TextureData> LoadFromFile(const std::filesystem::path& path, std::string& error);
		static std::optional<TextureData> LoadFromMemory(const uint8_t* data, size_t size, std::string& error);

		static bool WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, uint32_t rowPitch);
	};

	class GltfImporter
	{
	public:
		// Imports a .gltf/.glb file: all scene nodes are flattened into a single mesh,
		// with node transforms baked into vertices. Materials and textures are imported.
		static std::optional<MeshData> Import(const std::filesystem::path& path, std::string& error);
	};

}
