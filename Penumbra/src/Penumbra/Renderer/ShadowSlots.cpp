#include "pnpch.h"
#include "Penumbra/Renderer/ShadowSlots.h"

#include <algorithm>
#include <limits>

namespace Penumbra {

	ShadowSlotAssignment AssignShadowSlots(const std::vector<ShadowCandidate>& candidates, const Frustum& view, const glm::vec3& eye,
		uint32_t slots)
	{
		struct Ranked
		{
			size_t LightIndex;
			float Distance;
		};
		std::vector<Ranked> inView;
		inView.reserve(candidates.size());
		ShadowSlotAssignment result;
		for (const ShadowCandidate& candidate : candidates)
		{
			if (view.IntersectsSphere(candidate.Position, candidate.Range))
				inView.push_back({ candidate.LightIndex, glm::distance(candidate.Position, eye) });
			else
				result.LeftOut.push_back(candidate.LightIndex);
		}
		// Ties (equal distances) by index, so the choice does not depend on the sort's stability.
		std::sort(inView.begin(), inView.end(), [](const Ranked& a, const Ranked& b) {
			return a.Distance != b.Distance ? a.Distance < b.Distance : a.LightIndex < b.LightIndex;
		});
		for (size_t i = 0; i < inView.size(); i++)
			(i < slots ? result.Shadowed : result.LeftOut).push_back(inView[i].LightIndex);
		result.Overflow = static_cast<uint32_t>(inView.size() > slots ? inView.size() - slots : 0);
		return result;
	}

	std::vector<uint32_t> AssignCacheSlots(const std::vector<uint64_t>& lamps, std::vector<ShadowCacheSlot>& cache, uint64_t frame)
	{
		PN_CORE_ASSERT(lamps.size() <= cache.size(), "more shadowed lamps than cache slots");
		constexpr uint32_t None = std::numeric_limits<uint32_t>::max();
		std::vector<uint32_t> result(lamps.size(), None);
		std::vector<bool> taken(cache.size(), false);
		// Lamps that already have a map keep it.
		for (size_t i = 0; i < lamps.size(); i++)
		{
			for (uint32_t slot = 0; slot < cache.size(); slot++)
			{
				if (!taken[slot] && cache[slot].Lamp == lamps[i])
				{
					result[i] = slot;
					taken[slot] = true;
					break;
				}
			}
		}
		// The others take the slots nobody needs this frame, the longest unused first (free ones have LastUsed 0).
		std::vector<uint32_t> spare;
		for (uint32_t slot = 0; slot < cache.size(); slot++)
		{
			if (!taken[slot])
				spare.push_back(slot);
		}
		std::sort(spare.begin(), spare.end(), [&](uint32_t a, uint32_t b) {
			return cache[a].LastUsed != cache[b].LastUsed ? cache[a].LastUsed < cache[b].LastUsed : a < b;
		});
		size_t next = 0;
		for (size_t i = 0; i < lamps.size(); i++)
		{
			if (result[i] != None)
				continue;
			const uint32_t slot = spare[next++];
			cache[slot] = { lamps[i], 0, 0, frame };
			result[i] = slot;
		}
		for (uint32_t slot : result)
			cache[slot].LastUsed = frame;
		return result;
	}

}
