#pragma once

#include "Penumbra/AI/LuminanceProvider.h"
#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <functional>
#include <optional>

#include <unordered_set>
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
		float Loudness = 0.0f; // 0-10
		UUID Source = UUID::Invalid;
		std::string Cause;     // what made it ("Footstep", "Impact"...), reported to scripts
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

		// A noise NPCs may hear, processed on the next perception update. Loudness is on the
		// 0-10 scale of docs/Perception.md (3 = a quiet crack, 9 = a metal bucket on cobbles).
		void EmitNoise(const glm::vec3& position, float loudness, UUID source, std::string cause = {});
		// Loudness (0-10) the observer would hear a noise at, after distance, walls and paths
		// around them; <= 0 means inaudible. Does not include the scene's ambient masking.
		float HearingLevel(Entity observer, const glm::vec3& position, float loudness, UUID source = UUID::Invalid);

		// Gives the observer a stimulus of the given strength (0-10). It is taken when it is at
		// least as strong as what the observer is still reacting to; it then sets the reaction
		// time, the stimulus position and the state. Returns whether it was taken.
		bool Notify(Entity observer, const glm::vec3& position, float strength, StimulusKind kind, std::string_view cause);
		// Ends any reaction and clears suspicion (alert memory is kept).
		static void Calm(PerceptionComponent& perception);

		// A distraction offers an innocent explanation ("just the storm") to every NPC reacting to
		// something it heard or was told (never to what it saw), optionally only within radius of
		// a centre. Those for whom it wins (PerceptionModel::DistractionWins, rolls from the scene's
		// random generator) calm down. Returns them.
		std::vector<Entity> ExplainAway(float strength, std::string_view cause, std::optional<glm::vec3> center = std::nullopt,
			float radius = 0.0f, float spread = 0.0f);

		// Knocks an NPC out (or kills it): it stops perceiving and walking, its reaction ends, its
		// physics character is removed (the body lies where it fell) and it becomes a Body target
		// other NPCs can notice. Returns false when it was already down.
		bool KnockOut(Entity npc, VitalState state = VitalState::Unconscious);
		// Takes health from an NPC (adding Vitality with full health when missing); at 0 it dies
		// (KnockOut Dead). Returns the health left.
		float Damage(Entity npc, float amount);
		static bool IsAwake(Entity entity);

		// The speaker calls out what he is reacting to. Every other awake NPC who hears the shout
		// (loudness 0-10, like any noise) gets a Reported stimulus at the speaker's stimulus
		// position, one level weaker than the speaker's reaction. Returns those who took it.
		std::vector<Entity> Shout(Entity speaker, float loudness = 8.0f);

		// Full evaluation of what observer sees of target right now (does not modify state).
		VisibilityResult ComputeVisibility(Entity observer, Entity target);
		// Whether the observer's body and the target's are in contact: their capsules (the
		// character controllers, or the target's Radius) closer than the observer's TouchRange.
		bool IsTouching(Entity observer, Entity target);
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
		// Surface.SoundLoss of a body (negative: a wall).
		float SoundLossOf(UUID entity);
		// Levels lost by sound going straight from one point to another through doors (Surface.SoundLoss),
		// or nullopt when a wall is in the way. doorsOnly ignores everything but doors.
		std::optional<float> TransmissionLoss(const glm::vec3& from, const glm::vec3& to, const std::function<bool(UUID)>& blocker, bool doorsOnly);
		float HorizontalDistance(Entity a, Entity b);

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
		std::unordered_map<UUID, UUID> m_Seeing;         // target each observer currently sees clearly (B >= 1)
		std::unordered_map<UUID, std::unordered_set<UUID>> m_Noticed; // bodies and arrows each observer has already noticed
		float m_Time = 0.0f;
	};

}
