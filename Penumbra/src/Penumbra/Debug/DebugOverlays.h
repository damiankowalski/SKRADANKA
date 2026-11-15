#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Scene/Components.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <vector>

namespace Penumbra {

	class DebugDraw;
	class Scene;

	// One ray of an NPC's view fan (DebugOverlays::ComputeVisionFan).
	struct VisionFanRay
	{
		glm::vec3 End{ 0.0f }; // where the ray stops: the first wall or the end of the view distance
		bool Blocked = false;  // stopped by something solid before ViewDistance
		bool Focus = false;    // inside the focus cone (full contrast); otherwise a peripheral band
	};

	// How one silhouette point of a target looks to an observer (lines drawn by DrawVision).
	enum class SightLine
	{
		OutOfView, // beyond the cone or the view distance
		Occluded,  // in the cone, behind something solid
		Peripheral,
		Focus,
	};

	// A noise kept on screen for a while after it was made.
	struct DebugNoise
	{
		glm::vec3 Position{ 0.0f };
		float Loudness = 0.0f;
		float Time = 0.0f; // scene time it was made
	};

	// World-space debug drawing shared by the editor and the runtime's debug panel: what NPCs see
	// (view fans cut by walls, the thief's silhouette points in their eyes, suspicion), what they
	// hear (noise rings), the navmesh with agent paths, colliders.
	namespace DebugOverlays {

		glm::vec4 StateColor(AwarenessState state);

		// Rays every stepDegrees across the observer's whole field of view, from the eye, horizontal
		// around its forward direction, cut short by solid bodies (walls, closed doors, crates).
		// Characters and the observer's own body do not stop them. Uncut without running physics (editing).
		std::vector<VisionFanRay> ComputeVisionFan(Scene& scene, Entity observer, float stepDegrees = 5.0f);
		// Each silhouette point of the target as the observer's eye finds it (the geometry only:
		// light and contrast decide the rest, see PerceptionSystem::ComputeVisibility).
		std::vector<SightLine> ComputeSightLines(Scene& scene, Entity observer, Entity target);

		// Every awake NPC with Perception: its view fan in the colour of its state (focus bright,
		// peripheral bands dim), a suspicion bar above the head, a line to what it is reacting to,
		// and lines to each intruder's silhouette points it has in view.
		// onlyObserver: draw this NPC's fan alone (the inspector's "only his view").
		void DrawVision(Scene& scene, DebugDraw& debug, UUID onlyObserver = UUID::Invalid);
		// Rings around recent noises: the outer one is how far it carries at all, the inner one where
		// a guard would still hear it at InvestigateLevel 4 (walks over to look). Fade over lifetime seconds.
		void DrawNoises(DebugDraw& debug, const std::vector<DebugNoise>& noises, float now, float lifetime);
		void DrawNavigation(Scene& scene, DebugDraw& debug);
		void DrawColliders(Scene& scene, DebugDraw& debug);
		void DrawSmoke(Scene& scene, DebugDraw& debug);

	}

}
