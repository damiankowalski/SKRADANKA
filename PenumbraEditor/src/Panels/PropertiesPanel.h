#pragma once

#include "Penumbra/Scene/Entity.h"

namespace Penumbra {

	// Inspector for the selected entity. Fields are generated from the component registry's
	// JSON form, so every component (including future ones) is editable without custom UI.
	class PropertiesPanel
	{
	public:
		// Returns true when the user changed something this frame (the caller records undo).
		bool Draw(Entity entity, bool showRuntimeState);
	};

}
