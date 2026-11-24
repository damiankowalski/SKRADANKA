#pragma once

#include "Penumbra/Core/Base.h"

#include <imgui.h>

#include <span>

namespace Penumbra {

	// Where a panel goes in a default docked layout.
	enum class DockArea
	{
		Center,     // what is left in the middle (the editor's viewport)
		Left,
		LeftBottom, // under Left
		Right,
		Bottom      // under Center; several panels here become tabs
	};

	struct DockedWindow
	{
		const char* Name; // the window's ImGui name, as passed to ImGui::Begin
		DockArea Area;
	};

	// Default docked layouts for the developer tools, built with ImGui's dock builder.
	class DockLayout
	{
	public:
		// Share of the space each side takes when the layout is built.
		static constexpr float LeftShare = 0.18f;
		static constexpr float RightShare = 0.22f;
		static constexpr float BottomShare = 0.27f;
		static constexpr float LeftBottomShare = 0.38f;

		// True when the dock space holds nothing yet: the first start, or a layout file without it.
		static bool IsEmpty(ImGuiID dockSpace);

		// Clears `dockSpace` and docks each window in its area; within an area the first window listed is the selected
		// tab. Call before the dock space and the windows are submitted in the frame. The Center node hides its
		// tab bar (a small triangle brings it back).
		static void Build(ImGuiID dockSpace, ImVec2 position, ImVec2 size, std::span<const DockedWindow> windows);
	};

}
