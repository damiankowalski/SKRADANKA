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

	// One slot of the shadow map cache: whose map it holds and what the map was drawn from. A lamp's shadow map is
	// drawn again only when that changes (a lamp moves, a door swings, someone walks within its reach), so a street
	// of lanterns costs its shadows once, not every frame.
	struct ShadowCacheSlot
	{
		uint64_t Lamp = 0;     // the lamp's UUID; 0: free
		uint64_t Pose = 0;     // where the lamp stood and how far it reached when its map was drawn; 0: not drawn
		uint64_t Content = 0;  // what the map shows: the lamp and the shadow casters in its reach
		uint64_t LastUsed = 0; // the frame it last served
	};

	// Gives each lamp that gets a shadow map this frame (UUIDs, nearest first) a slot of the cache: the one that already
	// holds its map, else a slot no such lamp holds, the longest unused first (cleared: Pose = Content = 0).
	// Returns the slot of each lamp in the same order.
	std::vector<uint32_t> AssignCacheSlots(const std::vector<uint64_t>& lamps, std::vector<ShadowCacheSlot>& cache, uint64_t frame);

}
