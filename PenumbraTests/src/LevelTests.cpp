#include "TestHelpers.h"

#include "Penumbra/Animation/Animation.h"
#include "Penumbra/Asset/AssetManager.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/AI/PerceptionSystem.h"
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
}
