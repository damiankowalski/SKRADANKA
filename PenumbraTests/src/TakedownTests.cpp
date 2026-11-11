#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

using namespace Penumbra;

// Blackjack, carrying bodies, guards finding them, the servant's bell and sleeping gas, end to
// end on the AILab demo level with the real scripts (Player.lua, Guard.lua, Servant.lua).

namespace {

	struct Lab
	{
		Scene SceneInstance;

		// guardWaypoints / guard2Waypoints override the patrols ("" keeps them).
		Lab(const std::string& guardWaypoints, const std::string& guard2Waypoints, const glm::vec3& playerPosition, float playerYaw)
		{
			std::string error;
			REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
			SceneInstance.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(SceneInstance, Project::GetAssetDirectory() / "Scenes/AILab.pscene", error), error);
			if (!guardWaypoints.empty())
				Entity("Guard").GetComponent<ScriptComponent>().Properties["waypoints"] = guardWaypoints;
			if (!guard2Waypoints.empty())
				Entity("Guard2").GetComponent<ScriptComponent>().Properties["waypoints"] = guard2Waypoints;
			Penumbra::Entity player = Entity("Player");
			player.Transform().Translation = playerPosition;
			player.Transform().SetEulerDegrees({ 0.0f, playerYaw, 0.0f });
			// Unseen: these tests are about what the player does, not about being spotted.
			player.GetComponent<PerceptionTargetComponent>().SilhouettePoints.clear();
			// No weather or kitchen surprises.
			Entity("World").GetComponent<ScriptComponent>().Properties["storms"] = false;
			Entity("World").GetComponent<ScriptComponent>().Properties["dogs"] = false;
			Entity("KitchenZone").GetComponent<ScriptComponent>().Properties["chance_scale"] = 0.0;
			SceneInstance.OnRuntimeStart();
		}

		~Lab()
		{
			SceneInstance.OnRuntimeStop();
			Project::Unload();
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

		PerceptionComponent& Perception(const std::string& name) { return Entity(name).GetComponent<PerceptionComponent>(); }
		std::string Vital(const std::string& name) { return Lua("return ai.vital_state(scene.find('" + name + "'))"); }
		void Run(float seconds) { Test::Simulate(SceneInstance, seconds); }
	};

}

TEST_SUITE("World")
{
	TEST_CASE("Blackjack from behind knocks a guard out; the body is carried off and put down in the dark")
	{
		// The guard waits at WP_1 (-10, 8) facing -X; the player creeps up behind him.
		Lab lab("WP_1", "", { -8.9f, 0.05f, 8.0f }, 90.0f);
		lab.Run(1.0f);
		REQUIRE(lab.Perception("Guard").RuntimeState == AwarenessState::Unaware);
		CHECK(lab.Lua("return scene.find('Player'):script():interaction_prompt()") == "\"[F] knock out\"");
		CHECK(lab.Lua("return scene.find('Player'):script():blackjack()") == "\"knockout\"");
		CHECK(lab.Vital("Guard") == "\"Unconscious\"");
		lab.Run(2.0f);
		// He fell (Death_A) and holds the pose on the floor.
		CHECK(lab.Lua("for _, c in ipairs(scene.find('Guard'):children()) do if c:get_name() == 'Model' then return c:get('Animator').Clip end end") ==
			"\"Death_A_Pose\"");

		// Pick him up: he goes onto the player's shoulders and is no body anyone can see.
		lab.Run(0.9f); // the swing cooldown
		CHECK(lab.Lua("return scene.find('Player'):script():interaction_prompt()") == "\"[E] pick up the body\"");
		CHECK(lab.Lua("return scene.find('Player'):script():use()") == "\"carry\"");
		CHECK(lab.Entity("Guard").GetParent() == lab.Entity("Player"));
		CHECK_FALSE(lab.Entity("Guard").HasComponent<PerceptionTargetComponent>());
		CHECK(lab.Lua("return scene.find('Player'):script():blackjack() == nil") == "true"); // hands full

		// Carry him into the dark corner and put him down there.
		lab.Lua("scene.find('Player'):set_position(vec3(-13.5, 0.05, 10.5))");
		lab.Run(0.5f);
		CHECK(lab.Lua("return scene.find('Player'):script():use()") == "\"drop\"");
		Entity guard = lab.Entity("Guard");
		CHECK_FALSE(guard.GetParent());
		CHECK(guard.GetComponent<PerceptionTargetComponent>().Kind == TargetKind::Body);
		CHECK(guard.GetComponent<PerceptionTargetComponent>().SilhouettePoints.size() == 7);
		CHECK(glm::distance(lab.SceneInstance.GetWorldPosition(guard), glm::vec3(-13.5f, 0.0f, 10.5f)) < 1.5f);
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("A guard who sees the attack coming blocks it and raises the alarm")
	{
		// The player stands in front of the waiting guard, facing him.
		Lab lab("WP_1", "", { -11.1f, 0.05f, 8.0f }, -90.0f);
		lab.Run(1.0f);
		CHECK(lab.Lua("return scene.find('Player'):script():blackjack()") == "\"blocked\"");
		CHECK(lab.Vital("Guard") == "\"Awake\"");
		lab.Run(0.1f);
		CHECK(lab.Perception("Guard").RuntimeState == AwarenessState::Alarmed);
		CHECK(lab.Lua("return scene.find('Guard'):script().bark_text ~= nil") == "true");
	}

	TEST_CASE("A body left in torchlight is found by the next guard, who searches and calls out")
	{
		// Guard 1 is knocked out next to the torch by the east door; guard 2 walks by.
		Lab lab("WP_1", "WP_6,WP_7", { 30.0f, 0.05f, 30.0f }, 0.0f);
		lab.Run(0.2f);
		REQUIRE(lab.Lua("return ai.knock_out(scene.find('Guard'))") == "true");
		lab.Lua("scene.find('Guard'):set_position(vec3(4, 0.05, -0.6))"); // no physics character any more: stays there
		bool found = false, barked = false;
		for (int i = 0; i < 60 * 40 && !found; ++i)
		{
			lab.Run(1.0f / 60.0f);
			found = lab.Perception("Guard2").RuntimeStimulusCause == "Body";
		}
		REQUIRE(found);
		const auto& perception = lab.Perception("Guard2");
		CHECK(perception.RuntimeState == AwarenessState::Searching);
		lab.Run(2.0f / 60.0f); // his script reacts on the next frame
		barked = lab.Lua("return scene.find('Guard2'):script().bark_text ~= nil") == "true";
		CHECK(barked);
		lab.Run(5.0f);
		CHECK(glm::distance(lab.SceneInstance.GetWorldPosition(lab.Entity("Guard2")), glm::vec3(4.0f, 0.0f, -0.6f)) < 4.0f);
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("A frightened servant runs to the bell and every guard comes; sleeping gas knocks people out")
	{
		Lab lab("WP_1", "WP_5", { 30.0f, 0.05f, 30.0f }, 0.0f);
		lab.Run(0.5f);
		const glm::vec3 trouble(-6.0f, 0.0f, -7.0f);
		REQUIRE(lab.Lua("return ai.notify(scene.find('Servant'), vec3(-6, 0, -7), 6, 'Crack')") == "true");
		int rang = 0;
		for (int i = 0; i < 60 * 30 && rang == 0; ++i)
		{
			lab.Run(1.0f / 60.0f);
			rang = std::stoi(lab.Lua("return scene.find('Servant'):script().rang"));
		}
		REQUIRE(rang == 1);
		for (const char* name : { "Guard", "Guard2" })
		{
			CAPTURE(name);
			const auto& perception = lab.Perception(name);
			CHECK(perception.RuntimeStimulusCause == "Bell");
			CHECK(static_cast<int>(perception.RuntimeState) >= static_cast<int>(AwarenessState::Searching));
			CHECK(Test::Near(perception.RuntimeLastStimulusPosition, trouble, 0.01f));
		}

		// A gas cloud by the bell: the servant falls asleep where he stands.
		lab.Lua("scene.instantiate('Prefabs/GasCloud.pprefab', scene.find('Servant'):get_position())");
		lab.Run(0.5f);
		CHECK(lab.Vital("Servant") == "\"Unconscious\"");
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Arrows from the demo prefabs: a miss on the metal grate is heard, two fire arrows kill a guard")
	{
		// The guard waits at WP_1 (-10, 8); the metal grate is at (-8, 6).
		Lab lab("WP_1", "WP_5", { 30.0f, 0.05f, 30.0f }, 0.0f);
		lab.Run(1.0f);
		lab.Lua("local a = scene.instantiate('Prefabs/WaterArrow.pprefab', vec3(-8, 3, 6)); a:look_at(vec3(-8, 0, 6))");
		lab.Run(1.0f / 60.0f);
		lab.Lua("for _, e in ipairs(scene.entities_with('StimSource')) do if e:get_name() == 'WaterArrow' then physics.set_velocity(e, vec3(0, -25, 0)) end end");
		lab.Run(0.5f);
		CHECK(lab.Perception("Guard").RuntimeStimulusCause == "Arrow.Metal");
		CHECK(lab.Perception("Guard").RuntimeState != AwarenessState::Unaware);

		// Two fire arrows straight at him from the east: hurt, then dead (a body).
		for (int shot = 0; shot < 2; ++shot)
		{
			lab.Lua("local a = scene.instantiate('Prefabs/FireArrow.pprefab', vec3(-6, 1.2, 8)); a:look_at(vec3(-10, 1.2, 8))");
			lab.Run(1.0f / 60.0f);
			lab.Lua("for _, e in ipairs(scene.entities_with('StimSource')) do if e:get_name() == 'FireArrow' then physics.set_velocity(e, vec3(-30, 0, 0)) end end");
			lab.Run(0.5f);
			if (shot == 0)
			{
				CHECK(lab.Vital("Guard") == "\"Awake\"");
				CHECK(lab.Perception("Guard").RuntimeStimulusCause == "Hit");
				CHECK(lab.Perception("Guard").RuntimeLastStimulusPosition.x > -4.0f); // back along the flight, east of him
			}
		}
		CHECK(lab.Vital("Guard") == "\"Dead\"");
		CHECK(lab.Entity("Guard").GetComponent<PerceptionTargetComponent>().Kind == TargetKind::Body);
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}
}

