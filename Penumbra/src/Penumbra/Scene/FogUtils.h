#pragma once

#include "Penumbra/Core/Base.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <vector>

// How much fog there is at a point: the environment's fog (thinning with height) plus local fog
// boxes (FogVolumeComponent), and the light it sends towards an eye. Shared by the renderer (mirrored
// in shaders/Fog.hlsli, Fog.hlsl and FogIntegrate.hlsl) and the perception system (guards see the
// same fog the player sees, glow included). Change both or neither.

namespace Penumbra {

	class Scene;

	// A lamp without a shadow map lights the fog at this share of its strength (it would shine through
	// walls). The renderer's default (SceneRendererSettings::FogUnshadowedLight) and perception's value.
	constexpr float FogUnshadowedLight = 0.5f;

	// Henyey-Greenstein phase, scaled so scattering the same way in every direction gives 1. cosTheta is
	// the cosine between the light's travel (lamp to point) and the way to the eye (point to eye).
	// Keep in step with FogPhase in shaders/Fog.hlsli.
	float FogPhase(float cosTheta, float anisotropy);

	// What the fog does to a sight line: how much of what lies behind it gets through, and the light it
	// adds on the way (luminance, the units of PerceptionModel::SurfaceLuminance).
	struct FogPath
	{
		float Transmittance = 1.0f;
		float InScattered = 0.0f;
	};

	struct FogBox
	{
		glm::vec3 Center{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f }; // box space to world
		glm::vec3 HalfExtents{ 1.0f };
		float Density = 0.0f;
		float EdgeFade = 1.0f;

		// 0..1: 1 deep inside, falling to 0 at the sides over EdgeFade metres.
		float Weight(const glm::vec3& point) const;
		// The part of the segment from + direction * [0, length] inside the box, if any.
		bool Clip(const glm::vec3& from, const glm::vec3& direction, float length, float& enter, float& exit) const;
	};

	struct FogMedium
	{
		float Density = 0.0f;        // Environment.FogDensity
		float Height = 0.0f;         // Environment.FogHeight
		float HeightFalloff = 0.0f;  // Environment.FogHeightFalloff
		std::vector<FogBox> Boxes;   // enabled FogVolume components (ignored, like the renderer, without VolumetricFog)
		std::vector<FogBox> Clears;  // enabled Clear FogVolumes: interiors the environment's fog stays out of (likewise)

		// How it is lit (Environment): its own glow, and how much of the lamps' light it scatters.
		float ColorLuminance = 0.0f; // luminance of FogColor
		float Scattering = 0.5f;     // FogScattering
		float Anisotropy = 0.3f;     // FogAnisotropy
		bool Volumetric = true;      // VolumetricFog: lamps light the fog (off: only FogColor)

		// The scene's environment and fog volumes now.
		static FogMedium FromScene(Scene& scene);

		// No fog anywhere: sight lines are untouched.
		bool IsEmpty() const { return Density <= 0.0f && Boxes.empty(); }

		// The environment's part at a height.
		float GlobalDensityAt(float y) const;
		// 0..1: how much of the environment's fog a Clear volume keeps out at a point (the strongest one).
		float ClearAt(const glm::vec3& point) const;
		// Extinction per metre at a point: environment * (1 - ClearAt) + boxes.
		float DensityAt(const glm::vec3& point) const;
		// Integral of DensityAt along the segment: exact for the environment's part, numerical
		// (BoxSamples midpoints over the part inside each box) for the boxes, and for what the Clear volumes
		// take away (steps of at most ClearStep metres over the parts inside any of them).
		float OpticalDepth(const glm::vec3& from, const glm::vec3& to) const;

		// The fog along the segment as an eye at `from` sees it: steps of at most MaxScatterStep metres
		// (midpoint samples), each adding its glow the way shaders/FogIntegrate.hlsl adds a slice's:
		// S += T * source * (1 - exp(-density * step)), T *= exp(-density * step). The source is the
		// fog's own glow plus Scattering times lampLight(point), the luminance the lamps send from that
		// point towards `from` (phase and shadows included; only called where there is fog, and only
		// with Volumetric).
		FogPath Integrate(const glm::vec3& from, const glm::vec3& to, const std::function<float(const glm::vec3&)>& lampLight) const;

		static constexpr int BoxSamples = 8;
		static constexpr float ClearStep = 0.25f;
		// Fine enough to catch a lamp's peak (the falloff is steep near it); the walk stops once almost nothing
		// behind gets through (MinScatterTransmittance).
		static constexpr float MaxScatterStep = 0.75f;
		static constexpr int MinScatterSteps = 4;
		static constexpr int MaxScatterSteps = 64;
		static constexpr float MinScatterTransmittance = 1e-3f;
	};

}
