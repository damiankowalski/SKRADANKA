#pragma once

#include "Penumbra/Core/Timestep.h"
#include "Penumbra/Scene/Entity.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Penumbra {

	// Data-driven "Act/React" system in the spirit of classic immersive sims: sources emit
	// named stims (Fire, Water, Kinetic, ...) and receivers react according to their rules
	// (extinguish, ignite, explode, spawn, ...). Reactions are applied at the end of the
	// update so chains (an explosion igniting the next keg) unfold over consecutive ticks.
	class StimSystem
	{
	public:
		explicit StimSystem(Scene& scene);

		void Update(Timestep ts);

		// Called by the scene for every new physics contact.
		void OnContact(UUID a, UUID b, const glm::vec3& point);

		// Delivers a stim to every receiver within radius (falloff towards the edge).
		void Emit(const std::string& stim, const glm::vec3& position, float radius, float intensity, UUID source = UUID::Invalid,
			bool requireLineOfSight = false, UUID exclude = UUID::Invalid);
		// Delivers a stim directly to one entity.
		void Deliver(Entity receiver, const std::string& stim, float intensity, UUID source = UUID::Invalid);

		// Utility used by the DisableLights / EnableLights actions and scripts.
		static void SetLightsEnabled(Entity entity, bool enabled);

		uint64_t GetDeliveredCount() const { return m_DeliveredCount; }

	private:
		struct PendingAction
		{
			UUID Target = UUID::Invalid;
			StimAction Action;
			glm::vec3 Position{ 0.0f };
		};

		void HandleContactSource(Entity source, Entity other, const glm::vec3& point);
		// The object that reacts to a hit on this entity: itself or its nearest ancestor with
		// a StimResponse or Script component.
		Entity ResolveReceiver(Entity hit);
		float DistanceToObject(Entity receiver, const glm::vec3& point);
		void Execute(const PendingAction& pending);

	private:
		Scene& m_Scene;
		std::vector<PendingAction> m_PendingActions;
		uint64_t m_DeliveredCount = 0;
	};

}
