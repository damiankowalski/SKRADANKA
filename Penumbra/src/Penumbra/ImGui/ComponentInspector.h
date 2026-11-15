#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/Entity.h"

namespace Penumbra {

	// Dear ImGui widgets for a component's fields, generated from the component registry's JSON
	// form, so every component (including future ones) is editable without custom UI. Shared by
	// the editor's Properties panel and the runtime's inspector (F4).
	namespace ComponentInspector {

		// Draws the editable fields of one component inside the current window and applies an edit
		// at once. Returns true when something changed this frame.
		bool DrawFields(Entity entity, const ComponentInfo& info);
		// Runtime-only state (read only), one line per field.
		void DrawRuntimeState(Entity entity, const ComponentInfo& info);

	}

}
