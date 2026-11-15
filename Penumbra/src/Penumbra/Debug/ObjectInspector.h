#pragma once

#include "Penumbra/Asset/MeshData.h"
#include "Penumbra/Core/Base.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Penumbra {

	class Scene;

	// The runtime's object inspector (F4): what a click in the world selects, what kind of thing it is
	// and which of its components matter first, and the actions its right-click menu offers. The ImGui
	// window over it lives in PenumbraRuntime/src/RuntimeDebugPanel.
	namespace ObjectInspector {

		struct PickResult
		{
			Entity Hit;     // the entity whose mesh (or light) the ray met
			Entity Object;  // what gets selected: SelectionRoot(Hit)
			float Distance = 0.0f;
		};

		// The nearest visible mesh along the ray (by its bounds), or a light (a small sphere at the
		// lamp). Skips hidden meshes, characters the fog of war hides and boxes the ray starts inside
		// (a room's volume around the camera).
		std::optional<PickResult> Pick(Scene& scene, const glm::vec3& origin, const glm::vec3& direction);

		// A click on a part selects the whole: the nearest character above it (a guard, not his helmet),
		// else the nearest prefab instance or scripted object (a torch, a door), else the entity itself.
		Entity SelectionRoot(Entity entity);

		struct ObjectContext
		{
			std::string Kind;                       // "NPC", "Thief", "Body", "Light", "Door", "Loose object"...
			std::vector<std::string> KeyComponents; // registry names, the ones that matter first, present on the entity
		};
		ObjectContext Describe(Entity entity);

		// World-space box around the entity's meshes and its children's (invalid when there are none).
		BoundingBox WorldBounds(Scene& scene, Entity entity);

		// Moves an entity, its physics body or character and, when limp, its ragdoll (keeps its rotation).
		void MoveTo(Scene& scene, Entity entity, const glm::vec3& position);
		// Lights on the entity and below it: whether any is on, and switching them all.
		bool HasLights(Entity entity);
		bool AreLightsOn(Entity entity);
		void SetLightsOn(Entity entity, bool on);

	}

}
