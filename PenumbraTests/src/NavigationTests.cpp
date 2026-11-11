#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Navigation/NavMesh.h"
#include "Penumbra/Physics/PhysicsWorld.h"

#include <sstream>

using namespace Penumbra;

namespace {

	// Appends an axis-aligned box as 12 counter-clockwise (outward facing) triangles.
	void AddBox(std::vector<glm::vec3>& vertices, std::vector<uint32_t>& indices, const glm::vec3& center, const glm::vec3& halfExtents)
	{
		const uint32_t base = static_cast<uint32_t>(vertices.size());
		for (int i = 0; i < 8; ++i)
		{
			const glm::vec3 sign((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
			vertices.push_back(center + sign * halfExtents);
		}
		// Corner index bits: x = 1, y = 2, z = 4.
		constexpr uint32_t faces[6][4] = {
			{ 2, 6, 7, 3 }, // +y
			{ 0, 1, 5, 4 }, // -y
			{ 1, 3, 7, 5 }, // +x
			{ 0, 4, 6, 2 }, // -x
			{ 4, 5, 7, 6 }, // +z
			{ 0, 2, 3, 1 }, // -z
		};
		for (const auto& face : faces)
		{
			for (uint32_t index : { face[0], face[1], face[2], face[0], face[2], face[3] })
				indices.push_back(base + index);
		}
	}

	// 20 x 20 m floor with a 16 m wall across the middle (gaps of 2 m at both ends).
	Scope<NavMesh> BuildWalledFloor(bool closeGaps = false)
	{
		std::vector<glm::vec3> vertices;
		std::vector<uint32_t> indices;
		AddBox(vertices, indices, { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f });
		AddBox(vertices, indices, { 0.0f, 1.5f, 0.0f }, { closeGaps ? 10.5f : 8.0f, 1.5f, 0.2f });
		Scope<NavMesh> navMesh = CreateScope<NavMesh>();
		std::string error;
		REQUIRE_MESSAGE(navMesh->Build(vertices, indices, {}, error), error);
		return navMesh;
	}

	struct NavLab
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;
		Entity Agent;

		NavLab()
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			Test::CreateBox(SceneInstance, "Ground", { 0.0f, -0.5f, 0.0f }, { 10.0f, 0.5f, 10.0f });
			Test::CreateBox(SceneInstance, "Wall", { 0.0f, 1.5f, 0.0f }, { 8.0f, 1.5f, 0.2f });
			SceneInstance.CreateEntity("Navigation").AddComponent<NavMeshComponent>();
			Agent = SceneInstance.CreateEntity("Agent");
			Agent.Transform().Translation = { 0.0f, 0.05f, 5.0f };
			Agent.AddComponent<CharacterControllerComponent>();
		}

		NavigationSystem& Navigation() { return *SceneInstance.GetNavigationSystem(); }
		NavAgentComponent& AgentState() { return Agent.GetComponent<NavAgentComponent>(); }
	};

}

TEST_SUITE("Navigation")
{
	TEST_CASE("NavMesh paths go around walls and report their length")
	{
		Scope<NavMesh> navMesh = BuildWalledFloor();
		CHECK(navMesh->GetPolygonCount() > 0);

		const glm::vec3 from(0.0f, 0.0f, 5.0f), to(0.0f, 0.0f, -5.0f);
		std::optional<NavPath> path = navMesh->FindPath(from, to);
		REQUIRE(path.has_value());
		CHECK(path->Complete);
		CHECK(path->Points.size() > 2); // a straight line would cross the wall
		CHECK(path->Length > 2.0f * std::sqrt(8.0f * 8.0f + 5.0f * 5.0f) - 1.0f); // via a gap at x = +-9
		CHECK(Test::Near(path->Points.back(), to, 0.2f));
		for (const glm::vec3& point : path->Points)
			CHECK(std::abs(point.y) < 0.3f); // on the floor, never on top of the wall

		// Points inside the wall or far above the floor are off the navmesh.
		CHECK_FALSE(navMesh->ClosestPoint({ 0.0f, 10.0f, 0.0f }).has_value());
		CHECK(navMesh->ClosestPoint({ 3.0f, 0.5f, 3.0f }).has_value());
	}

	TEST_CASE("Unreachable goals give a partial path to the closest point")
	{
		Scope<NavMesh> navMesh = BuildWalledFloor(true);
		std::optional<NavPath> path = navMesh->FindPath({ 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -5.0f });
		REQUIRE(path.has_value());
		CHECK_FALSE(path->Complete);
		CHECK(path->Points.back().z > 0.0f); // stops on the near side of the wall
	}

	TEST_CASE("Random navmesh points are reachable, inside the radius and deterministic")
	{
		Scope<NavMesh> navMesh = BuildWalledFloor();
		uint32_t stateA = 42, stateB = 42;
		for (int i = 0; i < 20; ++i)
		{
			std::optional<glm::vec3> a = navMesh->RandomPointAround({ 4.0f, 0.0f, 5.0f }, 3.0f, stateA);
			std::optional<glm::vec3> b = navMesh->RandomPointAround({ 4.0f, 0.0f, 5.0f }, 3.0f, stateB);
			REQUIRE(a.has_value());
			REQUIRE(b.has_value());
			CHECK(Test::Near(*a, *b));
			CHECK(glm::distance(glm::vec2(a->x, a->z), glm::vec2(4.0f, 5.0f)) <= 3.0f);
			CHECK(navMesh->FindPath({ 4.0f, 0.0f, 5.0f }, *a).value_or(NavPath{ {}, false }).Complete);
		}
	}

	TEST_CASE("Empty or invalid input fails with a message")
	{
		NavMesh navMesh;
		std::string error;
		CHECK_FALSE(navMesh.Build({}, {}, {}, error));
		CHECK_FALSE(error.empty());
		CHECK_FALSE(navMesh.IsValid());
		CHECK_FALSE(navMesh.FindPath({}, { 1.0f, 0.0f, 0.0f }).has_value());
	}

	TEST_CASE("Agents walk around obstacles to their destination")
	{
		NavLab lab;
		lab.SceneInstance.OnRuntimeStart();
		CHECK(lab.SceneInstance.FindEntityByName("Navigation").GetComponent<NavMeshComponent>().RuntimePolygons > 0);

		const glm::vec3 destination(0.0f, 0.0f, -5.0f);
		REQUIRE(lab.Navigation().MoveTo(lab.Agent, destination, 3.0f));
		CHECK(lab.AgentState().RuntimeStatus == NavStatus::Moving);
		for (int i = 0; i < 60 * 15 && lab.AgentState().RuntimeStatus == NavStatus::Moving; ++i)
			lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));

		CHECK(lab.AgentState().RuntimeStatus == NavStatus::Arrived);
		const glm::vec3 position = lab.SceneInstance.GetWorldPosition(lab.Agent);
		CHECK(glm::distance(glm::vec2(position.x, position.z), glm::vec2(destination.x, destination.z)) < 0.5f);
		// It faces the way it walked (towards -Z at the end).
		CHECK(Math::Forward(lab.SceneInstance.GetWorldRotation(lab.Agent)).z < 0.0f);
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Destroying a static obstacle rebuilds the navmesh; character.move style steering takes over")
	{
		NavLab lab;
		// Close both gaps: the far side is unreachable until the plug is destroyed.
		Entity plugA = Test::CreateBox(lab.SceneInstance, "PlugA", { 9.1f, 1.5f, 0.0f }, { 0.95f, 1.5f, 0.2f });
		Test::CreateBox(lab.SceneInstance, "PlugB", { -9.1f, 1.5f, 0.0f }, { 0.95f, 1.5f, 0.2f });
		lab.SceneInstance.OnRuntimeStart();

		NavigationSystem& navigation = lab.Navigation();
		std::optional<NavPath> blocked = navigation.FindPath({ 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -5.0f });
		REQUIRE(blocked.has_value());
		CHECK_FALSE(blocked->Complete);

		lab.SceneInstance.DestroyEntity(plugA);
		Test::Simulate(lab.SceneInstance, 0.1f);
		std::optional<NavPath> open = navigation.FindPath({ 0.0f, 0.0f, 5.0f }, { 0.0f, 0.0f, -5.0f });
		REQUIRE(open.has_value());
		CHECK(open->Complete);

		// Stop hands the character back: no more desired velocity from navigation.
		REQUIRE(navigation.MoveTo(lab.Agent, { 0.0f, 0.0f, -5.0f }));
		Test::Simulate(lab.SceneInstance, 0.2f);
		navigation.Stop(lab.Agent);
		CHECK(lab.AgentState().RuntimeStatus == NavStatus::Idle);
		CHECK(glm::length(lab.Agent.GetComponent<CharacterControllerComponent>().RuntimeDesiredVelocity) == 0.0f);

		// A destination nowhere near the navmesh fails cleanly.
		CHECK_FALSE(navigation.MoveTo(lab.Agent, { 0.0f, 50.0f, 0.0f }));
		CHECK(lab.AgentState().RuntimeStatus == NavStatus::Failed);
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Sound travels around walls along the navmesh")
	{
		NavLab lab;
		// The guard stands behind the wall, 5 m from its middle; the gap is at x = 8..10.
		Entity guard = lab.SceneInstance.CreateEntity("Guard");
		guard.Transform().Translation = { 6.0f, 0.0f, -2.0f };
		guard.AddComponent<PerceptionComponent>();
		lab.SceneInstance.OnRuntimeStart();
		PerceptionSystem& perception = *lab.SceneInstance.GetPerceptionSystem();

		const glm::vec3 noise(6.0f, 0.1f, 2.0f);
		const float straight = glm::distance(noise, glm::vec3(6.0f, 1.65f, -2.0f));
		const float throughWall = PerceptionModel::HeardLevel(7.0f, straight) - PerceptionModel::WallLoss;
		std::optional<NavPath> path = lab.Navigation().FindPath(noise, { 6.0f, 0.0f, -2.0f });
		REQUIRE(path.has_value());
		const float aroundCorner = PerceptionModel::HeardLevel(7.0f, path->Length) - PerceptionModel::CornerLoss;
		REQUIRE(aroundCorner > throughWall);

		CHECK(Test::Near(perception.HearingLevel(guard, noise, 7.0f), aroundCorner, 0.05f));
		// The far end of the wall (the gap is on the other side) is reached only through the wall.
		const glm::vec3 farNoise(-6.0f, 0.1f, 2.0f);
		CHECK(perception.HearingLevel(guard, farNoise, 7.0f) < perception.HearingLevel(guard, noise, 7.0f));
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Static triangles come from solid static bodies only")
	{
		Test::TemporaryProject project;
		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		Test::CreateBox(scene, "Static", { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
		Entity trigger = Test::CreateBox(scene, "Trigger", { 5.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
		trigger.AddComponent<RigidBodyComponent>().IsTrigger = true;
		Entity crate = Test::CreateBox(scene, "Crate", { -5.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });
		crate.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Dynamic;
		scene.OnRuntimeStart();

		std::vector<glm::vec3> vertices;
		std::vector<uint32_t> indices;
		scene.GetPhysicsWorld()->CollectStaticTriangles(vertices, indices);
		REQUIRE(indices.size() >= 36);
		for (const glm::vec3& vertex : vertices)
			CHECK(std::abs(vertex.x) <= 1.01f);
		scene.OnRuntimeStop();
	}

	TEST_CASE("Offset and compound static colliders feed the navmesh too")
	{
		Test::TemporaryProject project;
		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		// A capsule standing on its feet (offset collider) and a box + sphere compound.
		Entity capsule = scene.CreateEntity("Capsule");
		capsule.Transform().Translation = { 5.0f, 0.0f, 0.0f };
		auto& shape = capsule.AddComponent<CapsuleColliderComponent>();
		shape.Offset = { 0.0f, 0.9f, 0.0f };
		Entity compound = scene.CreateEntity("Compound");
		compound.Transform().Translation = { -5.0f, 0.0f, 0.0f };
		compound.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.5f);
		compound.AddComponent<SphereColliderComponent>().Offset = { 0.0f, 1.0f, 0.0f };
		scene.OnRuntimeStart();

		std::vector<glm::vec3> vertices;
		std::vector<uint32_t> indices;
		scene.GetPhysicsWorld()->CollectStaticTriangles(vertices, indices);
		bool capsuleTop = false, sphereTop = false;
		for (const glm::vec3& vertex : vertices)
		{
			capsuleTop |= vertex.x > 4.0f && vertex.y > 1.7f; // the offset lifts the capsule to 0..1.8 m
			sphereTop |= vertex.x < -4.0f && vertex.y > 1.4f;
		}
		CHECK(capsuleTop);
		CHECK(sphereTop);
		scene.OnRuntimeStop();
	}
}

#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

TEST_SUITE("Navigation")
{
	// End to end on the real demo content: Scripts/Guard.lua on the AILab level.
	TEST_CASE("The AI lab guard patrols, checks a noise for as long as it was loud, then goes back to his route")
	{
		std::string error;
		REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
		{
			Scene scene;
			scene.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(scene, Project::GetAssetDirectory() / "Scenes/AILab.pscene", error), error);
			// Nobody to see: only what the guard hears matters here.
			scene.FindEntityByName("Player").GetComponent<PerceptionTargetComponent>().SilhouettePoints.clear();
			scene.OnRuntimeStart();

			Entity guard = scene.FindEntityByName("Guard");
			REQUIRE(guard);
			const auto& perception = guard.GetComponent<PerceptionComponent>();
			const float dt = 1.0f / 60.0f;
			float still = 0.0f, longestStill = 0.0f;
			glm::vec3 previous = scene.GetWorldPosition(guard);
			auto step = [&](float seconds) {
				for (float t = 0.0f; t < seconds; t += dt)
				{
					scene.OnUpdateRuntime(Timestep(dt));
					const glm::vec3 position = scene.GetWorldPosition(guard);
					still = glm::distance(position, previous) < 0.1f * dt ? still + dt : 0.0f;
					longestStill = std::max(longestStill, still);
					previous = position;
				}
			};

			// Patrol: he walks the route.
			const glm::vec3 start = scene.GetWorldPosition(guard);
			step(12.0f);
			CHECK(perception.RuntimeState == AwarenessState::Unaware);
			CHECK(glm::distance(start, scene.GetWorldPosition(guard)) > 5.0f);

			// A loud clang a few metres away: he goes to look.
			const glm::vec3 guardPosition = scene.GetWorldPosition(guard);
			const std::optional<glm::vec3> noise = scene.GetNavigationSystem()->RandomPoint(guardPosition, 6.0f);
			REQUIRE(noise.has_value());
			scene.GetPerceptionSystem()->EmitNoise(*noise + glm::vec3(0.0f, 0.1f, 0.0f), 7.0f, UUID::Invalid, "Clang");
			step(0.1f);
			const float strength = perception.RuntimeReactionStrength;
			CHECK(strength > 3.0f);
			CHECK(perception.RuntimeStimulusCause == "Clang");
			float closest = glm::distance(scene.GetWorldPosition(guard), *noise);
			float busy = 0.0f;
			while (perception.RuntimeState != AwarenessState::Unaware && busy < 120.0f)
			{
				step(0.5f);
				busy += 0.5f;
				closest = std::min(closest, glm::distance(scene.GetWorldPosition(guard), *noise));
			}
			CHECK(closest < 1.5f); // he went there
			// Busy for as long as the noise was strong: 90 s * (s/10)^2.
			const float expected = 90.0f * (strength / 10.0f) * (strength / 10.0f);
			CHECK(busy > expected - 1.0f);
			CHECK(busy < expected + 1.0f);

			// Then back to the route, never standing around for long at any point.
			const glm::vec3 calm = scene.GetWorldPosition(guard);
			step(10.0f);
			CHECK(perception.RuntimeState == AwarenessState::Unaware);
			CHECK(glm::distance(calm, scene.GetWorldPosition(guard)) > 3.0f);
			MESSAGE("reaction strength " << strength << ", busy " << busy << " s (expected " << expected << "), longest still " << longestStill
										 << " s");
			CHECK(longestStill < 8.0f);
			CHECK(scene.GetScriptEngine()->GetErrors().empty()); // guard, player (footsteps, landing) and HUD scripts
			scene.OnRuntimeStop();
		}
		Project::Unload();
	}
}

TEST_SUITE("Navigation")
{
	// Level design check: a waypoint behind a blocked door leaves a guard stuck at the doorway.
	TEST_CASE("Every patrol route in the demo levels is walkable end to end")
	{
		std::string error;
		REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
		for (const char* scenePath : { "Scenes/Stealth.pscene", "Scenes/AILab.pscene" })
		{
			CAPTURE(scenePath);
			Scene scene;
			scene.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(scene, Project::GetAssetDirectory() / scenePath, error), error);
			scene.OnRuntimeStart();
			int routes = 0;
			for (auto [handle, script] : scene.GetRegistry().view<ScriptComponent>().each())
			{
				const auto property = script.Properties.find("waypoints");
				if ((script.Script != "Scripts/Guard.lua" && script.Script != "Scripts/Servant.lua") || property == script.Properties.end())
					continue;
				std::vector<glm::vec3> points;
				std::stringstream names(std::get<std::string>(property->second));
				for (std::string name; std::getline(names, name, ',');)
				{
					Entity waypoint = scene.FindEntityByName(name);
					REQUIRE_MESSAGE(waypoint, name);
					points.push_back(scene.GetWorldPosition(waypoint));
				}
				for (size_t i = 0; i < points.size(); ++i)
				{
					const glm::vec3& from = points[i];
					const glm::vec3& to = points[(i + 1) % points.size()];
					std::optional<NavPath> path = scene.GetNavigationSystem()->FindPath(from, to);
					CAPTURE(i);
					REQUIRE(path.has_value());
					CHECK(path->Complete);
					CHECK(glm::distance(glm::vec2(path->Points.back().x, path->Points.back().z), glm::vec2(to.x, to.z)) < 0.5f);
				}
				routes++;
			}
			CHECK(routes > 0);
			scene.OnRuntimeStop();
		}
		Project::Unload();
	}
}
