#include "pnpch.h"
#include "Penumbra/ImGui/DockLayout.h"

#include <imgui_internal.h>

namespace Penumbra {

	bool DockLayout::IsEmpty(ImGuiID dockSpace)
	{
		const ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockSpace);
		return !node || (!node->IsSplitNode() && node->Windows.empty());
	}

	void DockLayout::Build(ImGuiID dockSpace, ImVec2 position, ImVec2 size, std::span<const DockedWindow> windows)
	{
		ImGui::DockBuilderRemoveNode(dockSpace);
		ImGui::DockBuilderAddNode(dockSpace, ImGuiDockNodeFlags_DockSpace);
		ImGui::DockBuilderSetNodePos(dockSpace, position);
		ImGui::DockBuilderSetNodeSize(dockSpace, size);

		ImGuiID rest = dockSpace;
		const ImGuiID left = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Left, LeftShare, nullptr, &rest);
		const ImGuiID right = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Right, RightShare / (1.0f - LeftShare), nullptr, &rest);
		const ImGuiID bottom = ImGui::DockBuilderSplitNode(rest, ImGuiDir_Down, BottomShare, nullptr, &rest);
		ImGuiID leftTop = left;
		const ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, LeftBottomShare, nullptr, &leftTop);
		const ImGuiID center = rest;

		auto nodeFor = [&](DockArea area) {
			switch (area)
			{
				case DockArea::Left: return leftTop;
				case DockArea::LeftBottom: return leftBottom;
				case DockArea::Right: return right;
				case DockArea::Bottom: return bottom;
				case DockArea::Center: break;
			}
			return center;
		};
		for (const DockedWindow& window : windows)
			ImGui::DockBuilderDockWindow(window.Name, nodeFor(window.Area));
		// The first window listed in an area is its selected tab.
		for (auto it = windows.rbegin(); it != windows.rend(); ++it)
		{
			if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(nodeFor(it->Area)))
				node->SelectedTabId = ImHashStr("#TAB", 0, ImHashStr(it->Name)); // the window's TabId
		}
		if (ImGuiDockNode* node = ImGui::DockBuilderGetNode(center))
			node->SetLocalFlags(node->LocalFlags | ImGuiDockNodeFlags_HiddenTabBar);
		ImGui::DockBuilderFinish(dockSpace);
	}

}
