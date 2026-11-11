#pragma once

#include "Penumbra/Core/Base.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace Penumbra {

	// One argument of an automation command, parsed from CommandProcessor::CommandInfo::Arguments.
	// Syntax: "path, width?, id|name, parent|parentName (null to unparent)".
	struct CommandArgument
	{
		std::string Name;
		bool Required = false;					// plain "name": not "name?" and not part of an "a|b" choice
		std::vector<std::string> Alternatives;	// the other names of an "a|b" choice (give one of them)
		std::string Note;						// parenthesized remark, e.g. "null to unparent"
	};

	std::vector<CommandArgument> ParseCommandArguments(std::string_view arguments);

	// JSON Schema of a command's arguments object (used for MCP tool input schemas). Argument
	// types come from a table of known argument names, so commands keep their one-line
	// argument strings; arguments missing from the table are accepted with any type.
	nlohmann::json BuildCommandInputSchema(std::string_view arguments);

	// True if the argument name has a typed schema. Tests require every command argument to be known.
	bool IsKnownCommandArgument(std::string_view name);

}
