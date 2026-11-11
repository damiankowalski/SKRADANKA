#include "pnpch.h"
#include "Penumbra/Automation/CommandSchema.h"

namespace Penumbra {

	using json = nlohmann::json;

	namespace {

		std::string_view Trim(std::string_view text)
		{
			const size_t first = text.find_first_not_of(" \t");
			if (first == std::string_view::npos)
				return {};
			const size_t last = text.find_last_not_of(" \t");
			return text.substr(first, last - first + 1);
		}

		json Vec3Schema(const char* description)
		{
			return { { "type", "array" }, { "items", { { "type", "number" } } }, { "minItems", 3 }, { "maxItems", 3 }, { "description", description } };
		}

		// Types of every argument name used by the command table. Add new names here when a
		// command gains an argument (the Mcp test suite fails otherwise).
		const std::unordered_map<std::string, json>& GetArgumentTypes()
		{
			static const std::unordered_map<std::string, json> types = [] {
				std::unordered_map<std::string, json> t;
				auto string = [](const char* description) { return json{ { "type", "string" }, { "description", description } }; };
				auto integer = [](const char* description) { return json{ { "type", "integer" }, { "description", description } }; };
				auto number = [](const char* description) { return json{ { "type", "number" }, { "description", description } }; };
				auto boolean = [](const char* description) { return json{ { "type", "boolean" }, { "description", description } }; };

				t["id"] = string("Entity id (decimal string, from entity.create or scene.list).");
				t["name"] = string("Name: the entity to address or create, or the project name for project.new.");
				t["parent"] = { { "type", json::array({ "string", "null" }) }, { "description", "Parent entity id." } };
				t["parentName"] = string("Parent entity name.");
				t["components"] = { { "type", "object" },
					{ "description", "Components by name with (partial) field values, e.g. {\"Mesh\":{\"Mesh\":\"builtin:cube\"}}. "
									 "component.list shows every component, field and default." } };
				t["component"] = string("Component name (see component.list).");
				t["runtime"] = boolean("Include runtime state.");
				t["keepWorld"] = boolean("Keep the world transform when re-parenting (default true).");
				t["count"] = integer("Number of messages.");
				t["path"] = string("File path. Scene, prefab and asset paths are relative to the project's asset directory.");
				t["directory"] = string("Asset subdirectory.");
				t["content"] = string("Full text content of the file.");
				t["source"] = string("Absolute path of the file to copy into the project.");
				t["destination"] = string("Asset-relative destination path.");
				t["output"] = string("Output folder.");
				t["scene"] = string("Asset-relative scene path, e.g. Scenes/Test.pscene.");
				t["startScene"] = string("Asset-relative scene loaded when the game starts.");
				t["windowWidth"] = integer("Game window width in pixels.");
				t["windowHeight"] = integer("Game window height in pixels.");
				t["position"] = Vec3Schema("World position [x, y, z].");
				t["rotation"] = Vec3Schema("Rotation as Euler angles in degrees [x, y, z].");
				t["paused"] = boolean("true pauses, false resumes.");
				t["seconds"] = number("Simulated seconds.");
				t["frames"] = integer("Number of frames (overrides seconds).");
				t["dt"] = number("Fixed time step in seconds (default 1/60).");
				t["key"] = string("Key name: A-Z, 0-9, F1-F12, Space, Escape, Enter, LeftShift, ...");
				t["down"] = boolean("Pressed (true) or released (false).");
				t["button"] = { { "type", "string" }, { "enum", json::array({ "Left", "Right", "Middle" }) }, { "description", "Mouse button." } };
				t["x"] = number("Mouse x in window pixels.");
				t["y"] = number("Mouse y in window pixels.");
				t["code"] = string("Lua chunk; its return value is the command result.");
				t["width"] = integer("Image width in pixels (default 1280).");
				t["height"] = integer("Image height in pixels (default 720).");
				t["camera"] = { { "type", "object" },
					{ "description", "Overrides the scene camera." },
					{ "properties",
						{ { "position", Vec3Schema("Camera position.") }, { "target", Vec3Schema("Point the camera looks at.") },
							{ "fov", number("Vertical field of view in degrees (default 60).") } } } };
				t["allowFailure"] = boolean("Return the report instead of failing when assertions fail.");
				return t;
			}();
			return types;
		}

	}

	std::vector<CommandArgument> ParseCommandArguments(std::string_view arguments)
	{
		std::vector<CommandArgument> result;
		size_t start = 0;
		while (start <= arguments.size())
		{
			// Split on top-level commas (notes in parentheses may contain anything).
			size_t end = start;
			int depth = 0;
			while (end < arguments.size() && (arguments[end] != ',' || depth > 0))
			{
				depth += arguments[end] == '(' ? 1 : arguments[end] == ')' ? -1 : 0;
				end++;
			}
			std::string_view token = Trim(arguments.substr(start, end - start));
			start = end + 1;
			if (token.empty())
				continue;

			std::string note;
			if (const size_t open = token.find('('); open != std::string_view::npos)
			{
				const size_t close = token.rfind(')');
				note = std::string(Trim(token.substr(open + 1, (close == std::string_view::npos || close < open ? token.size() : close) - open - 1)));
				token = Trim(token.substr(0, open));
			}
			bool optional = false;
			if (!token.empty() && token.back() == '?')
			{
				optional = true;
				token = Trim(token.substr(0, token.size() - 1));
			}

			std::vector<std::string> names;
			size_t nameStart = 0;
			while (nameStart <= token.size())
			{
				size_t bar = token.find('|', nameStart);
				if (bar == std::string_view::npos)
					bar = token.size();
				if (std::string_view name = Trim(token.substr(nameStart, bar - nameStart)); !name.empty())
					names.emplace_back(name);
				nameStart = bar + 1;
			}

			for (const std::string& name : names)
			{
				CommandArgument argument;
				argument.Name = name;
				argument.Required = !optional && names.size() == 1;
				argument.Note = note;
				for (const std::string& other : names)
				{
					if (other != name)
						argument.Alternatives.push_back(other);
				}
				result.push_back(std::move(argument));
			}
		}
		return result;
	}

	json BuildCommandInputSchema(std::string_view arguments)
	{
		json properties = json::object();
		json required = json::array();
		for (const CommandArgument& argument : ParseCommandArguments(arguments))
		{
			const auto& types = GetArgumentTypes();
			const auto it = types.find(argument.Name);
			json property = it != types.end() ? it->second : json{ { "description", argument.Name } };
			std::string description = property.value("description", std::string());
			if (!argument.Alternatives.empty())
			{
				std::string others;
				for (const std::string& other : argument.Alternatives)
					others += (others.empty() ? "" : ", ") + other;
				description += std::format(" Alternative to {}: give one of them.", others);
			}
			if (!argument.Note.empty())
				description += std::format(" ({})", argument.Note);
			property["description"] = description;
			properties[argument.Name] = std::move(property);
			if (argument.Required)
				required.push_back(argument.Name);
		}

		json schema = { { "type", "object" }, { "properties", std::move(properties) } };
		if (!required.empty())
			schema["required"] = std::move(required);
		return schema;
	}

	bool IsKnownCommandArgument(std::string_view name) { return GetArgumentTypes().contains(std::string(name)); }

}
