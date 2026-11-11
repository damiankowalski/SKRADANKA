#include "pnpch.h"
#include "Penumbra/Scene/Scene.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Animation/AnimationSystem.h"
#include "Penumbra/Audio/AudioScene.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Scene/Entity.h"
#include "Penumbra/Scripting/ScriptEngine.h"
#include "Penumbra/Stim/StimSystem.h"
#include "Penumbra/UI/Overlay.h"

namespace Penumbra {

	namespace {
		template<typename... Component>
		void CopyComponents(ComponentGroup<Component...>, entt::registry& source, entt::entity sourceEntity, entt::registry& destination,
			entt::entity destinationEntity)
		{
			(
				[&]() {
					if (const Component* component = source.try_get<Component>(sourceEntity))
						destination.emplace_or_replace<Component>(destinationEntity, *component);
				}(),
				...);
		}
	}

	Scene::Scene()
		: m_Overlay(CreateScope<OverlayList>())
	{
	}

	Scene::~Scene()
	{
		if (m_IsRunning)
			OnRuntimeStop();
	}

	Ref<Scene> Scene::Copy(const Ref<Scene>& other)
	{
		Ref<Scene> scene = CreateRef<Scene>();
		scene->m_ViewportWidth = other->m_ViewportWidth;
		scene->m_ViewportHeight = other->m_ViewportHeight;
		scene->m_FilePath = other->m_FilePath;
		scene->m_AudioDeviceEnabled = other->m_AudioDeviceEnabled;

		// Preserve the source's creation order so iteration order is stable.
		std::vector<entt::entity> sourceEntities;
		for (entt::entity entity : other->m_Registry.view<IDComponent>())
			sourceEntities.push_back(entity);
		std::reverse(sourceEntities.begin(), sourceEntities.end());

		for (entt::entity sourceEntity : sourceEntities)
		{
			const UUID uuid = other->m_Registry.get<IDComponent>(sourceEntity).ID;
			const std::string& name = other->m_Registry.get<TagComponent>(sourceEntity).Tag;
			Entity copy = scene->CreateEntityWithUUID(uuid, name);
			CopyComponents(AllComponents{}, other->m_Registry, sourceEntity, scene->m_Registry, copy.GetHandle());
		}
		return scene;
	}

	Entity Scene::CreateEntity(const std::string& name)
	{
		return CreateEntityWithUUID(UUID(), name);
	}

	Entity Scene::CreateEntityWithUUID(UUID uuid, const std::string& name)
	{
		PN_CORE_ASSERT(uuid.IsValid(), "Entity UUID must be valid");
		PN_CORE_ASSERT(!m_EntityMap.contains(uuid), "Duplicate entity UUID");

		Entity entity(m_Registry.create(), this);
		entity.AddComponent<IDComponent>(uuid);
		entity.AddComponent<TagComponent>(name.empty() ? std::string("Entity") : name);
		entity.AddComponent<TransformComponent>();
		entity.AddComponent<RelationshipComponent>();
		m_EntityMap[uuid] = entity.GetHandle();
		return entity;
	}

	void Scene::DestroyEntity(Entity entity)
	{
		if (!entity.IsValid())
			return;
		if (m_IsRunning)
		{
			const UUID uuid = entity.GetUUID();
			if (std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), uuid) == m_PendingDestroy.end())
				m_PendingDestroy.push_back(uuid);
			// Outside of a frame (e.g. automation commands) destroy right away.
			if (!m_IsUpdating)
				FlushPendingDestroys();
			return;
		}
		DestroyEntityImmediate(entity);
	}

	bool Scene::IsEntityPendingDestroy(Entity entity) const
	{
		if (!entity.IsValid())
			return true;
		const UUID uuid = m_Registry.get<IDComponent>(entity.GetHandle()).ID;
		return std::find(m_PendingDestroy.begin(), m_PendingDestroy.end(), uuid) != m_PendingDestroy.end();
	}

	void Scene::FlushPendingDestroys()
	{
		// Destroying can queue more destroys (scripts' on_destroy), so loop until stable.
		while (!m_PendingDestroy.empty())
		{
			std::vector<UUID> pending = std::move(m_PendingDestroy);
			m_PendingDestroy.clear();
			for (UUID uuid : pending)
			{
				if (std::optional<Entity> entity = TryGetEntityByUUID(uuid))
					DestroyEntityImmediate(*entity);
			}
		}
	}

	void Scene::DestroyEntityImmediate(Entity entity)
	{
		if (!entity.IsValid())
			return;

		// Children first (copy the list: destroying mutates relationships).
		for (Entity child : entity.GetChildren())
			DestroyEntityImmediate(child);

		if (m_IsRunning)
		{
			if (m_ScriptEngine)
				m_ScriptEngine->OnEntityDestroyed(entity);
			if (m_PhysicsWorld)
				m_PhysicsWorld->OnEntityDestroyed(entity);
			if (m_AudioScene)
				m_AudioScene->OnEntityDestroyed(entity);
		}

		if (Entity parent = entity.GetParent())
		{
			auto& siblings = parent.GetComponent<RelationshipComponent>().Children;
			std::erase(siblings, entity.GetUUID());
		}

		m_EntityMap.erase(entity.GetUUID());
		m_Registry.destroy(entity.GetHandle());
	}

	Entity Scene::DuplicateEntity(Entity entity)
	{
		Entity duplicate = CreateEntity(entity.GetName());
		CopyComponents(AllComponents{}, m_Registry, entity.GetHandle(), m_Registry, duplicate.GetHandle());

		// The copy shares the parent but gets fresh copies of the children.
		auto& relationship = duplicate.GetComponent<RelationshipComponent>();
		relationship.Children.clear();
		if (Entity parent = duplicate.GetParent())
			parent.GetComponent<RelationshipComponent>().Children.push_back(duplicate.GetUUID());

		for (Entity child : entity.GetChildren())
		{
			Entity childCopy = DuplicateEntity(child);
			SetParent(childCopy, duplicate, false);
		}
		return duplicate;
	}

	Entity Scene::GetEntityByUUID(UUID uuid)
	{
		const auto it = m_EntityMap.find(uuid);
		PN_CORE_ASSERT(it != m_EntityMap.end(), "Entity UUID not found");
		return Entity(it->second, this);
	}

	std::optional<Entity> Scene::TryGetEntityByUUID(UUID uuid)
	{
		const auto it = m_EntityMap.find(uuid);
		if (it == m_EntityMap.end())
			return std::nullopt;
		return Entity(it->second, this);
	}

	Entity Scene::FindEntityByName(std::string_view name)
	{
		for (auto [entity, tag] : m_Registry.view<TagComponent>().each())
		{
			if (tag.Tag == name)
				return Entity(entity, this);
		}
		return {};
	}

	std::vector<Entity> Scene::GetAllEntities()
	{
		std::vector<Entity> entities;
		auto view = m_Registry.view<IDComponent>();
		entities.reserve(view.size());
		for (entt::entity entity : view)
			entities.emplace_back(entity, this);
		std::reverse(entities.begin(), entities.end());
		return entities;
	}

	std::vector<Entity> Scene::GetRootEntities()
	{
		std::vector<Entity> roots;
		for (Entity entity : GetAllEntities())
		{
			if (!entity.GetComponent<RelationshipComponent>().Parent.IsValid())
				roots.push_back(entity);
		}
		return roots;
	}

	bool Scene::IsAncestor(Entity ancestor, Entity entity)
	{
		Entity current = entity.GetParent();
		while (current)
		{
			if (current == ancestor)
				return true;
			current = current.GetParent();
		}
		return false;
	}

	void Scene::SetParent(Entity child, Entity parent, bool keepWorldTransform)
	{
		PN_CORE_ASSERT(child.IsValid() && parent.IsValid(), "Invalid entities");
		if (child == parent || IsAncestor(child, parent))
		{
			PN_CORE_WARN("Cannot parent '{}' to '{}': would create a cycle", child.GetName(), parent.GetName());
			return;
		}

		const glm::mat4 world = GetWorldTransform(child);
		Unparent(child, false);

		child.GetComponent<RelationshipComponent>().Parent = parent.GetUUID();
		parent.GetComponent<RelationshipComponent>().Children.push_back(child.GetUUID());

		if (keepWorldTransform)
			SetWorldTransform(child, world);
	}

	void Scene::Unparent(Entity child, bool keepWorldTransform)
	{
		Entity parent = child.GetParent();
		if (!parent)
		{
			child.GetComponent<RelationshipComponent>().Parent = UUID::Invalid;
			return;
		}
		const glm::mat4 world = GetWorldTransform(child);
		std::erase(parent.GetComponent<RelationshipComponent>().Children, child.GetUUID());
		child.GetComponent<RelationshipComponent>().Parent = UUID::Invalid;
		if (keepWorldTransform)
			child.GetComponent<TransformComponent>().SetTransform(world);
	}

	glm::mat4 Scene::GetWorldTransform(Entity entity)
	{
		glm::mat4 transform = entity.GetComponent<TransformComponent>().GetTransform();
		Entity parent = entity.GetParent();
		while (parent)
		{
			transform = parent.GetComponent<TransformComponent>().GetTransform() * transform;
			parent = parent.GetParent();
		}
		return transform;
	}

	glm::vec3 Scene::GetWorldPosition(Entity entity) { return glm::vec3(GetWorldTransform(entity)[3]); }

	glm::quat Scene::GetWorldRotation(Entity entity)
	{
		glm::quat rotation = entity.GetComponent<TransformComponent>().Rotation;
		Entity parent = entity.GetParent();
		while (parent)
		{
			rotation = parent.GetComponent<TransformComponent>().Rotation * rotation;
			parent = parent.GetParent();
		}
		return glm::normalize(rotation);
	}

	void Scene::SetWorldTransform(Entity entity, const glm::mat4& worldTransform)
	{
		glm::mat4 local = worldTransform;
		if (Entity parent = entity.GetParent())
			local = glm::inverse(GetWorldTransform(parent)) * worldTransform;
		entity.GetComponent<TransformComponent>().SetTransform(local);
	}

	Entity Scene::GetPrimaryCameraEntity()
	{
		Entity fallback;
		for (auto [entity, camera] : m_Registry.view<CameraComponent>().each())
		{
			if (camera.Primary)
				return Entity(entity, this);
			if (!fallback)
				fallback = Entity(entity, this);
		}
		return fallback;
	}

	Entity Scene::GetEnvironmentEntity()
	{
		auto view = m_Registry.view<EnvironmentComponent>();
		if (view.empty())
			return {};
		return Entity(view.front(), this);
	}

	std::optional<bool> Scene::ConsumeCursorLockRequest()
	{
		std::optional<bool> request = m_RequestedCursorLock;
		m_RequestedCursorLock.reset();
		return request;
	}

	void Scene::OnRuntimeStart()
	{
		PN_CORE_ASSERT(!m_IsRunning, "Scene already running");
		m_IsRunning = true;
		m_Time = 0.0f;
		m_FrameCount = 0;
		m_TestReport = {};

		m_PhysicsWorld = CreateScope<PhysicsWorld>(*this);
		m_AudioScene = CreateScope<AudioScene>(*this, m_AudioDeviceEnabled);
		m_PerceptionSystem = CreateScope<PerceptionSystem>(*this);
		m_StimSystem = CreateScope<StimSystem>(*this);
		m_ScriptEngine = CreateScope<ScriptEngine>(*this);

		m_IsUpdating = true;
		m_PhysicsWorld->Start();
		m_AudioScene->Start();
		m_ScriptEngine->Start();
		m_IsUpdating = false;
		FlushPendingDestroys();
	}

	void Scene::OnRuntimeStop()
	{
		if (!m_IsRunning)
			return;

		if (m_ScriptEngine)
			m_ScriptEngine->Stop();
		FlushPendingDestroys();

		m_ScriptEngine.reset();
		m_StimSystem.reset();
		m_PerceptionSystem.reset();
		if (m_AudioScene)
			m_AudioScene->Stop();
		m_AudioScene.reset();
		if (m_PhysicsWorld)
			m_PhysicsWorld->Stop();
		m_PhysicsWorld.reset();
		m_IsRunning = false;
	}

	void Scene::OnUpdateRuntime(Timestep ts)
	{
		if (!m_IsRunning)
			return;
		if (m_IsPaused)
		{
			if (m_StepFrames <= 0)
				return;
			m_StepFrames--;
		}
		RunRuntimeFrame(ts);
	}

	void Scene::RunRuntimeFrame(Timestep ts)
	{
		m_IsUpdating = true;
		m_Overlay->Clear();

		m_ScriptEngine->Update(ts);
		m_PhysicsWorld->Step(ts);
		for (const ContactEvent& contact : m_PhysicsWorld->ConsumeContactEvents())
		{
			m_ScriptEngine->OnCollision(contact.EntityA, contact.EntityB);
			m_StimSystem->OnContact(contact.EntityA, contact.EntityB, contact.Point);
		}
		m_StimSystem->Update(ts);
		m_PerceptionSystem->Update(ts);
		UpdateSmokeVolumes(ts);
		m_AudioScene->Update(ts);
		AnimationSystem::Update(*this, ts);

		m_IsUpdating = false;
		FlushPendingDestroys();

		m_Time += ts;
		m_FrameCount++;
	}

	void Scene::OnUpdateEditor(Timestep)
	{
		// The editor scene is static; rendering reads it directly.
	}

	void Scene::UpdateSmokeVolumes(Timestep ts)
	{
		for (auto [entity, smoke] : m_Registry.view<SmokeVolumeComponent>().each())
		{
			if (smoke.Lifetime <= 0.0f)
				continue;
			smoke.RuntimeAge += ts;
			if (smoke.RuntimeAge >= smoke.Lifetime)
				DestroyEntity(Entity(entity, this));
		}
	}

	void Scene::OnViewportResize(uint32_t width, uint32_t height)
	{
		m_ViewportWidth = std::max(width, 1u);
		m_ViewportHeight = std::max(height, 1u);
	}

}
