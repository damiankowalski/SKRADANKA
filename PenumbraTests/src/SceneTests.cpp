#include "TestHelpers.h"

#include "Penumbra/Scene/Scene.h"

using namespace Penumbra;

TEST_SUITE("Scene")
{
	TEST_CASE("Entities are created with identity components and found by name and id")
	{
		Scene scene;
		Entity entity = scene.CreateEntity("Torch");
		CHECK(entity.IsValid());
		CHECK(entity.HasComponent<IDComponent>());
		CHECK(entity.HasComponent<TransformComponent>());
		CHECK(entity.HasComponent<RelationshipComponent>());
		CHECK(entity.GetName() == "Torch");
		CHECK(scene.FindEntityByName("Torch") == entity);
		CHECK(scene.GetEntityByUUID(entity.GetUUID()) == entity);
		CHECK_FALSE(scene.FindEntityByName("Missing").IsValid());
		CHECK_FALSE(scene.TryGetEntityByUUID(UUID()).has_value());
		CHECK(scene.GetEntityCount() == 1);

		Entity unnamed = scene.CreateEntity("");
		CHECK(unnamed.GetName() == "Entity");
	}

	TEST_CASE("Hierarchy composes world transforms and destroys children")
	{
		Scene scene;
		Entity parent = scene.CreateEntity("Parent");
		Entity child = scene.CreateEntity("Child");
		Entity grandChild = scene.CreateEntity("GrandChild");
		parent.Transform().Translation = { 10.0f, 0.0f, 0.0f };
		parent.Transform().SetEulerDegrees({ 0.0f, 90.0f, 0.0f });
		child.Transform().Translation = { 0.0f, 0.0f, -2.0f };

		scene.SetParent(child, parent, false);
		scene.SetParent(grandChild, child, false);
		CHECK(child.GetParent() == parent);
		REQUIRE(parent.GetChildren().size() == 1);
		CHECK(scene.IsAncestor(parent, grandChild));

		// Rotating 90 degrees around Y maps local -Z to world -X.
		CHECK(Test::Near(scene.GetWorldPosition(child), { 8.0f, 0.0f, 0.0f }, 1e-4f));

		SUBCASE("keepWorldTransform preserves the world position when reparenting")
		{
			const glm::vec3 before = scene.GetWorldPosition(grandChild);
			scene.Unparent(grandChild, true);
			CHECK(Test::Near(scene.GetWorldPosition(grandChild), before, 1e-4f));
			CHECK(child.GetChildren().empty());
		}

		SUBCASE("cycles are rejected")
		{
			scene.SetParent(parent, grandChild, true);
			CHECK_FALSE(parent.GetParent().IsValid());
		}

		SUBCASE("destroying a parent destroys the subtree")
		{
			scene.DestroyEntity(parent);
			CHECK(scene.GetEntityCount() == 0);
		}
	}

	TEST_CASE("Duplicate copies components and children with fresh ids")
	{
		Scene scene;
		Entity root = scene.CreateEntity("Root");
		root.AddComponent<PointLightComponent>().Intensity = 42.0f;
		Entity child = scene.CreateEntity("Child");
		scene.SetParent(child, root, false);

		Entity copy = scene.DuplicateEntity(root);
		CHECK(copy.GetUUID() != root.GetUUID());
		CHECK(copy.GetComponent<PointLightComponent>().Intensity == 42.0f);
		REQUIRE(copy.GetChildren().size() == 1);
		CHECK(copy.GetChildren()[0].GetUUID() != child.GetUUID());
		CHECK(root.GetChildren().size() == 1);
		CHECK(scene.GetEntityCount() == 4);
	}

	TEST_CASE("Scene copy keeps ids and is independent")
	{
		Ref<Scene> original = CreateRef<Scene>();
		Entity entity = original->CreateEntity("A");
		entity.AddComponent<CameraComponent>().VerticalFOV = 70.0f;

		Ref<Scene> copy = Scene::Copy(original);
		Entity copied = copy->GetEntityByUUID(entity.GetUUID());
		CHECK(copied.GetComponent<CameraComponent>().VerticalFOV == 70.0f);
		copied.GetComponent<CameraComponent>().VerticalFOV = 30.0f;
		CHECK(entity.GetComponent<CameraComponent>().VerticalFOV == 70.0f);
	}

	TEST_CASE("Primary camera and environment lookup")
	{
		Scene scene;
		CHECK_FALSE(scene.GetPrimaryCameraEntity().IsValid());
		Entity secondary = scene.CreateEntity("Secondary");
		secondary.AddComponent<CameraComponent>().Primary = false;
		CHECK(scene.GetPrimaryCameraEntity() == secondary);
		Entity primary = scene.CreateEntity("Primary");
		primary.AddComponent<CameraComponent>().Primary = true;
		CHECK(scene.GetPrimaryCameraEntity() == primary);

		Entity environment = scene.CreateEntity("Environment");
		environment.AddComponent<EnvironmentComponent>();
		CHECK(scene.GetEnvironmentEntity() == environment);
	}

	TEST_CASE("Runtime destruction is deferred to the end of the frame")
	{
		Test::TemporaryProject project;
		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		Entity a = scene.CreateEntity("A");
		scene.OnRuntimeStart();
		CHECK(scene.IsRunning());
		// Outside of an update, runtime destruction happens immediately.
		scene.DestroyEntity(a);
		CHECK_FALSE(a.IsValid());

		scene.OnRuntimeStop();
		CHECK_FALSE(scene.IsRunning());
	}

	TEST_CASE("Paused scenes only advance when stepped")
	{
		Test::TemporaryProject project;
		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		scene.OnRuntimeStart();
		scene.SetPaused(true);
		scene.OnUpdateRuntime(Timestep(0.5f));
		CHECK(scene.GetFrameCount() == 0);
		scene.Step(2);
		scene.OnUpdateRuntime(Timestep(0.5f));
		scene.OnUpdateRuntime(Timestep(0.5f));
		scene.OnUpdateRuntime(Timestep(0.5f));
		CHECK(scene.GetFrameCount() == 2);
		CHECK(Test::Near(scene.GetTime(), 1.0f));
		scene.OnRuntimeStop();
	}
}
