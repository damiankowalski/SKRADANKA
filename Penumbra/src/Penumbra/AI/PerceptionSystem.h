#pragma once

#include "Penumbra/AI/LuminanceProvider.h"
#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <vector>

namespace Penumbra {

	// Everything that went into one observer -> target evaluation; exposed to scripts
	// and the editor so designers can see *why* a guard did or did not notice the player.
	struct VisibilityResult
	{
		float Stimulus = 0.0f;          // B
		float VisibleFraction = 0.0f;   // f
		float Contrast = 0.0f;          // C_eff after atmosphere
		float ContrastThreshold = 0.0f; // C_t
		float SizeFactor = 0.0f;        // S
		float Peripheral = 0.0f;        // P(theta)
		float Motion = 1.0f;            // M
		float Atmosphere = 1.0f;        // exp(-optical depth)
		float TargetLuminance = 0.0f;   // mean L_p over visible points
		float BackgroundLuminance = 0.0f; // mean L_b over visible points
		float Distance = 0.0f;
		uint32_t VisiblePoints = 0;
		uint32_t TotalPoints = 0;
	};

	struct NoiseEvent
	{
		glm::vec3 Position{ 0.0f };
		float Radius = 0.0f;
		UUID Source = UUID::Invalid;
	};

	class PerceptionSystem
	{
	public:
		// Vision is re-evaluated at this interval (spec suggests 5-10 Hz); suspicion integrates every frame.
		static constexpr float VisionInterval = 0.1f;
		// How long the light on a target stays valid. Shadow rays to every light are the expensive
		// part; refreshing at 20 Hz is invisible (the HUD gem smooths it) and shared by all observers.
		static constexpr float LightingRefresh = 0.05f;

		explicit PerceptionSystem(Scene& scene);

		void Update(Timestep ts);

		void EmitNoise(const glm::vec3& position, float radius, UUID source);

		// Full evaluation of what observer sees of target right now (does not modify state).
		VisibilityResult ComputeVisibility(Entity observer, Entity target);
		// Brightness at a point as perceived by NPC eyes (omnidirectional).
		float SampleLuminance(const glm::vec3& point);
		// Integrated extinction (fog + smoke) along a segment.
		float OpticalDepth(const glm::vec3& from, const glm::vec3& to);

		void SetLuminanceProvider(Scope<LuminanceProvider> provider) { m_Provider = std::move(provider); }

		// Light on the target's silhouette points this frame (shared by every observer).
		struct TargetLighting
		{
			std::vector<PointLighting> Points; // one per silhouette point, same order
			float Bounce = 0.0f;               // irradiance reflected up from the floor under the target
			float Time = -1.0f; // perception time of the last refresh
		};
		const TargetLighting& GetTargetLighting(Entity target);
		LuminanceProvider& GetLuminanceProvider() { return *m_Provider; }

		const std::vector<NoiseEvent>& GetRecentNoises() const { return m_RecentNoises; }

	private:
		void UpdateTargets(float dt);
		void ProcessNoises();
		// visionElapsed > 0 re-evaluates vision and adaptation over that much time.
		void UpdateObserver(Entity observer, float dt, float visionElapsed);
		float EnvironmentLuminanceFor(Entity observer, float extraLuminanceSum, uint32_t extraSamples);
		bool IsSightBlocker(UUID entity, const std::vector<UUID>& ignore);

	private:
		Scene& m_Scene;
		Scope<LuminanceProvider> m_Provider;
		std::vector<NoiseEvent> m_PendingNoises;
		std::vector<NoiseEvent> m_RecentNoises;
		std::unordered_map<UUID, TargetLighting> m_TargetLighting;
		std::unordered_map<UUID, float> m_VisionTimers; // per observer, staggered so guards do not all think on one frame
		struct ObserverFocus
		{
			UUID Target = UUID::Invalid;
			float Stimulus = 0.0f;
		};
		std::unordered_map<UUID, ObserverFocus> m_Focus; // last evaluation of each observer
		float m_Time = 0.0f;
	};

}
