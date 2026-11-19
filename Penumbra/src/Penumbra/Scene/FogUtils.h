#pragma once

#include "Penumbra/Core/Base.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

// How much fog there is at a point: the environment's fog (thinning with height) plus local fog
// boxes (FogVolumeComponent). Shared by the renderer (mirrored in shaders/Fog.hlsli, which lights
// it) and the perception system (guards see through the same fog the player sees). Change both or
// neither.

namespace Penumbra {

	class Scene;

	struct FogBox
	{
		glm::vec3 Center{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // box space to world
		glm::vec3 HalfExtents{ 1.0f };
		float Density = 0.0f;
		float EdgeFade = 1.0f;

		// 0..1: 1 deep inside, falling to 0 at the sides over EdgeFade metres.
		float Weight(const glm::vec3& point) const;
	};

	struct FogMedium
	{
		float Density = 0.0f;        // Environment.FogDensity
		float Height = 0.0f;         // Environment.FogHeight
		float HeightFalloff = 0.0f;  // Environment.FogHeightFalloff
		std::vector<FogBox> Boxes;   // enabled FogVolume components

		// The scene's environment and fog volumes now.
		static FogMedium FromScene(Scene& scene);

		// The environment's part at a height.
		float GlobalDensityAt(float y) const;
		// Extinction per metre at a point (environment + boxes).
		float DensityAt(const glm::vec3& point) const;
		// Integral of DensityAt along the segment: exact for the environment's part, numerical
		// (BoxSamples midpoints over the part inside each box) for the boxes.
		float OpticalDepth(const glm::vec3& from, const glm::vec3& to) const;

		static constexpr int BoxSamples = 8;
	};

}
