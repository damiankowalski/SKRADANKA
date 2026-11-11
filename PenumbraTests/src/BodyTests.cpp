#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Navigation/NavigationSystem.h"
#include "Penumbra/Physics/PhysicsWorld.h"
#include "Penumbra/Stim/StimSystem.h"

using namespace Penumbra;

// Knockouts, bodies other guards find, and shouts that spread the news.

namespace {

	struct GuardPost
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;

		explicit GuardPost(float ambient = 0.02f)
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			SceneInstance.CreateEntity("Environment").AddComponent<EnvironmentComponent>().AmbientColor = glm::vec3(ambient);
			Test::CreateBox(SceneInstance, "Ground", { 0.0f, -0.5f, -10.0f }, { 40.0f, 0.5f, 40.0f }, glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
		}

		// A guard at the position looking down -Z.
		Entity Guard(const std::string& name, const glm::vec3& position)
		{
			Entity guard = SceneInstance.CreateEntity(name);
			guard.Transform().Translation = position;
			guard.AddComponent<PerceptionComponent>();
			guard.AddComponent<CharacterControllerComponent>();
			return guard;
		}

		void Light(const glm::vec3& position, float intensity)
		{
			Entity light = SceneInstance.CreateEntity("Light");
			light.Transform().Translation = position;
			auto& component = light.AddComponent<PointLightComponent>();
			component.Intensity = intensity;
			component.Range = 12.0f;
		}

		PerceptionSystem& Perception() { return *SceneInstance.GetPerceptionSystem(); }
	};

}

TEST_SUITE("Perception")
{
	TEST_CASE("A knocked-out guard stops perceiving and walking and lies where he fell")
	{
		GuardPost post;
		Entity guard = post.Guard("Guard", { 0.0f, 0.05f, 0.0f });
		post.SceneInstance.OnRuntimeStart();
		Test::Simulate(post.SceneInstance, 0.2f);
		REQUIRE(post.SceneInstance.GetPhysicsWorld()->HasCharacter(guard.GetUUID()));
		REQUIRE(post.SceneInstance.GetNavigationSystem()->MoveTo(guard, { 0.0f, 0.0f, -5.0f }));
		post.Perception().Notify(guard, { 0.0f, 0.0f, -3.0f }, 6.0f, StimulusKind::Heard, "Crack");

		CHECK(PerceptionSystem::IsAwake(guard));
		CHECK(post.Perception().KnockOut(guard));
		CHECK_FALSE(post.Perception().KnockOut(guard)); // already down
		CHECK_FALSE(PerceptionSystem::IsAwake(guard));
		CHECK(guard.GetComponent<VitalityComponent>().State == VitalState::Unconscious);
		CHECK(guard.GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Unaware);
		CHECK(guard.GetComponent<NavAgentComponent>().RuntimeStatus == NavStatus::Idle);
		CHECK_FALSE(guard.HasComponent<CharacterControllerComponent>());
		CHECK_FALSE(post.SceneInstance.GetPhysicsWorld()->HasCharacter(guard.GetUUID()));
		CHECK(guard.GetComponent<PerceptionTargetComponent>().Kind == TargetKind::Body);

		// He hears nothing any more and is not told anything either.
		const glm::vec3 where = post.SceneInstance.GetWorldPosition(guard);
		Test::Simulate(post.SceneInstance, 0.5f);
		CHECK(Test::Near(post.SceneInstance.GetWorldPosition(guard), where, 0.01f)); // no gravity, no walking: stays put
		post.Perception().EmitNoise(where + glm::vec3(1.0f, 0.0f, 0.0f), 10.0f, UUID::Invalid, "Bang");
		Test::Simulate(post.SceneInstance, 0.1f);
		CHECK(guard.GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Unaware);
		CHECK_FALSE(post.Perception().Notify(guard, where, 9.0f, StimulusKind::Reported, "Shout"));

		CHECK(post.Perception().KnockOut(post.Guard("Other", { 5.0f, 0.05f, 0.0f }), VitalState::Dead));
		CHECK_FALSE(post.Perception().KnockOut(post.SceneInstance.FindEntityByName("Other"), VitalState::Awake));
		post.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("A body left in the light is found once and searched around; in the dark it is not")
	{
		auto run = [](bool lit) {
			GuardPost post;
			Entity watcher = post.Guard("Watcher", { 0.0f, 0.05f, 0.0f });
			Entity victim = post.Guard("Victim", { 0.0f, 0.05f, -6.0f });
			victim.Transform().Rotation = Math::QuatFromEulerDegrees({ 0.0f, 180.0f, 0.0f }); // facing away from him
			if (lit)
				post.Light({ 0.0f, 2.5f, -6.0f }, 25.0f);
			post.SceneInstance.OnRuntimeStart();
			Test::Simulate(post.SceneInstance, 0.5f);
			REQUIRE(post.Perception().KnockOut(victim));
			Test::Simulate(post.SceneInstance, 1.0f);
			const auto perception = watcher.GetComponent<PerceptionComponent>();

			// Found once: after calming down he does not find the same body again.
			bool foundAgain = false;
			if (perception.RuntimeStimulusCause == "Body")
			{
				PerceptionSystem::Calm(watcher.GetComponent<PerceptionComponent>());
				Test::Simulate(post.SceneInstance, 1.0f);
				foundAgain = watcher.GetComponent<PerceptionComponent>().RuntimeState != AwarenessState::Unaware;
			}
			post.SceneInstance.OnRuntimeStop();
			return std::make_pair(perception, foundAgain);
		};

		const auto [lit, litAgain] = run(true);
		CHECK(lit.RuntimeStimulusCause == "Body");
		CHECK(lit.RuntimeStimulusKind == StimulusKind::Seen);
		CHECK(lit.RuntimeState == AwarenessState::Searching); // BodyAlarm 8
		CHECK(Test::Near(lit.RuntimeLastStimulusPosition, { 0.0f, 0.05f, -6.0f }, 0.1f));
		CHECK_FALSE(litAgain);

		const auto [dark, darkAgain] = run(false);
		CHECK(dark.RuntimeState == AwarenessState::Unaware);
	}

	TEST_CASE("A shout tells guards within earshot where the trouble is, one level weaker")
	{
		GuardPost post;
		Entity speaker = post.Guard("Speaker", { 0.0f, 0.05f, 0.0f });
		Entity near = post.Guard("Near", { 8.0f, 0.05f, 0.0f });
		Entity far = post.Guard("Far", { 0.0f, 0.05f, 60.0f });
		Entity down = post.Guard("Down", { -3.0f, 0.05f, 0.0f });
		post.SceneInstance.OnRuntimeStart();
		Test::Simulate(post.SceneInstance, 0.1f);
		REQUIRE(post.Perception().KnockOut(down));

		CHECK(post.Perception().Shout(speaker).empty()); // nothing to shout about
		const glm::vec3 trouble(2.0f, 0.0f, -10.0f);
		REQUIRE(post.Perception().Notify(speaker, trouble, 8.0f, StimulusKind::Heard, "Clang"));
		const std::vector<Entity> alerted = post.Perception().Shout(speaker);
		REQUIRE(alerted.size() == 1);
		CHECK(alerted[0] == near);
		const auto& heard = near.GetComponent<PerceptionComponent>();
		CHECK(heard.RuntimeStimulusKind == StimulusKind::Reported);
		CHECK(heard.RuntimeStimulusCause == "Shout");
		CHECK(Test::Near(heard.RuntimeLastStimulusPosition, trouble));
		CHECK(Test::Near(heard.RuntimeReactionStrength, 7.0f, 0.05f));
		CHECK(heard.RuntimeState == AwarenessState::Searching);
		CHECK(far.GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Unaware);
		CHECK(down.GetComponent<PerceptionComponent>().RuntimeState == AwarenessState::Unaware);

		// A quieter word does not carry as far.
		PerceptionSystem::Calm(near.GetComponent<PerceptionComponent>());
		CHECK(post.Perception().Shout(speaker, 4.0f).empty());
		post.SceneInstance.OnRuntimeStop();
	}
}

TEST_SUITE("Stim")
{
	TEST_CASE("Gas knocks out NPCs whose response rule says so")
	{
		GuardPost post;
		Entity guard = post.Guard("Guard", { 0.0f, 0.05f, 0.0f });
		guard.AddComponent<StimResponseComponent>().Rules.push_back({ "Gas", 0.3f, true, { { StimActionType::Knockout } } });
		Entity bystander = post.Guard("Bystander", { 10.0f, 0.05f, 0.0f });
		post.SceneInstance.OnRuntimeStart();
		Test::Simulate(post.SceneInstance, 0.1f);
		post.SceneInstance.GetStimSystem()->Emit("Gas", { 0.0f, 1.0f, 0.0f }, 3.0f, 1.0f);
		Test::Simulate(post.SceneInstance, 0.1f);
		CHECK(guard.GetComponent<VitalityComponent>().State == VitalState::Unconscious);
		CHECK_FALSE(bystander.HasComponent<VitalityComponent>());
		post.SceneInstance.OnRuntimeStop();
	}
}
