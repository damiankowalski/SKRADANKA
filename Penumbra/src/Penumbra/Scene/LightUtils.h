#pragma once

#include "Penumbra/Core/UUID.h"

#include <glm/glm.hpp>

#include <cmath>

// Light falloff shared by the renderer (mirrored in shaders/Lighting.hlsli) and the
// perception system, so what the player sees and what NPCs "see" always agree.

namespace Penumbra::LightUtils {

	// Windowed inverse-square falloff: physically based near the light, reaching exactly
	// zero at range so lights can be culled.
	inline float DistanceAttenuation(float distance, float range)
	{
		if (range <= 0.0f || distance >= range)
			return 0.0f;
		const float ratio = distance / range;
		const float ratio4 = ratio * ratio * ratio * ratio;
		const float window = glm::clamp(1.0f - ratio4, 0.0f, 1.0f);
		return (window * window) / (distance * distance + 1.0f);
	}

	// cosOuter/cosInner are cosines of the half-angles.
	inline float SpotAttenuation(const glm::vec3& lightToPoint, const glm::vec3& spotDirection, float cosOuter, float cosInner)
	{
		const float cosAngle = glm::dot(glm::normalize(lightToPoint), glm::normalize(spotDirection));
		const float t = glm::clamp((cosAngle - cosOuter) / std::max(cosInner - cosOuter, 1e-4f), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	// Deterministic torch flicker multiplier in [1 - amount, 1 + amount].
	inline float Flicker(float time, UUID seed, float amount)
	{
		if (amount <= 0.0f)
			return 1.0f;
		const float phase = static_cast<float>(static_cast<uint64_t>(seed) % 1000) * 0.137f;
		const float wave = std::sin(time * 9.1f + phase) * 0.5f + std::sin(time * 23.3f + phase * 1.7f) * 0.3f +
						   std::sin(time * 3.7f + phase * 0.3f) * 0.2f;
		return 1.0f + amount * wave;
	}

}
