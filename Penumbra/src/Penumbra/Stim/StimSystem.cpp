#include "pnpch.h"
#include "Penumbra/Stim/StimSystem.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Audio/AudioScene.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Scene/JsonSerialization.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

#include <tuple>

namespace Penumbra {

	namespace {
		// Intensity received at the edge of a radius emission, relative to the centre.
		constexpr float EdgeFalloff = 0.5f;
	}

	Entity StimSystem::ResolveReceiver(Entity hit)
	{
		for (Entity current = hit; current; current = current.GetParent())
		{
			if (current.HasComponent<StimResponseComponent>() || current.HasComponent<ScriptComponent>())
				return current;
		}
		return hit;
	}

	float StimSystem::DistanceToObject(Entity receiver, const glm::vec3& point)
	{
		float closest = glm::length(m_Scene.GetWorldPosition(receiver) - point);
		for (Entity child : receiver.GetChildren())
			closest = std::min(closest, DistanceToObject(child, point));
		return closest;
	}

	StimSystem::StimSystem(Scene& scene)
		: m_Scene(scene)
	{
	}

	void StimSystem::SetLightsEnabled(Entity entity, bool enabled)
	{
		if (!entity.IsValid())
			return;
		if (auto* light = entity.TryGetComponent<PointLightComponent>())
			light->Enabled = enabled;
		if (auto* light = entity.TryGetComponent<SpotLightComponent>())
			light->Enabled = enabled;
		if (auto* light = entity.TryGetComponent<DirectionalLightComponent>())
			light->Enabled = enabled;
		if (auto* material = entity.TryGetComponent<MaterialComponent>())
			material->EmissiveEnabled = enabled;
		if (auto* flame = entity.TryGetComponent<FlameComponent>())
			flame->Enabled = enabled;
		// A flame is also what ignites things: extinguishing it stops continuous stims.
		if (auto* source = entity.TryGetComponent<StimSourceComponent>(); source && source->Delivery == StimDelivery::Continuous)
			source->Enabled = enabled;
		for (Entity child : entity.GetChildren())
			SetLightsEnabled(child, enabled);
	}

	void StimSystem::Deliver(Entity receiver, const std::string& stim, float intensity, UUID source, std::optional<glm::vec3> origin)
	{
		if (!receiver.IsValid() || m_Scene.IsEntityPendingDestroy(receiver) || intensity <= 0.0f)
			return;
		m_DeliveredCount++;
		PN_CORE_TRACE("Stim '{}' ({:.2f}) -> '{}'", stim, intensity, receiver.GetName());

		if (ScriptEngine* scripts = m_Scene.GetScriptEngine())
			scripts->OnStim(receiver, stim, intensity, source);

		auto* response = receiver.TryGetComponent<StimResponseComponent>();
		if (!response)
			return;

		const glm::vec3 position = m_Scene.GetWorldPosition(receiver);
		for (StimResponseRule& rule : response->Rules)
		{
			if (rule.Stim != stim || intensity < rule.Threshold || (rule.Once && rule.RuntimeFired))
				continue;
			rule.RuntimeFired = true;
			for (const StimAction& action : rule.Actions)
				m_PendingActions.push_back({ receiver.GetUUID(), action, position, origin.value_or(position) });
		}
	}

	void StimSystem::Emit(const std::string& stim, const glm::vec3& position, float radius, float intensity, UUID source,
		bool requireLineOfSight, UUID exclude)
	{
		if (radius <= 0.0f || intensity <= 0.0f)
			return;

		std::vector<Entity> receivers;
		for (entt::entity handle : m_Scene.GetRegistry().view<StimResponseComponent>())
			receivers.emplace_back(handle, &m_Scene);
		// Scripted receivers react too (on_stim), even without response rules.
		for (entt::entity handle : m_Scene.GetRegistry().view<ScriptComponent>(entt::exclude<StimResponseComponent>))
			receivers.emplace_back(handle, &m_Scene);

		// An emitter never stimulates its own object: a torch's flame must not keep re-igniting
		// the torch it is attached to, an exploding keg's children are going away with it.
		std::optional<Entity> sourceEntity = source.IsValid() ? m_Scene.TryGetEntityByUUID(source) : std::nullopt;
		auto isOwnHierarchy = [&](Entity receiver) {
			return sourceEntity && (m_Scene.IsAncestor(receiver, *sourceEntity) || m_Scene.IsAncestor(*sourceEntity, receiver));
		};

		const PhysicsWorld* physics = m_Scene.GetPhysicsWorld();
		for (Entity receiver : receivers)
		{
			if (receiver.GetUUID() == source || receiver.GetUUID() == exclude || isOwnHierarchy(receiver))
				continue;
			// Measured to the closest part of the object: splashing water next to a torch's
			// flame reaches the torch even though the torch's origin is on the ground.
			const float distance = DistanceToObject(receiver, position);
			if (distance > radius)
				continue;
			const glm::vec3 target = m_Scene.GetWorldPosition(receiver);
			if (requireLineOfSight && physics && distance > Math::Epsilon)
			{
				const UUID receiverID = receiver.GetUUID();
				auto filter = [&](UUID entity) { return entity != receiverID && entity != source; };
				if (physics->Raycast(position, target - position, distance, filter))
					continue;
			}
			const float falloff = 1.0f - (1.0f - EdgeFalloff) * (distance / radius);
			Deliver(receiver, stim, intensity * falloff, source, position);
		}
	}

	void StimSystem::HandleContactSource(Entity source, Entity other, const glm::vec3& point, const glm::vec3& sourceVelocity)
	{
		auto* stim = source.TryGetComponent<StimSourceComponent>();
		if (!stim || !stim->Enabled || stim->Delivery != StimDelivery::Contact)
			return;
		// Colliders are often children of the object that owns the reactions (a torch's pole):
		// the hit is delivered to the nearest self-or-ancestor that can react.
		Entity receiver = ResolveReceiver(other);
		// Direct hit at full intensity, then a splash to everything else nearby. A fast projectile
		// came from back along its flight; anything else from where it touched.
		const float speed = glm::length(sourceVelocity);
		const glm::vec3 origin = speed > 1.0f ? point - sourceVelocity / speed * ProjectileBacktrack : point;
		Deliver(receiver, stim->Stim, stim->Intensity, source.GetUUID(), origin);
		if (stim->Radius > 0.0f)
			Emit(stim->Stim, point, stim->Radius, stim->Intensity, source.GetUUID(), stim->RequireLineOfSight, receiver.GetUUID());
		if (stim->DestroyOnContact)
		{
			stim->Enabled = false;
			m_Scene.DestroyEntity(source);
		}
	}

	void StimSystem::OnContact(UUID a, UUID b, const glm::vec3& point, const glm::vec3& velocityA, const glm::vec3& velocityB)
	{
		std::optional<Entity> entityA = m_Scene.TryGetEntityByUUID(a);
		std::optional<Entity> entityB = m_Scene.TryGetEntityByUUID(b);
		if (!entityA || !entityB)
			return;
		HandleContactSource(*entityA, *entityB, point, velocityA - velocityB);
		if (entityA->IsValid() && entityB->IsValid())
			HandleContactSource(*entityB, *entityA, point, velocityB - velocityA);
	}

	void StimSystem::Execute(const PendingAction& pending)
	{
		std::optional<Entity> target = m_Scene.TryGetEntityByUUID(pending.Target);
		const StimAction& action = pending.Action;
		const glm::vec3 position = target ? m_Scene.GetWorldPosition(*target) : pending.Position;
		PN_CORE_TRACE("Stim reaction {} on '{}'", EnumToString(action.Type), target ? target->GetName() : std::string("<destroyed>"));

		switch (action.Type)
		{
			case StimActionType::DisableLights:
				if (target)
					SetLightsEnabled(*target, false);
				break;
			case StimActionType::EnableLights:
				if (target)
					SetLightsEnabled(*target, true);
				break;
			case StimActionType::Destroy:
				if (target)
					m_Scene.DestroyEntity(*target);
				break;
			case StimActionType::SpawnPrefab:
				Prefab::Instantiate(m_Scene, action.Asset, position);
				break;
			case StimActionType::EmitStim:
				Emit(action.Asset, position, action.Radius, action.Value, pending.Target);
				break;
			case StimActionType::RadialImpulse:
				if (PhysicsWorld* physics = m_Scene.GetPhysicsWorld())
					physics->AddRadialImpulse(position, action.Radius, action.Value);
				break;
			case StimActionType::PlaySound:
				if (AudioScene* audio = m_Scene.GetAudioScene())
					audio->PlayOneShot(action.Asset, position, action.Value > 0.0f ? action.Value : 1.0f, 0.0f);
				break;
			case StimActionType::EmitNoise:
				if (PerceptionSystem* perception = m_Scene.GetPerceptionSystem())
					perception->EmitNoise(position, action.Value, pending.Target, action.Asset.empty() ? "Stim" : action.Asset);
				break;
			case StimActionType::Damage:
				if (target)
				{
					if (PerceptionSystem* perception = m_Scene.GetPerceptionSystem())
						perception->Damage(*target, action.Value);
				}
				break;
			case StimActionType::Alarm:
				if (target && target->HasComponent<PerceptionComponent>())
				{
					if (PerceptionSystem* perception = m_Scene.GetPerceptionSystem())
						perception->Notify(*target, pending.Origin, action.Value > 0.0f ? action.Value : 9.5f, StimulusKind::Seen,
							action.Asset.empty() ? std::string_view("Hit") : std::string_view(action.Asset));
				}
				break;
			case StimActionType::Knockout:
				if (target)
				{
					if (PerceptionSystem* perception = m_Scene.GetPerceptionSystem())
						perception->KnockOut(*target, action.Asset == "Dead" ? VitalState::Dead : VitalState::Unconscious);
				}
				break;
			case StimActionType::ScriptEvent:
				if (target)
				{
					if (ScriptEngine* scripts = m_Scene.GetScriptEngine())
						scripts->OnStimEvent(*target, action.Asset);
				}
				break;
		}
	}

	void StimSystem::Update(Timestep)
	{
		// Continuous emitters (flames) and one-shot radius emitters.
		std::vector<std::tuple<std::string, glm::vec3, float, float, UUID, bool>> emissions;
		for (auto [handle, source] : m_Scene.GetRegistry().view<StimSourceComponent>().each())
		{
			if (!source.Enabled || source.Delivery == StimDelivery::Contact)
				continue;
			Entity entity(handle, &m_Scene);
			emissions.emplace_back(source.Stim, m_Scene.GetWorldPosition(entity), source.Radius, source.Intensity, entity.GetUUID(),
				source.RequireLineOfSight);
			if (source.Delivery == StimDelivery::Radius)
				source.Enabled = false;
		}
		for (const auto& [stim, position, radius, intensity, source, lineOfSight] : emissions)
			Emit(stim, position, radius, intensity, source, lineOfSight);

		// Reactions queued during this tick run now; anything they trigger waits for the next tick.
		std::vector<PendingAction> actions = std::move(m_PendingActions);
		m_PendingActions.clear();
		for (const PendingAction& pending : actions)
			Execute(pending);
	}

}
