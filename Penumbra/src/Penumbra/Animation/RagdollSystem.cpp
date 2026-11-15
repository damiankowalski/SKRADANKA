#include "pnpch.h"
#include "Penumbra/Animation/RagdollSystem.h"

#include "Penumbra/Animation/Animation.h"
#include "Penumbra/Animation/AnimationSystem.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Asset/MeshData.h"
#include "Penumbra/Scene/Components.h"
#include "Penumbra/Scene/Entity.h"
#include "Penumbra/Scene/Scene.h"

#include <glm/gtx/quaternion.hpp>

namespace Penumbra {

	namespace {

		// One body of the humanoid: the bone it follows, the bone its capsule reaches to (none: a
		// sphere, the head), its parent part, radius (m, for a 1.8 m character), share of the mass
		// and joint limits (degrees).
		struct PartSpec
		{
			const char* Bone;
			const char* Tip;
			int32_t Parent;
			float Radius;
			float Mass;
			float Swing;
			float Twist;
		};

		constexpr PartSpec Humanoid[] = {
			{ "hips", "spine", -1, 0.14f, 0.15f, 0.0f, 0.0f },
			{ "spine", "chest", 0, 0.13f, 0.12f, 25.0f, 15.0f },
			{ "chest", "head", 1, 0.16f, 0.18f, 25.0f, 15.0f },
			{ "head", nullptr, 2, 0.22f, 0.08f, 35.0f, 40.0f },
			{ "upperarm.l", "lowerarm.l", 2, 0.06f, 0.03f, 80.0f, 45.0f },
			{ "lowerarm.l", "wrist.l", 4, 0.055f, 0.02f, 75.0f, 20.0f },
			{ "upperarm.r", "lowerarm.r", 2, 0.06f, 0.03f, 80.0f, 45.0f },
			{ "lowerarm.r", "wrist.r", 6, 0.055f, 0.02f, 75.0f, 20.0f },
			{ "upperleg.l", "lowerleg.l", 0, 0.08f, 0.10f, 55.0f, 15.0f },
			{ "lowerleg.l", "foot.l", 8, 0.07f, 0.05f, 65.0f, 10.0f },
			{ "upperleg.r", "lowerleg.r", 0, 0.08f, 0.10f, 55.0f, 15.0f },
			{ "lowerleg.r", "foot.r", 10, 0.07f, 0.05f, 65.0f, 10.0f },
		};
		constexpr float ReferenceTorso = 0.836f; // hips to head of the reference rig, metres
		// Parts whose centres are the body's silhouette points for guards: hips, chest, head, the
		// forearms and the shins (seven, like the fixed points of a body without a ragdoll).
		constexpr size_t SilhouetteParts[] = { 0, 2, 3, 5, 7, 9, 11 };

		glm::vec3 PositionOf(const glm::mat4& m) { return glm::vec3(m[3]); }

		glm::mat4 BodyMatrix(const glm::vec3& position, const glm::quat& rotation)
		{
			return glm::translate(glm::mat4(1.0f), position) * glm::toMat4(rotation);
		}

		// The first entity in the hierarchy (itself first) with a skinned mesh and an Animator.
		std::optional<Entity> FindSkinnedModel(Entity entity)
		{
			if (const MeshComponent* mesh = entity.TryGetComponent<MeshComponent>(); mesh && entity.HasComponent<AnimatorComponent>())
			{
				if (std::shared_ptr<const MeshData> data = AssetManager::GetMesh(mesh->Mesh); data && data->IsSkinned())
					return entity;
			}
			for (Entity child : entity.GetChildren())
			{
				if (std::optional<Entity> found = FindSkinnedModel(child))
					return found;
			}
			return std::nullopt;
		}

	}

	bool RagdollSystem::BuildHumanoid(const Skeleton& skeleton, const std::vector<glm::mat4>& nodeWorld, float radiusScale, float mass,
		std::vector<RagdollPartDesc>& parts, std::vector<int32_t>& partNodes, std::string& error)
	{
		parts.clear();
		partNodes.clear();
		auto node = [&](const char* name) { return skeleton.FindNode(name); };
		for (const PartSpec& spec : Humanoid)
		{
			if (node(spec.Bone) < 0 || (spec.Tip && node(spec.Tip) < 0))
			{
				error = std::format("the skeleton has no '{}' bone: not a humanoid rig", node(spec.Bone) < 0 ? spec.Bone : spec.Tip);
				return false;
			}
		}
		if (nodeWorld.size() != skeleton.Nodes.size())
		{
			error = "one world transform per skeleton node expected";
			return false;
		}

		const float torso = glm::distance(PositionOf(nodeWorld[node("hips")]), PositionOf(nodeWorld[node("head")]));
		const float size = torso > 0.01f ? torso / ReferenceTorso : 1.0f;
		float massShares = 0.0f;
		for (const PartSpec& spec : Humanoid)
			massShares += spec.Mass;

		for (const PartSpec& spec : Humanoid)
		{
			const glm::mat4& bone = nodeWorld[node(spec.Bone)];
			const glm::vec3 start = PositionOf(bone);
			RagdollPartDesc part;
			part.Parent = spec.Parent;
			part.Radius = std::max(spec.Radius * size * radiusScale, 0.02f);
			part.Mass = std::max(mass, 1.0f) * spec.Mass / massShares;
			part.SwingDegrees = spec.Swing;
			part.TwistDegrees = spec.Twist;
			part.JointPosition = start;
			if (spec.Tip)
			{
				const glm::vec3 end = PositionOf(nodeWorld[node(spec.Tip)]);
				const glm::vec3 along = end - start;
				const float length = glm::length(along);
				const glm::vec3 direction = length > 1e-4f ? along / length : glm::vec3(0.0f, 1.0f, 0.0f);
				part.Position = (start + end) * 0.5f;
				part.Rotation = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), direction);
				part.HalfHeight = std::max(length * 0.5f - part.Radius, 0.02f);
				part.TwistAxis = direction;
			}
			else
			{
				// The head: a ball above the head bone, along the bone's up axis.
				const glm::vec3 up = glm::normalize(glm::vec3(bone[1]));
				part.Position = start + up * part.Radius * 1.1f;
				part.Rotation = glm::quat_cast(glm::mat3(glm::normalize(glm::vec3(bone[0])), up, glm::normalize(glm::vec3(bone[2]))));
				part.HalfHeight = 0.0f;
				part.TwistAxis = up;
			}
			parts.push_back(part);
			partNodes.push_back(node(spec.Bone));
		}
		return true;
	}

	bool RagdollSystem::Activate(Scene& scene, Entity entity, std::string& error)
	{
		RagdollComponent* component = entity.TryGetComponent<RagdollComponent>();
		PhysicsWorld* physics = scene.GetPhysicsWorld();
		if (!component || !physics)
		{
			error = component ? "physics is not running" : "no Ragdoll component";
			return false;
		}
		std::optional<Entity> model = FindSkinnedModel(entity);
		if (!model)
		{
			error = std::format("'{}' has no skinned model with an Animator", entity.GetName());
			return false;
		}
		const std::shared_ptr<const MeshData> mesh = AssetManager::GetMesh(model->GetComponent<MeshComponent>().Mesh);
		AnimatorComponent& animator = model->GetComponent<AnimatorComponent>();

		// From the pose shown now: the animation's, or the last ragdoll pose when restarted (Teleport).
		Pose pose;
		AnimationSystem::EvaluatePose(*mesh, &animator, pose);
		std::vector<glm::mat4> globals;
		Animation::ComputeGlobalTransforms(mesh->Skeleton, pose, globals);
		const glm::mat4 modelWorld = scene.GetWorldTransform(*model);
		std::vector<glm::mat4> nodeWorld(globals.size());
		for (size_t i = 0; i < globals.size(); ++i)
			nodeWorld[i] = modelWorld * globals[i];

		std::vector<RagdollPartDesc> parts;
		auto rig = std::make_shared<RagdollRig>();
		if (!BuildHumanoid(mesh->Skeleton, nodeWorld, component->RadiusScale, component->Mass, parts, rig->PartNodes, error))
			return false;
		const CharacterControllerComponent* controller = entity.TryGetComponent<CharacterControllerComponent>();
		const glm::vec3 velocity = controller ? controller->RuntimeVelocity : glm::vec3(0.0f);
		if (!physics->CreateRagdoll(entity.GetUUID(), parts, velocity, error))
			return false;

		rig->Model = model->GetUUID();
		rig->BasePose = pose;
		for (size_t p = 0; p < parts.size(); ++p)
			rig->BoneFromBody.push_back(glm::inverse(BodyMatrix(parts[p].Position, parts[p].Rotation)) * nodeWorld[rig->PartNodes[p]]);
		component->RuntimeRig = rig;
		component->RuntimeActive = true;
		component->RuntimeAwake = true;
		PN_CORE_TRACE("'{}' goes limp: a ragdoll of {} parts", entity.GetName(), parts.size());
		return true;
	}

	void RagdollSystem::Deactivate(Scene& scene, Entity entity)
	{
		RagdollComponent* component = entity.TryGetComponent<RagdollComponent>();
		if (!component || !component->RuntimeActive)
			return;
		if (PhysicsWorld* physics = scene.GetPhysicsWorld())
			physics->RemoveRagdoll(entity.GetUUID());
		if (component->RuntimeRig)
		{
			if (std::optional<Entity> model = scene.TryGetEntityByUUID(component->RuntimeRig->Model))
			{
				if (AnimatorComponent* animator = model->TryGetComponent<AnimatorComponent>())
					animator->RuntimePoseOverride.reset();
			}
		}
		component->RuntimeRig.reset();
		component->RuntimeActive = false;
		component->RuntimeAwake = false;
	}

	bool RagdollSystem::Teleport(Scene& scene, Entity entity, std::string& error)
	{
		RagdollComponent* component = entity.TryGetComponent<RagdollComponent>();
		if (!component || !component->RuntimeActive)
			return false;
		if (PhysicsWorld* physics = scene.GetPhysicsWorld())
			physics->RemoveRagdoll(entity.GetUUID());
		component->RuntimeActive = false;
		return Activate(scene, entity, error); // the pose override is kept: the body as it lay, where it now is
	}

	void RagdollSystem::Update(Scene& scene)
	{
		PhysicsWorld* physics = scene.GetPhysicsWorld();
		if (!physics)
			return;
		std::vector<RagdollPartTransform> bodies;
		for (auto [handle, component] : scene.GetRegistry().view<RagdollComponent>().each())
		{
			if (!component.RuntimeActive || !component.RuntimeRig)
				continue;
			Entity entity(handle, &scene);
			const RagdollRig& rig = *component.RuntimeRig;
			std::optional<Entity> model = scene.TryGetEntityByUUID(rig.Model);
			if (!model || !model->HasComponent<AnimatorComponent>() || !physics->GetRagdollTransforms(entity.GetUUID(), bodies) ||
				bodies.size() != rig.PartNodes.size())
			{
				Deactivate(scene, entity);
				continue;
			}
			component.RuntimeAwake = physics->IsRagdollAwake(entity.GetUUID());

			// The entity's origin follows the hips: on the floor under them, its rotation kept.
			if (!entity.GetParent())
			{
				const glm::vec3 hips = bodies.front().Position;
				const UUID self = entity.GetUUID();
				glm::vec3& translation = entity.GetComponent<TransformComponent>().Translation;
				const std::optional<RaycastHit> floor = physics->Raycast(hips, { 0.0f, -1.0f, 0.0f }, 4.0f, [self](UUID e) { return e != self; });
				translation = glm::vec3(hips.x, floor ? floor->Point.y : std::min(translation.y, hips.y), hips.z);
			}

			// Guards see the body where its parts lie: a body folded behind a crate shows less of itself.
			if (PerceptionTargetComponent* target = entity.TryGetComponent<PerceptionTargetComponent>();
				target && target->Kind == TargetKind::Body && bodies.size() > SilhouetteParts[std::size(SilhouetteParts) - 1])
			{
				const glm::mat4 toLocal = glm::inverse(scene.GetWorldTransform(entity));
				target->SilhouettePoints.clear();
				for (size_t part : SilhouetteParts)
					target->SilhouettePoints.push_back(glm::vec3(toLocal * glm::vec4(bodies[part].Position, 1.0f)));
			}

			// Driven nodes take their part's transform; the others keep their pose under their parent.
			const std::shared_ptr<const MeshData> mesh = AssetManager::GetMesh(model->GetComponent<MeshComponent>().Mesh);
			if (!mesh || mesh->Skeleton.Nodes.size() != rig.BasePose.size())
				continue;
			const glm::mat4 fromWorld = glm::inverse(scene.GetWorldTransform(*model));
			std::vector<int32_t> nodePart(mesh->Skeleton.Nodes.size(), -1);
			for (size_t p = 0; p < rig.PartNodes.size(); ++p)
				nodePart[static_cast<size_t>(rig.PartNodes[p])] = static_cast<int32_t>(p);

			auto pose = std::make_shared<Pose>(rig.BasePose);
			std::vector<glm::mat4> globals(pose->size());
			for (size_t i = 0; i < pose->size(); ++i)
			{
				const int32_t parent = mesh->Skeleton.Nodes[i].Parent;
				const glm::mat4 parentGlobal = parent >= 0 ? globals[static_cast<size_t>(parent)] : glm::mat4(1.0f);
				if (const int32_t part = nodePart[i]; part >= 0)
				{
					const RagdollPartTransform& body = bodies[static_cast<size_t>(part)];
					const glm::mat4 global = fromWorld * BodyMatrix(body.Position, body.Rotation) * rig.BoneFromBody[static_cast<size_t>(part)];
					NodeTransform local;
					glm::vec3 scale;
					if (Math::DecomposeTransform(glm::inverse(parentGlobal) * global, local.Translation, local.Rotation, scale))
					{
						local.Scale = (*pose)[i].Scale; // physics only turns bones
						(*pose)[i] = local;
					}
				}
				globals[i] = parentGlobal * Animation::ToMatrix((*pose)[i]);
			}
			model->GetComponent<AnimatorComponent>().RuntimePoseOverride = std::move(pose);
		}
	}

}
