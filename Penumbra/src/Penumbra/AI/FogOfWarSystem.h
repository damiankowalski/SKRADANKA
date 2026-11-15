#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Core/UUID.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace Penumbra {

	class Scene;
	struct FogOfWarComponent;

	// The player's view this frame: where his eye is, which way he faces and how far he sees.
	// The renderer darkens what lies outside it (shaders/FogOfWar.hlsli mirrors Coverage).
	struct FogOfWarView
	{
		glm::vec3 Eye{ 0.0f };
		glm::vec3 Forward{ 0.0f, 0.0f, -1.0f }; // horizontal, normalised
		float HalfAngle = 60.0f;                // degrees
		float EdgeSoftness = 15.0f;             // degrees
		float ViewDistance = 40.0f;
		float AwarenessRadius = 1.8f;
		float Brightness = 0.22f;
		float Saturation = 0.1f;
		UUID Viewer = UUID::Invalid;

		static FogOfWarView FromComponent(const FogOfWarComponent& component);
		// 0..1: how much of the point lies in the cone or the circle of awareness and within the
		// view distance, ignoring walls. Smooth edges; > 0.5 counts as inside.
		float Coverage(const glm::vec3& point) const;
	};

	// Fog of war (FogOfWarComponent): finds the viewer, places his eye and facing (where the primary
	// camera looks), and decides which characters he cannot see so the renderer leaves them out.
	// Runs after perception: everything has moved by then.
	class FogOfWarSystem
	{
	public:
		explicit FogOfWarSystem(Scene& scene);

		void Update(Timestep ts);

		// Testing switch (the runtime's F4 panel): no fog whatever the components say.
		void SetForcedOff(bool off) { m_ForcedOff = off; }
		bool IsForcedOff() const { return m_ForcedOff; }

		// The active view, if a viewer with an enabled FogOfWar component exists (and fog is not forced off).
		const std::optional<FogOfWarView>& GetView() const { return m_View; }
		// Whether the entity, or an entity above it in the hierarchy, is a character out of sight.
		bool IsHidden(Entity entity);
		// Whether the viewer sees the point now: inside the cone or circle and nothing solid on the line from his eye.
		// True when there is no fog.
		bool CanSee(const glm::vec3& point);
		// Whether the viewer sees any part of the entity (its silhouette points, or points up its height).
		bool CanSeeEntity(Entity entity);

	private:
		std::optional<FogOfWarView> ComputeView();
		bool IsLineClear(const glm::vec3& eye, const glm::vec3& point, UUID target);
		bool IsActor(Entity entity) const;

	private:
		Scene& m_Scene;
		bool m_ForcedOff = false;
		std::optional<FogOfWarView> m_View;
		std::unordered_map<UUID, float> m_LastSeen; // scene time each character was last in sight
		std::unordered_set<UUID> m_Hidden;
	};

}
