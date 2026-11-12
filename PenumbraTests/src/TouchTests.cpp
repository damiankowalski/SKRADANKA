#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionSystem.h"

using namespace Penumbra;

// Touch beats shadow: a guard who bumps into the player (or is bumped into) knows he is there
// however dark it is, and one standing right in front of him is made out in the dark, slowly.

namespace {

	struct DarkRoom
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;
		Entity Guard;
		Entity Player;

		// A pitch-dark room: a guard at the origin looking down -Z and the player at playerPosition.
		explicit DarkRoom(const glm::vec3& playerPosition)
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			SceneInstance.CreateEntity("Environment").AddComponent<EnvironmentComponent>().AmbientColor = glm::vec3(0.0f);
			Test::CreateBox(SceneInstance, "Floor", { 0.0f, -0.5f, 0.0f }, { 20.0f, 0.5f, 20.0f }, glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));

			Guard = SceneInstance.CreateEntity("Guard");
			Guard.Transform().Translation = { 0.0f, 0.05f, 0.0f };
			Guard.AddComponent<PerceptionComponent>();
			Guard.AddComponent<CharacterControllerComponent>();

			Player = SceneInstance.CreateEntity("Player");
			Player.Transform().Translation = playerPosition + glm::vec3(0.0f, 0.05f, 0.0f);
			Player.AddComponent<PerceptionTargetComponent>();
			Player.AddComponent<CharacterControllerComponent>();
			SceneInstance.OnRuntimeStart();
		}

		~DarkRoom() { SceneInstance.OnRuntimeStop(); }

		PerceptionComponent& Perception() { return Guard.GetComponent<PerceptionComponent>(); }
		float Gap()
		{
			const glm::vec3 delta = Player.Transform().Translation - Guard.Transform().Translation;
			return glm::length(glm::vec2(delta.x, delta.z));
		}
	};

}

TEST_SUITE("Perception")
{
	TEST_CASE("A guard who walks into the player in the dark knows he is there and cannot walk through him")
	{
		DarkRoom room({ 0.0f, 0.0f, -3.0f });
		Test::Simulate(room.SceneInstance, 0.5f);
		CHECK(room.Perception().RuntimeState == AwarenessState::Unaware); // 3 m away in the dark: unseen

		room.Guard.GetComponent<CharacterControllerComponent>().RuntimeDesiredVelocity = { 0.0f, 0.0f, -1.5f };
		float time = 0.0f;
		while (!room.SceneInstance.GetPerceptionSystem()->IsTouching(room.Guard, room.Player) && time < 3.0f)
		{
			room.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			time += 1.0f / 60.0f;
		}
		REQUIRE(time < 3.0f);
		room.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(room.Perception().RuntimeState == AwarenessState::Alarmed); // at once, not after staring
		CHECK(room.Perception().RuntimeLastTarget == room.Player.GetUUID());
		CHECK(Test::Near(room.Perception().RuntimeLastStimulusPosition, room.Player.Transform().Translation, 0.1f));

		// He keeps pushing: the capsules collide, he does not walk into or through the player.
		Test::Simulate(room.SceneInstance, 1.5f);
		CHECK(room.Gap() > 0.55f);
		CHECK(room.Guard.Transform().Translation.z > room.Player.Transform().Translation.z);
	}

	TEST_CASE("The player bumping into a guard from behind alerts him")
	{
		DarkRoom room({ 0.0f, 0.0f, 2.0f });
		Test::Simulate(room.SceneInstance, 0.5f);
		room.Player.GetComponent<CharacterControllerComponent>().RuntimeDesiredVelocity = { 0.0f, 0.0f, -2.0f };
		Test::Simulate(room.SceneInstance, 1.5f);
		CHECK(room.Gap() > 0.55f);
		CHECK(room.Perception().RuntimeState == AwarenessState::Alarmed);
		CHECK(room.Perception().RuntimeStimulusCause == "Touch");
	}

	TEST_CASE("Right in front of him in the dark the player is made out, but not at once; two metres away he is not")
	{
		DarkRoom close({ 0.0f, 0.0f, -1.0f });
		CHECK_FALSE(close.SceneInstance.GetPerceptionSystem()->IsTouching(close.Guard, close.Player));
		Test::Simulate(close.SceneInstance, 0.3f);
		CHECK(close.Perception().RuntimeState == AwarenessState::Unaware); // a moment to step back
		Test::Simulate(close.SceneInstance, 3.7f);
		CHECK(close.Perception().RuntimeState == AwarenessState::Alarmed);
		CHECK(close.Perception().RuntimeStimulusCause == "Seen");

		DarkRoom away({ 0.0f, 0.0f, -2.0f });
		Test::Simulate(away.SceneInstance, 4.0f);
		CHECK(away.Perception().RuntimeState == AwarenessState::Unaware);

		DarkRoom behind({ 0.0f, 0.0f, 1.0f }); // close, but behind his back and not touching
		Test::Simulate(behind.SceneInstance, 4.0f);
		CHECK(behind.Perception().RuntimeState == AwarenessState::Unaware);
	}

	TEST_CASE("Touch is felt only by the awake, only through contact, and only from intruders")
	{
		DarkRoom room({ 0.0f, 0.0f, 0.7f }); // just behind him, capsules almost touching
		PerceptionSystem& perception = *room.SceneInstance.GetPerceptionSystem();
		CHECK(perception.IsTouching(room.Guard, room.Player));
		room.Player.Transform().Translation.y = 2.5f; // on a ledge above his head
		CHECK_FALSE(perception.IsTouching(room.Guard, room.Player));
		room.Player.Transform().Translation.y = 0.05f;

		room.Player.GetComponent<PerceptionTargetComponent>().Kind = TargetKind::Body;
		room.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(room.Perception().RuntimeState == AwarenessState::Unaware); // a body against his back is not a bump

		room.Player.GetComponent<PerceptionTargetComponent>().Kind = TargetKind::Intruder;
		REQUIRE(perception.KnockOut(room.Guard));
		room.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(room.Perception().RuntimeState == AwarenessState::Unaware);
	}
}
