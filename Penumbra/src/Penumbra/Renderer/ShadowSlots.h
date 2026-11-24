#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Renderer/RenderCamera.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Penumbra {

	// A point or spot light with CastShadows, asking for one of the renderer's few shadow maps.
	struct ShadowCandidate
	{
		size_t LightIndex = 0; // the caller's own index
		glm::vec3 Position{ 0.0f };
		float Range = 0.0f;
	};

	struct ShadowSlotAssignment
	{
		std::vector<size_t> Shadowed; // LightIndex of the lamps that get a map; the position is the slot
		std::vector<size_t> LeftOut;  // LightIndex of the lamps left out of the picture
		uint32_t Overflow = 0;        // of those, lamps in view that found no map left
	};

	// A lamp with CastShadows never lights through walls. The shadow maps go to the lamps whose light reaches the
	// view (the sphere of their range meets the frustum), the nearest to the eye first. A lamp out of view lights
	// nothing on screen and is left out; a lamp in view with no map left (the farthest ones) is left out too rather
	// than drawn without its shadow, shining through every wall around it. Perception always shadows lamps by ray.
	ShadowSlotAssignment AssignShadowSlots(const std::vector<ShadowCandidate>& candidates, const Frustum& view, const glm::vec3& eye,
		uint32_t slots);

}
