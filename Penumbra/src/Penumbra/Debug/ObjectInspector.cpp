#include "pnpch.h"
#include "Penumbra/Debug/ObjectInspector.h"

#include "Penumbra/AI/FogOfWarSystem.h"
#include "Penumbra/Animation/RagdollSystem.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/Components.h"
#include "Penumbra/Scene/Scene.h"

#include <filesystem>
#include <limits>

namespace Penumbra::ObjectInspector {

	namespace {

		constexpr float LightPickRadius = 0.25f;

		// Distance along the ray to where it enters the box (in the box's own space), or nothing when
		// it misses or starts inside.
		std::optional<float> RayEntersBox(const glm::vec3& origin, const glm::vec3& direction, const BoundingBox& box, const glm::mat4& world)
		{
			const glm::mat4 inverse = glm::inverse(world);
			const glm::vec3 localOrigin = glm::vec3(inverse * glm::vec4(origin, 1.0f));
			const glm::vec3 localDirection = glm::vec3(inverse * glm::vec4(direction, 0.0f));
			float tMin = 0.0f, tMax = std::numeric_limits<float>::max();
			bool inside = true;
			for (int axis = 0; axis < 3; axis++)
			{
				if (localOrigin[axis] < box.Min[axis] || localOrigin[axis] > box.Max[axis])
					inside = false;
				if (std::abs(localDirection[axis]) < 1e-8f)
				{
					if (localOrigin[axis] < box.Min[axis] || localOrigin[axis] > box.Max[axis])
						return std::nullopt;
					continue;
				}
				float t1 = (box.Min[axis] - localOrigin[axis]) / localDirection[axis];
				float t2 = (box.Max[axis] - localOrigin[axis]) / localDirection[axis];
				if (t1 > t2)
					std::swap(t1, t2);
				tMin = std::max(tMin, t1);
				tMax = std::min(tMax, t2);
				if (tMin > tMax)
					return std::nullopt;
			}
			if (inside)
				return std::nullopt;
			const glm::vec3 worldHit = glm::vec3(world * glm::vec4(localOrigin + localDirection * tMin, 1.0f));
			return glm::distance(origin, worldHit);
		}

		std::optional<float> RayHitsSphere(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& center, float radius)
		{
			const glm::vec3 offset = center - origin;
			const float along = glm::dot(offset, direction);
			if (along < 0.0f)
				return std::nullopt;
			const float missSquared = glm::dot(offset, offset) - along * along;
			if (missSquared > radius * radius)
				return std::nullopt;
			return along - std::sqrt(radius * radius - missSquared);
		}

		bool IsCharacter(Entity entity)
		{
			return entity.HasComponent<PerceptionComponent>() || entity.HasComponent<CharacterControllerComponent>() ||
				   entity.HasComponent<RagdollComponent>();
		}

		bool IsLight(Entity entity)
		{
			return entity.HasComponent<PointLightComponent>() || entity.HasComponent<SpotLightComponent>() ||
				   entity.HasComponent<DirectionalLightComponent>();
		}

		template<typename Function>
		void ForEachInHierarchy(Entity entity, const Function& function)
		{
			function(entity);
			for (Entity child : entity.GetChildren())
				ForEachInHierarchy(child, function);
		}

	}

	std::optional<PickResult> Pick(Scene& scene, const glm::vec3& origin, const glm::vec3& direction)
	{
		FogOfWarSystem* fog = scene.IsRunning() ? scene.GetFogOfWarSystem() : nullptr;
		const bool fogActive = fog && fog->GetView().has_value();
		std::optional<PickResult> best;
		auto consider = [&](Entity entity, std::optional<float> distance) {
			if (!distance || (best && *distance >= best->Distance))
				return;
			if (fogActive && fog->IsHidden(entity))
				return;
			best = PickResult{ entity, Entity{}, *distance };
		};

		auto& registry = scene.GetRegistry();
		for (auto [handle, mesh] : registry.view<MeshComponent>().each())
		{
			if (!mesh.Visible)
				continue;
			std::shared_ptr<const MeshData> data = AssetManager::GetMesh(mesh.Mesh);
			if (!data || !data->Bounds.IsValid())
				continue;
			Entity entity(handle, &scene);
			consider(entity, RayEntersBox(origin, direction, data->Bounds, scene.GetWorldTransform(entity)));
		}
		auto considerLight = [&](entt::entity handle) {
			Entity entity(handle, &scene);
			consider(entity, RayHitsSphere(origin, direction, scene.GetWorldPosition(entity), LightPickRadius));
		};
		for (entt::entity handle : registry.view<PointLightComponent>())
			considerLight(handle);
		for (entt::entity handle : registry.view<SpotLightComponent>())
			considerLight(handle);

		if (best)
			best->Object = SelectionRoot(best->Hit);
		return best;
	}

	Entity SelectionRoot(Entity entity)
	{
		if (!entity)
			return entity;
		for (Entity current = entity; current; current = current.GetParent())
		{
			if (IsCharacter(current))
				return current;
		}
		for (Entity current = entity; current; current = current.GetParent())
		{
			if (current.HasComponent<PrefabComponent>() || current.HasComponent<ScriptComponent>())
				return current;
		}
		return entity;
	}

	ObjectContext Describe(Entity entity)
	{
		ObjectContext context;
		auto keys = [&](std::initializer_list<const char*> names) {
			for (const char* name : names)
			{
				const ComponentInfo* info = ComponentRegistry::Find(name);
				if (info && info->Has(entity))
					context.KeyComponents.emplace_back(name);
			}
		};
		const PerceptionTargetComponent* target = entity.TryGetComponent<PerceptionTargetComponent>();

		if (entity.HasComponent<PerceptionComponent>())
		{
			context.Kind = "NPC";
			keys({ "Perception", "Vitality", "NavAgent", "Script", "PerceptionTarget", "Ragdoll" });
		}
		else if (entity.HasComponent<FogOfWarComponent>() || (target && target->Kind == TargetKind::Intruder))
		{
			context.Kind = "Thief";
			keys({ "FogOfWar", "CharacterController", "PerceptionTarget", "Vitality", "Script" });
		}
		else if (entity.HasComponent<RagdollComponent>() || (target && target->Kind == TargetKind::Body))
		{
			context.Kind = "Body";
			keys({ "Ragdoll", "PerceptionTarget", "Vitality" });
		}
		else if (IsLight(entity))
		{
			context.Kind = "Light";
			keys({ "PointLight", "SpotLight", "DirectionalLight", "Flame" });
		}
		else if (entity.HasComponent<SmokeVolumeComponent>())
		{
			context.Kind = "Smoke";
			keys({ "SmokeVolume" });
		}
		else if (const ScriptComponent* script = entity.TryGetComponent<ScriptComponent>())
		{
			// Scripted things are named after their script: Door.lua is a door.
			const std::string stem = std::filesystem::path(script->Script).stem().string();
			context.Kind = stem.empty() ? std::string("Scripted object") : stem;
			keys({ "Script", "RigidBody", "Surface", "StimSource", "StimResponse" });
		}
		else if (const RigidBodyComponent* body = entity.TryGetComponent<RigidBodyComponent>(); body && body->Type == RigidBodyType::Dynamic)
		{
			context.Kind = "Loose object";
			keys({ "RigidBody", "Surface", "StimSource", "StimResponse" });
		}
		else if (entity.HasComponent<NavMeshComponent>())
		{
			context.Kind = "Navmesh";
			keys({ "NavMesh" });
		}
		else
		{
			const PrefabComponent* prefab = entity.TryGetComponent<PrefabComponent>();
			context.Kind = prefab ? std::filesystem::path(prefab->Prefab).stem().string() : std::string("Object");
			keys({ "Mesh", "Material", "Surface", "StimSource", "StimResponse" });
		}
		return context;
	}

	BoundingBox WorldBounds(Scene& scene, Entity entity)
	{
		BoundingBox bounds;
		ForEachInHierarchy(entity, [&](Entity current) {
			const MeshComponent* mesh = current.TryGetComponent<MeshComponent>();
			if (!mesh || !mesh->Visible)
				return;
			std::shared_ptr<const MeshData> data = AssetManager::GetMesh(mesh->Mesh);
			if (!data || !data->Bounds.IsValid())
				return;
			const glm::mat4 world = scene.GetWorldTransform(current);
			for (int corner = 0; corner < 8; corner++)
			{
				const glm::vec3 local((corner & 1) ? data->Bounds.Max.x : data->Bounds.Min.x, (corner & 2) ? data->Bounds.Max.y : data->Bounds.Min.y,
					(corner & 4) ? data->Bounds.Max.z : data->Bounds.Min.z);
				bounds.Expand(glm::vec3(world * glm::vec4(local, 1.0f)));
			}
		});
		return bounds;
	}

	void MoveTo(Scene& scene, Entity entity, const glm::vec3& position)
	{
		glm::vec3 translation, scale;
		glm::quat rotation;
		Math::DecomposeTransform(scene.GetWorldTransform(entity), translation, rotation, scale);
		scene.SetWorldTransform(entity, Math::ComposeTransform(position, rotation, scale));
		if (PhysicsWorld* physics = scene.GetPhysicsWorld())
			physics->Teleport(entity, position, rotation);
		// A limp body moves whole, in the pose it lay in.
		if (const RagdollComponent* ragdoll = entity.TryGetComponent<RagdollComponent>(); ragdoll && ragdoll->RuntimeActive)
		{
			std::string error;
			if (!RagdollSystem::Teleport(scene, entity, error))
				PN_CORE_WARN("The ragdoll of '{}' could not follow it: {}", entity.GetName(), error);
		}
	}

	bool HasLights(Entity entity)
	{
		bool found = false;
		ForEachInHierarchy(entity, [&](Entity current) { found |= IsLight(current); });
		return found;
	}

	bool AreLightsOn(Entity entity)
	{
		bool on = false;
		ForEachInHierarchy(entity, [&](Entity current) {
			if (const auto* point = current.TryGetComponent<PointLightComponent>())
				on |= point->Enabled;
			if (const auto* spot = current.TryGetComponent<SpotLightComponent>())
				on |= spot->Enabled;
			if (const auto* directional = current.TryGetComponent<DirectionalLightComponent>())
				on |= directional->Enabled;
		});
		return on;
	}

	void SetLightsOn(Entity entity, bool on)
	{
		ForEachInHierarchy(entity, [&](Entity current) {
			if (auto* point = current.TryGetComponent<PointLightComponent>())
				point->Enabled = on;
			if (auto* spot = current.TryGetComponent<SpotLightComponent>())
				spot->Enabled = on;
			if (auto* directional = current.TryGetComponent<DirectionalLightComponent>())
				directional->Enabled = on;
		});
	}

}
