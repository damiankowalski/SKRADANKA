#pragma once

#include "Penumbra/Animation/AnimationData.h"
#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/UUID.h"
#include "Penumbra/Physics/PhysicsWorld.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Penumbra {

	class Scene;
	class Entity;

	// Per-body state of an active ragdoll (RagdollComponent::RuntimeRig).
	struct RagdollRig
	{
		UUID Model = UUID::Invalid;          // the child entity with the skinned mesh and its Animator
		std::vector<int32_t> PartNodes;      // skeleton node each part drives
		std::vector<glm::mat4> BoneFromBody; // the node's world transform relative to its part's body
		Pose BasePose;                       // the pose when physics took over; undriven nodes keep it
	};

	// Ragdolls for characters (RagdollComponent): their skinned model's skeleton is turned into
	// jointed capsules (Jolt) that take over from the animation on a knockout or death, falling
	// from the pose and the speed the character had. Each frame after physics the bodies write the
	// skeleton pose (AnimatorComponent::RuntimePoseOverride) and the entity's origin follows the
	// hips along the floor, so the body is found, searched and picked up where it lies; a body's
	// silhouette points for guards (PerceptionTarget) are its hips, chest, head, forearms and shins.
	class RagdollSystem
	{
	public:
		// The humanoid ragdoll of a rig with the usual bone names (hips, spine, chest, head,
		// upperarm/lowerarm/upperleg/lowerleg .l/.r and their tips: wrist, foot): 12 parts, built
		// from the world transform of every skeleton node. Capsule radii are sized for a 1.8 m
		// character and follow the rig's size. partNodes gets the node each part follows.
		static bool BuildHumanoid(const Skeleton& skeleton, const std::vector<glm::mat4>& nodeWorld, float radiusScale, float mass,
			std::vector<RagdollPartDesc>& parts, std::vector<int32_t>& partNodes, std::string& error);

		// Hands the entity's body to physics from its current pose and speed. False with a message
		// when there is no Ragdoll component, no skinned model, an unknown skeleton or no running physics.
		static bool Activate(Scene& scene, Entity entity, std::string& error);
		// Removes the bodies; the animation shows again (a body lifted onto the thief's shoulders).
		static void Deactivate(Scene& scene, Entity entity);
		// Rebuilds an active ragdoll where its entity now is, keeping the pose it lay in (a script
		// moved the body). False when the entity has no active ragdoll.
		static bool Teleport(Scene& scene, Entity entity, std::string& error);
		// After physics: active ragdolls pose their skeletons and move their entities.
		static void Update(Scene& scene);
	};

}
