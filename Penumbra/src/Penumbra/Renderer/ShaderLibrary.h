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

	// DXIL bytecode compiled at build time (see cmake/Shaders.cmake) and linked into the engine.
	class ShaderLibrary
	{
	public:
		static ShaderBytecode Find(const char* name);
	};

}
