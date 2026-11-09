#pragma once

#include <cstdint>
#include <functional>

namespace Penumbra {

	// 64-bit random identifier used to reference entities and assets across sessions.
	// The value 0 is reserved and means "invalid / none".
	class UUID
	{
	public:
		UUID();
		constexpr UUID(uint64_t value) : m_UUID(value) {}

		constexpr operator uint64_t() const { return m_UUID; }
		constexpr bool IsValid() const { return m_UUID != 0; }

		static constexpr uint64_t Invalid = 0;

	private:
		uint64_t m_UUID;
	};

}

template<>
struct std::hash<Penumbra::UUID>
{
	size_t operator()(const Penumbra::UUID& uuid) const noexcept { return std::hash<uint64_t>()(static_cast<uint64_t>(uuid)); }
};
