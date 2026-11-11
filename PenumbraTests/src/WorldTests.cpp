#include "TestHelpers.h"

#include <set>

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

using namespace Penumbra;

// Area and world events (Scripts/World/*.lua) end to end on the AILab demo level.

namespace {

	struct WorldLab
	{
		Scene SceneInstance;
		Entity Guard;
		Entity World;

		// stormy: start with a storm; kitchen: multiplier on the kitchen's event odds.
		WorldLab(bool stormy, float kitchen, const std::string& waypoints = "", uint64_t seed = Scene::DefaultRandomSeed)
		{
			std::string error;
			REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
			SceneInstance.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(SceneInstance, Project::GetAssetDirectory() / "Scenes/AILab.pscene", error), error);
			SceneInstance.SetRandomSeed(seed);

			// The player waits unseen in the west room, within reach of the kitchen zone.
			Entity player = SceneInstance.FindEntityByName("Player");
			player.Transform().Translation = { -6.0f, 0.05f, -7.0f };
			player.GetComponent<PerceptionTargetComponent>().SilhouettePoints.clear();

			World = SceneInstance.FindEntityByName("World");
			REQUIRE(World);
			auto& world = World.GetComponent<ScriptComponent>().Properties;
			world["force_storm"] = stormy;
			world["storms"] = stormy;
			world["dogs"] = false;
			SceneInstance.FindEntityByName("KitchenZone").GetComponent<ScriptComponent>().Properties["chance_scale"] = static_cast<double>(kitchen);

			Guard = SceneInstance.FindEntityByName("Guard");
			REQUIRE(Guard);
			if (!waypoints.empty())
				Guard.GetComponent<ScriptComponent>().Properties["waypoints"] = waypoints;
			SceneInstance.OnRuntimeStart();
		}

		~WorldLab()
		{
			SceneInstance.OnRuntimeStop();
			Project::Unload();
		}

		std::string Lua(const std::string& code)
		{
			std::string output;
			REQUIRE_MESSAGE(SceneInstance.GetScriptEngine()->Execute(code, output), output);
			return output;
		}

		float AmbientNoise() { return SceneInstance.GetEnvironmentEntity().GetComponent<EnvironmentComponent>().AmbientNoise; }
		PerceptionComponent& Perception() { return Guard.GetComponent<PerceptionComponent>(); }
	};

}

TEST_SUITE("World")
{
	TEST_CASE("A storm rains, flashes, masks footsteps with thunder and dazzles the guard")
	{
		WorldLab lab(true, 0.0f);
		Entity lightning = lab.SceneInstance.FindEntityByName("Lightning");
		REQUIRE(lightning);

		const float dt = 1.0f / 60.0f;
		float maxNoise = 0.0f, baselineAdaptation = lab.Perception().RuntimeAdaptation, maxAdaptation = 0.0f;
		bool flashed = false, maskedStep = false;
		for (int frame = 0; frame < 60 * 45; ++frame)
		{
			lab.SceneInstance.OnUpdateRuntime(Timestep(dt));
			maxNoise = std::max(maxNoise, lab.AmbientNoise());
			flashed |= lightning.GetComponent<DirectionalLightComponent>().Enabled;
			if (frame == 60)
				baselineAdaptation = lab.Perception().RuntimeAdaptation;
			if (frame > 60)
				maxAdaptation = std::max(maxAdaptation, lab.Perception().RuntimeAdaptation);

			// While it thunders, a running footstep right next to the guard goes unheard.
			if (!maskedStep && lab.AmbientNoise() >= 7.0f && lab.Perception().RuntimeState == AwarenessState::Unaware)
			{
				const glm::vec3 near = lab.SceneInstance.GetWorldPosition(lab.Guard) + glm::vec3(2.0f, 0.0f, 0.0f);
				lab.SceneInstance.GetPerceptionSystem()->EmitNoise(near, 5.0f, UUID::Invalid, "Footstep.Stone");
				lab.SceneInstance.OnUpdateRuntime(Timestep(dt));
				CHECK(lab.Perception().RuntimeStimulusCause != "Footstep.Stone");
				maskedStep = true;
			}
		}
		CHECK(lab.Lua("return scene.find('World'):script().strikes >= 1") == "true");
		CHECK(flashed);
		CHECK(maxNoise >= 7.0f); // thunder
		CHECK(lab.AmbientNoise() >= 2.0f); // still raining
		CHECK(maskedStep);
		// The flash lit up the yard: the guard's eyes adapted upwards (he is dazzled for a moment).
		MESSAGE("adaptation " << baselineAdaptation << " -> max " << maxAdaptation);
		CHECK(maxAdaptation > baselineAdaptation * 1.5f);
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Thunder talks a guard out of checking a quiet noise, not out of a loud one")
	{
		WorldLab lab(false, 0.0f);
		Test::Simulate(lab.SceneInstance, 1.0f);
		PerceptionSystem& perception = *lab.SceneInstance.GetPerceptionSystem();
		const glm::vec3 near = lab.SceneInstance.GetWorldPosition(lab.Guard) + glm::vec3(1.0f, 0.0f, 0.0f);

		perception.EmitNoise(near, 4.5f, UUID::Invalid, "Crack"); // heard at ~3.6: he wonders
		Test::Simulate(lab.SceneInstance, 0.1f);
		REQUIRE(lab.Perception().RuntimeState != AwarenessState::Unaware);
		lab.Lua("scene.find('World'):script():thunder()"); // strength 6 +- 2 always beats 3.6
		CHECK(lab.Perception().RuntimeState == AwarenessState::Unaware);
		CHECK(lab.Lua("return scene.find('World'):script().excused") == "1");

		Test::Simulate(lab.SceneInstance, 3.5f); // let the rumble end so he can hear again
		const glm::vec3 nearNow = lab.SceneInstance.GetWorldPosition(lab.Guard) + glm::vec3(1.0f, 0.0f, 0.0f);
		perception.EmitNoise(nearNow, 9.0f, UUID::Invalid, "Bucket"); // heard at ~8.1: too loud to be the weather
		Test::Simulate(lab.SceneInstance, 0.1f);
		REQUIRE(lab.Perception().RuntimeState == AwarenessState::Searching);
		lab.Lua("scene.find('World'):script():thunder()"); // 6 + 2 at best < 8.1
		CHECK(lab.Perception().RuntimeState == AwarenessState::Searching);
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("The cook's kitchen noises bring the guard over like the player's would")
	{
		std::string fired;
		{
			// His route ends by the kitchen door, about 3 m from the cook at the table.
			WorldLab lab(false, 30.0f, "WP_3");
			std::set<std::string> causes;
			float closestToKitchen = 1e9f;
			const glm::vec3 kitchen = lab.SceneInstance.GetWorldPosition(lab.SceneInstance.FindEntityByName("Cook"));
			for (int frame = 0; frame < 60 * 90; ++frame)
			{
				lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
				const std::string& cause = lab.Perception().RuntimeStimulusCause;
				if (!cause.empty())
					causes.insert(cause);
				if (cause == "Knife" || cause == "Pot" || cause == "Crack")
					closestToKitchen = std::min(closestToKitchen, glm::distance(lab.SceneInstance.GetWorldPosition(lab.Guard), kitchen));
			}
			fired = lab.Lua("return scene.find('KitchenZone'):script().fired");
			CHECK(std::stoi(fired) >= 3);
			const bool heardKitchen = causes.contains("Knife") || causes.contains("Pot") || causes.contains("Crack");
			CHECK(heardKitchen);
			CHECK(closestToKitchen < 2.0f); // he walked over to see
			MESSAGE("kitchen events " << fired << ", closest approach " << closestToKitchen << " m");
			CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
		}
		// Same seed, same evening: the zone's events are reproducible.
		WorldLab again(false, 30.0f, "WP_3");
		Test::Simulate(again.SceneInstance, 90.0f);
		CHECK(again.Lua("return scene.find('KitchenZone'):script().fired") == fired);
	}
}
