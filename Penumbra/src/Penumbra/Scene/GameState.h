#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Scene/Components.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace Penumbra {

	// Values a game keeps across scene loads: the chosen difficulty, a running score, what the
	// player carries into the next mission. The host hands the state of the scene that asked for
	// a load to the scene it loads (a fresh play session starts empty). Scripts read and write it
	// with game.get / game.set; PenumbraRuntime --set seeds it.
	using GameState = std::unordered_map<std::string, ScriptValue>;

	// Parses "difficulty=Hard,lives=3,cheats=true" into values: numbers, true/false, anything
	// else a string. Returns false (and leaves out unchanged) on a malformed pair.
	bool ParseGameValues(std::string_view text, GameState& out, std::string& error);

}
