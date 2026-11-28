#include "TestHelpers.h"

#include "Penumbra/Animation/Animation.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Asset/Importers.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/FogUtils.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

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
	TEST_CASE("Cloister: a knocked-out guard goes limp, falls, settles; carried he is stiff, put down limp again")
	{
		Level cloister("Scenes/Cloister.pscene");
		cloister.Run(1.0f); // walking his round
		Entity guard = cloister.Entity("GuardNorth");
		REQUIRE(guard.HasComponent<RagdollComponent>());
		CHECK(cloister.Lua("return ai.knock_out(scene.find('GuardNorth'))") == "true");
		CHECK(guard.GetComponent<RagdollComponent>().RuntimeActive);

		// The head, from the pose the ragdoll writes into the model's skeleton.
		auto headHeight = [&]() {
			Entity model = guard.GetChildren().front();
			for (Entity child : guard.GetChildren())
				if (child.GetName() == "Model")
					model = child;
			const AnimatorComponent& animator = model.GetComponent<AnimatorComponent>();
			REQUIRE(animator.RuntimePoseOverride);
			const std::shared_ptr<const MeshData> mesh = AssetManager::GetMesh(model.GetComponent<MeshComponent>().Mesh);
			std::vector<glm::mat4> globals;
			Animation::ComputeGlobalTransforms(mesh->Skeleton, *animator.RuntimePoseOverride, globals);
			const int32_t head = mesh->Skeleton.FindNode("head");
			return (cloister.SceneInstance.GetWorldTransform(model) * globals[static_cast<size_t>(head)])[3].y;
		};
		cloister.Run(0.1f);
		const float standing = headHeight();
		CHECK(standing > 0.8f); // the head bone of a 1.8 m guard (the model is scaled 0.8), still up
		bool settled = false;
		for (int step = 0; step < 15 * 10 && !settled; ++step)
		{
			cloister.Run(0.1f);
			settled = !guard.GetComponent<RagdollComponent>().RuntimeAwake;
		}
		CHECK(settled); // came to rest: physics put the bodies to sleep
		CHECK(headHeight() < 0.6f); // lying on the floor
		CHECK(std::abs(cloister.Position("GuardNorth").y) < 0.2f); // the origin followed the hips down to the floor

		// Lifted onto the thief's shoulders: the animation's carrying pose; put down: limp again.
		cloister.Lua("local p = scene.find('Player'); local g = scene.find('GuardNorth'); p:set_position(g:get_position() + vec3(0, 0.1, 0.8)); "
					 "p:script():pick_up(g)");
		CHECK_FALSE(guard.GetComponent<RagdollComponent>().RuntimeActive);
		cloister.Run(0.2f);
		cloister.Lua("scene.find('Player'):script():drop_body()");
		CHECK(guard.GetComponent<RagdollComponent>().RuntimeActive);
		cloister.Run(2.0f);
		CHECK(headHeight() < 0.7f);
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Physics lab: an arrow knocks the copper jug off the table, and the guards hear it land")
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
		// The lookout on the ramp hears the clang; the guard below hears it too, or the lookout's shout.
		const PerceptionComponent& lookout = lab.Entity("Lookout").GetComponent<PerceptionComponent>();
		CHECK(lookout.RuntimeStimulusCause.find("Metal") != std::string::npos);
		const PerceptionComponent& guard = lab.Entity("Guard").GetComponent<PerceptionComponent>();
		CHECK(guard.RuntimeState != AwarenessState::Unaware);
		CHECK((guard.RuntimeStimulusCause.find("Metal") != std::string::npos || guard.RuntimeStimulusCause == "Shout"));
		CHECK(lab.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Physics lab: the lookout on the ramp, knocked out, slumps on the slope; bolts shove him down")
	{
		Level lab("Scenes/PhysicsLab.pscene");
		lab.Run(0.5f);
		const glm::vec3 standing = lab.Position("Lookout");
		REQUIRE(standing.y > 1.4f); // on the ramp, well above the floor
		REQUIRE(lab.Lua("return ai.knock_out(scene.find('Lookout'))") == "true");
		lab.Run(4.0f);
		const glm::vec3 slumped = lab.Position("Lookout");
		CHECK(slumped.y < standing.y);      // down on the slope
		CHECK(slumped.y > standing.y - 0.6f); // 18 degrees: he does not roll off by himself
		for (int bolt = 0; bolt < 3; ++bolt) // shot from up the ramp, into the body
		{
			lab.Lua("local g = scene.find('Lookout'):get_position(); local a = scene.instantiate('Prefabs/WaterArrow.pprefab', g + vec3(0, 1.2, -1.5)); "
					"a:look_at(g + vec3(0, 0.2, 0)); _G.lab_bolt = a");
			lab.Run(1.0f / 60.0f);
			lab.Lua("physics.set_velocity(_G.lab_bolt, (scene.find('Lookout'):get_position() + vec3(0, 0.2, 0) - _G.lab_bolt:get_position()):normalized() * 32)");
			lab.Run(1.0f);
		}
		const glm::vec3 shoved = lab.Position("Lookout");
		CHECK(shoved.z > slumped.z + 0.2f); // further down the ramp (it descends southwards)
		CHECK(shoved.y < slumped.y - 0.05f);
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

	TEST_CASE("Cloister: hugging a wall, the camera never sees through it at any angle")
	{
		Level cloister("Scenes/Cloister.pscene");
		// In the east wing (3 m wide), pressed against its north wall, then its south wall.
		for (const char* spot : { "vec3(18, 0.05, -1.15)", "vec3(18, 0.05, 1.15)" })
		{
			cloister.Lua(std::string("scene.find('Player'):set_position(") + spot + ")");
			cloister.Run(0.3f);
			const std::string result = cloister.Lua(R"(
				local p = scene.find('Player'); local s = p:script(); local camera = scene.find('Camera')
				local bad = {}
				for yaw = 0, 350, 10 do
					for _, pitch in ipairs({ -65, -10, 40 }) do
						s.yaw, s.pitch = yaw, pitch
						s:update_camera(10) -- a long step: the boom has eased back out
						local head = p:get_position() + vec3(0, s.visual_height + 0.15, 0)
						local pivot, eye = s.aim_origin, camera:get_position()
						local blocked = physics.raycast(head, pivot - head, (pivot - head):length(), p)
							or physics.raycast(pivot, eye - pivot, (eye - pivot):length(), p)
						local inside = false
						for _, e in ipairs(physics.overlap_sphere(eye, 0.1)) do -- solid bodies only (a torch's flame is a sensor)
							if e ~= p and not (e:has('RigidBody') and e:get('RigidBody').IsTrigger) then inside = true end
						end
						if blocked or inside then table.insert(bad, yaw .. '/' .. pitch) end
					end
				end
				return table.concat(bad, ' '))");
			CHECK_MESSAGE(result == "\"\"", spot, " ", result);
		}
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Cloister: Q and E lean the thief out past a corner; a wall stops his head; a movement key ends it")
	{
		Level cloister("Scenes/Cloister.pscene");
		// In the middle of the east wing (walls 1.5 m to either side), looking west down it: his right is north (-Z).
		cloister.Lua("local p = scene.find('Player'); p:set_position(vec3(18, 0.05, 0)); local s = p:script(); s.yaw, s.pitch = 90, -10");
		cloister.Run(0.3f);
		Entity player = cloister.Entity("Player");
		auto lean = [&]() { return player.GetComponent<FogOfWarComponent>().Lean; };
		CHECK(lean() == 0.0f);

		Input::SetKeyState(Key::E, true);
		cloister.Run(0.6f);
		CHECK(lean() == doctest::Approx(0.45f).epsilon(0.02));
		const glm::vec3 eye = player.GetComponent<FogOfWarComponent>().RuntimeEye;
		CHECK(eye.z == doctest::Approx(cloister.Position("Player").z - 0.45f).epsilon(0.02)); // the eye he sees with
		CHECK(cloister.Lua("return scene.find('Player'):script().leaning") == "true");
		// Guards see his head and shoulders out to the right, his feet where they stand.
		const std::vector<glm::vec3>& points = player.GetComponent<PerceptionTargetComponent>().SilhouettePoints;
		REQUIRE(points.size() == 7);
		CHECK(points.front().x == doctest::Approx(0.45f).epsilon(0.02));
		CHECK(std::abs(points.back().x) < 0.15f);
		// The model tilts the same way: its up leans towards his right.
		CHECK(cloister.Lua("local p = scene.find('Player'); for _, c in ipairs(p:children()) do if c:get_name() == 'Model' then "
						   "return c:up():dot(vec3(0, 0, -1)) > 0.15 end end") == "true");

		// A movement key ends the lean, and he walks.
		const glm::vec3 before = cloister.Position("Player");
		Input::SetKeyState(Key::W, true);
		cloister.Run(0.6f);
		Input::SetKeyState(Key::W, false);
		CHECK(std::abs(lean()) < 0.01f);
		CHECK(glm::distance(cloister.Position("Player"), before) > 0.5f);

		// Q leans the other way.
		Input::SetKeyState(Key::E, false);
		Input::SetKeyState(Key::Q, true);
		cloister.Run(0.6f);
		CHECK(lean() == doctest::Approx(-0.45f).epsilon(0.02));
		Input::SetKeyState(Key::Q, false);

		// Close to the north wall his head stops short of it, and the eye stays on this side.
		cloister.Lua("local p = scene.find('Player'); p:set_position(vec3(18, 0.05, -1.1)); p:script().yaw = 90");
		cloister.Run(0.6f);
		Input::SetKeyState(Key::E, true);
		cloister.Run(0.6f);
		CHECK(lean() > 0.0f);
		CHECK(lean() < 0.3f);
		CHECK(cloister.Lua("local p = scene.find('Player'); local eye = p:get('FogOfWar').Eye; local body = p:get_position() + vec3(0, 1.0, 0); "
						   "return physics.raycast(body, eye - body, (eye - body):length(), p) == nil") == "true");
		Input::SetKeyState(Key::E, false);
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("PhysicsLab: the haze its torches light gives a still thief away slowly, not at once")
	{
		// 10 m in front of the guard, standing still: hidden before the fog glow; with it, the haze behind him
		// outlines him after a few seconds (FogScattering 0.25, Tools/BuildDemoProject.py; at the default 0.5
		// within one or two).
		Level lab("Scenes/PhysicsLab.pscene");
		Entity player = lab.Entity("Player");
		lab.SceneInstance.OnRuntimeStop(); // restart without the player's controller and the scripts
		player.RemoveComponent<CharacterControllerComponent>();
		for (auto handle : std::vector<entt::entity>(lab.SceneInstance.GetRegistry().view<ScriptComponent>().begin(),
				 lab.SceneInstance.GetRegistry().view<ScriptComponent>().end()))
			lab.SceneInstance.GetRegistry().remove<ScriptComponent>(handle);
		player.GetComponent<PerceptionTargetComponent>().SilhouettePoints = PerceptionTargetComponent().SilhouettePoints;
		player.Transform().Translation = { 9.0f, 0.05f, 2.0f };
		lab.SceneInstance.OnRuntimeStart();
		CHECK(lab.SceneInstance.GetPerceptionSystem()->IsFogGlowEnabled());
		PerceptionComponent& guard = lab.Entity("Guard").GetComponent<PerceptionComponent>();
		lab.Run(2.0f);
		const VisibilityResult seen = lab.SceneInstance.GetPerceptionSystem()->ComputeVisibility(lab.Entity("Guard"), player);
		CHECK(seen.VisiblePoints == seen.TotalPoints); // in plain view
		CHECK(seen.FogGlowBehind > 0.0f);              // the haze behind him glows
		CHECK(guard.RuntimeSuspicion < 0.3f);          // not at once
		lab.Run(8.0f);
		CHECK(guard.RuntimeSuspicion >= 0.3f);         // but in the end
	}

	TEST_CASE("PerceptionLab's fog spots: lit mist behind the thief gives him away, lit mist in front hides him")
	{
		// The playtest places of the fog glow (Tools/BuildDemoProject.py, FOG_SPOT_EAST / FOG_SPOT_WEST). The thief
		// stands still there; seconds until the guard's suspicion reaches 0.3 (-1: not within 10 s).
		auto timeAt = [](const glm::vec3& spot, bool glow, bool lampBetween) {
			Level lab("Scenes/PerceptionLab.pscene");
			Entity player = lab.Entity("Player");
			lab.SceneInstance.OnRuntimeStop(); // restart without the player's controller and scripts
			player.RemoveComponent<CharacterControllerComponent>();
			player.RemoveComponent<ScriptComponent>();
			lab.Entity("Guard").RemoveComponent<ScriptComponent>();
			player.GetComponent<PerceptionTargetComponent>().SilhouettePoints = PerceptionTargetComponent().SilhouettePoints;
			lab.Entity("FogLampBetween").GetComponent<PointLightComponent>().Enabled = lampBetween;
			player.Transform().Translation = spot;
			lab.SceneInstance.OnRuntimeStart();
			lab.SceneInstance.GetPerceptionSystem()->SetFogGlow(glow);
			PerceptionComponent& guard = lab.Entity("Guard").GetComponent<PerceptionComponent>();
			const float dt = 1.0f / 60.0f;
			for (float t = 0.0f; t < 10.0f; t += dt)
			{
				lab.SceneInstance.OnUpdateRuntime(Timestep(dt));
				if (guard.RuntimeSuspicion >= 0.3f)
					return t + dt;
			}
			return -1.0f;
		};
		const glm::vec3 east(6.0f, 0.05f, -1.0f), west(-7.0f, 0.05f, 0.4f);

		const float eastBefore = timeAt(east, false, true), eastAfter = timeAt(east, true, true);
		const float westNoLamp = timeAt(west, true, false), westLamp = timeAt(west, true, true), westBefore = timeAt(west, false, true);
		MESSAGE("east: fog only dims " << eastBefore << " s, fog glows " << eastAfter << " s; west: no lamp " << westNoLamp
				<< " s, lamp in the mist " << westLamp << " s (fog only dims: " << westBefore << " s)");
		REQUIRE(eastAfter > 0.0f);
		CHECK((eastBefore < 0.0f || eastAfter < eastBefore * 0.5f));
		REQUIRE(westNoLamp > 0.0f);
		CHECK((westLamp < 0.0f || westLamp > westNoLamp * 2.0f));
		CHECK((westLamp < 0.0f || (westBefore > 0.0f && westLamp > westBefore * 2.0f)));
	}

	// Detection times before and after a change to perception: for every guard of the given levels the thief stands
	// still 6, 10 and 14 m in front of him (and on PerceptionLab's two fog spots); seconds until the guard's suspicion
	// reaches 0.3. mode(scene, after) switches the change off (after = false) or on, once the scene runs. Scripts are
	// removed so nobody moves; MoodLab gets a guard where the wide shot stands, in variant C.
	void DetectionReport(const std::vector<std::string>& scenes, const std::function<void(Scene&, bool)>& mode)
	{
		struct Spot
		{
			std::string Guard;
			glm::vec3 Position;
		};
		auto load = [](const std::string& scenePath, Scene& scene) {
			std::string error;
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(scene, Project::GetAssetDirectory() / scenePath, error), error);
			std::vector<entt::entity> scripted;
			for (auto handle : scene.GetRegistry().view<ScriptComponent>())
				scripted.push_back(handle);
			for (auto handle : scripted)
				scene.GetRegistry().remove<ScriptComponent>(handle);
		};
		struct Outcome
		{
			float Time = -1.0f;
			VisibilityResult Seen;
		};
		auto timeFor = [&](const std::string& scenePath, const Spot& spot, bool after) {
			Scene scene;
			scene.SetAudioDeviceEnabled(false);
			load(scenePath, scene);
			if (scenePath == "Scenes/MoodLab.pscene")
			{
				Entity guard = scene.CreateEntity("Guard");
				guard.Transform().Translation = { 0.0f, 0.05f, 7.3f };
				guard.AddComponent<PerceptionComponent>();
			}
			Entity player = scene.FindEntityByName("Thief") ? scene.FindEntityByName("Thief") : scene.FindEntityByName("Player");
			REQUIRE(player);
			if (!player.HasComponent<PerceptionTargetComponent>())
				player.AddComponent<PerceptionTargetComponent>();
			// Placed by hand and standing still: no physics character that would put him back.
			if (player.HasComponent<CharacterControllerComponent>())
				player.RemoveComponent<CharacterControllerComponent>();
			scene.OnRuntimeStart();
			// On the floor under the spot (a guard on a ledge would put him in the air).
			glm::vec3 position = spot.Position;
			auto notPlayer = [&](UUID id) { return id != player.GetUUID(); };
			if (std::optional<RaycastHit> floor = scene.GetPhysicsWorld()->Raycast(position + glm::vec3(0.0f, 0.5f, 0.0f), { 0.0f, -1.0f, 0.0f }, 10.0f, notPlayer))
				position = floor->Point + glm::vec3(0.0f, 0.05f, 0.0f);
			player.Transform().Translation = position;
			if (scenePath == "Scenes/MoodLab.pscene")
			{
				std::string output;
				REQUIRE(scene.GetScriptEngine()->Execute("return require('Scripts/MoodLab/Variants').apply('Mixed')", output));
			}
			mode(scene, after);
			Entity guard = scene.FindEntityByName(spot.Guard);
			REQUIRE(guard);
			Outcome outcome;
			const float dt = 1.0f / 60.0f;
			for (float t = 0.0f; t < 10.0f; t += dt)
			{
				player.Transform().Translation = position; // nothing pushes him away
				scene.OnUpdateRuntime(Timestep(dt));
				if (guard.GetComponent<PerceptionComponent>().RuntimeSuspicion >= 0.3f)
				{
					outcome.Time = t + dt;
					break;
				}
			}
			outcome.Seen = scene.GetPerceptionSystem()->ComputeVisibility(guard, player);
			scene.OnRuntimeStop();
			return outcome;
		};
		auto spotsFor = [&](const std::string& scenePath) {
			std::vector<Spot> spots;
			if (scenePath == "Scenes/PerceptionLab.pscene")
				return std::vector<Spot>{ { "Guard", { 6.0f, 0.05f, -1.0f } }, { "Guard", { -7.0f, 0.05f, 0.4f } } };
			if (scenePath == "Scenes/MoodLab.pscene")
			{
				for (const float d : { 6.0f, 10.0f, 14.0f })
					spots.push_back({ "Guard", { 0.0f, 0.05f, 7.3f - d } });
				return spots;
			}
			Scene scene;
			load(scenePath, scene);
			for (auto [handle, perception] : scene.GetRegistry().view<PerceptionComponent>().each())
			{
				Entity guard(handle, &scene);
				glm::vec3 forward = Math::Forward(guard.Transform().Rotation);
				forward.y = 0.0f;
				forward = glm::normalize(forward);
				for (const float d : { 6.0f, 10.0f, 14.0f })
					spots.push_back({ guard.GetName(), guard.Transform().Translation + forward * d });
				// And beside the lamps he looks at (4-20 m away, within 50 degrees of his gaze): 1.2 m to the side of
				// each, lit from the side as seen from the guard. At most three.
				int nearLamps = 0;
				for (auto [lampHandle, lamp] : scene.GetRegistry().view<PointLightComponent>().each())
				{
					if (!lamp.Enabled || nearLamps >= 3)
						continue;
					const glm::vec3 lampPosition = scene.GetWorldPosition(Entity(lampHandle, &scene));
					glm::vec3 toLamp = lampPosition - guard.Transform().Translation;
					toLamp.y = 0.0f;
					const float distance = glm::length(toLamp);
					if (distance < 4.0f || distance > 20.0f || glm::dot(toLamp / distance, forward) < std::cos(glm::radians(50.0f)))
						continue;
					const glm::vec3 side = glm::normalize(glm::cross(toLamp, glm::vec3(0.0f, 1.0f, 0.0f)));
					spots.push_back({ guard.GetName(), glm::vec3(lampPosition.x, guard.Transform().Translation.y, lampPosition.z) + side * 1.2f });
					nearLamps++;
				}
			}
			return spots;
		};

		std::string error;
		REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
		Input::Reset();
		for (const std::string& scenePath : scenes)
		{
			for (const Spot& spot : spotsFor(scenePath))
			{
				const Outcome before = timeFor(scenePath, spot, false);
				const Outcome afterOutcome = timeFor(scenePath, spot, true);
				auto text = [](float t) { return t < 0.0f ? std::string("-") : std::format("{:.1f}", t); };
				const VisibilityResult& seen = afterOutcome.Seen;
				MESSAGE(std::format("{} | {} | ({:.1f}, {:.1f}) | {} | {} | B {:.2f} -> {:.2f}, points {}/{}, L thief {:.4f}, bg {:.4f}, "
									"T {:.2f}, glow front {:.4f}, behind {:.4f}, self {:.3f}",
					scenePath, spot.Guard, spot.Position.x, spot.Position.z, text(before.Time), text(afterOutcome.Time), before.Seen.Stimulus,
					seen.Stimulus, seen.VisiblePoints, seen.TotalPoints, seen.TargetLuminance, seen.BackgroundLuminance, seen.FogTransmittance,
					seen.FogGlowFront, seen.FogGlowBehind, seen.SelfContrast));
			}
		}
		Project::Unload();
	}

	TEST_CASE("Fog glow report: detection times in the foggy levels, before and after" * doctest::skip())
	{
		// PenumbraTests -tc="Fog glow report*" --no-skip (release): the fog only dimming contrast, then seen lit.
		DetectionReport({ "Scenes/Cloister.pscene", "Scenes/Stealth.pscene", "Scenes/PhysicsLab.pscene", "Scenes/MoodLab.pscene",
							"Scenes/PerceptionLab.pscene" },
			[](Scene& scene, bool after) { scene.GetPerceptionSystem()->SetFogGlow(after); });
	}

	TEST_CASE("Self-shading report: detection times in every level with guards, before and after" * doctest::skip())
	{
		// PenumbraTests -tc="Self-shading report*" --no-skip (release): SelfShading 0, then each guard's own value.
		DetectionReport({ "Scenes/AILab.pscene", "Scenes/Cloister.pscene", "Scenes/Stealth.pscene", "Scenes/PhysicsLab.pscene",
							"Scenes/MoodLab.pscene", "Scenes/PerceptionLab.pscene" },
			[](Scene& scene, bool after) {
				if (!after)
					for (auto [handle, perception] : scene.GetRegistry().view<PerceptionComponent>().each())
						perception.SelfShading = 0.0f;
			});
	}

	TEST_CASE("Stealth: the level's fog stays out of the hall and the cellar, creeping in at their doorways")
	{
		Level stealth("Scenes/Stealth.pscene");
		const FogMedium medium = FogMedium::FromScene(stealth.SceneInstance);
		CHECK(medium.Clears.size() == 2);
		CHECK(medium.DensityAt({ 10.0f, 1.5f, -12.0f }) == doctest::Approx(0.0f));    // the hall
		CHECK(medium.DensityAt({ -10.25f, 1.5f, -17.0f }) == doctest::Approx(0.0f));  // the cellar
		CHECK(medium.DensityAt({ 10.0f, 1.5f, -4.0f }) == doctest::Approx(medium.Density)); // the yard in front of the hall
		CHECK(medium.Density > 0.0f);
		// In the hall's doorway (the front wall's outer face is at z = -7.7): half a metre in, half the fog.
		CHECK(medium.DensityAt({ 10.0f, 1.0f, -8.2f }) == doctest::Approx(medium.Density * 0.5f));
	}

	TEST_CASE("MoodLab: the sample walls show every material of the first mission; each generated texture loads and has its provenance")
	{
		std::string error;
		REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
		Scene scene;
		REQUIRE_MESSAGE(SceneSerializer::Deserialize(scene, Project::GetAssetDirectory() / "Scenes/MoodLab.pscene", error), error);

		// Tools/BuildDemoProject.py SAMPLE_MATERIALS and SAMPLE_MATERIALS_2: (bay, texture stem).
		const std::vector<std::pair<std::string, std::string>> samples = {
			{ "Planks_Oak", "Textures/Planks/planks_1000" },
			{ "Planks_Tarred", "Textures/Planks/planks_1002" },
			{ "Plaster_Lime", "Textures/Plaster/plaster_1000" },
			{ "Plaster_HalfTimber", "Textures/Plaster/plaster_1001" },
			{ "Metal_Rust", "Textures/Metal/metal_1000" },
			{ "Roof_Thatch", "Textures/Roof/roof_tiles_3001" },
			{ "StoneFloor_Flags", "Textures/StoneFloor/stone_floor_1000" },
			{ "StoneFloor_Wet", "Textures/StoneFloor/stone_floor_2002" },
			{ "Ground_Earth", "Textures/Ground/ground_1000" },
			{ "Brick_Sooty", "Textures/Brick/brick_1001" },
			{ "DressedStone_Sandstone", "Textures/DressedStone/dressed_stone_1000" },
			{ "Rock_Wet", "Textures/Rock/rock_1002" },
			{ "Ossuary_Skulls", "Textures/Ossuary/ossuary_1001" },
			{ "WoodPanel_Oak", "Textures/WoodPanel/wood_panel_1000" },
			{ "WoodPanel_Walnut", "Textures/WoodPanel/wood_panel_1001" },
			{ "Wallpaper_Lilies", "Textures/Wallpaper/wallpaper_2002" },
			{ "PolishedWood_Ebony", "Textures/PolishedWood/polished_wood_1002" },
			{ "Grass_Grounds", "Textures/Grass/grass_1000" },
			{ "Grass_Trodden", "Textures/Grass/grass_1001" },
			{ "Rug_Medallion", "Textures/Rug/rug_2000" },
			{ "Rug_Floral", "Textures/Rug/rug_1001" },
			{ "Tiles_Terracotta", "Textures/CeramicTiles/ceramic_tiles_1000" },
			{ "Tiles_Painted", "Textures/CeramicTiles/ceramic_tiles_1002" },
		};
		for (const auto& [bay, stem] : samples)
		{
			CAPTURE(bay);
			Entity panel = scene.FindEntityByName("Sample_" + bay);
			Entity floor = scene.FindEntityByName("Sample_" + bay + "_Floor");
			REQUIRE(panel);
			REQUIRE(floor);
			const std::string panelAlbedo = panel.GetComponent<MaterialComponent>().AlbedoTexture;
			const std::string floorAlbedo = floor.GetComponent<MaterialComponent>().AlbedoTexture;
			CHECK(((panelAlbedo == stem + "_albedo.png") != (floorAlbedo == stem + "_albedo.png")));  // on the wall or the floor
		}

		// Every generated texture the scene uses (the cloister's and the samples') loads, and its provenance .json sits beside it.
		std::set<std::string> paths;
		for (auto handle : scene.GetRegistry().view<MaterialComponent>())
		{
			const auto& material = scene.GetRegistry().get<MaterialComponent>(handle);
			for (const std::string* path : { &material.AlbedoTexture, &material.NormalTexture, &material.MetallicRoughnessTexture })
				if (!path->empty())
					paths.insert(*path);
		}
		CHECK(paths.size() == 3 * (5 + samples.size()));  // stone x2, cobblestone, shingles, ceiling + the samples
		for (const std::string& path : paths)
		{
			CAPTURE(path);
			const std::filesystem::path file = Project::GetAssetDirectory() / path;
			const auto texture = TextureImporter::LoadFromFile(file, error);
			REQUIRE_MESSAGE(texture.has_value(), error);
			CHECK(texture->Width == 1024);
			const std::string name = file.filename().string();
			const std::filesystem::path provenance = file.parent_path() / (name.substr(0, name.rfind('_')) + ".json");
			CHECK(std::filesystem::exists(provenance));
		}
		Project::Unload();
	}

	TEST_CASE("PhysicsLab: a thief barely made out beside the patrol is reacted to sooner with the suspicion floor")
	{
		// The playtest place "Haze, beside the patrol" (Assets/Playtest/PhysicsLab/HazeSide.lua): the patrol passes him at
		// 4-9 m and makes him out just above the threshold. Seconds until the guard mutters (D 0.3), without the floor
		// and with the default.
		auto timeTo = [](float floor) {
			Level lab("Scenes/PhysicsLab.pscene");
			Entity player = lab.Entity("Player");
			lab.SceneInstance.OnRuntimeStop(); // restart with a still thief in sight; the guard keeps his patrol
			player.RemoveComponent<CharacterControllerComponent>();
			player.RemoveComponent<ScriptComponent>();
			player.GetComponent<PerceptionTargetComponent>().SilhouettePoints = PerceptionTargetComponent().SilhouettePoints;
			player.Transform().Translation = { 13.0f, 0.05f, 0.0f };
			lab.Entity("Guard").GetComponent<PerceptionComponent>().SuspicionFloor = floor;
			lab.SceneInstance.OnRuntimeStart();
			PerceptionComponent& guard = lab.Entity("Guard").GetComponent<PerceptionComponent>();
			const float dt = 1.0f / 60.0f;
			for (float t = 0.0f; t < 15.0f; t += dt)
			{
				lab.SceneInstance.OnUpdateRuntime(Timestep(dt));
				if (guard.RuntimeSuspicion >= 0.3f)
					return t + dt;
			}
			return -1.0f;
		};
		const float without = timeTo(0.0f), with = timeTo(0.25f);
		MESSAGE("guard mutters after " << without << " s without the floor, " << with << " s with it");
		REQUIRE(without > 0.0f);
		CHECK(with > 0.0f);
		CHECK(with < without - 1.0f);
	}

	TEST_CASE("Suspicion tempo report: detection times in every level with guards, before and after" * doctest::skip())
	{
		// PenumbraTests -tc="Suspicion tempo report*" --no-skip (release): SuspicionFloor 0, then each guard's own value.
		DetectionReport({ "Scenes/AILab.pscene", "Scenes/Cloister.pscene", "Scenes/Stealth.pscene", "Scenes/PhysicsLab.pscene",
							"Scenes/MoodLab.pscene", "Scenes/PerceptionLab.pscene" },
			[](Scene& scene, bool after) {
				if (!after)
					for (auto [handle, perception] : scene.GetRegistry().view<PerceptionComponent>().each())
						perception.SuspicionFloor = 0.0f;
			});
	}

	TEST_CASE("No lamp in the demo's scenes sits in or on a solid body (it would light nothing on that side, for the eye or the guards)")
	{
		std::string error;
		REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
		int lamps = 0;
		for (const auto& file : std::filesystem::directory_iterator(Project::GetAssetDirectory() / "Scenes"))
		{
			if (file.path().extension() != ".pscene")
				continue;
			CAPTURE(file.path().filename().string());
			Scene scene;
			scene.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(scene, file.path(), error), error);
			scene.OnRuntimeStart();
			const PhysicsWorld& physics = *scene.GetPhysicsWorld();
			auto check = [&](entt::entity handle) {
				Entity light(handle, &scene);
				const glm::vec3 position = scene.GetWorldPosition(light);
				// 5 cm of clearance all round: a torch's flame 8 cm over its pole is fine, a lamp on a post's top is not.
				const std::optional<RaycastHit> hit = physics.SphereCast(position, { 0.0f, -1.0f, 0.0f }, 0.05f, 0.001f);
				CAPTURE(light.GetName());
				CAPTURE(position);
				CHECK_FALSE(hit.has_value());
				++lamps;
			};
			for (entt::entity handle : scene.GetRegistry().view<PointLightComponent>())
				check(handle);
			for (entt::entity handle : scene.GetRegistry().view<SpotLightComponent>())
				check(handle);
			scene.OnRuntimeStop();
		}
		CHECK(lamps > 100);
		Project::Unload();
	}

	TEST_CASE("Cloister: the crossbow shoots only while aimed (right mouse); the compass follows the camera")
	{
		Level cloister("Scenes/Cloister.pscene");
		cloister.Run(0.3f);
		auto bolts = [&]() {
			return cloister.Lua("local n = 0; for _, e in ipairs(scene.entities_with('StimSource')) do "
								"if e:get_name() == 'WaterArrow' then n = n + 1 end end; return n");
		};
		Input::SetMouseButtonState(Mouse::ButtonLeft, true);
		cloister.Run(1.0f / 60.0f);
		Input::SetMouseButtonState(Mouse::ButtonLeft, false);
		cloister.Run(0.1f);
		CHECK(bolts() == "0"); // a plain click does nothing yet

		Input::SetMouseButtonState(Mouse::ButtonRight, true);
		cloister.Run(0.1f);
		CHECK(cloister.Lua("return scene.find('Player'):script().aiming") == "true");
		Input::SetMouseButtonState(Mouse::ButtonLeft, true);
		cloister.Run(1.0f / 60.0f);
		Input::SetMouseButtonState(Mouse::ButtonLeft, false);
		Input::SetMouseButtonState(Mouse::ButtonRight, false);
		cloister.Run(1.0f / 60.0f);
		CHECK(bolts() == "1");

		// Bearings clockwise from north (-Z): the player starts facing west, down the wing.
		CHECK(cloister.Lua("local C = require('Scripts/Compass'); return math.floor(C.heading(scene.find('Player'):script().yaw) + 0.5)") ==
			  "270");
		CHECK(cloister.Lua("local C = require('Scripts/Compass'); return C.heading(0) == 0 and C.heading(-90) == 90 and C.heading(180) == 180") ==
			  "true");
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Cloister: a bolt shoves a limp body; guards see the body by its bones, less of it behind a crate")
	{
		Level cloister("Scenes/Cloister.pscene");
		cloister.Run(1.0f);
		REQUIRE(cloister.Lua("return ai.knock_out(scene.find('GuardNorth'))") == "true");
		Entity body = cloister.Entity("GuardNorth");
		for (int step = 0; step < 150 && body.GetComponent<RagdollComponent>().RuntimeAwake; ++step)
			cloister.Run(0.1f);
		REQUIRE_FALSE(body.GetComponent<RagdollComponent>().RuntimeAwake);

		// Seven silhouette points on the body's parts, all low: it lies on the floor.
		const PerceptionTargetComponent& target = body.GetComponent<PerceptionTargetComponent>();
		CHECK(target.Kind == TargetKind::Body);
		REQUIRE(target.SilhouettePoints.size() == 7);
		for (const glm::vec3& point : target.SilhouettePoints)
			CHECK(point.y < 0.6f);

		// Seen down the north corridor by the other guard, then with a waist-high crate in between.
		auto visible = [&]() {
			cloister.Lua("local g = scene.find('GuardSouth'); g:set_position(vec3(4, 0.05, -9.6)); g:set_rotation(vec3(0, -90, 0))");
			return cloister.SceneInstance.GetPerceptionSystem()->ComputeVisibility(cloister.Entity("GuardSouth"), body).VisiblePoints;
		};
		const uint32_t open = visible();
		CHECK(open >= 4);
		cloister.Lua("local c = scene.create('Crate'); c:set_position(vec3(7.5, 0.5, -9.6)); c:add('BoxCollider', { HalfExtents = vec3(0.3, 0.5, 1.5) })");
		cloister.Run(1.0f / 60.0f); // its body exists after a physics step
		CHECK(visible() < open);

		// A bolt into the body from the corridor shoves it along its flight (moved out of the corner
		// first, so there is no wall behind it).
		cloister.Lua("scene.find('GuardNorth'):set_position(vec3(2, 0.05, -9.6))");
		cloister.Run(1.5f);
		auto worldPoints = [&]() {
			std::vector<glm::vec3> points;
			const glm::mat4 world = cloister.SceneInstance.GetWorldTransform(body);
			for (const glm::vec3& point : body.GetComponent<PerceptionTargetComponent>().SilhouettePoints)
				points.push_back(glm::vec3(world * glm::vec4(point, 1.0f)));
			return points;
		};
		const std::vector<glm::vec3> limbsBefore = worldPoints();
		cloister.Lua("local g = scene.find('GuardNorth'):get_position(); local a = scene.instantiate('Prefabs/WaterArrow.pprefab', g + vec3(-1.5, 0.25, 0)); "
					 "a:look_at(g + vec3(0, 0.15, 0)); _G.test_bolt = a");
		cloister.Run(1.0f / 60.0f);
		cloister.Lua("physics.set_velocity(_G.test_bolt, (scene.find('GuardNorth'):get_position() + vec3(0, 0.15, 0) - _G.test_bolt:get_position()):normalized() * 32)");
		cloister.Run(1.0f);
		const std::vector<glm::vec3> limbsAfter = worldPoints();
		float moved = 0.0f, pushedAway = 0.0f;
		for (size_t i = 0; i < limbsBefore.size(); ++i)
		{
			if (glm::distance(limbsAfter[i], limbsBefore[i]) > moved)
			{
				moved = glm::distance(limbsAfter[i], limbsBefore[i]);
				pushedAway = limbsAfter[i].x - limbsBefore[i].x;
			}
		}
		CHECK(moved > 0.15f);     // the struck limb swung
		CHECK(pushedAway > 0.0f); // away from the shooter
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Cloister: the two guards walking the ring opposite ways meet and pass each other")
	{
		Level cloister("Scenes/Cloister.pscene");
		float together = 0.0f, longest = 0.0f, closest = 100.0f, travelledNorth = 0.0f, travelledSouth = 0.0f;
		glm::vec3 lastNorth = cloister.Position("GuardNorth"), lastSouth = cloister.Position("GuardSouth");
		for (int step = 0; step < 90 * 10; ++step) // 90 s: they meet several times
		{
			cloister.Run(0.1f);
			const glm::vec3 north = cloister.Position("GuardNorth"), south = cloister.Position("GuardSouth");
			const float distance = glm::length(glm::vec2(north.x - south.x, north.z - south.z));
			closest = std::min(closest, distance);
			together = distance < 1.0f ? together + 0.1f : 0.0f; // a stretch of standing chest to chest
			longest = std::max(longest, together);
			travelledNorth += glm::distance(north, lastNorth);
			travelledSouth += glm::distance(south, lastSouth);
			lastNorth = north;
			lastSouth = south;
		}
		CHECK(closest < 3.0f);    // they did meet
		CHECK(longest < 5.0f);    // a pause together at a shared corner, never a deadlock (they used to circle it for good)
		CHECK(travelledNorth > 60.0f);
		CHECK(travelledSouth > 60.0f);
		CHECK(cloister.SceneInstance.GetScriptEngine()->GetErrors().empty());
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

	TEST_CASE("Listening: the wind plays, the doors creak in turn, the floors sound; every clip the game's noises name loads")
	{
		Level listening("Scenes/Listening.pscene");
		listening.Run(0.5f);
		CHECK(listening.Lua("return audio.is_playing(scene.find('Wind'))") == "true");
		CHECK(listening.Lua("return audio.is_playing(scene.find('Hearth'))") == "true");
		listening.Lua("scene.find('Listening'):script():toggle_rain()");
		CHECK(listening.Lua("return audio.is_playing(scene.find('Rain')) and not audio.is_playing(scene.find('Wind'))") == "true");
		listening.Lua("scene.find('Listening'):script():toggle_rain()");
		CHECK(listening.Lua("return audio.is_playing(scene.find('Wind')) and not audio.is_playing(scene.find('Rain'))") == "true");
		CHECK(listening.Lua("return scene.find('Door_3m'):script():is_open()") == "false");

		// The cycle: every 4 s a door sound, each door opened and shut before the next.
		listening.Run(4.0f);
		CHECK(listening.Lua("return scene.find('Door_3m'):script():is_open()") == "true");
		listening.Run(4.0f);
		CHECK(listening.Lua("return scene.find('Door_3m'):script():is_open()") == "false");
		listening.Run(4.0f);
		CHECK(listening.Lua("return scene.find('Door_10m'):script():is_open()") == "true");
		CHECK(listening.Lua("return scene.find('Listening'):script().worked") == "3");

		// Walking the strip: each floor under the thief is the one its lamp names.
		for (const char* floor : { "Stone", "Wood", "Metal", "Gravel", "Carpet", "Grass", "Moss" })
		{
			CAPTURE(floor);
			const glm::vec3 lamp = listening.Position(std::string("Lamp_") + floor);
			listening.Lua("scene.find('Player'):set_position(vec3(0, 0.05, " + std::to_string(lamp.z) + "))");
			listening.Run(0.2f);
			CHECK(listening.Lua("return (require('Scripts/AI/Noises').surface_under(scene.find('Player')))") ==
				  std::string("\"") + floor + "\"");
		}

		// A list of variants: each play picks one of them (the scene's seeded math.random), all of them in time.
		CHECK(listening.Lua("local N = require('Scripts/AI/Noises'); local seen = {}; "
								"for i = 1, 60 do seen[N.clip({ 'a', 'b', 'c' })] = true end; "
								"return N.clip('x') == 'x' and seen.a and seen.b and seen.c and N.clip({ 'y' }) == 'y'") == "true");

		// Every clip Noises.lua names (a clip or a list of generated variants) decodes and plays.
		const std::string failed = listening.Lua(R"(
			local Noises = require('Scripts/AI/Noises')
			local clips, failed = {}, {}
			local function add(clip)
				if type(clip) == 'table' then for _, c in ipairs(clip) do clips[c] = true end
				elseif clip then clips[clip] = true end
			end
			for _, clip in pairs(Noises.footstep_clips) do add(clip) end
			for _, sound in pairs(Noises.sounds) do add(sound.clip) end
			add(Noises.arrow.clip)
			for clip in pairs(clips) do
				if not audio.play_at(clip, vec3(0, 0, 0), 0.0) then table.insert(failed, clip) end
			end
			return table.concat(failed, ','))");
		CHECK(failed == "\"\"");
		CHECK(listening.SceneInstance.GetScriptEngine()->GetErrors().empty());
	}

	TEST_CASE("Generated sounds: every one in the demo has its provenance, accepted by the owner (Tools/AudioGen)")
	{
		const std::filesystem::path folder = std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "Assets/Audio/Generated";
		if (!std::filesystem::exists(folder))
			return;
		for (const auto& file : std::filesystem::directory_iterator(folder))
		{
			if (file.path().extension() != ".wav")
				continue;
			CAPTURE(file.path().string());
			std::filesystem::path provenance = file.path();
			provenance.replace_extension(".json");
			REQUIRE(std::filesystem::exists(provenance));
			std::ifstream stream(provenance);
			const nlohmann::json data = nlohmann::json::parse(stream, nullptr, false);
			REQUIRE_FALSE(data.is_discarded());
			CHECK(data.value("ai_generated", false));
			CHECK(data["owner"].value("decision", "").starts_with("accepted")); // "accepted", or "accepted as Footstep_Gravel"
			CHECK_FALSE(data.value("model", "").empty());
			CHECK_FALSE(data.value("license", "").empty());
			CHECK(data["checks"].value("passed", false));
		}
	}
}
