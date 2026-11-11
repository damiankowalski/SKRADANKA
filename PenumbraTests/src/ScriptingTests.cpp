#include "TestHelpers.h"

#include "Penumbra/Scripting/ScriptEngine.h"
#include "Penumbra/UI/Overlay.h"

using namespace Penumbra;

namespace {

	struct ScriptFixture
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;

		ScriptFixture() { SceneInstance.SetAudioDeviceEnabled(false); }

		Entity AddScripted(const std::string& name, const std::string& scriptPath, const std::string& source)
		{
			Project.WriteAsset(scriptPath, source);
			Entity entity = SceneInstance.CreateEntity(name);
			entity.AddComponent<ScriptComponent>().Script = scriptPath;
			return entity;
		}

		std::string Run(const std::string& code)
		{
			std::string output;
			const bool ok = SceneInstance.GetScriptEngine()->Execute(code, output);
			REQUIRE_MESSAGE(ok, output);
			return output;
		}

		std::string RunExpectingError(const std::string& code)
		{
			std::string output;
			CHECK_FALSE(SceneInstance.GetScriptEngine()->Execute(code, output));
			return output;
		}
	};

}

TEST_SUITE("Scripting")
{
	TEST_CASE("Lifecycle callbacks run in order with properties applied")
	{
		ScriptFixture fixture;
		Entity mover = fixture.AddScripted("Mover", "Scripts/Mover.lua", R"(
			local Mover = { Properties = { speed = 1.0, label = "default" } }
			function Mover:on_create()
				self.created = true
				self.updates = 0
			end
			function Mover:on_update(dt)
				self.updates = self.updates + 1
				local p = self.entity:get_position()
				self.entity:set_position(p + vec3(self.speed * dt, 0, 0))
			end
			function Mover:on_destroy()
				log.info("destroyed", self.label)
			end
			return Mover
		)");
		mover.GetComponent<ScriptComponent>().Properties["speed"] = 6.0;

		fixture.SceneInstance.OnRuntimeStart();
		CHECK(fixture.SceneInstance.GetScriptEngine()->HasInstance(mover.GetUUID()));
		Test::Simulate(fixture.SceneInstance, 0.5f);
		CHECK(Test::Near(mover.Transform().Translation.x, 3.0f, 0.05f));
		CHECK(fixture.Run("return scene.find('Mover'):script().updates") == "30");
		CHECK(fixture.Run("return scene.find('Mover'):script().label") == "\"default\"");
		CHECK(fixture.SceneInstance.GetScriptEngine()->GetErrors().empty());
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Errors disable the failing instance only and are recorded")
	{
		ScriptFixture fixture;
		fixture.AddScripted("Broken", "Scripts/Broken.lua", R"(
			local Broken = {}
			function Broken:on_update(dt) error("boom") end
			return Broken
		)");
		Entity healthy = fixture.AddScripted("Healthy", "Scripts/Healthy.lua", R"(
			local Healthy = {}
			function Healthy:on_update(dt) self.entity:set_local_position(self.entity:get_local_position() + vec3(0, 1, 0)) end
			return Healthy
		)");
		fixture.AddScripted("Syntax", "Scripts/Syntax.lua", "this is not lua");
		fixture.AddScripted("NoTable", "Scripts/NoTable.lua", "return 42");
		Entity missing = fixture.SceneInstance.CreateEntity("Missing");
		missing.AddComponent<ScriptComponent>().Script = "Scripts/DoesNotExist.lua";

		fixture.SceneInstance.OnRuntimeStart();
		Test::Simulate(fixture.SceneInstance, 0.1f);
		const auto& errors = fixture.SceneInstance.GetScriptEngine()->GetErrors();
		CHECK(errors.size() == 4); // boom (once), syntax, not a table, missing file
		CHECK(healthy.Transform().Translation.y >= 5.0f);
		bool sawBoom = false;
		for (const ScriptError& error : errors)
			sawBoom |= error.Message.find("boom") != std::string::npos;
		CHECK(sawBoom);
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("vec3 math")
	{
		ScriptFixture fixture;
		fixture.SceneInstance.OnRuntimeStart();
		CHECK(fixture.Run("return (vec3(1,2,3) + vec3(1,1,1)) * 2") == "[4.0,6.0,8.0]");
		CHECK(fixture.Run("return vec3(3,4,0):length()") == "5");
		CHECK(fixture.Run("return vec3(1,0,0):cross(vec3(0,1,0)).z") == "1");
		CHECK(fixture.Run("return vec3(1,2,3):dot(vec3(4,5,6))") == "32");
		CHECK(fixture.Run("return -vec3(1,0,0) == vec3(-1,0,0)") == "true");
		CHECK(fixture.Run("return vec3(0,0,0):normalized():length()") == "0");
		CHECK(fixture.Run("return tostring(vec3(1,2,3))") == "\"vec3(1.000, 2.000, 3.000)\"");
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Entities and generic component access")
	{
		ScriptFixture fixture;
		fixture.SceneInstance.OnRuntimeStart();
		fixture.Run(R"(
			local e = scene.create("Lamp")
			e:add("PointLight", { Intensity = 12, Color = vec3(1, 0, 0) })
			e:set_position(vec3(1, 2, 3))
			e:set_rotation(vec3(0, 90, 0))
			local child = scene.create("Bulb")
			child:set_parent(e)
		)");
		Entity lamp = fixture.SceneInstance.FindEntityByName("Lamp");
		REQUIRE(lamp.IsValid());
		CHECK(lamp.GetComponent<PointLightComponent>().Intensity == 12.0f);
		CHECK(Test::Near(lamp.GetComponent<PointLightComponent>().Color, { 1.0f, 0.0f, 0.0f }));
		CHECK(Test::Near(lamp.Transform().Translation, { 1.0f, 2.0f, 3.0f }));
		CHECK(lamp.GetChildren().size() == 1);

		CHECK(fixture.Run("return scene.find('Lamp'):get('PointLight').Intensity") == "12");
		CHECK(fixture.Run("return scene.find('Lamp'):has('SpotLight')") == "false");
		CHECK(fixture.Run("return scene.find('Lamp'):get('SpotLight')") == "null");
		CHECK(fixture.Run("return #scene.find('Lamp'):children()") == "1");
		CHECK(fixture.Run("return scene.find('Bulb'):parent():get_name()") == "\"Lamp\"");
		CHECK(fixture.Run("return #scene.entities_with('PointLight')") == "1");
		CHECK(fixture.Run("local e = scene.find('Lamp'); return scene.get(e.id) == e") == "true");
		fixture.Run("scene.find('Lamp'):set('PointLight', { Range = 3 })");
		CHECK(lamp.GetComponent<PointLightComponent>().Range == 3.0f);
		CHECK(lamp.GetComponent<PointLightComponent>().Intensity == 12.0f);
		fixture.Run("scene.find('Lamp'):remove('PointLight')");
		CHECK_FALSE(lamp.HasComponent<PointLightComponent>());

		CHECK(fixture.RunExpectingError("scene.find('Lamp'):add('Nope')").find("unknown component") != std::string::npos);
		CHECK(fixture.RunExpectingError("scene.find('Lamp'):add('SpotLight', { Rnage = 1 })").find("unknown field") != std::string::npos);
		CHECK(fixture.RunExpectingError("scene.find('Lamp'):remove('Transform')").find("cannot be removed") != std::string::npos);

		fixture.Run("local e = scene.find('Lamp'); _G.saved = e; e:destroy()");
		CHECK_FALSE(fixture.SceneInstance.FindEntityByName("Lamp").IsValid());
		CHECK_FALSE(fixture.SceneInstance.FindEntityByName("Bulb").IsValid());
		CHECK(fixture.Run("return saved:valid()") == "false");
		CHECK(fixture.RunExpectingError("return saved:get_position()").find("destroyed") != std::string::npos);
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Spawning prefabs, physics, input, AI, stim, ui and test APIs")
	{
		ScriptFixture fixture;
		Test::CreateBox(fixture.SceneInstance, "Ground", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f });
		Entity guard = fixture.SceneInstance.CreateEntity("Guard");
		guard.AddComponent<PerceptionComponent>();
		Entity player = fixture.SceneInstance.CreateEntity("Player");
		player.Transform().Translation = { 0.0f, 0.0f, -5.0f };
		player.AddComponent<PerceptionTargetComponent>();
		player.AddComponent<CharacterControllerComponent>();
		fixture.Project.WriteAsset("Prefabs/Ball.pprefab", R"({ "Format": "PenumbraPrefab", "Version": 1, "Entities": [
			{ "ID": "1", "Components": { "Tag": { "Tag": "Ball" }, "SphereCollider": { "Radius": 0.25 }, "RigidBody": { "Type": "Dynamic" } } } ] })");

		fixture.SceneInstance.OnRuntimeStart();
		fixture.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));

		fixture.Run("_G.ball = scene.instantiate('Prefabs/Ball.pprefab', vec3(0, 3, 0))");
		CHECK(fixture.SceneInstance.FindEntityByName("Ball").IsValid());
		CHECK(fixture.RunExpectingError("scene.instantiate('Prefabs/Nope.pprefab')").find("not found") != std::string::npos);
		Test::Simulate(fixture.SceneInstance, 0.1f);
		CHECK(fixture.Run("return physics.get_velocity(ball).y < 0") == "true");
		CHECK(fixture.Run("local hit = physics.raycast(vec3(0, 5, 3), vec3(0, -1, 0), 10); return hit.entity:get_name()") == "\"Ground\"");
		CHECK(fixture.Run("return physics.raycast(vec3(0, 5, 30), vec3(0, -1, 0), 10)") == "null");
		CHECK(fixture.Run("return #physics.overlap_sphere(vec3(0, 0, 0), 1) >= 1") == "true");

		fixture.Run("character.move(scene.find('Player'), vec3(2, 0, 0))");
		Test::Simulate(fixture.SceneInstance, 0.5f);
		CHECK(player.Transform().Translation.x > 0.5f);
		CHECK(fixture.Run("return character.is_grounded(scene.find('Player'))") == "true");

		CHECK(fixture.Run("return input.key('W')") == "false");
		CHECK(fixture.RunExpectingError("return input.key('NotAKey')").find("unknown key") != std::string::npos);

		CHECK(fixture.Run("return ai.state(scene.find('Guard'))") == "\"Unaware\"");
		fixture.Run("ai.set_suspicion(scene.find('Guard'), 0.8)");
		CHECK(fixture.Run("return math.abs(ai.suspicion(scene.find('Guard')) - 0.8) < 1e-4") == "true");
		CHECK(fixture.Run("return ai.notify(scene.find('Guard'), vec3(1, 0, 2), 8, 'Shout')") == "true");
		CHECK(fixture.Run("return ai.state(scene.find('Guard'))") == "\"Searching\"");
		CHECK(fixture.Run("return ai.reaction(scene.find('Guard')).cause") == "\"Shout\"");
		CHECK(fixture.Run("return ai.reaction(scene.find('Guard')).kind") == "\"Reported\"");
		CHECK(fixture.Run("return ai.reaction(scene.find('Guard')).position.z == 2") == "true");
		CHECK(fixture.Run("return ai.notify(scene.find('Guard'), vec3(0, 0, 0), 2)") == "false"); // weaker: ignored
		CHECK(fixture.Run("return ai.alertness(scene.find('Guard')) > 0.7") == "true");
		fixture.Run("ai.calm(scene.find('Guard'))");
		CHECK(fixture.Run("return ai.state(scene.find('Guard'))") == "\"Unaware\"");
		CHECK(fixture.Run("return math.floor(ai.noise_range(6))") == "11");
		CHECK(fixture.Run("return ai.hearing(scene.find('Guard'), vec3(0, 1.6, -1.4), 5) > 4.9") == "true");
		CHECK(fixture.Run("return ai.visibility(scene.find('Guard'), scene.find('Player')).total_points") == "7");
		CHECK(fixture.Run("return ai.luminance(vec3(0, 1, 0)) >= 0") == "true");
		fixture.Run("ai.emit_noise(vec3(0, 1.6, -1), 10)");

		fixture.Run("ui.text(10, 10, 'hello', 2, {1, 0, 0, 1}); ui.rect(0, 0, 50, 5)");
		CHECK(fixture.SceneInstance.GetOverlay().GetTexts().size() == 1);
		CHECK(fixture.SceneInstance.GetOverlay().GetRects().size() == 1);

		fixture.Run("test.check(true); test.equal({1, 2}, {1, 2}); test.near(1.0, 1.00001, 0.001); test.check(false, 'expected failure'); test.complete()");
		const TestReport& report = fixture.SceneInstance.GetTestReport();
		CHECK(report.Completed);
		CHECK(report.Assertions == 4);
		CHECK(report.Failures == 1);
		CHECK_FALSE(report.Passed);

		fixture.Run("scene.load('Scenes/Next.pscene'); scene.quit(3)");
		CHECK(fixture.SceneInstance.GetRequestedScene() == "Scenes/Next.pscene");
		CHECK(fixture.SceneInstance.GetRequestedQuit() == 3);
		CHECK(fixture.Run("return time.frame() > 0") == "true");
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Collision and stim callbacks reach scripts; require loads modules")
	{
		ScriptFixture fixture;
		fixture.Project.WriteAsset("Scripts/Util.lua", "return { double = function(x) return x * 2 end }");
		Test::CreateBox(fixture.SceneInstance, "Ground", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f });
		Entity ball = fixture.AddScripted("Ball", "Scripts/Ball.lua", R"(
			local util = require("Scripts/Util")
			local Ball = {}
			function Ball:on_create() self.hits = 0; self.stims = {}; self.value = util.double(21) end
			function Ball:on_collision(other) self.hits = self.hits + 1; self.last = other:get_name() end
			function Ball:on_stim(stim, intensity, source) table.insert(self.stims, stim) end
			return Ball
		)");
		ball.Transform().Translation = { 0.0f, 2.0f, 0.0f };
		ball.AddComponent<SphereColliderComponent>();
		ball.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Dynamic;

		fixture.SceneInstance.OnRuntimeStart();
		Test::Simulate(fixture.SceneInstance, 1.5f);
		CHECK(fixture.Run("return scene.find('Ball'):script().value") == "42");
		CHECK(fixture.Run("return scene.find('Ball'):script().hits >= 1") == "true");
		CHECK(fixture.Run("return scene.find('Ball'):script().last") == "\"Ground\"");
		fixture.Run("stim.send(scene.find('Ball'), 'Water', 1.0)");
		CHECK(fixture.Run("return scene.find('Ball'):script().stims[1]") == "\"Water\"");
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("math.random and engine rolls follow the scene's seed")
	{
		auto sequence = [](std::optional<uint64_t> seed) {
			ScriptFixture fixture;
			if (seed)
				fixture.SceneInstance.SetRandomSeed(*seed);
			fixture.SceneInstance.OnRuntimeStart();
			std::string values = fixture.Run("return math.random(1000000) .. ',' .. math.random(1000000) .. ',' .. math.random()");
			values += ";" + std::to_string(fixture.SceneInstance.NextRandom());
			fixture.SceneInstance.OnRuntimeStop();
			return values;
		};
		CHECK(sequence(std::nullopt) == sequence(std::nullopt)); // tests and headless runs are reproducible
		CHECK(sequence(7) == sequence(7));
		CHECK(sequence(7) != sequence(8));
		CHECK(sequence(std::nullopt) == sequence(Scene::DefaultRandomSeed));

		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		scene.SetRandomSeed(3);
		scene.OnRuntimeStart();
		for (int i = 0; i < 1000; ++i)
		{
			const float value = scene.NextRandom();
			REQUIRE(value >= 0.0f);
			REQUIRE(value < 1.0f);
		}
		scene.OnRuntimeStop();
	}
}
