#pragma once

#include <cstddef>
#include <cstdint>

namespace Penumbra {

	struct ShaderBytecode
	{
		const void* Data = nullptr;
		size_t Size = 0;

		explicit operator bool() const { return Data != nullptr && Size > 0; }
	};

	enum class ShaderFormat
	{
		Dxil,  // Direct3D 12 (Windows builds only)
		Spirv, // Vulkan
	};

	// Bytecode compiled at build time (see cmake/Shaders.cmake) and linked into the engine.
	class ShaderLibrary
	{
	public:
		// Empty when the shader does not exist or was not compiled for that format.
		static ShaderBytecode Find(const char* name, ShaderFormat format);
	};

}
