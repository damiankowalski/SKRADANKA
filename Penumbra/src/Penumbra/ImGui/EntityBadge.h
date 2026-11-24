#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Scene/Entity.h"

namespace Penumbra {

	// The coloured letter the editor's Hierarchy shows before an entity's name: what kind of thing it is, at a glance.
	struct EntityBadge
	{
		char Letter = '-';
		uint32_t Color = 0xFF808080; // ImU32, ABGR
		const char* Kind = "Group";  // the tooltip
	};

	// First match wins: guard, thief, light, camera, environment, script, prefab instance, sound, physics body,
	// mesh, else a group (an empty or a parent).
	EntityBadge GetEntityBadge(Entity entity);

}
