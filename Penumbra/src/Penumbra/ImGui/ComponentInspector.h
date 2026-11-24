#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/Entity.h"

#include <string>
#include <string_view>

namespace Penumbra {

	// Dear ImGui widgets for a component's fields, generated from the component registry (field order, groups,
	// units), so every component (including future ones) is editable without custom UI. Shared by the editor's
	// Properties panel and the runtime's inspector (F4). Labels sit in a left column sized to fit them; hovering a
	// label shows its key and unit, right-clicking it resets the field or copies its value.
	namespace ComponentInspector {

		// Draws the editable fields of one component inside the current window and applies an edit
		// at once. Returns true when something changed this frame.
		bool DrawFields(Entity entity, const ComponentInfo& info);
		// Runtime-only state (read only), one line per field.
		void DrawRuntimeState(Entity entity, const ComponentInfo& info);
		// The component header's right-click actions (reset, copy, paste, remove when removable) for the item drawn
		// last. Returns true when the entity changed; `removed` tells the caller the component is gone.
		bool DrawComponentMenu(Entity entity, const ComponentInfo& info, bool& removed);

		// "MaxSlopeDegrees" -> "Max slope degrees", "SSAOEnabled" -> "SSAO enabled"; Transform's Translation reads
		// "Position".
		std::string DisplayName(std::string_view component, std::string_view field);

		// The actions behind the menus, usable without ImGui. Errors are values: false and `error`.
		bool ResetComponent(Entity entity, const ComponentInfo& info, std::string& error);
		bool ResetField(Entity entity, const ComponentInfo& info, std::string_view field, std::string& error);
		// The component's editable fields as JSON text (what Paste takes).
		std::string CopyComponent(Entity entity, const ComponentInfo& info);
		bool PasteComponent(Entity entity, const ComponentInfo& info, std::string_view text, std::string& error);

	}

}
