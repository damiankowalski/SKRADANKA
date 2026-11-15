#include "Panels/PropertiesPanel.h"

#include "Penumbra/ImGui/ComponentInspector.h"
#include "Penumbra/Scene/ComponentRegistry.h"

#include <imgui.h>

namespace Penumbra {

	bool PropertiesPanel::Draw(Entity entity, bool showRuntimeState)
	{
		ImGui::Begin("Properties");
		if (!entity)
		{
			ImGui::TextDisabled("Nothing selected");
			ImGui::End();
			return false;
		}

		bool changed = false;
		ImGui::TextDisabled("ID %llu", static_cast<unsigned long long>(static_cast<uint64_t>(entity.GetUUID())));

		for (const ComponentInfo& info : ComponentRegistry::GetAll())
		{
			if (!info.Has(entity))
				continue;
			ImGui::PushID(info.Name.c_str());
			const bool open = ImGui::CollapsingHeader(info.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
			bool removeRequested = false;
			if (info.Removable && ImGui::BeginPopupContextItem("ComponentMenu"))
			{
				if (ImGui::MenuItem("Remove component"))
					removeRequested = true;
				ImGui::EndPopup();
			}
			if (open)
			{
				changed |= ComponentInspector::DrawFields(entity, info);
				if (showRuntimeState)
					ComponentInspector::DrawRuntimeState(entity, info);
			}
			ImGui::PopID();
			if (removeRequested)
			{
				info.Remove(entity);
				changed = true;
			}
		}

		ImGui::Separator();
		if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
			ImGui::OpenPopup("AddComponent");
		if (ImGui::BeginPopup("AddComponent"))
		{
			for (const ComponentInfo& info : ComponentRegistry::GetAll())
			{
				if (info.Removable && !info.Has(entity) && ImGui::MenuItem(info.Name.c_str()))
				{
					info.Add(entity);
					changed = true;
				}
			}
			ImGui::EndPopup();
		}
		ImGui::End();
		return changed;
	}

}
