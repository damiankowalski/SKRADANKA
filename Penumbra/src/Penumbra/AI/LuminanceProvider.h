#pragma once

#include "Penumbra/Core/UUID.h"

#include <glm/glm.hpp>

#include <vector>

namespace Penumbra {

	class Scene;

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
		virtual SurfaceSample GetSurface(UUID entity) const = 0;
		// Luminance of the sky seen when a sight line escapes the level.
		virtual float SkyLuminance() const = 0;
	};

	// Approach (1): sums the contributions of the scene's lights with physics shadow rays.
	class AnalyticLuminanceProvider final : public LuminanceProvider
	{
	public:
		explicit AnalyticLuminanceProvider(Scene& scene) : m_Scene(scene) {}

		void BeginFrame(float time) override;
		float Illuminance(const glm::vec3& point, const glm::vec3& normal, const std::vector<UUID>& ignore) const override;
		SurfaceSample GetSurface(UUID entity) const override;
		float SkyLuminance() const override { return m_SkyLuminance; }

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
		};

		bool IsOccluded(const glm::vec3& point, const LightSample& light, const std::vector<UUID>& ignore) const;

	private:
		Scene& m_Scene;
		std::vector<LightSample> m_Lights;
		float m_Ambient = 0.0f;
		float m_SkyLuminance = 0.0f;
	};

}
