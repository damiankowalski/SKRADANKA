#include "TestHelpers.h"

#include "Penumbra/Core/Input.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

using namespace Penumbra;

// The demo's test levels with the real scripts: the physics sandbox (Scenes/PhysicsLab.pscene: a
// table and a copper jug at the top of a ramp) and the cloister (Scenes/Cloister.pscene: a garden
// ringed by corridors, two guards, from the east wing to the west wing).

namespace {

	struct Level
	{
		Scene SceneInstance;

		explicit Level(const std::string& scenePath)
		{
			std::string error;
			REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
			SceneInstance.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(SceneInstance, Project::GetAssetDirectory() / scenePath, error), error);
			// The thief unseen: these tests are about the levels, not about being spotted.
			Entity("Player").GetComponent<PerceptionTargetComponent>().SilhouettePoints.clear();
			Input::Reset();
			SceneInstance.OnRuntimeStart();
		}

		~Level()
		{
			Input::Reset();
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

		glm::vec3 Position(const std::string& name) { return SceneInstance.GetWorldPosition(Entity(name)); }
		void Run(float seconds) { Test::Simulate(SceneInstance, seconds); }
	};

}

TEST_SUITE("World")
{
	TEST_CASE("Physics lab: an arrow knocks the copper jug off the table, and the guard hears it land")
	{
		Level lab("Scenes/PhysicsLab.pscene");
		lab.Run(0.5f);
		const glm::vec3 jug = lab.Position("CopperJug");
		REQUIRE(jug.y > 2.9f); // standing on the table at the top of the ramp

		// A water arrow from the foot of the ramp, level with the jug.
		lab.Lua("local a = scene.instantiate('Prefabs/WaterArrow.pprefab', vec3(0.3, 3.1, -1)); a:look_at(vec3(0.3, 3.1, -6))");
		lab.Run(1.0f / 60.0f);
		lab.Lua("for _, e in ipairs(scene.entities_with('StimSource')) do if e:get_name() == 'WaterArrow' then physics.set_velocity(e, vec3(0, 0, -32)) end end");
		lab.Run(2.0f);
		CHECK(lab.Position("CopperJug").y < 2.5f); // fell off the table onto the platform
		const PerceptionComponent& guard = lab.Entity("Guard").GetComponent<PerceptionComponent>();
		CHECK(guard.RuntimeState != AwarenessState::Unaware);
		CHECK(guard.RuntimeStimulusCause.find("Metal") != std::string::npos);
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Physics lab: the thief pushes the table down the ramp")
	{
		Level lab("Scenes/PhysicsLab.pscene");
		// Behind the table on the platform, facing south down the ramp.
		lab.Lua("local p = scene.find('Player'); p:set_position(vec3(0, 2.1, -7.2)); p:script().yaw = 180");
		lab.Run(0.3f);
		const glm::vec3 before = lab.Position("Table");
		Input::SetKeyState(Key::W, true);
		lab.Run(5.0f);
		Input::SetKeyState(Key::W, false);
		lab.Run(2.0f);
		const glm::vec3 after = lab.Position("Table");
		CHECK(after.z > before.z + 1.0f); // pushed south, over the edge
		CHECK(after.y < before.y - 0.5f); // and down the ramp
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Cloister: the way from the east wing to the west leads around the ring or through the garden doors")
	{
		Level cloister("Scenes/Cloister.pscene");
		cloister.Run(0.5f);
		NavigationSystem& navigation = *cloister.SceneInstance.GetNavigationSystem();
		std::optional<NavPath> path = navigation.FindPath({ 24.5f, 0.0f, 0.0f }, { -24.5f, 0.0f, 0.0f });
		REQUIRE(path.has_value());
		CHECK(path->Complete);
		// Straight through the garden: in by the east doors, out by the west ones.
		CHECK(path->Length < 52.0f);
		std::optional<NavPath> intoGarden = navigation.FindPath({ 9.6f, 0.0f, 0.0f }, { 0.0f, 0.0f, 3.0f });
		REQUIRE(intoGarden.has_value());
		CHECK(intoGarden->Complete);
		CHECK(intoGarden->Length < 12.0f);

		// Eight door leaves, two on each side of the garden, all closed.
		CHECK(cloister.Lua("local n = 0; for _, e in ipairs(scene.entities_with('Script')) do local s = e:script(); "
						   "if s and s.is_door and not s:is_open() then n = n + 1 end end; return n") == "8");
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Cloister: the guards walk the ring past the garden doors without opening them; the west wing ends the mission")
	{
		Level cloister("Scenes/Cloister.pscene");
		bool opened = false;
		bool moved = false;
		const glm::vec3 start = cloister.Position("GuardNorth");
		for (int step = 0; step < 50; ++step) // 25 s: both guards pass at least one doorway
		{
			cloister.Run(0.5f);
			opened = opened || cloister.Lua("for _, e in ipairs(scene.entities_with('Script')) do local s = e:script(); "
											"if s and s.is_door and s:is_open() then return true end end return false") == "true";
			moved = moved || glm::distance(cloister.Position("GuardNorth"), start) > 8.0f;
		}
		CHECK(moved);
		CHECK_FALSE(opened);

		// The thief walks into the west wing's end: the mission is won.
		cloister.Lua("scene.find('Player'):set_position(vec3(-24.5, 0.1, 0))");
		cloister.Run(0.5f);
		CHECK(cloister.Lua("return require('Scripts/Mission/Mission').current.state") == "\"complete\"");
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}
}
