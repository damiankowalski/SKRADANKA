#include "Panels/PropertiesPanel.h"

#include "Penumbra/Scene/ComponentRegistry.h"
#include "Penumbra/Scene/JsonSerialization.h"

#include <imgui.h>

#include <map>

namespace Penumbra {

	using json = nlohmann::json;

	namespace {

		// String fields that are really enums: the editor offers a combo box for them.
		const std::map<std::string, std::vector<std::string>>& EnumFields()
		{
			static const std::map<std::string, std::vector<std::string>> s_Fields = [] {
				auto names = [](const auto& values) {
					std::vector<std::string> result;
					for (const auto& [value, name] : values)
						result.emplace_back(name);
					return result;
				};
				return std::map<std::string, std::vector<std::string>>{
					{ "RigidBody.Type", names(EnumNames<RigidBodyType>::Values) },
					{ "StimSource.Delivery", names(EnumNames<StimDelivery>::Values) },
					{ "StimResponse.Rules.Actions.Type", names(EnumNames<StimActionType>::Values) },
				};
			}();
			return s_Fields;
		}

		bool IsColorField(const std::string& key) { return key.find("Color") != std::string::npos; }

		bool EditValue(const std::string& label, const std::string& path, json& value);

		bool EditArray(const std::string& label, const std::string& path, json& value)
		{
			bool changed = false;
			if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%s (%zu)", label.c_str(), value.size()))
				return false;
			int removeIndex = -1;
			for (size_t i = 0; i < value.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				changed |= EditValue(std::format("[{}]", i), path, value[i]);
				ImGui::SameLine();
				if (ImGui::SmallButton("x"))
					removeIndex = static_cast<int>(i);
				ImGui::PopID();
			}
			if (removeIndex >= 0)
			{
				value.erase(static_cast<size_t>(removeIndex));
				changed = true;
			}
			if (ImGui::SmallButton("+ Add"))
			{
				// New elements copy the last one (or a sensible empty value).
				value.push_back(value.empty() ? json::object() : value.back());
				changed = true;
			}
			ImGui::TreePop();
			return changed;
		}

		bool EditValue(const std::string& label, const std::string& path, json& value)
		{
			const std::string& key = label;
			bool changed = false;

			if (value.is_boolean())
			{
				bool b = value.get<bool>();
				if (ImGui::Checkbox(label.c_str(), &b))
				{
					value = b;
					changed = true;
				}
			}
			else if (value.is_number())
			{
				float f = value.get<float>();
				if (ImGui::DragFloat(label.c_str(), &f, 0.01f))
				{
					value = f;
					changed = true;
				}
			}
			else if (value.is_string())
			{
				const auto enumIt = EnumFields().find(path + "." + key);
				std::string text = value.get<std::string>();
				if (enumIt != EnumFields().end())
				{
					if (ImGui::BeginCombo(label.c_str(), text.c_str()))
					{
						for (const std::string& option : enumIt->second)
						{
							if (ImGui::Selectable(option.c_str(), option == text))
							{
								value = option;
								changed = true;
							}
						}
						ImGui::EndCombo();
					}
				}
				else
				{
					char buffer[512];
					std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
					if (ImGui::InputText(label.c_str(), buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
					{
						value = std::string(buffer);
						changed = true;
					}
					// Accept dropped asset paths from the content browser.
					if (ImGui::BeginDragDropTarget())
					{
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("PENUMBRA_ASSET"))
						{
							value = std::string(static_cast<const char*>(payload->Data));
							changed = true;
						}
						ImGui::EndDragDropTarget();
					}
				}
			}
			else if (value.is_array() && (value.size() == 3 || value.size() == 4) &&
					 std::all_of(value.begin(), value.end(), [](const json& element) { return element.is_number(); }))
			{
				float v[4] = {};
				for (size_t i = 0; i < value.size(); i++)
					v[i] = value[i].get<float>();
				bool edited;
				if (IsColorField(key))
					edited = value.size() == 3 ? ImGui::ColorEdit3(label.c_str(), v, ImGuiColorEditFlags_Float) : ImGui::ColorEdit4(label.c_str(), v, ImGuiColorEditFlags_Float);
				else
					edited = value.size() == 3 ? ImGui::DragFloat3(label.c_str(), v, 0.02f) : ImGui::DragFloat4(label.c_str(), v, 0.02f);
				if (edited)
				{
					for (size_t i = 0; i < value.size(); i++)
						value[i] = v[i];
					changed = true;
				}
			}
			else if (value.is_array())
			{
				changed = EditArray(label, path + "." + key, value);
			}
			else if (value.is_object())
			{
				if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen))
				{
					// Object elements of arrays keep the array's path so enum lookups work.
					const std::string childPath = key.starts_with("[") ? path : path + "." + key;
					for (auto& [childKey, childValue] : value.items())
					{
						ImGui::PushID(childKey.c_str());
						changed |= EditValue(childKey, childPath, childValue);
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
			}
			else if (value.is_null())
			{
				ImGui::TextDisabled("%s: null", label.c_str());
			}
			return changed;
		}

	}

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
				json data = info.Serialize(entity, { .IncludeRuntimeState = false });
				bool edited = false;
				for (auto& [key, value] : data.items())
				{
					ImGui::PushID(key.c_str());
					edited |= EditValue(key, info.Name, value);
					ImGui::PopID();
				}
				if (edited)
				{
					std::string error;
					if (info.Deserialize(entity, data, error))
						changed = true;
					else
						PN_CORE_WARN("{}: {}", info.Name, error);
				}
				if (showRuntimeState)
				{
					const json runtime = info.Serialize(entity, { .IncludeRuntimeState = true });
					for (const auto& [key, value] : runtime.items())
					{
						if (!data.contains(key))
							ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s: %s", key.c_str(), value.dump().c_str());
					}
				}
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
