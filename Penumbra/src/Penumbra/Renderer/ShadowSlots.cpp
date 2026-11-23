#include "pnpch.h"
#include "Penumbra/Renderer/ShadowSlots.h"

#include <algorithm>

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

}
