#include "pnpch.h"
#include "Penumbra/Scene/Entity.h"

namespace Penumbra {

	bool Entity::IsValid() const
	{
		return m_Scene != nullptr && m_EntityHandle != entt::null && m_Scene->m_Registry.valid(m_EntityHandle);
	}

	Entity Entity::GetParent() const
	{
		const RelationshipComponent* relationship = m_Scene->m_Registry.try_get<RelationshipComponent>(m_EntityHandle);
		if (!relationship || !relationship->Parent.IsValid())
			return {};
		return m_Scene->TryGetEntityByUUID(relationship->Parent).value_or(Entity{});
	}

	std::vector<Entity> Entity::GetChildren() const
	{
		std::vector<Entity> children;
		const RelationshipComponent* relationship = m_Scene->m_Registry.try_get<RelationshipComponent>(m_EntityHandle);
		if (!relationship)
			return children;
		children.reserve(relationship->Children.size());
		for (UUID childID : relationship->Children)
		{
			if (std::optional<Entity> child = m_Scene->TryGetEntityByUUID(childID))
				children.push_back(*child);
		}
		return children;
	}

}
