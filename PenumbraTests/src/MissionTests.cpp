#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/GameState.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

using namespace Penumbra;

// The mission loop on the demo's Stealth level with the real scripts: the menu's choice of
// difficulty, loot, pickpocketing, objectives, the gate and the end-of-mission summary.

namespace {

	struct Demo
	{
		Scene SceneInstance;

		explicit Demo(const std::string& scenePath, const std::string& difficulty = "")
		{
			std::string error;
			REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
			SceneInstance.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(SceneInstance, Project::GetAssetDirectory() / scenePath, error), error);
			if (!difficulty.empty())
				SceneInstance.GetGameState()["difficulty"] = difficulty;
		}

		~Demo()
		{
			SceneInstance.OnRuntimeStop();
			Project::Unload();
		}

		// The Stealth level, quiet: no weather, no kitchen noises, the thief unseen at position facing yaw.
		void PrepareManor(const glm::vec3& playerPosition, float playerYaw, const std::string& guardBWaypoints = "")
		{
			auto& world = Entity("World").GetComponent<ScriptComponent>().Properties;
			world["storms"] = false;
			world["dogs"] = false;
			Entity("KitchenZone").GetComponent<ScriptComponent>().Properties["chance_scale"] = 0.0;
			if (!guardBWaypoints.empty())
				Entity("GuardB").GetComponent<ScriptComponent>().Properties["waypoints"] = guardBWaypoints;
			Penumbra::Entity player = Entity("Player");
			player.Transform().Translation = playerPosition;
			player.Transform().SetEulerDegrees({ 0.0f, playerYaw, 0.0f });
			player.GetComponent<PerceptionTargetComponent>().SilhouettePoints.clear();
		}

		Penumbra::Entity Entity(const std::string& name)
		{
			Penumbra::Entity entity = SceneInstance.FindEntityByName(name);
			REQUIRE_MESSAGE(entity, name);
			return entity;
		}

		std::string Lua(const std::string& code)
		{
			std::string output;
			REQUIRE_MESSAGE(SceneInstance.GetScriptEngine()->Execute(code, output), output);
			return output;
		}

		// Evaluates a Lua condition with M = the mission module.
		bool Holds(const std::string& condition)
		{
			return Lua("local M = require('Scripts/Mission/Mission'); return " + condition) == "true";
		}

		void Run(float seconds) { Test::Simulate(SceneInstance, seconds); }
	};

}

TEST_SUITE("Mission")
{
	TEST_CASE("Game values parse from the command line and outlive a scene load")
	{
		GameState values;
		std::string error;
		REQUIRE(ParseGameValues("difficulty=Hard, lives=3,cheats=true,name=", values, error));
		CHECK(std::get<std::string>(values["difficulty"]) == "Hard");
		CHECK(std::get<double>(values["lives"]) == 3.0);
		CHECK(std::get<bool>(values["cheats"]) == true);
		CHECK(std::get<std::string>(values["name"]).empty());
		CHECK_FALSE(ParseGameValues("oops", values, error));
		CHECK_FALSE(error.empty());
		CHECK(values.size() == 4); // unchanged by the bad one

		// What the hosts do on scene.load: the next scene gets the state of the one that asked.
		Demo menu("Scenes/Menu.pscene");
		menu.SceneInstance.OnRuntimeStart();
		menu.Lua("game.set('difficulty', 'Expert'); game.set('score', 40)");
		Scene next;
		next.SetGameState(menu.SceneInstance.GetGameStateRef());
		CHECK(std::get<std::string>(next.GetGameState()["difficulty"]) == "Expert");
		CHECK(std::get<double>(next.GetGameState()["score"]) == 40.0);
		std::string output;
		CHECK_FALSE(menu.SceneInstance.GetScriptEngine()->Execute("game.set('bad', {})", output));
	}

	TEST_CASE("The menu starts the mission at the chosen difficulty")
	{
		Demo menu("Scenes/Menu.pscene");
		menu.SceneInstance.OnRuntimeStart();
		menu.Run(0.1f);
		CHECK(menu.Lua("return scene.find('Menu'):script():difficulty()") == "\"Normal\"");
		CHECK(menu.Lua("return scene.find('Menu'):script():choose(3)") == "\"Expert\"");
		CHECK(menu.Lua("return scene.find('Menu'):script():begin()") == "\"Expert\"");
		REQUIRE(menu.SceneInstance.GetRequestedScene());
		CHECK(*menu.SceneInstance.GetRequestedScene() == "Scenes/Stealth.pscene");
		CHECK(std::get<std::string>(menu.SceneInstance.GetGameState()["difficulty"]) == "Expert");
		CHECK(menu.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Loot is counted; the gate lets the thief out only once the objectives are done; the summary follows")
	{
		Demo demo("Scenes/Stealth.pscene"); // no difficulty chosen: Normal
		demo.PrepareManor({ 10.0f, 0.05f, -11.5f }, 180.0f);
		demo.SceneInstance.OnRuntimeStart();
		demo.Run(0.1f);
		CHECK(demo.Holds("M.current.difficulty == 'Normal' and #M.current.objectives == 2"));
		CHECK(demo.Holds("M.current.total_items == 11"));

		CHECK(demo.Lua("return scene.find('Gate'):script():reach()") == "false");
		CHECK(demo.Holds("M.current.state == 'running'"));

		// Taken as E would (Player:use on what is under the crosshair calls on_use).
		demo.Lua("scene.find('GoldGoblet'):script():on_use(scene.find('Player'))");
		demo.Lua("local box = scene.find('JewelBox'):script(); box:on_use(scene.find('Player')); "
				 "box:on_use(scene.find('Player'))"); // the second time it is gone already: nothing more
		demo.Run(0.1f);
		CHECK(demo.Holds("M.current.loot == 350 and M.current.loot_items == 2"));
		CHECK_FALSE(demo.SceneInstance.FindEntityByName("GoldGoblet"));

		CHECK(demo.Lua("return scene.find('Gate'):script():reach()") == "true");
		CHECK(demo.Holds("M.current.state == 'complete' and M.over()"));
		const std::string summary = demo.Lua("local M = require('Scripts/Mission/Mission'); return table.concat(M.summary(M.current), '|')");
		CHECK(summary.find("Loot            350  (2 of 11 pieces)") != std::string::npos);
		CHECK(summary.find("The Ashcombe Manor  -  Normal") != std::string::npos);
		CHECK(demo.Lua("return scene.find('Mission'):script():continue()") == "\"menu\"");
		CHECK(demo.SceneInstance.GetRequestedScene().value_or("") == "Scenes/Menu.pscene");
		CHECK(demo.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Pickpocketing: purse and key from behind a calm guard; a guard on alert feels the hand")
	{
		// The captain (GuardB) waits at WP_B1 (14, 12) facing +Z; the thief stands behind him.
		Demo demo("Scenes/Stealth.pscene", "Hard");
		demo.PrepareManor({ 14.0f, 0.05f, 11.1f }, 180.0f, "WP_B1");
		demo.SceneInstance.OnRuntimeStart();
		demo.Run(1.0f);
		REQUIRE(demo.Entity("GuardB").GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Unaware);
		CHECK(demo.Holds("#M.current.objectives == 4")); // loot 500, the key, spare the servants, escape
		CHECK(demo.Lua("return scene.find('Player'):script():interaction_prompt()") == "\"[E] pick the purse (100)\"");

		// On alert he feels it and turns on the thief.
		demo.Lua("ai.notify(scene.find('GuardB'), vec3(20, 0, 20), 7.5, 'Noise')");
		CHECK(demo.Lua("return scene.find('Player'):script():use()") == "\"noticed\"");
		CHECK(demo.Entity("GuardB").GetComponent<PerceptionComponent>().RuntimeStimulusCause == "Theft");
		CHECK(demo.Entity("GuardB").GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Alarmed);
		CHECK(demo.Holds("M.current.loot == 0"));

		// Calm again: purse, then key.
		demo.Lua("ai.calm(scene.find('GuardB'))");
		CHECK(demo.Lua("return scene.find('Player'):script():use()") == "\"pickpocket\"");
		CHECK(demo.Lua("return scene.find('Player'):script():interaction_prompt()") == "\"[E] pick the cellar key\"");
		CHECK(demo.Lua("return scene.find('Player'):script():use()") == "\"pickpocket\"");
		demo.Run(0.1f);
		CHECK(demo.Holds("M.current.loot == 100 and M.current.stats.pickpocketed == 2 and M.current.items.CellarKey"));
		CHECK(demo.Holds("M.status(M.current, M.current.objectives[2]) == 'done'"));
		CHECK(demo.Entity("GuardB").GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Unaware);
		CHECK(demo.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Kills and knockouts are counted; Hard forbids killing the servants, Expert anyone")
	{
		{
			Demo demo("Scenes/Stealth.pscene", "Hard");
			demo.PrepareManor({ -17.0f, 0.05f, -17.0f }, 0.0f);
			demo.SceneInstance.OnRuntimeStart();
			demo.Run(0.1f);
			demo.Lua("ai.knock_out(scene.find('GuardA'))");
			demo.Run(0.1f);
			CHECK(demo.Holds("M.current.stats.knockouts == 1 and M.current.stats.kills == 0"));
			demo.Lua("ai.damage(scene.find('GuardA'), 100)"); // finished off: a kill, no longer a knockout
			demo.Run(0.1f);
			CHECK(demo.Holds("M.current.stats.knockouts == 0 and M.current.stats.kills == 1"));
			CHECK(demo.Holds("M.current.state == 'running'")); // a guard is not a servant
			demo.Lua("ai.damage(scene.find('Servant'), 100)");
			demo.Run(0.1f);
			CHECK(demo.Holds("M.current.state == 'failed' and M.current.reason == 'You killed Servant.'"));
			CHECK(demo.Holds("M.status(M.current, M.current.objectives[3]) == 'failed'"));
			CHECK(demo.Lua("return scene.find('Mission'):script():continue()") == "\"retry\"");
			CHECK(demo.SceneInstance.GetRequestedScene().value_or("") == "Scenes/Stealth.pscene");
			CHECK(demo.SceneInstance.GetScriptEngine()->GetErrors().empty());
		}
		{
			Demo demo("Scenes/Stealth.pscene", "Expert");
			demo.PrepareManor({ -17.0f, 0.05f, -17.0f }, 0.0f);
			demo.SceneInstance.OnRuntimeStart();
			demo.Run(0.1f);
			CHECK(demo.Holds("M.current.objectives[1].target == 700"));
			demo.Lua("ai.damage(scene.find('GuardA'), 100)");
			demo.Run(0.1f);
			CHECK(demo.Holds("M.current.state == 'failed' and M.current.reason == 'You killed GuardA.'"));
			// Over: the thief stands still and guards no longer catch him.
			CHECK(demo.Holds("M.over()"));
		}
	}
}
