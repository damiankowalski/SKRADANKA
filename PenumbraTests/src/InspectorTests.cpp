#include "TestHelpers.h"

#include "Penumbra/AI/FogOfWarSystem.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Debug/ObjectInspector.h"

using namespace Penumbra;

// The runtime's object inspector (F4, PenumbraRuntime/src/RuntimeDebugPanel): what a click in the
// world selects, how the inspector describes it, and the actions of its right-click menu. The ImGui
// window is glue over these and over ComponentInspector, which the editor's Properties panel shares.

namespace {

	// A floor, a crate, a guard (a box for a body) and a wall lamp, seen from a camera at z = 6.
	struct InspectorLevel
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;
		Entity Crate;
		Entity Guard;
		Entity GuardBody;
		Entity Lamp;

		InspectorLevel()
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			Test::CreateBox(SceneInstance, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f });
			Crate = Test::CreateBox(SceneInstance, "Crate", { -2.0f, 0.5f, 0.0f }, { 0.5f, 0.5f, 0.5f });

			Guard = SceneInstance.CreateEntity("Guard");
			Guard.Transform().Translation = { 0.0f, 0.05f, 0.0f };
			Guard.AddComponent<PerceptionComponent>();
			GuardBody = Test::CreateBox(SceneInstance, "GuardBody", { 0.0f, 0.9f, 0.0f }, { 0.3f, 0.9f, 0.3f });
			GuardBody.RemoveComponent<BoxColliderComponent>();
			SceneInstance.SetParent(GuardBody, Guard, true);

			// A torch on a post: the prefab's root holds a mesh, its flame a light.
			Lamp = SceneInstance.CreateEntity("Torch");
			Lamp.Transform().Translation = { 2.0f, 0.0f, 0.0f };
			Lamp.AddComponent<PrefabComponent>().Prefab = "Prefabs/Torch.pprefab";
			Entity flame = SceneInstance.CreateEntity("Flame");
			flame.AddComponent<PointLightComponent>();
			SceneInstance.SetParent(flame, Lamp, false);
			flame.Transform().Translation = { 0.0f, 2.0f, 0.0f };
		}

		~InspectorLevel()
		{
			if (SceneInstance.IsRunning())
				SceneInstance.OnRuntimeStop();
		}

		// A ray from z = 6, at the target's height, straight at it.
		std::optional<ObjectInspector::PickResult> PickAt(const glm::vec3& target)
		{
			const glm::vec3 origin(target.x, target.y, 6.0f);
			return ObjectInspector::Pick(SceneInstance, origin, glm::normalize(target - origin));
		}
	};

}

TEST_SUITE("Debug")
{
	TEST_CASE("A click on a guard's body selects the guard; a click on a torch's flame selects the torch")
	{
		InspectorLevel level;

		const auto guard = level.PickAt({ 0.0f, 1.0f, 0.0f });
		REQUIRE(guard);
		CHECK(guard->Hit == level.GuardBody);
		CHECK(guard->Object == level.Guard);
		CHECK(guard->Distance == doctest::Approx(5.7f).epsilon(0.01));

		const auto crate = level.PickAt({ -2.0f, 0.5f, 0.0f });
		REQUIRE(crate);
		CHECK(crate->Object == level.Crate);

		// The lamp has no mesh: its light is picked as a small ball, and selects the prefab it belongs to.
		const auto flame = level.PickAt({ 2.0f, 2.0f, 0.0f });
		REQUIRE(flame);
		CHECK(flame->Hit.GetName() == "Flame");
		CHECK(flame->Object == level.Lamp);

		CHECK_FALSE(level.PickAt({ 8.0f, 3.0f, 0.0f })); // empty air

		// A box the ray starts inside (a room's volume round the camera) is not picked.
		const glm::vec3 inside(-2.0f, 0.5f, 0.0f);
		const auto fromInside = ObjectInspector::Pick(level.SceneInstance, inside, { 0.0f, 0.0f, -1.0f });
		CHECK((!fromInside || fromInside->Object != level.Crate));
	}

	TEST_CASE("A guard hidden by the fog of war cannot be clicked")
	{
		InspectorLevel level;
		Entity thief = level.SceneInstance.CreateEntity("Thief");
		thief.Transform().Translation = { 0.0f, 0.05f, 5.0f };
		thief.AddComponent<CharacterControllerComponent>();
		auto& fog = thief.AddComponent<FogOfWarComponent>();
		fog.ViewAngle = 60.0f;
		fog.AwarenessRadius = 0.5f;
		// The camera looks away from the guard (+Z): he is behind the thief's back.
		Entity camera = level.SceneInstance.CreateEntity("Camera");
		camera.Transform().Translation = { 0.0f, 2.0f, 4.0f };
		camera.Transform().Rotation = Math::LookRotation(glm::vec3(0.0f, 0.0f, 1.0f));
		camera.AddComponent<CameraComponent>();
		level.SceneInstance.OnRuntimeStart();
		level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		REQUIRE(level.SceneInstance.GetFogOfWarSystem()->IsHidden(level.Guard));

		const auto pick = level.PickAt({ 0.0f, 1.0f, 0.0f });
		CHECK((!pick || pick->Object != level.Guard));

		level.SceneInstance.GetFogOfWarSystem()->SetForcedOff(true);
		level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		const auto visible = level.PickAt({ 0.0f, 1.0f, 0.0f });
		REQUIRE(visible);
		CHECK(visible->Object == level.Guard);
	}

	TEST_CASE("The inspector names the kind of thing and puts the components that matter for it first")
	{
		InspectorLevel level;
		level.Guard.AddComponent<VitalityComponent>();

		const ObjectInspector::ObjectContext guard = ObjectInspector::Describe(level.Guard);
		CHECK(guard.Kind == "NPC");
		REQUIRE(guard.KeyComponents.size() == 2);
		CHECK(guard.KeyComponents[0] == "Perception");
		CHECK(guard.KeyComponents[1] == "Vitality");

		Entity flame = level.Lamp.GetChildren().front();
		CHECK(ObjectInspector::Describe(flame).Kind == "Light");
		CHECK(ObjectInspector::Describe(flame).KeyComponents == std::vector<std::string>{ "PointLight" });
		CHECK(ObjectInspector::Describe(level.Lamp).Kind == "Torch"); // named after its prefab

		Entity door = level.SceneInstance.CreateEntity("CellarDoor");
		door.AddComponent<ScriptComponent>().Script = "Scripts/Door.lua";
		CHECK(ObjectInspector::Describe(door).Kind == "Door");
		CHECK(ObjectInspector::Describe(door).KeyComponents.front() == "Script");

		level.Crate.AddOrReplaceComponent<RigidBodyComponent>().Type = RigidBodyType::Dynamic;
		CHECK(ObjectInspector::Describe(level.Crate).Kind == "Loose object");

		Entity thief = level.SceneInstance.CreateEntity("Thief");
		thief.AddComponent<PerceptionTargetComponent>().Kind = TargetKind::Intruder;
		CHECK(ObjectInspector::Describe(thief).Kind == "Thief");
	}

	TEST_CASE("The menu's actions: move an object, switch a lamp, outline what is selected")
	{
		InspectorLevel level;
		level.SceneInstance.OnRuntimeStart();

		ObjectInspector::MoveTo(level.SceneInstance, level.Crate, { 4.0f, 0.5f, -3.0f });
		CHECK(Test::Near(level.SceneInstance.GetWorldPosition(level.Crate), { 4.0f, 0.5f, -3.0f }));
		Test::Simulate(level.SceneInstance, 0.1f); // the physics body went with it (static: it stays)
		CHECK(Test::Near(level.SceneInstance.GetWorldPosition(level.Crate), { 4.0f, 0.5f, -3.0f }, 1e-2f));

		CHECK(ObjectInspector::HasLights(level.Lamp));
		CHECK_FALSE(ObjectInspector::HasLights(level.Crate));
		CHECK(ObjectInspector::AreLightsOn(level.Lamp));
		ObjectInspector::SetLightsOn(level.Lamp, false);
		CHECK_FALSE(ObjectInspector::AreLightsOn(level.Lamp));
		CHECK_FALSE(level.Lamp.GetChildren().front().GetComponent<PointLightComponent>().Enabled);

		// The guard's outline covers his body.
		const BoundingBox bounds = ObjectInspector::WorldBounds(level.SceneInstance, level.Guard);
		REQUIRE(bounds.IsValid());
		CHECK(Test::Near(bounds.Min, { -0.3f, 0.0f, -0.3f }, 1e-3f));
		CHECK(Test::Near(bounds.Max, { 0.3f, 1.8f, 0.3f }, 1e-3f));
		CHECK_FALSE(ObjectInspector::WorldBounds(level.SceneInstance, level.Lamp.GetChildren().front()).IsValid());
	}

	TEST_CASE("One guard can be told to ignore the thief while the others still notice him")
	{
		Test::TemporaryProject project;
		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		Test::CreateBox(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 30.0f, 0.5f, 30.0f });
		auto addGuard = [&](const std::string& name, const glm::vec3& position) {
			Entity guard = scene.CreateEntity(name);
			guard.Transform().Translation = position;
			guard.AddComponent<PerceptionComponent>();
			return guard;
		};
		Entity deaf = addGuard("Deaf", { 2.0f, 0.05f, 0.0f });
		Entity other = addGuard("Other", { -2.0f, 0.05f, 0.0f });
		Entity thief = scene.CreateEntity("Thief");
		thief.AddComponent<PerceptionTargetComponent>().Kind = TargetKind::Intruder;
		scene.OnRuntimeStart();

		PerceptionSystem& perception = *scene.GetPerceptionSystem();
		perception.SetIgnoresIntruders(deaf, true);
		CHECK(perception.IgnoresIntruders(deaf));
		CHECK_FALSE(perception.IgnoresIntruders(other));
		perception.EmitNoise({ 0.0f, 0.1f, 0.0f }, 6.0f, thief.GetUUID(), "Footstep");
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(deaf.GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Unaware);
		CHECK(other.GetComponent<PerceptionComponent>().RuntimeState != AwarenessState::Unaware);

		// A noise someone else makes reaches him as usual; and he can be given his ears back.
		perception.EmitNoise({ 0.0f, 0.1f, 0.0f }, 6.0f, UUID::Invalid, "Bucket");
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(deaf.GetComponent<PerceptionComponent>().RuntimeStimulusCause == "Bucket");
		perception.SetIgnoresIntruders(deaf, false);
		CHECK_FALSE(perception.IgnoresIntruders(deaf));
		scene.OnRuntimeStop();
	}
}
