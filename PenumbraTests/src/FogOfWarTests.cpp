#include "TestHelpers.h"

#include "Penumbra/AI/FogOfWarSystem.h"
#include "Penumbra/Scene/ComponentRegistry.h"

using namespace Penumbra;

// Fog of war (AI/FogOfWarSystem): the player sees a cone where the camera looks and a small circle
// around him, never through walls; characters he does not see are hidden from the picture. The
// rendering half (darkened, drained of colour) is tested in RendererTests.cpp.

namespace {

	// The thief at the origin; the camera behind him looks down -Z, so he faces -Z. A wall 3 m in
	// front of him (3 m wide, centred on x = 0).
	struct FogLevel
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;
		Entity Thief;
		Entity Camera;

		FogLevel()
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			Test::CreateBox(SceneInstance, "Floor", { 0.0f, -0.5f, 0.0f }, { 30.0f, 0.5f, 30.0f });
			Test::CreateBox(SceneInstance, "Wall", { 0.0f, 1.5f, -3.1f }, { 1.5f, 1.5f, 0.1f });

			Thief = SceneInstance.CreateEntity("Thief");
			Thief.Transform().Translation = { 0.0f, 0.05f, 0.0f };
			Thief.AddComponent<CharacterControllerComponent>();
			Thief.AddComponent<FogOfWarComponent>();

			Camera = SceneInstance.CreateEntity("Camera");
			Camera.Transform().Translation = { 0.0f, 3.0f, 4.0f };
			Camera.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, -3.0f, -4.0f));
			Camera.AddComponent<CameraComponent>();
		}

		~FogLevel()
		{
			if (SceneInstance.IsRunning())
				SceneInstance.OnRuntimeStop();
		}

		// A guard (no physics character: tests move him by hand).
		Entity AddGuard(const std::string& name, const glm::vec3& position)
		{
			Entity guard = SceneInstance.CreateEntity(name);
			guard.Transform().Translation = position;
			guard.AddComponent<PerceptionComponent>();
			return guard;
		}

		void Start()
		{
			SceneInstance.OnRuntimeStart();
			Frame();
		}

		void Frame() { SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f)); }

		FogOfWarSystem& Fog() { return *SceneInstance.GetFogOfWarSystem(); }
		FogOfWarComponent& Component() { return Thief.GetComponent<FogOfWarComponent>(); }
	};

}

TEST_SUITE("FogOfWar")
{
	TEST_CASE("The view covers a cone in front, a circle around the eye and nothing past the view distance")
	{
		FogOfWarComponent component;
		component.ViewAngle = 120.0f;
		component.EdgeSoftness = 10.0f;
		component.AwarenessRadius = 2.0f;
		component.ViewDistance = 20.0f;
		FogOfWarView view = FogOfWarView::FromComponent(component);
		view.Eye = { 0.0f, 1.6f, 0.0f };
		view.Forward = { 0.0f, 0.0f, -1.0f };

		CHECK(view.Coverage({ 0.0f, 0.0f, -5.0f }) == doctest::Approx(1.0f));        // straight ahead
		CHECK(view.Coverage({ 4.0f, 3.0f, -4.0f }) == doctest::Approx(1.0f));        // 45 degrees off, up high: only the ground plane counts
		CHECK(view.Coverage({ 0.0f, 0.0f, 5.0f }) == doctest::Approx(0.0f));         // behind him
		CHECK(view.Coverage({ 0.0f, 0.0f, 1.0f }) == doctest::Approx(1.0f));         // behind him, inside the circle
		CHECK(view.Coverage({ 0.0f, 0.0f, -25.0f }) == doctest::Approx(0.0f));       // too far
		// The cone's edge (60 degrees off) is half way through its fade.
		const glm::vec3 edge = glm::vec3(std::sin(glm::radians(60.0f)), 0.0f, -std::cos(glm::radians(60.0f))) * 8.0f;
		CHECK(view.Coverage(edge + glm::vec3(0.0f, 1.6f, 0.0f)) == doctest::Approx(0.5f).epsilon(0.01));
	}

	TEST_CASE("Guards in sight are drawn; behind a wall or behind his back they are hidden")
	{
		FogLevel level;
		Entity inView = level.AddGuard("InView", { 2.0f, 0.0f, -2.0f });
		Entity behindWall = level.AddGuard("BehindWall", { 0.0f, 0.0f, -6.0f });
		Entity behindBack = level.AddGuard("BehindBack", { 0.0f, 0.0f, 5.0f });
		Entity closeBehind = level.AddGuard("CloseBehind", { 0.0f, 0.0f, 1.0f });
		Entity model = level.SceneInstance.CreateEntity("Model");
		level.SceneInstance.SetParent(model, behindWall, false);
		level.Start();

		FogOfWarSystem& fog = level.Fog();
		REQUIRE(fog.GetView());
		CHECK(level.Component().RuntimeActive);
		CHECK(Test::Near(level.Component().RuntimeForward, { 0.0f, 0.0f, -1.0f }, 1e-3f));
		CHECK_FALSE(fog.IsHidden(inView));
		CHECK(fog.IsHidden(behindWall));
		CHECK(fog.IsHidden(model)); // his body goes with him
		CHECK(fog.IsHidden(behindBack));
		CHECK_FALSE(fog.IsHidden(closeBehind)); // felt, not seen: the circle of awareness
		CHECK_FALSE(fog.IsHidden(level.Thief));
		CHECK(level.Component().RuntimeHiddenActors == 2);

		CHECK(fog.CanSee({ 2.0f, 1.0f, -2.0f }));
		CHECK_FALSE(fog.CanSee({ 0.0f, 1.0f, -6.0f }));
		CHECK_FALSE(fog.CanSee({ 0.0f, 1.0f, 5.0f }));
		CHECK(fog.CanSeeEntity(inView));
		CHECK_FALSE(fog.CanSeeEntity(behindWall));
	}

	TEST_CASE("Turning the camera turns the cone")
	{
		FogLevel level;
		Entity behindBack = level.AddGuard("BehindBack", { 0.0f, 0.0f, 5.0f });
		level.Start();
		CHECK(level.Fog().IsHidden(behindBack));

		level.Camera.Transform().Translation = { 0.0f, 3.0f, -4.0f };
		level.Camera.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, -3.0f, 4.0f));
		level.Frame();
		CHECK(Test::Near(level.Component().RuntimeForward, { 0.0f, 0.0f, 1.0f }, 1e-3f));
		CHECK_FALSE(level.Fog().IsHidden(behindBack));
	}

	TEST_CASE("A guard who leaves the view stays drawn for MemoryTime, then disappears")
	{
		FogLevel level;
		Entity guard = level.AddGuard("Guard", { 2.0f, 0.0f, -2.0f });
		level.Start();
		CHECK_FALSE(level.Fog().IsHidden(guard));

		guard.Transform().Translation = { 0.0f, 0.0f, -6.0f };
		Test::Simulate(level.SceneInstance, 0.1f);
		CHECK_FALSE(level.Fog().IsHidden(guard));
		Test::Simulate(level.SceneInstance, 0.3f);
		CHECK(level.Fog().IsHidden(guard));
		// Back in sight: drawn at once.
		guard.Transform().Translation = { 2.0f, 0.0f, -2.0f };
		level.Frame();
		CHECK_FALSE(level.Fog().IsHidden(guard));
	}

	TEST_CASE("The eye sinks when he crouches; what he carries is never hidden")
	{
		FogLevel level;
		level.Thief.GetComponent<CharacterControllerComponent>().Height = 1.0f;
		Entity body = level.AddGuard("CarriedBody", { 0.0f, 0.0f, 0.0f });
		level.SceneInstance.SetParent(body, level.Thief, false);
		body.Transform().Translation = { 0.0f, 0.0f, 4.0f }; // behind his back, but on his shoulders
		level.Start();

		const glm::vec3 eye = level.Component().RuntimeEye;
		CHECK(eye.y - level.SceneInstance.GetWorldPosition(level.Thief).y == doctest::Approx(0.85f).epsilon(0.01));
		CHECK_FALSE(level.Fog().IsHidden(body));
		CHECK(level.Fog().CanSeeEntity(body));
	}

	TEST_CASE("Leaning moves the eye sideways from where he looks: he sees round the wall's edge")
	{
		FogLevel level;
		// Behind the wall's right end (it reaches x = 1.5): out of the eye's sight from x = 0.
		Entity guard = level.AddGuard("PastTheCorner", { 2.5f, 0.0f, -6.0f });
		level.Start();
		CHECK(level.Fog().IsHidden(guard));

		level.Component().Lean = 0.8f; // right of where the camera looks (-Z): +X
		level.Frame();
		CHECK(level.Component().RuntimeEye.x == doctest::Approx(0.8f));
		CHECK(level.Component().RuntimeEye.z == doctest::Approx(0.0f).epsilon(1e-4));
		CHECK_FALSE(level.Fog().IsHidden(guard));

		level.Component().Lean = -0.8f; // the other way: further behind the wall
		Test::Simulate(level.SceneInstance, 0.5f); // past MemoryTime
		CHECK(level.Component().RuntimeEye.x == doctest::Approx(-0.8f));
		CHECK(level.Fog().IsHidden(guard));

		// The lean follows the camera: looking down +Z, leaning right is towards -X.
		level.Camera.Transform().Translation = { 0.0f, 3.0f, -4.0f };
		level.Camera.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, -3.0f, 4.0f));
		level.Component().Lean = 0.8f;
		level.Frame();
		CHECK(level.Component().RuntimeEye.x == doctest::Approx(-0.8f));
	}

	TEST_CASE("Fog can be switched off: by the component, by the testing switch, or only for characters")
	{
		FogLevel level;
		Entity guard = level.AddGuard("BehindWall", { 0.0f, 0.0f, -6.0f });
		level.Start();
		REQUIRE(level.Fog().IsHidden(guard));

		SUBCASE("Component disabled")
		{
			level.Component().Enabled = false;
			level.Frame();
			CHECK_FALSE(level.Fog().GetView());
			CHECK_FALSE(level.Component().RuntimeActive);
		}
		SUBCASE("Testing switch (the runtime's F7)")
		{
			level.Fog().SetForcedOff(true);
			level.Frame();
			CHECK_FALSE(level.Fog().GetView());
			CHECK(level.Fog().CanSee({ 0.0f, 1.0f, -6.0f }));
		}
		SUBCASE("Characters always drawn")
		{
			level.Component().HideActors = false;
			level.Frame();
			CHECK(level.Fog().GetView());
		}
		CHECK_FALSE(level.Fog().IsHidden(guard));
	}

	TEST_CASE("Scenes save the fog's settings but not its runtime state")
	{
		FogLevel level;
		level.Component().ViewAngle = 90.0f;
		level.Start();
		const nlohmann::json saved = ComponentRegistry::SerializeEntityComponents(level.Thief);
		REQUIRE(saved.contains("FogOfWar"));
		CHECK(saved["FogOfWar"]["ViewAngle"] == 90.0f);
		CHECK_FALSE(saved["FogOfWar"].contains("Eye"));
	}
}
