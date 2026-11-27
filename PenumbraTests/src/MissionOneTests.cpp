#include "TestHelpers.h"

#include "Penumbra/Core/Input.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

#include <map>

using namespace Penumbra;

// The first mission's blockout (Scenes/Mission1.pscene, docs/research/MissionOne.md) with the real scripts: the route
// from the tavern through the catacombs and the tunnel to the manor, its two staircases, the patrols, and the mission's
// way out back into the tunnel.

namespace {

	struct MissionOne
	{
		Scene SceneInstance;

		explicit MissionOne(const std::string& difficulty = "")
		{
			std::string error;
			REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
			SceneInstance.SetAudioDeviceEnabled(false);
			const std::filesystem::path scene = Project::GetAssetDirectory() / "Scenes/Mission1.pscene";
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(SceneInstance, scene, error), error);
			if (!difficulty.empty())
				SceneInstance.GetGameState()["difficulty"] = difficulty;
			// The thief unseen: these tests are about the level, not about being spotted.
			Entity("Player").GetComponent<PerceptionTargetComponent>().SilhouettePoints.clear();
			Input::Reset();
			SceneInstance.OnRuntimeStart();
		}

		~MissionOne()
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

		bool Holds(const std::string& condition)
		{
			return Lua("local M = require('Scripts/Mission/Mission'); return " + condition) == "true";
		}

		glm::vec3 Position(const std::string& name) { return SceneInstance.GetWorldPosition(Entity(name)); }
		void Run(float seconds) { Test::Simulate(SceneInstance, seconds); }

		NavPath Path(const glm::vec3& from, const glm::vec3& to)
		{
			std::optional<NavPath> path = SceneInstance.GetNavigationSystem()->FindPath(from, to);
			REQUIRE(path.has_value());
			return *path;
		}
	};

	float Lowest(const NavPath& path)
	{
		float lowest = 1000.0f;
		for (const glm::vec3& point : path.Points)
			lowest = std::min(lowest, point.y);
		return lowest;
	}

	const glm::vec3 Tavern{ -104.0f, 0.0f, -11.0f };

}

TEST_SUITE("MissionOne")
{
	TEST_CASE("Mission 1: from the tavern the only way to the manor runs down through the catacombs and the collapsed tunnel")
	{
		MissionOne mission;
		mission.Run(0.2f);

		// Through the old town to the church and down its crypt stairs.
		const NavPath church = mission.Path(Tavern, { -20.0f, 0.0f, 0.0f });
		CHECK(church.Complete);
		CHECK(church.Length < 110.0f);
		const NavPath crypt = mission.Path(Tavern, { -8.0f, -4.0f, -4.0f });
		CHECK(crypt.Complete);

		// The vineyard lies just beyond the city wall, yet the way there goes under it: down to the catacombs' floor.
		const NavPath vineyard = mission.Path(Tavern, { 30.0f, 0.0f, 8.0f });
		CHECK(vineyard.Complete);
		CHECK(Lowest(vineyard) < -3.5f);
		const NavPath tunnel = mission.Path({ -8.0f, -4.0f, -4.0f }, { 25.0f, 0.0f, -4.0f });
		CHECK(tunnel.Complete);
		CHECK(tunnel.Length < 45.0f);

		// On to the library on the manor's first floor.
		const NavPath library = mission.Path(Tavern, { 58.0f, 4.0f, -6.0f });
		CHECK(library.Complete);
		CHECK(Lowest(library) < -3.5f);
		CHECK(library.Length > 150.0f);
		CHECK(mission.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Mission 1: both staircases lead up to the manor's first floor; the hall guard climbs the grand one and comes down")
	{
		MissionOne mission;
		mission.Run(0.2f);

		const NavPath backStairs = mission.Path({ 54.0f, 0.0f, 5.0f }, { 54.0f, 4.0f, 6.0f }); // kitchen to the maid's room
		CHECK(backStairs.Complete);
		CHECK(backStairs.Length < 30.0f);
		const NavPath grandStairs = mission.Path({ 66.0f, 0.0f, 6.0f }, { 72.0f, 4.0f, 1.5f }); // hall to the landing
		CHECK(grandStairs.Complete);
		CHECK(grandStairs.Length < 18.0f);

		// His round: the hall, the drawing room, the hall, up to the first-floor landing and back.
		float highest = 0.0f;
		bool downAgain = false;
		for (int step = 0; step < 120 * 4; ++step)
		{
			mission.Run(0.25f);
			const float y = mission.Position("HallGuard").y;
			highest = std::max(highest, y);
			downAgain = downAgain || (highest > 3.5f && y < 0.5f);
			if (downAgain)
				break;
		}
		CHECK(highest > 3.5f);
		CHECK(downAgain);
		CHECK(mission.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Mission 1: every guard and servant walks his round for a minute")
	{
		MissionOne mission;
		const std::vector<std::string> walkers = { "StreetWatch", "LaneWatch", "SouthWatch", "SquareWatch", "Sexton", "CryptWatch",
			"VineWatch", "TerraceWatch", "HallGuard", "UpperGuard", "YardGuard", "Scullion", "Maid" };
		std::map<std::string, glm::vec3> last;
		std::map<std::string, float> travelled;
		for (const std::string& name : walkers)
			last[name] = mission.Position(name);
		for (int step = 0; step < 60 * 2; ++step)
		{
			mission.Run(0.5f);
			for (const std::string& name : walkers)
			{
				const glm::vec3 position = mission.Position(name);
				travelled[name] += glm::distance(position, last[name]);
				last[name] = position;
			}
		}
		for (const std::string& name : walkers)
		{
			INFO(name);
			CHECK(travelled[name] > 20.0f);
		}
		CHECK(mission.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Mission 1: the ledger and enough loot let the thief out into the tunnel; coming in through it, he is told nothing")
	{
		MissionOne mission; // no difficulty chosen: Normal
		mission.Run(0.1f);
		CHECK(mission.Holds("M.current.difficulty == 'Normal' and #M.current.objectives == 3"));
		CHECK(mission.Holds("M.current.total_items == 17")); // pieces of value: the ledger is an item, not loot

		// He comes up through the tunnel before he has anything: no "Not yet" in his way.
		CHECK(mission.Lua("return scene.find('Goal'):script():reach()") == "false");
		CHECK(mission.Lua("return scene.find('Goal'):script().notice == nil") == "true");
		CHECK(mission.Holds("M.current.state == 'running'"));

		// Taken as F would; the ledger alone is not enough, the loot target is 400.
		mission.Lua("local p = scene.find('Player'); for _, name in ipairs({ 'Ledger', 'FeastGoblet', 'Snuffbox' }) do "
					"scene.find(name):script():on_use(p) end");
		CHECK(mission.Holds("M.has('Ledger') and M.current.loot == 275"));
		CHECK(mission.Lua("return scene.find('Goal'):script():reach()") == "false");
		mission.Lua("local p = scene.find('Player'); for i = 1, 2 do scene.find('SilverPlate' .. i):script():on_use(p) end");
		CHECK(mission.Holds("M.current.loot == 425"));

		// Back in the tunnel: the mission is won.
		mission.Lua("scene.find('Player'):set_position(vec3(12, -3.9, -4))");
		mission.Run(0.5f);
		CHECK(mission.Holds("M.current.state == 'complete'"));
		CHECK(mission.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Mission 1: the manor's alarm bell calls the manor's guards, not the city watch beyond the wall")
	{
		MissionOne mission;
		mission.Run(0.2f);
		mission.Lua("local s = scene.find('Scullion'):script(); s.fleeing = vec3(56, 0, -5); s:ring()");
		mission.Run(0.1f);
		for (const std::string name : { "HallGuard", "UpperGuard", "YardGuard", "TerraceWatch" })
		{
			INFO(name);
			CHECK(mission.Entity(name).GetComponent<PerceptionComponent>().RuntimeStimulusCause == "Bell");
		}
		for (const std::string name : { "StreetWatch", "LaneWatch", "SquareWatch", "Sexton" })
		{
			INFO(name);
			CHECK(mission.Entity(name).GetComponent<PerceptionComponent>().RuntimeStimulusCause != "Bell");
		}
		CHECK(mission.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Mission 1: the guards take no note of the household's doors, only of the thief's")
	{
		MissionOne mission;
		mission.Run(0.2f);
		// Every noise offered to the guards' hearing, by name.
		mission.Lua("heard = {}; local emit = ai.emit_noise; ai.emit_noise = function(p, l, s, c) "
					"if c and c:find('^Door') then heard[#heard + 1] = c end; return emit(p, l, s, c) end");
		// The scullion opens and shuts the kitchen door: a sound, nothing for the guards.
		mission.Lua("scene.find('KitchenDoor'):script():open(scene.find('Scullion'))");
		mission.Run(1.0f);
		mission.Lua("scene.find('KitchenDoor'):script():close(scene.find('Scullion'))");
		mission.Run(1.0f);
		CHECK(mission.Lua("return #heard") == "0");
		// The thief does the same: both reach the guards' ears.
		mission.Lua("scene.find('KitchenDoor'):script():open(scene.find('Player'))");
		mission.Run(1.0f);
		mission.Lua("scene.find('KitchenDoor'):script():close(scene.find('Player'))");
		mission.Run(1.0f);
		CHECK(mission.Lua("return table.concat(heard, ',')") == "\"DoorOpen,DoorShut\"");
		CHECK(mission.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Mission 1: doors open for the people on their own storey; a leaf turned half round knows its hinge")
	{
		MissionOne mission;
		mission.Run(0.1f);
		// The right leaf of a double door in a wall along X stands turned 180 degrees: its middle is the opening's right half.
		CHECK(mission.Lua("local c = scene.find('HallDrawingDoorRight'):script():center(); return string.format('%.2f %.2f', c.x, c.z)") ==
			  "\"66.60 0.00\"");
		// The scullion walks below the maid's door (the first floor) towards its plane: it stays shut.
		mission.Lua("local s = scene.find('Scullion'); s:set_position(vec3(54, 0.05, 1.6))");
		mission.Lua("nav.move_to(scene.find('Scullion'), vec3(54, 0, 8), 1.2)");
		mission.Run(1.5f);
		CHECK(mission.Lua("return scene.find('MaidDoor'):script():is_open()") == "false");
		CHECK(mission.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Mission 1 on Hard also asks for the signet ring")
	{
		MissionOne mission("Hard");
		mission.Run(0.1f);
		CHECK(mission.Holds("#M.current.objectives == 4"));
		CHECK(mission.Lua("local M = require('Scripts/Mission/Mission'); for _, o in ipairs(M.current.objectives) do "
						  "if o.item == 'SignetRing' then return true end end return false") == "true");
	}
}
