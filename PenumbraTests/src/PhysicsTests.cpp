#include "TestHelpers.h"

#include "Penumbra/Physics/PhysicsWorld.h"

using namespace Penumbra;

namespace {

	struct PhysicsFixture
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;
		Entity Ground;

		PhysicsFixture()
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			Ground = Test::CreateBox(SceneInstance, "Ground", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f });
		}

		Entity CreateDynamicBall(const glm::vec3& position)
		{
			Entity ball = SceneInstance.CreateEntity("Ball");
			ball.Transform().Translation = position;
			ball.AddComponent<SphereColliderComponent>().Radius = 0.5f;
			auto& body = ball.AddComponent<RigidBodyComponent>();
			body.Type = RigidBodyType::Dynamic;
			body.Mass = 2.0f;
			return ball;
		}
	};

}

TEST_SUITE("Physics")
{
	TEST_CASE("Dynamic bodies fall and rest on static colliders")
	{
		PhysicsFixture fixture;
		Entity ball = fixture.CreateDynamicBall({ 0.0f, 5.0f, 0.0f });
		fixture.SceneInstance.OnRuntimeStart();
		PhysicsWorld* physics = fixture.SceneInstance.GetPhysicsWorld();
		REQUIRE(physics);
		CHECK(physics->HasBody(ball.GetUUID()));
		CHECK(physics->HasBody(fixture.Ground.GetUUID()));

		Test::Simulate(fixture.SceneInstance, 0.5f);
		CHECK(ball.Transform().Translation.y < 4.0f);
		Test::Simulate(fixture.SceneInstance, 3.0f);
		CHECK(Test::Near(ball.Transform().Translation.y, 0.5f, 0.05f));
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Raycasts report entity, point, normal and respect filters")
	{
		PhysicsFixture fixture;
		Entity wall = Test::CreateBox(fixture.SceneInstance, "Wall", { 0.0f, 1.0f, -5.0f }, { 2.0f, 1.0f, 0.1f });
		fixture.SceneInstance.OnRuntimeStart();
		PhysicsWorld& physics = *fixture.SceneInstance.GetPhysicsWorld();

		std::optional<RaycastHit> hit = physics.Raycast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f);
		REQUIRE(hit);
		CHECK(hit->Entity == wall.GetUUID());
		CHECK(Test::Near(hit->Distance, 4.9f, 0.01f));
		CHECK(Test::Near(hit->Normal, { 0.0f, 0.0f, 1.0f }, 0.01f));

		const UUID wallID = wall.GetUUID();
		CHECK_FALSE(physics.Raycast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 100.0f, [&](UUID e) { return e != wallID; }).has_value());
		CHECK_FALSE(physics.Raycast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 3.0f).has_value());
		CHECK_FALSE(physics.Raycast({ 0.0f, 1.0f, 0.0f }, glm::vec3(0.0f), 3.0f).has_value());

		const std::vector<UUID> overlaps = physics.OverlapSphere({ 0.0f, 1.0f, -5.0f }, 0.5f);
		CHECK(std::find(overlaps.begin(), overlaps.end(), wall.GetUUID()) != overlaps.end());
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Sphere casts are thick rays: they stop a radius short of a wall and do not slip through narrow gaps")
	{
		PhysicsFixture fixture;
		Entity wall = Test::CreateBox(fixture.SceneInstance, "Wall", { 0.0f, 1.0f, -5.0f }, { 2.0f, 1.0f, 0.1f });
		// Two posts with a 0.6 m gap centred on x = 4.
		Test::CreateBox(fixture.SceneInstance, "PostA", { 3.4f, 1.0f, -5.0f }, { 0.3f, 1.0f, 0.1f });
		Test::CreateBox(fixture.SceneInstance, "PostB", { 4.6f, 1.0f, -5.0f }, { 0.3f, 1.0f, 0.1f });
		fixture.SceneInstance.OnRuntimeStart();
		PhysicsWorld& physics = *fixture.SceneInstance.GetPhysicsWorld();

		std::optional<RaycastHit> hit = physics.SphereCast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.25f, 100.0f);
		REQUIRE(hit);
		CHECK(hit->Entity == wall.GetUUID());
		CHECK(Test::Near(hit->Distance, 4.65f, 0.02f)); // the centre stops a radius before the face at z = -4.9
		CHECK(Test::Near(hit->Point.z, -4.9f, 0.02f));
		CHECK(Test::Near(hit->Normal, { 0.0f, 0.0f, 1.0f }, 0.05f));

		// A ray threads the gap, a sphere wider than it does not.
		CHECK_FALSE(physics.Raycast({ 4.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 10.0f).has_value());
		CHECK(physics.SphereCast({ 4.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.4f, 10.0f).has_value());
		CHECK_FALSE(physics.SphereCast({ 4.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.2f, 10.0f).has_value());

		// Starting against the wall: distance 0. Filters, short casts and degenerate input miss.
		std::optional<RaycastHit> touching = physics.SphereCast({ 0.0f, 1.0f, -4.8f }, { 0.0f, 0.0f, -1.0f }, 0.25f, 1.0f);
		REQUIRE(touching);
		CHECK(touching->Distance < 0.01f);
		const UUID wallID = wall.GetUUID();
		CHECK_FALSE(physics.SphereCast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.25f, 100.0f, [&](UUID e) { return e != wallID; }).has_value());
		CHECK_FALSE(physics.SphereCast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.25f, 3.0f).has_value());
		CHECK_FALSE(physics.SphereCast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 0.0f, 10.0f).has_value());
		CHECK_FALSE(physics.SphereCast({ 0.0f, 1.0f, 0.0f }, glm::vec3(0.0f), 0.25f, 10.0f).has_value());
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Ragdoll hinges turn one way only: positive about the axis by the right-hand rule")
	{
		// A root high up (no gravity) and a shin hanging from it on a hinge about +X that may only turn
		// positive (0..90 degrees): a positive turn about +X swings the hanging part towards -Z.
		auto swing = [](float push) {
			PhysicsFixture fixture;
			fixture.SceneInstance.OnRuntimeStart();
			PhysicsWorld& physics = *fixture.SceneInstance.GetPhysicsWorld();
			physics.SetGravity(glm::vec3(0.0f));
			std::vector<RagdollPartDesc> parts(2);
			parts[0].Position = { 0.0f, 3.0f, 0.0f };
			parts[0].Radius = 0.1f;
			parts[0].HalfHeight = 0.2f;
			parts[0].Mass = 10.0f; // Stabilize() evens out large mass ratios between parts
			parts[1].Parent = 0;
			parts[1].Position = { 0.0f, 2.3f, 0.0f };
			parts[1].Radius = 0.08f;
			parts[1].HalfHeight = 0.2f;
			parts[1].Mass = 3.0f;
			parts[1].JointPosition = { 0.0f, 2.6f, 0.0f };
			parts[1].TwistAxis = { 0.0f, -1.0f, 0.0f };
			parts[1].Hinge = true;
			parts[1].HingeAxis = { 1.0f, 0.0f, 0.0f };
			parts[1].HingeMinDegrees = 0.0f;
			parts[1].HingeMaxDegrees = 90.0f;
			std::string error;
			const UUID owner(42);
			REQUIRE_MESSAGE(physics.CreateRagdoll(owner, parts, glm::vec3(0.0f), error), error);
			physics.AddRagdollImpulse(owner, { 0.0f, 0.0f, push }, { 0.0f, 2.1f, 0.0f });
			Test::Simulate(fixture.SceneInstance, 1.0f);
			std::vector<RagdollPartTransform> transforms;
			REQUIRE(physics.GetRagdollTransforms(owner, transforms));
			fixture.SceneInstance.OnRuntimeStop();
			// The hanging part's turn about X relative to the root (the pair also turns as a whole).
			const glm::quat relative = glm::inverse(transforms[0].Rotation) * transforms[1].Rotation;
			return glm::degrees(2.0f * std::atan2(relative.x, relative.w));
		};
		const float allowed = swing(-3.0f), blocked = swing(3.0f);
		CHECK(allowed > 20.0f);  // pushed towards -Z: turns positive, freely
		CHECK(blocked > -3.0f);  // pushed towards +Z: held at the limit (0)
	}

	TEST_CASE("Impulses, velocities, teleport and radial impulses")
	{
		PhysicsFixture fixture;
		Entity ball = fixture.CreateDynamicBall({ 0.0f, 0.5f, 0.0f });
		ball.GetComponent<RigidBodyComponent>().GravityFactor = 0.0f;
		fixture.SceneInstance.OnRuntimeStart();
		PhysicsWorld& physics = *fixture.SceneInstance.GetPhysicsWorld();

		physics.SetLinearVelocity(ball, { 2.0f, 0.0f, 0.0f });
		CHECK(Test::Near(physics.GetLinearVelocity(ball), { 2.0f, 0.0f, 0.0f }, 1e-3f));
		physics.Teleport(ball, { 0.0f, 3.0f, 0.0f }, glm::quat(1, 0, 0, 0));
		physics.SetLinearVelocity(ball, glm::vec3(0.0f));
		physics.AddImpulse(ball, { 0.0f, 0.0f, -4.0f }); // mass 2 -> 2 m/s
		CHECK(Test::Near(physics.GetLinearVelocity(ball).z, -2.0f, 0.01f));

		physics.SetLinearVelocity(ball, glm::vec3(0.0f));
		physics.AddRadialImpulse({ -1.0f, 3.0f, 0.0f }, 5.0f, 10.0f);
		CHECK(physics.GetLinearVelocity(ball).x > 0.5f);
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Character controller walks, stays grounded and is blocked by walls")
	{
		PhysicsFixture fixture;
		Test::CreateBox(fixture.SceneInstance, "Wall", { 0.0f, 1.0f, -3.0f }, { 3.0f, 1.0f, 0.2f });
		Entity player = fixture.SceneInstance.CreateEntity("Player");
		player.Transform().Translation = { 0.0f, 0.05f, 0.0f };
		player.AddComponent<CharacterControllerComponent>();
		fixture.SceneInstance.OnRuntimeStart();

		Test::Simulate(fixture.SceneInstance, 0.5f);
		CHECK(player.GetComponent<CharacterControllerComponent>().RuntimeGrounded);

		player.GetComponent<CharacterControllerComponent>().RuntimeDesiredVelocity = { 0.0f, 0.0f, -3.0f };
		Test::Simulate(fixture.SceneInstance, 3.0f);
		const glm::vec3 position = player.Transform().Translation;
		CHECK(position.z < -2.0f);  // walked forward
		CHECK(position.z > -2.85f); // ...but stopped at the wall (wall face at -2.8, radius 0.3)
		CHECK(Test::Near(position.y, 0.0f, 0.1f));
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Contacts and trigger overlaps are reported")
	{
		PhysicsFixture fixture;
		Entity ball = fixture.CreateDynamicBall({ 0.0f, 3.0f, 0.0f });
		Entity trigger = fixture.SceneInstance.CreateEntity("Trigger");
		trigger.Transform().Translation = { 0.0f, 1.5f, 0.0f };
		trigger.AddComponent<BoxColliderComponent>().HalfExtents = { 1.0f, 0.2f, 1.0f };
		trigger.AddComponent<RigidBodyComponent>().IsTrigger = true;
		fixture.SceneInstance.OnRuntimeStart();
		PhysicsWorld& physics = *fixture.SceneInstance.GetPhysicsWorld();

		bool hitGround = false, hitTrigger = false;
		for (int frame = 0; frame < 180; frame++)
		{
			physics.Step(Timestep(1.0f / 60.0f));
			for (const ContactEvent& contact : physics.ConsumeContactEvents())
			{
				const bool involvesBall = contact.EntityA == ball.GetUUID() || contact.EntityB == ball.GetUUID();
				hitGround |= involvesBall && (contact.EntityA == fixture.Ground.GetUUID() || contact.EntityB == fixture.Ground.GetUUID());
				hitTrigger |= involvesBall && (contact.EntityA == trigger.GetUUID() || contact.EntityB == trigger.GetUUID());
			}
		}
		CHECK(hitTrigger);
		CHECK(hitGround);
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Runtime-spawned and destroyed entities gain and lose bodies")
	{
		PhysicsFixture fixture;
		fixture.SceneInstance.OnRuntimeStart();
		PhysicsWorld& physics = *fixture.SceneInstance.GetPhysicsWorld();
		const size_t before = physics.GetBodyCount();

		Entity ball = fixture.CreateDynamicBall({ 0.0f, 2.0f, 0.0f });
		fixture.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(physics.HasBody(ball.GetUUID()));
		CHECK(physics.GetBodyCount() == before + 1);

		fixture.SceneInstance.DestroyEntity(ball);
		CHECK(physics.GetBodyCount() == before);
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Mesh colliders use the entity mesh")
	{
		PhysicsFixture fixture;
		Entity ramp = fixture.SceneInstance.CreateEntity("Ramp");
		ramp.Transform().Translation = { 10.0f, 2.0f, 0.0f };
		ramp.Transform().Scale = glm::vec3(4.0f);
		ramp.AddComponent<MeshComponent>().Mesh = "builtin:sphere";
		ramp.AddComponent<MeshColliderComponent>();
		fixture.SceneInstance.OnRuntimeStart();

		std::optional<RaycastHit> hit = fixture.SceneInstance.GetPhysicsWorld()->Raycast({ 10.0f, 10.0f, 0.0f }, { 0.0f, -1.0f, 0.0f }, 20.0f);
		REQUIRE(hit);
		CHECK(hit->Entity == ramp.GetUUID());
		CHECK(Test::Near(hit->Point.y, 4.0f, 0.05f));
		fixture.SceneInstance.OnRuntimeStop();
	}
}

TEST_SUITE("Physics")
{
	TEST_CASE("Characters crouch by changing height and cannot stand up under a low ceiling")
	{
		PhysicsFixture fixture;
		// A low passage: ceiling underside at 1.4 m, starting at z = -2.
		Test::CreateBox(fixture.SceneInstance, "Ceiling", { 0.0f, 1.9f, -6.0f }, { 3.0f, 0.5f, 4.0f });
		Entity player = fixture.SceneInstance.CreateEntity("Player");
		player.Transform().Translation = { 0.0f, 0.05f, 0.0f };
		auto& controller = player.AddComponent<CharacterControllerComponent>();
		fixture.SceneInstance.OnRuntimeStart();
		Test::Simulate(fixture.SceneInstance, 0.3f);

		// Standing, the passage blocks the character.
		controller.RuntimeDesiredVelocity = { 0.0f, 0.0f, -2.0f };
		Test::Simulate(fixture.SceneInstance, 1.5f);
		CHECK(player.Transform().Translation.z > -2.0f);

		// Crouched, it fits through.
		controller.Height = 1.1f;
		Test::Simulate(fixture.SceneInstance, 2.0f);
		CHECK(controller.Height == doctest::Approx(1.1f));
		CHECK(player.Transform().Translation.z < -3.0f);

		// Trying to stand up inside the passage is refused.
		controller.RuntimeDesiredVelocity = glm::vec3(0.0f);
		controller.Height = 1.8f;
		fixture.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(controller.Height == doctest::Approx(1.1f));

		// Out in the open it stands up again.
		controller.RuntimeDesiredVelocity = { 0.0f, 0.0f, -2.0f };
		Test::Simulate(fixture.SceneInstance, 3.0f);
		controller.RuntimeDesiredVelocity = glm::vec3(0.0f);
		controller.Height = 1.8f;
		Test::Simulate(fixture.SceneInstance, 0.1f);
		CHECK(player.Transform().Translation.z < -10.0f);
		CHECK(controller.Height == doctest::Approx(1.8f));
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Triggers never block raycasts")
	{
		PhysicsFixture fixture;
		Entity trigger = fixture.SceneInstance.CreateEntity("Trigger");
		trigger.Transform().Translation = { 0.0f, 1.0f, -3.0f };
		trigger.AddComponent<SphereColliderComponent>().Radius = 1.0f;
		trigger.AddComponent<RigidBodyComponent>().IsTrigger = true;
		fixture.SceneInstance.OnRuntimeStart();
		std::optional<RaycastHit> hit = fixture.SceneInstance.GetPhysicsWorld()->Raycast({ 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, 10.0f);
		CHECK_FALSE(hit.has_value());
		fixture.SceneInstance.OnRuntimeStop();
	}
}
