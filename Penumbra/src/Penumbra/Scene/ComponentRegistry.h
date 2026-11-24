#pragma once

#include "Penumbra/Scene/Entity.h"

#include <nlohmann/json.hpp>

#include <functional>
#include <string>
#include <vector>

namespace Penumbra {

	struct ComponentSerializeOptions
	{
		// Include runtime-only fields (read-only state such as an NPC's suspicion).
		bool IncludeRuntimeState = false;
	};

	// One field of a component, in the order the component declares them.
	struct FieldInfo
	{
		std::string Name;  // the key in scene files, the automation API and Lua
		std::string Group; // the editor's section for it ("Sight", "Fog"); empty: none
		std::string Unit;  // "m", "deg", "s", "0-10"...; empty: none or a plain factor
		bool Runtime = false;
	};

	// Type-erased description of a component that can be inspected and edited by name.
	// Scene files, prefabs, the editor, the automation API and the Lua API all go
	// through this single table, so every component is reachable from all of them.
	struct ComponentInfo
	{
		std::string Name;
		bool Removable = true;
		std::function<bool(Entity)> Has;
		std::function<void(Entity)> Add;
		std::function<void(Entity)> Remove;
		std::function<nlohmann::json(Entity, const ComponentSerializeOptions&)> Serialize;
		// Merges the given JSON object into the component: absent keys keep their value.
		// Returns false and fills error when a key is unknown or a value has the wrong type.
		std::function<bool(Entity, const nlohmann::json&, std::string& error)> Deserialize;
		std::function<nlohmann::json()> Defaults;
		std::vector<FieldInfo> Fields;
	};

	class ComponentRegistry
	{
	public:
		static const std::vector<ComponentInfo>& GetAll();
		static const ComponentInfo* Find(std::string_view name);

		// Serializes all components of an entity (IDComponent excluded) into an object.
		static nlohmann::json SerializeEntityComponents(Entity entity, const ComponentSerializeOptions& options = {});
		// Adds/merges the components found in the object. Unknown component names are errors.
		static bool DeserializeEntityComponents(Entity entity, const nlohmann::json& components, std::string& error);
	};

}
