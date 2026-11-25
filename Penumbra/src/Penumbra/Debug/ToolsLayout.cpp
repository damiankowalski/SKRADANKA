#include "pnpch.h"
#include "Penumbra/Debug/ToolsLayout.h"

namespace Penumbra {

	RuntimeToolsLayout RuntimeToolsLayout::Default(glm::vec2 display, float uiScale)
	{
		const float margin = Margin * uiScale;
		RuntimeToolsLayout layout;

		const float panelWidth = std::min(PanelWidth * uiScale, display.x * 0.45f);
		layout.Panel.Min = { display.x - margin - panelWidth, std::max(display.y * HudTopShare, margin) };
		layout.Panel.Max = { display.x - margin, display.y - margin };

		// F3 is centred, but never wider than the room left of F4.
		const float performanceWidth = std::min(PerformanceWidth * uiScale, 2.0f * (layout.Panel.Min.x - margin - display.x * 0.5f));
		layout.Performance.Min = { display.x * 0.5f - performanceWidth * 0.5f, margin };
		layout.Performance.Max = { display.x * 0.5f + performanceWidth * 0.5f, std::min(display.y * 0.4f, layout.Panel.Max.y) };
		return layout;
	}

}
