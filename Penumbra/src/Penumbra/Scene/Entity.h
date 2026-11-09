#pragma once

#include "Penumbra/Scene/Components.h"

#include <entt/entt.hpp>

namespace Penumbra {

	class Scene;

	// Lightweight handle: an EnTT entity plus the scene that owns it.
	class Entity
	{
	public:
		Entity() = default;
		Entity(entt::entity handle, Scene* scene) : m_EntityHandle(handle), m_Scene(scene) {}

		template<typename T, typename... Args>
		T& AddComponent(Args&&... args);

		template<typename T, typename... Args>
		T& AddOrReplaceComponent(Args&&... args);

		template<typename T>
		T& GetComponent();

		template<typename T>
		const T& GetComponent() const;

		template<typename T>
		T* TryGetComponent();

		template<typename T>
		bool HasComponent() const;

		template<typename T>
		void RemoveComponent();

		bool IsValid() const;
		explicit operator bool() const { return IsValid(); }
		operator entt::entity() const { return m_EntityHandle; }
		entt::entity GetHandle() const { return m_EntityHandle; }
		Scene* GetScene() const { return m_Scene; }

		UUID GetUUID() const { return GetComponent<IDComponent>().ID; }
		const std::string& GetName() const { return GetComponent<TagComponent>().Tag; }
		TransformComponent& Transform() { return GetComponent<TransformComponent>(); }

		Entity GetParent() const;
		std::vector<Entity> GetChildren() const;

		bool operator==(const Entity& other) const { return m_EntityHandle == other.m_EntityHandle && m_Scene == other.m_Scene; }
		bool operator!=(const Entity& other) const { return !(*this == other); }

	private:
		entt::entity m_EntityHandle = entt::null;
		Scene* m_Scene = nullptr;
	};

}

#include "Penumbra/Scene/Scene.h"

namespace Penumbra {

	template<typename T, typename... Args>
	T& Entity::AddComponent(Args&&... args)
	{
		PN_CORE_ASSERT(!HasComponent<T>(), "Entity already has component");
		return m_Scene->m_Registry.emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
	}

	template<typename T, typename... Args>
	T& Entity::AddOrReplaceComponent(Args&&... args)
	{
		return m_Scene->m_Registry.emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
	}

	template<typename T>
	T& Entity::GetComponent()
	{
		PN_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
		return m_Scene->m_Registry.get<T>(m_EntityHandle);
	}

	template<typename T>
	const T& Entity::GetComponent() const
	{
		PN_CORE_ASSERT(HasComponent<T>(), "Entity does not have component");
		return m_Scene->m_Registry.get<T>(m_EntityHandle);
	}

	template<typename T>
	T* Entity::TryGetComponent()
	{
		return m_Scene->m_Registry.try_get<T>(m_EntityHandle);
	}

	template<typename T>
	bool Entity::HasComponent() const
	{
		return m_Scene->m_Registry.all_of<T>(m_EntityHandle);
	}

	template<typename T>
	void Entity::RemoveComponent()
	{
		m_Scene->m_Registry.remove<T>(m_EntityHandle);
	}

}
