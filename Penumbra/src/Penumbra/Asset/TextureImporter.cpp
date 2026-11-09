#include "pnpch.h"
#include "Penumbra/Asset/Importers.h"

#include "Penumbra/Core/FileSystem.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace Penumbra {

	std::optional<TextureData> TextureImporter::LoadFromMemory(const uint8_t* data, size_t size, std::string& error)
	{
		if (!data || size == 0 || size > static_cast<size_t>(INT32_MAX))
		{
			error = "empty or oversized image buffer";
			return std::nullopt;
		}

		const int length = static_cast<int>(size);
		int width = 0, height = 0, channels = 0;
		TextureData texture;

		if (stbi_is_hdr_from_memory(data, length))
		{
			float* pixels = stbi_loadf_from_memory(data, length, &width, &height, &channels, 4);
			if (!pixels)
			{
				error = stbi_failure_reason();
				return std::nullopt;
			}
			texture.Format = TextureFormat::RGBA32F;
			texture.Pixels.resize(static_cast<size_t>(width) * height * 16);
			std::memcpy(texture.Pixels.data(), pixels, texture.Pixels.size());
			stbi_image_free(pixels);
		}
		else
		{
			stbi_uc* pixels = stbi_load_from_memory(data, length, &width, &height, &channels, 4);
			if (!pixels)
			{
				error = stbi_failure_reason();
				return std::nullopt;
			}
			texture.Format = TextureFormat::RGBA8;
			texture.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
			stbi_image_free(pixels);
		}

		texture.Width = static_cast<uint32_t>(width);
		texture.Height = static_cast<uint32_t>(height);
		return texture;
	}

	std::optional<TextureData> TextureImporter::LoadFromFile(const std::filesystem::path& path, std::string& error)
	{
		const std::optional<std::vector<uint8_t>> bytes = FileSystem::ReadBinaryFile(path);
		if (!bytes)
		{
			error = std::format("cannot read '{}'", path.string());
			return std::nullopt;
		}
		std::optional<TextureData> texture = LoadFromMemory(bytes->data(), bytes->size(), error);
		if (!texture)
			error = std::format("'{}': {}", path.string(), error);
		return texture;
	}

	bool TextureImporter::WritePNG(const std::filesystem::path& path, uint32_t width, uint32_t height, const uint8_t* rgba, uint32_t rowPitch)
	{
		int length = 0;
		unsigned char* png = stbi_write_png_to_mem(rgba, static_cast<int>(rowPitch), static_cast<int>(width), static_cast<int>(height), 4, &length);
		if (!png)
			return false;
		const bool ok = FileSystem::WriteBinaryFile(path, png, static_cast<size_t>(length));
		STBIW_FREE(png);
		return ok;
	}

}
