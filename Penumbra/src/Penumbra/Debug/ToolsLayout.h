#pragma once

#include "Penumbra/Core/Base.h"

#include <glm/glm.hpp>

namespace Penumbra {

	struct ToolsRect
	{
		glm::vec2 Min{ 0.0f };
		glm::vec2 Max{ 0.0f };

		bool Overlaps(const ToolsRect& other) const
		{
			return Min.x < other.Max.x && other.Min.x < Max.x && Min.y < other.Max.y && other.Min.y < Max.y;
		}
	};

	// Where the runtime's testing tools open by default (window units), clear of each other and of the demo HUD:
	// objectives and controls on the left, compass and loot top right, the noise probe and bolt bottom left.
	// F3 sits at the top centre, F4 (tabs, the inspector among them) down the right side under the compass. Once
	// moved, the panels keep their place (RuntimeLayout.ini).
	struct RuntimeToolsLayout
	{
		static constexpr float PerformanceWidth = 620.0f; // the widest F3 gets, at UI scale 1
		static constexpr float PanelWidth = 440.0f; // room for all seven tabs
		static constexpr float HudTopShare = 0.14f;       // the compass and loot band at the top right
		static constexpr float Margin = 10.0f;

		ToolsRect Performance;
		ToolsRect Panel;

		static RuntimeToolsLayout Default(glm::vec2 display, float uiScale);
	};

}
