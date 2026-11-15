#include "pnpch.h"
#include "Penumbra/Debug/NoiseProbe.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Scene/Entity.h"
#include "Penumbra/Scene/Scene.h"

namespace Penumbra::NoiseProbe {

	std::vector<NoiseHearing> Emit(Scene& scene, const glm::vec3& position, float loudness)
	{
		std::vector<NoiseHearing> heard;
		PerceptionSystem* perception = scene.GetPerceptionSystem();
		if (!perception)
			return heard;
		perception->EmitNoise(position, loudness, UUID::Invalid, "Debug");

		float masking = 0.0f;
		if (Entity environment = scene.GetEnvironmentEntity())
			masking = std::max(environment.GetComponent<EnvironmentComponent>().AmbientNoise, 0.0f);
		for (auto [handle, component] : scene.GetRegistry().view<PerceptionComponent>().each())
		{
			Entity npc(handle, &scene);
			if (!PerceptionSystem::IsAwake(npc))
				continue;
			const float level = perception->HearingLevel(npc, position, loudness) - masking;
			if (level <= 0.0f)
				continue;
			NoiseHearing entry;
			entry.Npc = npc.GetUUID();
			entry.Name = npc.GetName();
			entry.Level = level;
			entry.State = PerceptionModel::StateFromStrength(level, component.InvestigateLevel, component.SearchLevel, component.AlarmLevel);
			heard.push_back(std::move(entry));
		}
		std::sort(heard.begin(), heard.end(), [](const NoiseHearing& a, const NoiseHearing& b) { return a.Level > b.Level; });
		return heard;
	}

	const std::vector<NoiseReference>& References()
	{
		static const std::vector<NoiseReference> references = {
			{ 1.0f, "sneaking on stone, a lock pin set, a moss arrow" },
			{ 2.0f, "a light switch, a key turning in a lock" },
			{ 2.5f, "a body put down" },
			{ 3.0f, "walking, a twig cracks, a door creaks open, a blackjack, a bowstring" },
			{ 4.0f, "a door falling shut" },
			{ 5.0f, "running on stone, a knife dropped on stone" },
			{ 6.0f, "a pot banged on the stove" },
			{ 7.0f, "a table knocked over (4 m/s), running on a metal grate" },
			{ 8.0f, "a shout (heard by other guards)" },
			{ 8.5f, "the copper jug off the table onto wood" },
			{ 9.0f, "a metal bucket dropped on cobbles" },
			{ 10.0f, "the alarm bell, an explosion" },
		};
		return references;
	}

}
