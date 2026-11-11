#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Audio/AudioScene.h"

using namespace Penumbra;

TEST_SUITE("Audio")
{
	TEST_CASE("Sources play, loop, stop and finish without an output device")
	{
		Test::TemporaryProject project;
		Test::WriteToneWav(project.GetAssetDirectory() / "Audio" / "Beep.wav", 0.25f);

		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		Entity oneShot = scene.CreateEntity("OneShot");
		auto& source = oneShot.AddComponent<AudioSourceComponent>();
		source.Clip = "Audio/Beep.wav";
		source.PlayOnStart = true;
		Entity looping = scene.CreateEntity("Loop");
		auto& loopSource = looping.AddComponent<AudioSourceComponent>();
		loopSource.Clip = "Audio/Beep.wav";
		loopSource.Loop = true;
		scene.CreateEntity("Listener").AddComponent<AudioListenerComponent>();

		scene.OnRuntimeStart();
		AudioScene& audio = *scene.GetAudioScene();
		REQUIRE(audio.IsInitialized());
		CHECK(audio.IsPlaying(oneShot));
		CHECK_FALSE(audio.IsPlaying(looping));

		REQUIRE(audio.Play(looping));
		CHECK(audio.GetActiveVoiceCount() == 2);

		Test::Simulate(scene, 0.6f);
		CHECK_FALSE(audio.IsPlaying(oneShot)); // 0.25 s clip has finished
		CHECK(audio.IsPlaying(looping));       // loop keeps going

		audio.StopSource(looping);
		CHECK_FALSE(audio.IsPlaying(looping));
		CHECK(audio.GetActiveVoiceCount() == 0);

		CHECK(audio.PlayOneShot("Audio/Beep.wav", { 1.0f, 0.0f, 0.0f }));
		CHECK_FALSE(audio.PlayOneShot("Audio/Missing.wav", glm::vec3(0.0f)));
		scene.OnRuntimeStop();
	}

	TEST_CASE("Sounds with a noise loudness are heard by NPCs")
	{
		Test::TemporaryProject project;
		Test::WriteToneWav(project.GetAssetDirectory() / "Audio" / "Crash.wav", 0.2f);

		Scene scene;
		scene.SetAudioDeviceEnabled(false);
		Entity guard = scene.CreateEntity("Guard");
		guard.AddComponent<PerceptionComponent>();
		scene.OnRuntimeStart();

		CHECK(scene.GetAudioScene()->PlayOneShot("Audio/Crash.wav", { 3.0f, 1.6f, 0.0f }, 1.0f, 8.0f));
		scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		const auto& perception = guard.GetComponent<PerceptionComponent>();
		CHECK(perception.RuntimeReactionStrength > 5.0f);
		CHECK(perception.RuntimeStimulusKind == StimulusKind::Heard);
		CHECK(perception.RuntimeHasStimulus);
		CHECK(Test::Near(perception.RuntimeLastStimulusPosition, { 3.0f, 1.6f, 0.0f }));
		scene.OnRuntimeStop();
	}
}
