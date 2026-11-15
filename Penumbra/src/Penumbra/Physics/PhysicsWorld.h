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
		glm::vec3 VelocityA{ 0.0f }; // velocities of the two bodies at the contact point, before the impact
		glm::vec3 VelocityB{ 0.0f };
	};

	// Returns true when the body owned by the entity should be considered by the query.
	using PhysicsEntityFilter = std::function<bool(UUID)>;

	// One body of a ragdoll, in world space: a capsule (HalfHeight > 0, along its local Y) or a
	// sphere, joined to its parent part at JointPosition: by a swing-twist joint whose twist axis is
	// TwistAxis, or (Hinge) by a hinge about HingeAxis turning between HingeMinDegrees and
	// HingeMaxDegrees from the pose it was created in (right-hand rule). Parts come parents first;
	// the first part is the root.
	struct RagdollPartDesc
	{
		int32_t Parent = -1;
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		float HalfHeight = 0.0f;
		float Radius = 0.1f;
		float Mass = 1.0f;
		glm::vec3 JointPosition{ 0.0f };
		glm::vec3 TwistAxis{ 0.0f, 1.0f, 0.0f };
		float SwingDegrees = 45.0f; // half cone angle the part may swing away from its parent
		float TwistDegrees = 20.0f; // twist either way about TwistAxis
		float Friction = 0.6f;
		bool Hinge = false;         // a knee or an elbow
		glm::vec3 HingeAxis{ 1.0f, 0.0f, 0.0f };
		float HingeMinDegrees = 0.0f;
		float HingeMaxDegrees = 0.0f;
	};

	struct RagdollPartTransform
	{
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

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
		// Sweeps a sphere along the direction: the first solid body it touches (sensors are skipped).
		// Distance is how far the sphere's centre travelled; 0 when it starts overlapping something.
		// Point is the contact point on the surface. A thick ray: a camera boom uses it so the near
		// plane never slips past a wall's edge.
		std::optional<RaycastHit> SphereCast(const glm::vec3& origin, const glm::vec3& direction, float radius, float maxDistance,
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

		// Ragdolls: jointed bodies owned by an entity (raycasts and contacts report the owner).
		// Creating one replaces any the owner had. Parts keep the given velocity (the body falls
		// the way it was moving). False with a message when the description is unusable.
		bool CreateRagdoll(UUID owner, const std::vector<RagdollPartDesc>& parts, const glm::vec3& velocity, std::string& error);
		void RemoveRagdoll(UUID owner);
		bool HasRagdoll(UUID owner) const;
		// World transforms of the parts, in the order they were created; false without a ragdoll.
		bool GetRagdollTransforms(UUID owner, std::vector<RagdollPartTransform>& transforms) const;
		// False once every part has come to rest (Jolt put them to sleep).
		bool IsRagdollAwake(UUID owner) const;
		// An impulse on the part nearest the point (a bolt striking a limb).
		void AddRagdollImpulse(UUID owner, const glm::vec3& impulse, const glm::vec3& point);

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
