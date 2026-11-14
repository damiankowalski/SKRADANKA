#include "TestHelpers.h"

#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

using namespace Penumbra;

// The thief's workshop on the demo's Stealth level with the real scripts: the hall's light switch
// (Scripts/World/LightSwitch.lua) and the NPCs who light the lamps again (Scripts/AI/Lamps.lua),
// and the moss arrow (Scripts/MossArrow.lua, Prefabs/MossPatch.pprefab).

namespace {

	struct Manor
	{
		Scene SceneInstance;

		Manor()
		{
			std::string error;
			REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
			SceneInstance.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(SceneInstance, Project::GetAssetDirectory() / "Scenes/Stealth.pscene", error), error);
			// Quiet: no weather, dogs or kitchen noises; the thief far away and unseen.
			auto& world = Entity("World").GetComponent<ScriptComponent>().Properties;
			world["storms"] = false;
			world["dogs"] = false;
			Entity("KitchenZone").GetComponent<ScriptComponent>().Properties["chance_scale"] = 0.0;
			Penumbra::Entity player = Entity("Player");
			player.Transform().Translation = { -17.0f, 0.05f, 17.0f };
			player.GetComponent<PerceptionTargetComponent>().SilhouettePoints.clear();
			SceneInstance.OnRuntimeStart();
		}

		~Manor()
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

		float Number(const std::string& expression) { return std::stof(Lua("return " + expression)); }
		bool HallLit() { return Entity("HallLight1").GetComponent<PointLightComponent>().Enabled; }
		void Run(float seconds) { Test::Simulate(SceneInstance, seconds); }

		// Shoots a moss arrow from `from` with velocity `velocity`.
		void ShootMoss(const std::string& from, const std::string& velocity)
		{
			Lua("local a = scene.instantiate('Prefabs/MossArrow.pprefab', " + from + "); a:look_at(" + from + " + " + velocity + ")");
			Run(1.0f / 60.0f);
			Lua("for _, e in ipairs(scene.entities_with('Script')) do if e:get_name() == 'MossArrow' then physics.set_velocity(e, " + velocity +
				") end end");
			Run(0.5f);
		}

		int MossPatches() { return std::stoi(Lua("local n = 0; for _, e in ipairs(scene.entities_with('Surface')) do if e:get_name() == 'MossPatch' then n = n + 1 end end; return n")); }
	};

}

TEST_SUITE("World")
{
	TEST_CASE("The hall's light switch puts the lamps out and darkens the hall for the guards; a calm NPC lights them again")
	{
		Manor manor;
		manor.Run(1.0f);
		REQUIRE(manor.HallLit());
		const float litLuminance = manor.Number("ai.luminance(vec3(10, 1, -11))");

		// The thief flips the switch: lamps, their glowing lanterns and what the guards perceive all go dark.
		manor.Lua("local s = scene.find('HallSwitch'):script(); s:on_use(scene.find('Player'))");
		CHECK_FALSE(manor.HallLit());
		CHECK(manor.Lua("return scene.find('HallLight1'):children()[1]:get('Material').EmissiveEnabled") == "false");
		CHECK(manor.Lua("return scene.find('HallSwitch'):script().use_prompt") == "\"light the hall lamps\"");
		manor.Run(0.1f);
		CHECK(manor.Number("ai.luminance(vec3(10, 1, -11))") < litLuminance * 0.3f);
		// Flipping it again does nothing new when already in that position.
		CHECK(manor.Lua("return scene.find('HallSwitch'):script():set(false)") == "false");

		// Someone in or by the hall (the servant, or a guard passing the doorway) notices, walks to the
		// switch and turns the lamps back on.
		bool relit = false;
		for (int second = 0; second < 60 && !relit; ++second)
		{
			manor.Run(1.0f);
			relit = manor.HallLit();
		}
		CHECK(relit);
		CHECK(manor.Lua("return scene.find('HallSwitch'):script().claimed_by == nil") == "true");
		CHECK(manor.Number("ai.luminance(vec3(10, 1, -11))") > litLuminance * 0.9f);
		CHECK(manor.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("A moss arrow on a floor leaves a patch of moss that silences footsteps; on a wall it leaves nothing")
	{
		Manor manor;
		manor.Run(0.5f);
		const float buildMilliseconds = manor.Entity("Navigation").GetComponent<NavMeshComponent>().RuntimeBuildMilliseconds;

		// Straight down onto the cobbles west of the cart.
		manor.ShootMoss("vec3(-6, 3, 1)", "vec3(0, -25, 0)");
		CHECK(manor.MossPatches() == 1);
		// The thief walking across it: a footstep that was 3 on stone is 0 here.
		manor.Lua("scene.find('Player'):set_position(vec3(-6, 0.1, 1))");
		manor.Run(0.2f);
		CHECK(manor.Lua("local N = require('Scripts/AI/Noises'); local m, k = N.surface_under(scene.find('Player')); return m .. ' ' .. k") ==
			"\"Moss -3.0\"");
		CHECK(manor.Number("require('Scripts/AI/Noises').footstep(scene.find('Player'), 'walk')") == doctest::Approx(0.0f));
		CHECK(manor.Number("require('Scripts/AI/Noises').footstep(scene.find('Player'), 'run')") == doctest::Approx(2.0f));

		// Into the west wall: no moss, and no arrow left over.
		manor.ShootMoss("vec3(-16, 1.5, 0)", "vec3(-30, 0, 0)");
		CHECK(manor.MossPatches() == 1);
		CHECK(manor.Lua("for _, e in ipairs(scene.entities_with('Script')) do if e:get_name() == 'MossArrow' then return true end end return false") ==
			"false");

		// A patch is kinematic: it never sends the navmesh into a rebuild.
		manor.Run(3.0f);
		CHECK(manor.Entity("Navigation").GetComponent<NavMeshComponent>().RuntimeBuildMilliseconds == buildMilliseconds);
		CHECK(manor.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}
}
