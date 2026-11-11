#pragma once

#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Core/UUID.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <functional>
#include <optional>
#include <vector>

namespace Penumbra {

	struct RaycastHit
	{
		UUID Entity = UUID::Invalid;
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
		float Distance = 0.0f;
	};

	struct ContactEvent
	{
		UUID EntityA = UUID::Invalid;
		UUID EntityB = UUID::Invalid;
		glm::vec3 Point{ 0.0f };
		float Speed = 0.0f; // closing speed along the contact normal when the contact began, m/s
	};

	// Returns true when the body owned by the entity should be considered by the query.
	using PhysicsEntityFilter = std::function<bool(UUID)>;

	// Jolt-backed rigid body simulation for a running scene. Bodies are created for every
	// entity that has a collider (static if it has no RigidBody), characters for every
	// CharacterController. Entities spawned at runtime get their bodies on the next step.
	class PhysicsWorld
	{
	public:
		static constexpr float FixedTimestep = 1.0f / 60.0f;
		static constexpr int MaxStepsPerFrame = 4;

		explicit PhysicsWorld(Scene& scene);
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		void Start();
		void Stop();
		void Step(Timestep ts);
		void OnEntityDestroyed(Entity entity);
		// Removes the entity's physics character (it no longer collides or falls); the caller removes
		// the CharacterController component so it is not created again on the next step.
		void RemoveCharacter(Entity entity);
		bool HasCharacter(UUID entity) const;

		std::optional<RaycastHit> Raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
			const PhysicsEntityFilter& filter = {}) const;
		std::vector<UUID> OverlapSphere(const glm::vec3& center, float radius) const;
		// World-space triangles of every static, solid body (the navmesh input). Indices are
		// appended relative to the vertices already in the output.
		void CollectStaticTriangles(std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices) const;

		void AddImpulse(Entity entity, const glm::vec3& impulse);
		void AddForce(Entity entity, const glm::vec3& force);
		void SetLinearVelocity(Entity entity, const glm::vec3& velocity);
		glm::vec3 GetLinearVelocity(Entity entity) const;
		void Teleport(Entity entity, const glm::vec3& position, const glm::quat& rotation);
		void AddRadialImpulse(const glm::vec3& center, float radius, float strength);

		void SetGravity(const glm::vec3& gravity);
		glm::vec3 GetGravity() const;

		bool HasBody(UUID entity) const;
		size_t GetBodyCount() const;

		std::vector<ContactEvent> ConsumeContactEvents();

	private:
		struct Impl;
		Scope<Impl> m_Impl;
	};

}
