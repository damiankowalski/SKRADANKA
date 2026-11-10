#pragma once

#include <cstdint>
#include <memory>
#include <utility>

#define PN_BIT(x) (1u << (x))
#define PN_STRINGIFY_IMPL(x) #x
#define PN_STRINGIFY(x) PN_STRINGIFY_IMPL(x)

namespace Penumbra {

	template<typename T>
	using Scope = std::unique_ptr<T>;

	template<typename T, typename... Args>
	constexpr Scope<T> CreateScope(Args&&... args)
	{
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

	template<typename T>
	using Ref = std::shared_ptr<T>;

	template<typename T, typename... Args>
	constexpr Ref<T> CreateRef(Args&&... args)
	{
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	using byte = uint8_t;

}

#include "Penumbra/Core/Assert.h"
#include "Penumbra/Core/Log.h"
