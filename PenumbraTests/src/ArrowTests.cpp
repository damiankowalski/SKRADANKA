#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Physics/PhysicsWorld.h"

using namespace Penumbra;

// Arrows that matter: they hurt, they tell the guard where they came from, and they can be seen
// flying by (or not, against a dark background).

namespace {

	struct Range
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;

		explicit Range(float ambient = 0.02f)
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			SceneInstance.CreateEntity("Environment").AddComponent<EnvironmentComponent>().AmbientColor = glm::vec3(ambient);
			Test::CreateBox(SceneInstance, "Ground", { 0.0f, -0.5f, -10.0f }, { 40.0f, 0.5f, 40.0f }, glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
		}

		// A guard at the origin looking down -Z who takes fire and water like the demo's NPCs.
		Entity Guard()
		{
			Entity guard = SceneInstance.CreateEntity("Guard");
			guard.Transform().Translation = { 0.0f, 0.05f, 0.0f };
			guard.AddComponent<PerceptionComponent>();
			guard.AddComponent<CharacterControllerComponent>();
			auto& response = guard.AddComponent<StimResponseComponent>();
			response.Rules.push_back({ "Fire", 0.5f, false, { { StimActionType::Damage, "", 75.0f }, { StimActionType::Alarm } } });
			response.Rules.push_back({ "Water", 0.5f, false, { { StimActionType::Alarm } } });
			return guard;
		}

		Entity Arrow(const std::string& stim, const glm::vec3& position)
		{
			Entity arrow = SceneInstance.CreateEntity(stim + "Arrow");
			arrow.Transform().Translation = position;
			arrow.AddComponent<SphereColliderComponent>().Radius = 0.06f;
			auto& body = arrow.AddComponent<RigidBodyComponent>();
			body.Type = RigidBodyType::Dynamic;
			body.Mass = 0.1f;
			body.GravityFactor = 0.0f;
			body.ContinuousCollision = true;
			auto& source = arrow.AddComponent<StimSourceComponent>();
			source.Stim = stim;
			source.Delivery = StimDelivery::Contact;
			source.Radius = 0.0f;
			source.DestroyOnContact = true;
			return arrow;
		}

		void Shoot(Entity arrow, const glm::vec3& velocity)
		{
			SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f)); // the body exists from the next step
			SceneInstance.GetPhysicsWorld()->SetLinearVelocity(arrow, velocity);
			Test::Simulate(SceneInstance, 0.5f);
		}
	};

	// An arrow-like target moved by hand across the guard's view at 30 m/s, 6 m in front of him.
	PerceptionComponent WatchArrowFlyBy(bool brightBackground, float emissive)
	{
		Range range(0.02f);
		Entity guard = range.SceneInstance.CreateEntity("Guard");
		guard.Transform().Translation = { 0.0f, 0.05f, 0.0f };
		guard.AddComponent<PerceptionComponent>();
		Test::CreateBox(range.SceneInstance, "Wall", { 0.0f, 3.0f, -12.0f }, { 20.0f, 3.0f, 0.2f },
			brightBackground ? glm::vec4(0.9f, 0.9f, 0.9f, 1.0f) : glm::vec4(0.05f, 0.05f, 0.05f, 1.0f), brightBackground ? 3.0f : 0.0f);
		Entity arrow = range.SceneInstance.CreateEntity("Arrow");
		arrow.Transform().Translation = { -8.0f, 1.5f, -6.0f };
		auto& target = arrow.AddComponent<PerceptionTargetComponent>();
		target.Kind = TargetKind::Projectile;
		target.SilhouettePoints = { { -0.3f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.3f, 0.0f, 0.0f } };
		target.Radius = 0.04f;
		target.Reflectance = 0.25f;
		target.Emissive = emissive;
		range.SceneInstance.OnRuntimeStart();
		const float dt = 1.0f / 60.0f;
		for (int frame = 0; frame < 32; ++frame) // 16 m in about half a second
		{
			range.SceneInstance.OnUpdateRuntime(Timestep(dt));
			arrow.Transform().Translation.x += 30.0f * dt;
		}
		const PerceptionComponent result = guard.GetComponent<PerceptionComponent>();
		range.SceneInstance.OnRuntimeStop();
		return result;
	}

}

TEST_SUITE("Perception")
{
	TEST_CASE("Damage takes health; at zero the NPC dies and is a body; a knocked-out body can be finished off")
	{
		Range range;
		Entity guard = range.Guard();
		range.SceneInstance.OnRuntimeStart();
		PerceptionSystem& perception = *range.SceneInstance.GetPerceptionSystem();
		CHECK(perception.Damage(guard, 60.0f) == 40.0f);
		CHECK(PerceptionSystem::IsAwake(guard));
		CHECK(perception.Damage(guard, 60.0f) == 0.0f);
		CHECK(guard.GetComponent<VitalityComponent>().State == VitalState::Dead);
		CHECK(guard.GetComponent<PerceptionTargetComponent>().Kind == TargetKind::Body);
		CHECK(perception.Damage(guard, 10.0f) == 0.0f); // nothing more to take

		Entity other = range.SceneInstance.CreateEntity("Other");
		other.AddComponent<PerceptionComponent>();
		REQUIRE(perception.KnockOut(other));
		perception.Damage(other, 150.0f);
		CHECK(other.GetComponent<VitalityComponent>().State == VitalState::Dead);
		range.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("A fire arrow hurts the guard it hits and sends him back along its flight; a second one kills")
	{
		Range range;
		Entity guard = range.Guard();
		range.SceneInstance.OnRuntimeStart();
		range.Shoot(range.Arrow("Fire", { 7.0f, 1.2f, 0.0f }), { -30.0f, 0.0f, 0.0f });

		const auto& vitality = guard.GetComponent<VitalityComponent>();
		CHECK(vitality.Health == 25.0f);
		const auto& perception = guard.GetComponent<PerceptionComponent>();
		CHECK(perception.RuntimeStimulusCause == "Hit");
		CHECK(perception.RuntimeState == AwarenessState::Alarmed);
		// It flew in from +X: he goes to look about 8 m back along its path, not where it struck.
		CHECK(perception.RuntimeLastStimulusPosition.x > 6.0f);
		CHECK(std::abs(perception.RuntimeLastStimulusPosition.z) < 1.0f);

		range.Shoot(range.Arrow("Fire", { 0.0f, 1.2f, 7.0f }), { 0.0f, 0.0f, -30.0f });
		CHECK(vitality.State == VitalState::Dead);
		range.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("A water arrow does no harm but the guard it hits knows he was shot at")
	{
		Range range;
		Entity guard = range.Guard();
		range.SceneInstance.OnRuntimeStart();
		range.Shoot(range.Arrow("Water", { -7.0f, 1.2f, 0.0f }), { 30.0f, 0.0f, 0.0f });
		CHECK_FALSE(guard.HasComponent<VitalityComponent>());
		CHECK(guard.GetComponent<PerceptionComponent>().RuntimeStimulusCause == "Hit");
		CHECK(guard.GetComponent<PerceptionComponent>().RuntimeLastStimulusPosition.x < -6.0f);
		range.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("An arrow flying by is seen against a bright wall, not against a dark one, and a burning one always")
	{
		const PerceptionComponent bright = WatchArrowFlyBy(true, 0.0f);
		CHECK(bright.RuntimeStimulusCause == "Arrow");
		CHECK(bright.RuntimeState == AwarenessState::Searching); // ProjectileAlarm 8
		CHECK(Test::Near(bright.RuntimeLastStimulusPosition, { -8.0f, 1.5f, -6.0f }, 0.6f)); // where it came from

		const PerceptionComponent dark = WatchArrowFlyBy(false, 0.0f);
		CHECK(dark.RuntimeState == AwarenessState::Unaware);

		const PerceptionComponent burning = WatchArrowFlyBy(false, 1.5f);
		CHECK(burning.RuntimeStimulusCause == "Arrow");
	}

	TEST_CASE("The motion factor is capped")
	{
		CHECK(PerceptionModel::MotionFactor(32.0f, 1.0f, 0.3f) == PerceptionModel::MaxMotionFactor);
		CHECK(Test::Near(PerceptionModel::MotionFactor(1.0f, 0.0f, 0.3f), 1.3f));
	}
}
