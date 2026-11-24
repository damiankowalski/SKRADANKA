#include "pnpch.h"
#include "Penumbra/ImGui/ComponentInspector.h"

#include "Penumbra/Scene/JsonSerialization.h"

#include <imgui.h>

#include <cctype>
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

		std::string NumberFormat(const std::string& unit) { return unit.empty() ? "%.3f" : "%.3f " + unit; }

		// X, Y, Z (and W) fields side by side, each with a coloured axis letter.
		bool EditAxes(float* v, size_t count, const std::string& unit)
		{
			static constexpr const char* Axes[] = { "X", "Y", "Z", "W" };
			static constexpr ImU32 Colors[] = { IM_COL32(220, 80, 80, 255), IM_COL32(110, 190, 90, 255), IM_COL32(90, 140, 230, 255),
				IM_COL32(170, 170, 170, 255) };
			const ImGuiStyle& style = ImGui::GetStyle();
			const float letter = ImGui::CalcTextSize("X").x + style.ItemInnerSpacing.x;
			const float total = ImGui::GetContentRegionAvail().x;
			const float width = std::max((total - static_cast<float>(count) * letter - static_cast<float>(count - 1) * style.ItemSpacing.x) /
											 static_cast<float>(count),
				20.0f);
			const std::string format = NumberFormat(unit);
			bool edited = false;
			for (size_t i = 0; i < count; i++)
			{
				if (i > 0)
					ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Text, Colors[i]);
				ImGui::TextUnformatted(Axes[i]);
				ImGui::PopStyleColor();
				ImGui::SameLine(0.0f, style.ItemInnerSpacing.x);
				ImGui::PushID(static_cast<int>(i));
				ImGui::SetNextItemWidth(width);
				edited |= ImGui::DragFloat("##axis", &v[i], 0.02f, 0.0f, 0.0f, format.c_str());
				ImGui::PopID();
			}
			return edited;
		}

		bool EditValue(const std::string& key, const std::string& label, const std::string& path, const std::string& unit, json& value);

		bool EditArray(const std::string& key, const std::string& label, const std::string& path, json& value)
		{
			bool changed = false;
			if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth, "%zu item%s", value.size(), value.size() == 1 ? "" : "s"))
				return false;
			int removeIndex = -1;
			for (size_t i = 0; i < value.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				changed |= EditValue(std::format("[{}]", i), std::format("[{}]", i), path + "." + key, "", value[i]);
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

		// `label` is the ImGui label ("##Key" in the field table, the key inside nested objects).
		bool EditValue(const std::string& key, const std::string& label, const std::string& path, const std::string& unit, json& value)
		{
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
				if (ImGui::DragFloat(label.c_str(), &f, 0.01f, 0.0f, 0.0f, NumberFormat(unit).c_str()))
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
			else if (value.is_array() && value.size() >= 2 && value.size() <= 4 &&
					 std::all_of(value.begin(), value.end(), [](const json& element) { return element.is_number(); }))
			{
				float v[4] = {};
				for (size_t i = 0; i < value.size(); i++)
					v[i] = value[i].get<float>();
				bool edited;
				ImGui::PushID(label.c_str());
				if (IsColorField(key) && value.size() >= 3)
					edited = value.size() == 3 ? ImGui::ColorEdit3("##color", v, ImGuiColorEditFlags_Float)
												: ImGui::ColorEdit4("##color", v, ImGuiColorEditFlags_Float);
				else
					edited = EditAxes(v, value.size(), unit);
				ImGui::PopID();
				if (edited)
				{
					for (size_t i = 0; i < value.size(); i++)
						value[i] = v[i];
					changed = true;
				}
			}
			else if (value.is_array())
			{
				changed = EditArray(key, label, path, value);
			}
			else if (value.is_object())
			{
				if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen, "%zu field%s", value.size(),
						value.size() == 1 ? "" : "s"))
				{
					// Object elements of arrays keep the array's path so enum lookups work.
					const std::string childPath = key.starts_with("[") ? path : path + "." + key;
					for (auto& [childKey, childValue] : value.items())
					{
						ImGui::PushID(childKey.c_str());
						ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
						changed |= EditValue(childKey, childKey, childPath, "", childValue);
						ImGui::PopID();
					}
					ImGui::TreePop();
				}
			}
			else if (value.is_null())
			{
				ImGui::TextDisabled("null");
			}
			return changed;
		}

		bool Apply(Entity entity, const ComponentInfo& info, const json& data, std::string& error)
		{
			if (info.Deserialize(entity, data, error))
				return true;
			error = std::format("{}: {}", info.Name, error);
			return false;
		}

		// One field's row: the label (tooltip, right-click menu) and its editor. Returns true when `data` changed.
		bool DrawFieldRow(Entity entity, const ComponentInfo& info, const FieldInfo& field, json& data, bool& applied)
		{
			json& value = data[field.Name];
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::AlignTextToFramePadding();
			const std::string name = ComponentInspector::DisplayName(info.Name, field.Name);
			ImGui::TextUnformatted(name.c_str());
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
				ImGui::SetTooltip("%s%s%s", field.Name.c_str(), field.Unit.empty() ? "" : ", unit: ", field.Unit.c_str());
			if (ImGui::BeginPopupContextItem("FieldMenu"))
			{
				if (ImGui::MenuItem("Reset to default"))
				{
					std::string error;
					if (ComponentInspector::ResetField(entity, info, field.Name, error))
						applied = true;
					else
						PN_CORE_WARN("{}", error);
				}
				if (ImGui::MenuItem("Copy value"))
					ImGui::SetClipboardText(value.dump().c_str());
				ImGui::EndPopup();
			}
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-FLT_MIN);
			return EditValue(field.Name, "##" + field.Name, info.Name, field.Unit, value);
		}

	}

	namespace ComponentInspector {

		std::string DisplayName(std::string_view component, std::string_view field)
		{
			if (component == "Transform" && field == "Translation")
				return "Position";
			std::vector<std::string> words;
			std::string word;
			for (size_t i = 0; i < field.size(); i++)
			{
				const char c = field[i];
				const bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
				const unsigned char previous = i > 0 ? static_cast<unsigned char>(field[i - 1]) : 0;
				const bool previousLower = i > 0 && (std::islower(previous) || std::isdigit(previous));
				const bool nextLower = i + 1 < field.size() && std::islower(static_cast<unsigned char>(field[i + 1]));
				const bool previousUpper = i > 0 && std::isupper(static_cast<unsigned char>(field[i - 1]));
				if (!word.empty() && upper && (previousLower || (previousUpper && nextLower)))
				{
					words.push_back(word);
					word.clear();
				}
				word += c;
			}
			if (!word.empty())
				words.push_back(word);

			std::string result;
			for (size_t i = 0; i < words.size(); i++)
			{
				std::string w = words[i];
				const bool acronym = w.size() > 1 && std::all_of(w.begin(), w.end(), [](char c) {
					return std::isupper(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c));
				});
				if (i > 0 && !acronym)
					w[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(w[0])));
				if (i > 0)
					result += ' ';
				result += w;
			}
			return result;
		}

		bool ResetComponent(Entity entity, const ComponentInfo& info, std::string& error) { return Apply(entity, info, info.Defaults(), error); }

		bool ResetField(Entity entity, const ComponentInfo& info, std::string_view field, std::string& error)
		{
			const json defaults = info.Defaults();
			const auto it = defaults.find(field);
			if (it == defaults.end())
			{
				error = std::format("{} has no field '{}'", info.Name, field);
				return false;
			}
			return Apply(entity, info, json{ { std::string(field), *it } }, error);
		}

		std::string CopyComponent(Entity entity, const ComponentInfo& info)
		{
			return info.Serialize(entity, { .IncludeRuntimeState = false }).dump(2);
		}

		bool PasteComponent(Entity entity, const ComponentInfo& info, std::string_view text, std::string& error)
		{
			const json data = json::parse(text, nullptr, false);
			if (!data.is_object())
			{
				error = std::format("{}: the clipboard holds no component (a JSON object of its fields)", info.Name);
				return false;
			}
			return Apply(entity, info, data, error);
		}

		bool DrawFields(Entity entity, const ComponentInfo& info)
		{
			json data = info.Serialize(entity, { .IncludeRuntimeState = false });

			// The label column fits the longest label of the component, up to 45% of the width.
			float labelWidth = 0.0f;
			for (const FieldInfo& field : info.Fields)
			{
				if (!field.Runtime)
					labelWidth = std::max(labelWidth, ImGui::CalcTextSize(DisplayName(info.Name, field.Name).c_str()).x);
			}
			labelWidth = std::min(labelWidth + ImGui::GetStyle().CellPadding.x * 2.0f, ImGui::GetContentRegionAvail().x * 0.45f);

			bool edited = false;
			bool applied = false; // reset from a field's menu: already written to the entity
			std::string group;
			bool groupOpen = true;
			bool tableOpen = false;
			auto endTable = [&] {
				if (tableOpen)
					ImGui::EndTable();
				tableOpen = false;
			};
			for (const FieldInfo& field : info.Fields)
			{
				if (field.Runtime || !data.contains(field.Name))
					continue;
				if (field.Group != group)
				{
					endTable();
					group = field.Group;
					ImGui::PushID(group.c_str());
					groupOpen = group.empty() || ImGui::TreeNodeEx("##group", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_NoTreePushOnOpen |
																	  ImGuiTreeNodeFlags_SpanAvailWidth,
																	  "%s", group.c_str());
					ImGui::PopID();
				}
				if (!groupOpen)
					continue;
				if (!tableOpen)
				{
					// BeginTable pushes its own ID until EndTable: name each group's table instead of PushID round it.
					tableOpen = ImGui::BeginTable(("Fields#" + group).c_str(), 2, ImGuiTableFlags_SizingFixedFit);
					if (!tableOpen)
						continue;
					ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, labelWidth);
					ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
				}
				ImGui::PushID(field.Name.c_str());
				edited |= DrawFieldRow(entity, info, field, data, applied);
				ImGui::PopID();
			}
			endTable();

			if (applied)
				return true;
			if (!edited)
				return false;
			std::string error;
			if (Apply(entity, info, data, error))
				return true;
			PN_CORE_WARN("{}", error);
			return false;
		}

		void DrawRuntimeState(Entity entity, const ComponentInfo& info)
		{
			const json runtime = info.Serialize(entity, { .IncludeRuntimeState = true });
			for (const FieldInfo& field : info.Fields)
			{
				const auto it = runtime.find(field.Name);
				if (field.Runtime && it != runtime.end())
					ImGui::TextColored(ImVec4(0.6f, 0.8f, 1.0f, 1.0f), "%s: %s", DisplayName(info.Name, field.Name).c_str(), it->dump().c_str());
			}
		}

		bool DrawComponentMenu(Entity entity, const ComponentInfo& info, bool& removed)
		{
			removed = false;
			bool changed = false;
			if (!ImGui::BeginPopupContextItem("ComponentMenu"))
				return false;
			std::string error;
			if (ImGui::MenuItem("Reset to defaults"))
				changed = ResetComponent(entity, info, error);
			if (ImGui::MenuItem("Copy values"))
				ImGui::SetClipboardText(CopyComponent(entity, info).c_str());
			const char* clipboard = ImGui::GetClipboardText();
			if (ImGui::MenuItem("Paste values", nullptr, false, clipboard && *clipboard))
				changed = PasteComponent(entity, info, clipboard, error);
			if (info.Removable)
			{
				ImGui::Separator();
				if (ImGui::MenuItem("Remove component"))
				{
					info.Remove(entity);
					removed = true;
					changed = true;
				}
			}
			ImGui::EndPopup();
			if (!error.empty())
				PN_CORE_WARN("{}", error);
			return changed;
		}

	}

}
