#pragma once

#include "Penumbra/Core/UUID.h"

#include <glm/glm.hpp>

#include <vector>

namespace Penumbra {

	class Scene;

	struct IncomingLight
	{
		glm::vec3 Direction{ 0.0f, 1.0f, 0.0f }; // towards the light
		float Illuminance = 0.0f;                // on a surface facing the light, shadows applied
	};

	// Unshadowed light arriving at a point, independent of any surface orientation, so one
	// gather can be evaluated for any normal or viewing direction.
	struct PointLighting
	{
		float Ambient = 0.0f;
		std::vector<IncomingLight> Lights;

		// Flat surface with normal n (Lambert).
		float Facing(const glm::vec3& normal) const;
		// Rounded body (a person) seen from viewDirection (unit, from the body towards the viewer):
		// the visible half of a sphere receives on average E * (1 + cos) / 2 from each light, so a
		// side- or back-lit figure is still partly lit, as in reality.
		float Rounded(const glm::vec3& viewDirection) const;
		// Average over all viewing directions: how much light the body is standing in.
		float Average() const;
	};

	struct SurfaceSample
	{
		float Albedo = 0.5f;   // luminance of the diffuse colour
		float Emissive = 0.0f; // emitted luminance
	};

	// Source of brightness values for perception (spec section 5). The analytic provider
	// below is approach (1); a GPU "NPC eye" provider (approach 3) can implement the same
	// interface without touching the perception system.
	class LuminanceProvider
	{
	public:
		virtual ~LuminanceProvider() = default;

		virtual void BeginFrame(float time) = 0;
		// Illuminance arriving at a point. With a zero normal the point is treated as
		// omnidirectional (used for "how bright is it where I stand").
		virtual float Illuminance(const glm::vec3& point, const glm::vec3& normal, const std::vector<UUID>& ignore) const = 0;
		// All unshadowed light reaching a point (ambient plus each light's direction and strength).
		virtual PointLighting GatherLighting(const glm::vec3& point, const std::vector<UUID>& ignore) const = 0;
		virtual SurfaceSample GetSurface(UUID entity) const = 0;
		// Luminance of the sky seen when a sight line escapes the level.
		virtual float SkyLuminance() const = 0;
		// The lamps' light a speck of fog at the point sends towards the eye (towardEye: unit, from the point
		// to the eye), before FogScattering: each lamp's strength and falloff, its Henyey-Greenstein phase
		// (anisotropy) and its shadow as the renderer casts it (Scene/FogUtils.h, shaders/Fog.hlsl).
		virtual float FogLight(const glm::vec3& point, const glm::vec3& towardEye, float anisotropy) const = 0;
	};

	// Approach (1): sums the contributions of the scene's lights with physics shadow rays.
	class AnalyticLuminanceProvider final : public LuminanceProvider
	{
	public:
		explicit AnalyticLuminanceProvider(Scene& scene) : m_Scene(scene) {}

		void BeginFrame(float time) override;
		float Illuminance(const glm::vec3& point, const glm::vec3& normal, const std::vector<UUID>& ignore) const override;
		PointLighting GatherLighting(const glm::vec3& point, const std::vector<UUID>& ignore) const override;
		SurfaceSample GetSurface(UUID entity) const override;
		float SkyLuminance() const override { return m_SkyLuminance; }
		float FogLight(const glm::vec3& point, const glm::vec3& towardEye, float anisotropy) const override;

	private:
		struct LightSample
		{
			enum class Kind : uint8_t
			{
				Directional,
				Point,
				Spot
			};

			Kind Type = Kind::Point;
			UUID Owner = UUID::Invalid;
			std::vector<UUID> Related; // the light, its ancestors and descendants (the torch body itself)
			glm::vec3 Position{ 0.0f };
			glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
			float Intensity = 0.0f; // luminance of colour * intensity
			float Range = 0.0f;
			float CosOuter = 0.0f;
			float CosInner = 1.0f;
			bool CastsShadows = true;
			bool RenderShadows = false; // the renderer gives it a shadow map (CastShadows): it shadows the fog
		};

		bool IsOccluded(const glm::vec3& point, const LightSample& light, const std::vector<UUID>& ignore) const;

	private:
		Scene& m_Scene;
		std::vector<LightSample> m_Lights;
		float m_Ambient = 0.0f;
		float m_SkyLuminance = 0.0f;
	};

}
