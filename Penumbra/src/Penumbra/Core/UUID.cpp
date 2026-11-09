#include "pnpch.h"
#include "Penumbra/Core/UUID.h"

#include <random>

namespace Penumbra {

	namespace {
		uint64_t GenerateNonZero()
		{
			thread_local std::mt19937_64 s_Engine(std::random_device{}());
			thread_local std::uniform_int_distribution<uint64_t> s_Distribution(1, UINT64_MAX);
			return s_Distribution(s_Engine);
		}
	}

	UUID::UUID()
		: m_UUID(GenerateNonZero())
	{
	}

}
