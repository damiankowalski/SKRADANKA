#include "TestHelpers.h"

#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Stim/StimSystem.h"

using namespace Penumbra;

namespace {

	// Torch: a post with a flame child (light + continuous fire stim). Water extinguishes it,
	// fire re-ignites it.
	Entity CreateTorch(Scene& scene, const glm::vec3& position)
	{
		Entity torch = Test::CreateBox(scene, "Torch", position, { 0.05f, 0.4f, 0.05f });
		auto& response = torch.AddComponent<StimResponseComponent>();
		response.Rules.push_back({ "Water", 0.3f, false, { { StimActionType::DisableLights } } });
		response.Rules.push_back({ "Fire", 0.3f, false, { { StimActionType::EnableLights } } });

		Entity flame = scene.CreateEntity("Flame");
		scene.SetParent(flame, torch, false);
		flame.Transform().Translation = { 0.0f, 0.5f, 0.0f };
		flame.AddComponent<PointLightComponent>();
		auto& material = flame.AddComponent<MaterialComponent>();
		material.EmissiveIntensity = 5.0f;
		auto& fire = flame.AddComponent<StimSourceComponent>();
		fire.Stim = "Fire";
		fire.Delivery = StimDelivery::Continuous;
		fire.Radius = 0.4f;
		return torch;
	}

	Entity CreateKeg(Scene& scene, const glm::vec3& position)
	{
		Entity keg = Test::CreateBox(scene, "Keg", position, { 0.4f, 0.5f, 0.4f });
		auto& response = keg.AddComponent<StimResponseComponent>();
		response.Rules.push_back({ "Fire", 0.5f, true,
			{ { StimActionType::EmitStim, "Fire", 1.0f, 3.0f }, { StimActionType::RadialImpulse, "", 50.0f, 4.0f },
				{ StimActionType::EmitNoise, "", 0.0f, 30.0f }, { StimActionType::Destroy } } });
		return keg;
	}

	Entity CreateArrow(Scene& scene, const std::string& stim, const glm::vec3& position, const glm::vec3& velocity)
	{
		Entity arrow = scene.CreateEntity(stim + "Arrow");
		arrow.Transform().Translation = position;
		arrow.AddComponent<SphereColliderComponent>().Radius = 0.05f;
		auto& body = arrow.AddComponent<RigidBodyComponent>();
		body.Type = RigidBodyType::Dynamic;
		body.Mass = 0.1f;
		body.ContinuousCollision = true;
		body.GravityFactor = 0.0f;
		auto& source = arrow.AddComponent<StimSourceComponent>();
		source.Stim = stim;
		source.Delivery = StimDelivery::Contact;
		source.Radius = 0.5f;
		source.DestroyOnContact = true;
		scene.OnUpdateRuntime(Timestep(0.0f));
		scene.GetPhysicsWorld()->SetLinearVelocity(arrow, velocity);
		return arrow;
	}

	struct StimFixture
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;

		StimFixture()
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			Test::CreateBox(SceneInstance, "Ground", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f });
		}
	};

}

TEST_SUITE("Stim")
{
	TEST_CASE("A water arrow extinguishes a torch and a fire arrow re-ignites it")
	{
		StimFixture fixture;
		Entity torch = CreateTorch(fixture.SceneInstance, { 0.0f, 0.4f, -5.0f });
		Entity flame = torch.GetChildren()[0];
		fixture.SceneInstance.OnRuntimeStart();

		const UUID waterArrow = CreateArrow(fixture.SceneInstance, "Water", { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, -30.0f }).GetUUID();
		Test::Simulate(fixture.SceneInstance, 0.5f);
		CHECK_FALSE(fixture.SceneInstance.TryGetEntityByUUID(waterArrow).has_value()); // destroyed on contact
		CHECK_FALSE(flame.GetComponent<PointLightComponent>().Enabled);
		CHECK_FALSE(flame.GetComponent<MaterialComponent>().EmissiveEnabled);
		CHECK_FALSE(flame.GetComponent<StimSourceComponent>().Enabled);

		CreateArrow(fixture.SceneInstance, "Fire", { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, -30.0f });
		Test::Simulate(fixture.SceneInstance, 0.5f);
		CHECK(flame.GetComponent<PointLightComponent>().Enabled);
		CHECK(flame.GetComponent<StimSourceComponent>().Enabled);
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("A fire arrow detonates a powder keg, which chain-reacts and throws objects")
	{
		StimFixture fixture;
		Entity first = CreateKeg(fixture.SceneInstance, { 0.0f, 0.5f, -6.0f });
		Entity second = CreateKeg(fixture.SceneInstance, { 2.0f, 0.5f, -6.0f });  // inside the 3 m blast
		Entity far = CreateKeg(fixture.SceneInstance, { 12.0f, 0.5f, -6.0f });   // outside
		Entity crate = fixture.SceneInstance.CreateEntity("Crate");
		crate.Transform().Translation = { -1.5f, 0.3f, -6.0f };
		crate.AddComponent<BoxColliderComponent>().HalfExtents = glm::vec3(0.3f);
		crate.AddComponent<RigidBodyComponent>().Type = RigidBodyType::Dynamic;
		Entity guard = fixture.SceneInstance.CreateEntity("Guard");
		guard.Transform().Translation = { 0.0f, 0.0f, 10.0f };
		guard.AddComponent<PerceptionComponent>();

		const UUID firstID = first.GetUUID(), secondID = second.GetUUID(), farID = far.GetUUID();
		fixture.SceneInstance.OnRuntimeStart();
		CreateArrow(fixture.SceneInstance, "Fire", { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, -30.0f });
		Test::Simulate(fixture.SceneInstance, 1.0f);

		CHECK_FALSE(fixture.SceneInstance.TryGetEntityByUUID(firstID).has_value());
		CHECK_FALSE(fixture.SceneInstance.TryGetEntityByUUID(secondID).has_value());
		CHECK(fixture.SceneInstance.TryGetEntityByUUID(farID).has_value());
		CHECK(glm::length(crate.Transform().Translation - glm::vec3(-1.5f, 0.3f, -6.0f)) > 0.5f);
		CHECK(guard.GetComponent<PerceptionComponent>().RuntimeSuspicion > 0.0f); // heard the blast
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("A burning torch ignites a keg that touches its flame")
	{
		StimFixture fixture;
		CreateTorch(fixture.SceneInstance, { 0.0f, 0.4f, 0.0f });
		Entity keg = CreateKeg(fixture.SceneInstance, { 0.0f, 0.6f, 0.0f }); // 0.3 m from the flame
		const UUID kegID = keg.GetUUID();
		fixture.SceneInstance.OnRuntimeStart();
		Test::Simulate(fixture.SceneInstance, 0.2f);
		CHECK_FALSE(fixture.SceneInstance.TryGetEntityByUUID(kegID).has_value());
		fixture.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Thresholds, once-rules, spawning and direct delivery")
	{
		StimFixture fixture;
		Entity target = fixture.SceneInstance.CreateEntity("Target");
		auto& response = target.AddComponent<StimResponseComponent>();
		response.Rules.push_back({ "Holy", 0.8f, true, { { StimActionType::SpawnPrefab, "Prefabs/Spark.pprefab" } } });

		Scene prefabSource;
		Entity spark = prefabSource.CreateEntity("Spark");
		spark.AddComponent<PointLightComponent>();
		REQUIRE(Prefab::Save(spark, fixture.Project.GetAssetDirectory() / "Prefabs" / "Spark.pprefab"));

		fixture.SceneInstance.OnRuntimeStart();
		StimSystem& stims = *fixture.SceneInstance.GetStimSystem();
		const size_t before = fixture.SceneInstance.GetEntityCount();

		stims.Deliver(target, "Holy", 0.5f); // below threshold
		fixture.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(fixture.SceneInstance.GetEntityCount() == before);

		stims.Deliver(target, "Holy", 1.0f);
		fixture.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(fixture.SceneInstance.GetEntityCount() == before + 1);
		CHECK(fixture.SceneInstance.FindEntityByName("Spark").IsValid());

		stims.Deliver(target, "Holy", 1.0f); // once-rule already fired
		fixture.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(fixture.SceneInstance.GetEntityCount() == before + 1);

		stims.Emit("Holy", { 100.0f, 0.0f, 0.0f }, 5.0f, 1.0f); // out of range: nothing delivered
		CHECK(stims.GetDeliveredCount() == 3);
		fixture.SceneInstance.OnRuntimeStop();
	}
}
