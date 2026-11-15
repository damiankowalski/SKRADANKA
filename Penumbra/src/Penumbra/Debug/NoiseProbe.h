#pragma once

#include "Penumbra/Core/UUID.h"
#include "Penumbra/Scene/Components.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Penumbra {

	class Scene;

	// Who hears a test noise, and how they will take it.
	struct NoiseHearing
	{
		UUID Npc = UUID::Invalid;
		std::string Name;
		float Level = 0.0f;                           // loudness that reaches the NPC (0-10), after walls, doors, paths and ambient noise
		AwarenessState State = AwarenessState::Unaware; // the reaction a stimulus that strong starts (if nothing stronger is going on)
	};

	// A reference for a loudness on the 0-10 scale: what in the game is that loud.
	struct NoiseReference
	{
		float Loudness = 0.0f;
		const char* Example = "";
	};

	namespace NoiseProbe {

		// Makes a noise (cause "Debug", no source) that NPCs hear on the next perception update, the
		// way any noise is (PerceptionSystem::EmitNoise). Returns every awake NPC who will hear it,
		// loudest first. Empty when the scene is not running.
		std::vector<NoiseHearing> Emit(Scene& scene, const glm::vec3& position, float loudness);

		// The demo's sounds by loudness (docs/Perception.md, Scripts/AI/Noises.lua, Surface.ImpactLoudness).
		const std::vector<NoiseReference>& References();

	}

}
