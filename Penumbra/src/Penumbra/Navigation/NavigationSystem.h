#pragma once

#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Navigation/NavMesh.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Penumbra {

	// Owns the scene's navmesh and walks NavAgent characters along it. The navmesh is baked
	// from the physics world's static geometry when the game starts (scenes with a NavMesh
	// component) or on first use, and rebuilt when a static collider is destroyed.
	//
	// Agents are steered after scripts and before physics: nav.move_to() in on_update takes
	// effect the same frame, and a script calling character.move() takes the agent back.
	class NavigationSystem
	{
	public:
		static constexpr float ReplanInterval = 1.0f;   // seconds between path refreshes while moving
		static constexpr float StuckTime = 2.0f;        // seconds of no progress before an agent is Stuck
		static constexpr float CornerRadius = 0.25f;    // a path corner counts as reached this close
		static constexpr float RebuildCooldown = 0.25f; // minimum seconds between automatic rebuilds

		explicit NavigationSystem(Scene& scene);
		~NavigationSystem();

		// Builds the navmesh now (scene settings from the first NavMesh component, else defaults).
		bool Bake(std::string& error);
		// The navmesh, baked on first use; nullptr when the scene has nothing walkable.
		const NavMesh* GetNavMesh();
		// The navmesh only if it has already been baked (never triggers a bake).
		const NavMesh* GetBakedNavMesh() const { return m_NavMesh.get(); }

		void Update(Timestep ts);
		void OnEntityDestroyed(Entity entity);

		// Starts walking the agent (a CharacterController; a NavAgent is added when missing) to
		// the destination. Returns false, with the agent's status set to Failed, when there is no
		// path. speed overrides NavAgent.Speed for this trip.
		bool MoveTo(Entity agent, const glm::vec3& destination, std::optional<float> speed = std::nullopt);
		void Stop(Entity agent);
		// Remaining path corners of a moving agent (debug drawing), or nullptr.
		const std::vector<glm::vec3>* GetAgentPath(UUID agent) const;

		std::optional<NavPath> FindPath(const glm::vec3& from, const glm::vec3& to);
		std::optional<glm::vec3> ClosestPoint(const glm::vec3& point, float searchRadius = 1.5f);
		// Deterministic random reachable point within radius of the centre.
		std::optional<glm::vec3> RandomPoint(const glm::vec3& center, float radius);

	private:
		struct AgentState
		{
			std::vector<glm::vec3> Path;
			size_t Next = 1;
			float Speed = 0.0f;
			float ReplanTimer = 0.0f;
			float StuckTimer = 0.0f;
		};

		bool Replan(Entity agent, NavAgentComponent& component, AgentState& state);
		void SteerAgent(Entity agent, NavAgentComponent& component, CharacterControllerComponent& controller, AgentState& state, float dt);
		void FaceDirection(Entity agent, const glm::vec3& direction, float maxDegrees);

	private:
		Scene& m_Scene;
		Scope<NavMesh> m_NavMesh;
		bool m_BakeAttempted = false;
		bool m_Dirty = false;
		float m_RebuildTimer = 0.0f;
		uint32_t m_RandomState = 0x2545F491u;
		std::unordered_map<UUID, AgentState> m_Agents;
	};

}
