#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionModel.h"
#include "Penumbra/AI/PerceptionSystem.h"

using namespace Penumbra;

// Unit tests of the formulas plus the seven scenarios of docs/GAME ENGINE SKRADANKA.md
// (section 8) as end-to-end tests on real scenes with physics line-of-sight.

namespace {

	// Guard at the origin looking down -Z, intruder 8 m in front of him.
	struct StealthLab
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;
		Entity Guard;
		Entity Player;

		explicit StealthLab(float ambient = 0.02f, float fog = 0.0f)
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			Entity environment = SceneInstance.CreateEntity("Environment");
			auto& env = environment.AddComponent<EnvironmentComponent>();
			env.AmbientColor = glm::vec3(ambient);
			env.FogDensity = fog;

			Test::CreateBox(SceneInstance, "Ground", { 0.0f, -0.5f, -10.0f }, { 30.0f, 0.5f, 30.0f }, glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));

			Guard = SceneInstance.CreateEntity("Guard");
			Guard.AddComponent<PerceptionComponent>();

			Player = SceneInstance.CreateEntity("Player");
			Player.Transform().Translation = { 0.0f, 0.0f, -8.0f };
			Player.AddComponent<PerceptionTargetComponent>();
		}

		Entity Wall(float z, float albedo, float emissive = 0.0f, float halfWidth = 15.0f)
		{
			return Test::CreateBox(SceneInstance, "Wall", { 0.0f, 3.0f, z }, { halfWidth, 3.0f, 0.1f }, glm::vec4(glm::vec3(albedo), 1.0f),
				emissive);
		}

		Entity PointLight(const glm::vec3& position, float intensity, float range)
		{
			Entity light = SceneInstance.CreateEntity("Light");
			light.Transform().Translation = position;
			auto& component = light.AddComponent<PointLightComponent>();
			component.Color = glm::vec3(1.0f);
			component.Intensity = intensity;
			component.Range = range;
			return light;
		}

		PerceptionComponent& Perception() { return Guard.GetComponent<PerceptionComponent>(); }

		void Start() { SceneInstance.OnRuntimeStart(); }

		// Simulates until suspicion reaches threshold; returns the elapsed time or -1 if never.
		float TimeToSuspicion(float threshold, float maxSeconds, const std::function<void(float)>& perFrame = {})
		{
			const float dt = 1.0f / 60.0f;
			for (float t = 0.0f; t < maxSeconds; t += dt)
			{
				if (perFrame)
					perFrame(t);
				SceneInstance.OnUpdateRuntime(Timestep(dt));
				if (Perception().RuntimeSuspicion >= threshold)
					return t + dt;
			}
			return -1.0f;
		}

		VisibilityResult Visibility() { return SceneInstance.GetPerceptionSystem()->ComputeVisibility(Guard, Player); }
	};

}

TEST_SUITE("Perception")
{
	TEST_CASE("Weber contrast")
	{
		CHECK(Test::Near(PerceptionModel::WeberContrast(1.0f, 0.0f), 1.0f));
		CHECK(Test::Near(PerceptionModel::WeberContrast(0.0f, 1.0f), 1.0f));
		CHECK(Test::Near(PerceptionModel::WeberContrast(0.5f, 0.5f), 0.0f));
		CHECK(Test::Near(PerceptionModel::WeberContrast(2.0f, 1.0f), 0.5f));
		// Scale invariant in normal light...
		CHECK(Test::Near(PerceptionModel::WeberContrast(20.0f, 10.0f), PerceptionModel::WeberContrast(2.0f, 1.0f)));
		// ...but collapses below the noise floor (both black -> invisible).
		CHECK(PerceptionModel::WeberContrast(0.0002f, 0.0001f, 0.005f) < 0.05f);
		CHECK(PerceptionModel::WeberContrast(0.0f, 0.0f) == 0.0f);
	}

	TEST_CASE("Adaptation is fast towards bright and slow towards dark")
	{
		const float brighter = PerceptionModel::Adapt(0.1f, 1.0f, 0.5f, 0.5f, 6.0f);
		const float darker = PerceptionModel::Adapt(1.0f, 0.1f, 0.5f, 0.5f, 6.0f);
		CHECK(Test::Near(brighter, 0.1f + 0.9f * (1.0f - std::exp(-1.0f)), 1e-4f));
		CHECK((1.0f - darker) < (brighter - 0.1f) * 0.2f);
		CHECK(PerceptionModel::Adapt(0.3f, 0.3f, 10.0f, 0.5f, 6.0f) == 0.3f);
		CHECK(PerceptionModel::Adapt(0.3f, 1.0f, 0.0f, 0.5f, 6.0f) == 0.3f);
	}

	TEST_CASE("Contrast threshold: darkness and glare raise it")
	{
		const float lit = PerceptionModel::ContrastThreshold(0.5f, 0.5f, 0.1f, 0.004f, 0.5f);
		const float dark = PerceptionModel::ContrastThreshold(0.01f, 0.01f, 0.1f, 0.004f, 0.5f);
		const float glare = PerceptionModel::ContrastThreshold(5.0f, 0.05f, 0.1f, 0.004f, 0.5f);
		const float adapted = PerceptionModel::ContrastThreshold(0.05f, 0.05f, 0.1f, 0.004f, 0.5f);
		CHECK(lit < dark);
		CHECK(glare > adapted + 0.9f);       // two decades too bright -> +1.0
		CHECK(Test::Near(PerceptionModel::ContrastThreshold(0.0f, 1e6f, 0.1f, 0.0f, 0.5f), 0.1f));
		// A dark-adapted eye looking at something brighter gets no glare penalty.
		CHECK(Test::Near(PerceptionModel::ContrastThreshold(0.01f, 1.0f, 0.1f, 0.0f, 0.5f), 0.1f));
	}

	TEST_CASE("Size, atmosphere, periphery and motion factors")
	{
		CHECK(PerceptionModel::SizeFactor(0.3f, 2.0f, 0.05f) == 1.0f);
		CHECK(PerceptionModel::SizeFactor(0.3f, 60.0f, 0.05f) < 0.25f);
		CHECK(PerceptionModel::AtmosphereFactor(0.0f) == 1.0f);
		CHECK(Test::Near(PerceptionModel::AtmosphereFactor(1.0f), std::exp(-1.0f)));
		CHECK(PerceptionModel::PeripheralFactor(0.0f, 0.2f) == 1.0f);
		CHECK(Test::Near(PerceptionModel::PeripheralFactor(1.0f, 0.2f), 0.2f));
		CHECK(PerceptionModel::MotionFactor(0.0f, 1.0f, 0.3f) == 1.0f);
		CHECK(PerceptionModel::MotionFactor(4.0f, 1.0f, 0.3f) > PerceptionModel::MotionFactor(4.0f, 0.0f, 0.3f));
		CHECK(PerceptionModel::Stimulus(0.5f, 0.1f, 0.0f, 1.0f, 1.0f, 1.0f) == 0.0f);
		CHECK(Test::Near(PerceptionModel::Stimulus(0.5f, 0.1f, 0.5f, 1.0f, 0.5f, 2.0f), 2.5f));
	}

	TEST_CASE("Suspicion counter and awareness states")
	{
		float d = 0.0f;
		d = PerceptionModel::UpdateSuspicion(d, 3.0f, 0.5f, 0.6f, 0.1f);
		CHECK(Test::Near(d, 0.6f));
		d = PerceptionModel::UpdateSuspicion(d, 0.5f, 1.0f, 0.6f, 0.1f);
		CHECK(Test::Near(d, 0.5f));
		CHECK(PerceptionModel::UpdateSuspicion(0.9f, 100.0f, 1.0f, 0.6f, 0.1f) == 1.0f);
		CHECK(PerceptionModel::UpdateSuspicion(0.05f, 0.0f, 1.0f, 0.6f, 0.1f) == 0.0f);

	}

	TEST_CASE("Loudness carries 1.4 * 2^(L/2) metres and loses two levels per doubling of distance")
	{
		CHECK(PerceptionModel::NoiseRange(0.0f) == 0.0f);
		CHECK(Test::Near(PerceptionModel::NoiseRange(3.0f), 3.96f, 0.01f));
		CHECK(Test::Near(PerceptionModel::NoiseRange(6.0f), 11.2f, 0.01f));
		CHECK(Test::Near(PerceptionModel::NoiseRange(9.0f), 31.68f, 0.01f));
		CHECK(Test::Near(PerceptionModel::HeardLevel(9.0f, 1.0f), 9.0f));  // full loudness up close
		CHECK(Test::Near(PerceptionModel::HeardLevel(9.0f, 2.8f), 7.0f));  // one doubling
		CHECK(Test::Near(PerceptionModel::HeardLevel(9.0f, 11.2f), 3.0f)); // three doublings
		CHECK(Test::Near(PerceptionModel::HeardLevel(6.0f, PerceptionModel::NoiseRange(6.0f)), 0.0f));
	}

	TEST_CASE("Reactions last in proportion to the stimulus and step down through the states")
	{
		CHECK(Test::Near(PerceptionModel::ReactionDuration(10.0f, 90.0f), 90.0f));
		CHECK(Test::Near(PerceptionModel::ReactionDuration(3.0f, 90.0f), 8.1f));
		CHECK(Test::Near(PerceptionModel::ReactionDuration(9.0f, 90.0f), 72.9f));
		CHECK(PerceptionModel::ReactionDuration(-1.0f, 90.0f) == 0.0f);
		for (float s : { 1.0f, 4.5f, 8.0f })
			CHECK(Test::Near(PerceptionModel::ReactionStrength(PerceptionModel::ReactionDuration(s, 60.0f), 60.0f), s));

		using PerceptionModel::StateFromStrength;
		CHECK(StateFromStrength(0.0f, 4.0f, 7.0f, 9.0f) == AwarenessState::Unaware);
		CHECK(StateFromStrength(2.0f, 4.0f, 7.0f, 9.0f) == AwarenessState::Suspicious);
		CHECK(StateFromStrength(5.0f, 4.0f, 7.0f, 9.0f) == AwarenessState::Investigating);
		CHECK(StateFromStrength(7.5f, 4.0f, 7.0f, 9.0f) == AwarenessState::Searching);
		CHECK(StateFromStrength(9.0f, 4.0f, 7.0f, 9.0f) == AwarenessState::Alarmed);
	}

	TEST_CASE("Scenario 1: dark player in a dark corner against a dark wall is not detected")
	{
		StealthLab lab;
		lab.Wall(-9.0f, 0.3f);
		lab.Start();
		CHECK(lab.TimeToSuspicion(0.3f, 10.0f) < 0.0f);
		const VisibilityResult visibility = lab.Visibility();
		CHECK(visibility.VisibleFraction == 1.0f);
		CHECK(visibility.Stimulus < 1.0f);
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Scenario 2: the same corner with a bright doorway behind the player is detected within seconds")
	{
		StealthLab lab;
		lab.Wall(-9.0f, 0.3f, 2.0f);
		lab.Start();
		const float time = lab.TimeToSuspicion(1.0f, 5.0f);
		CHECK(time > 0.0f);
		CHECK(time < 3.0f);
		CHECK(lab.Perception().RuntimeState == AwarenessState::Alarmed);
		CHECK(lab.Perception().RuntimeHasStimulus);
		CHECK(Test::Near(lab.Perception().RuntimeLastStimulusPosition, { 0.0f, 0.0f, -8.0f }, 0.01f));
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Scenario 3: a lit player is detected more slowly against a bright wall than a dark one")
	{
		auto timeFor = [](float wallAlbedo, float wallEmissive) {
			StealthLab lab;
			lab.Wall(-9.0f, wallAlbedo, wallEmissive);
			lab.PointLight({ 0.0f, 1.2f, -6.5f }, 20.0f, 3.0f); // lights the player, not the wall 2.5 m further
			lab.Start();
			const float time = lab.TimeToSuspicion(0.3f, 8.0f);
			lab.SceneInstance.OnRuntimeStop();
			return time < 0.0f ? 1000.0f : time;
		};
		const float againstDark = timeFor(0.05f, 0.0f);
		const float againstBright = timeFor(0.3f, 0.45f);
		CHECK(againstDark < 2.0f);
		CHECK(againstBright > againstDark * 3.0f);
	}

	TEST_CASE("Scenario 4: a guard coming from a bright hall is blind for a while, then adapts")
	{
		auto setup = [](StealthLab& lab) {
			lab.Wall(-9.0f, 0.02f);
			lab.PointLight({ 0.0f, 1.2f, -7.0f }, 0.5f, 3.0f); // dim light on the player
			lab.Start();
		};

		StealthLab dazzled;
		setup(dazzled);
		dazzled.Perception().RuntimeAdaptation = 3.0f; // just walked in from a bright hall
		const float dazzledTime = dazzled.TimeToSuspicion(0.3f, 30.0f);
		dazzled.SceneInstance.OnRuntimeStop();

		StealthLab adapted;
		setup(adapted);
		adapted.Perception().RuntimeAdaptation = 0.01f; // has been standing in the dark
		const float adaptedTime = adapted.TimeToSuspicion(0.3f, 30.0f);
		adapted.SceneInstance.OnRuntimeStop();

		CHECK(adaptedTime > 0.0f);
		CHECK(adaptedTime < 1.0f);
		CHECK(dazzledTime > 1.5f);  // "for a few seconds it does not notice the player"
		CHECK(dazzledTime < 30.0f); // "then sees through"
	}

	TEST_CASE("Scenario 5: peeking only the head over cover is detected partially and more slowly")
	{
		auto timeFor = [](bool cover) {
			StealthLab lab;
			lab.Wall(-9.0f, 0.3f, 3.0f); // window-bright background
			if (cover)
				Test::CreateBox(lab.SceneInstance, "Crate", { 0.0f, 0.75f, -7.4f }, { 1.0f, 0.75f, 0.2f }, glm::vec4(0.3f, 0.3f, 0.3f, 1.0f));
			lab.Start();
			lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			const VisibilityResult visibility = lab.Visibility();
			const float time = lab.TimeToSuspicion(0.3f, 10.0f);
			lab.SceneInstance.OnRuntimeStop();
			return std::make_pair(visibility, time);
		};

		const auto [full, fullTime] = timeFor(false);
		const auto [peek, peekTime] = timeFor(true);
		CHECK(full.VisiblePoints == full.TotalPoints);
		CHECK(peek.VisiblePoints == 1); // only the head control point
		CHECK(peek.VisibleFraction < full.VisibleFraction);
		CHECK(peekTime > 0.0f);          // partial detection still happens
		CHECK(peekTime > fullTime * 2.0f);
	}

	TEST_CASE("Scenario 6: at the edge of the cone a running player is detected, a still one is not")
	{
		auto run = [](bool moving) {
			StealthLab lab(0.3f); // dim daylight: low but non-zero contrast
			lab.Wall(-14.0f, 0.5f, 0.0f, 30.0f);
			const float angle = glm::radians(55.0f); // half FOV is 60 degrees
			const glm::vec3 direction(-std::sin(angle), 0.0f, -std::cos(angle));
			lab.Player.Transform().Translation = direction * 8.0f;
			lab.Start();
			const float time = lab.TimeToSuspicion(0.3f, 4.0f, [&](float t) {
				if (moving) // run back and forth along the sight line at up to 6 m/s: constant angle, constant size
					lab.Player.Transform().Translation = direction * (8.0f + std::sin(t * 6.0f));
			});
			lab.SceneInstance.OnRuntimeStop();
			return time;
		};
		CHECK(run(false) < 0.0f);
		CHECK(run(true) > 0.0f);
	}

	TEST_CASE("Scenario 7: smoke between guard and player hides a silhouette that is otherwise obvious")
	{
		auto run = [](bool smoke) {
			StealthLab lab;
			lab.Wall(-9.0f, 0.3f, 2.0f);
			if (smoke)
			{
				Entity cloud = lab.SceneInstance.CreateEntity("Smoke");
				cloud.Transform().Translation = { 0.0f, 1.5f, -4.0f };
				auto& volume = cloud.AddComponent<SmokeVolumeComponent>();
				volume.Radius = 2.5f;
				volume.Density = 0.8f;
			}
			lab.Start();
			lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			const VisibilityResult visibility = lab.Visibility();
			const float time = lab.TimeToSuspicion(0.3f, 5.0f);
			lab.SceneInstance.OnRuntimeStop();
			return std::make_pair(visibility, time);
		};
		const auto [clear, clearTime] = run(false);
		const auto [smoky, smokyTime] = run(true);
		CHECK(clearTime > 0.0f);
		CHECK(smokyTime < 0.0f);
		CHECK(smoky.Atmosphere < 0.1f);
		CHECK(smoky.Contrast < clear.Contrast * 0.1f);
	}

	TEST_CASE("Walls block sight completely and muffle noises by four levels")
	{
		StealthLab lab;
		lab.Wall(-9.0f, 0.3f, 2.0f);
		Test::CreateBox(lab.SceneInstance, "Partition", { 0.0f, 2.0f, -4.0f }, { 3.0f, 2.0f, 0.1f });
		lab.Start();
		lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(lab.Visibility().VisiblePoints == 0);
		CHECK(lab.TimeToSuspicion(0.3f, 3.0f) < 0.0f);

		PerceptionSystem& perception = *lab.SceneInstance.GetPerceptionSystem();
		const glm::vec3 behind(0.0f, 1.0f, -6.0f); // ~6.1 m from the ear, behind the partition
		const float open = PerceptionModel::HeardLevel(5.0f, glm::distance(behind, glm::vec3(0.0f, 1.65f, 0.0f)));
		CHECK(Test::Near(perception.HearingLevel(lab.Guard, behind, 5.0f), open - PerceptionModel::WallLoss, 0.01f));

		// A loudness-5 noise is muffled below hearing...
		perception.EmitNoise(behind, 5.0f, UUID::Invalid, "Crack");
		lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(lab.Perception().RuntimeState == AwarenessState::Unaware);
		// ...a loudness-9 one gets through faintly (9 - 4.2 for distance - 4 for the wall) and
		// only makes him wonder. Without the partition it would be investigated (see below).
		perception.EmitNoise(behind, 9.0f, UUID::Invalid, "Bucket");
		lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(lab.Perception().RuntimeState == AwarenessState::Suspicious);
		CHECK(Test::Near(lab.Perception().RuntimeReactionStrength, PerceptionModel::HeardLevel(9.0f, 6.1f) - 4.0f, 0.1f));
		CHECK(PerceptionModel::HeardLevel(9.0f, 6.1f) >= 4.0f);
		CHECK(lab.Perception().RuntimeHasStimulus);
		CHECK(lab.Perception().RuntimeStimulusCause == "Bucket");
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("A noise keeps a guard busy as long as it was strong, then he calms down")
	{
		StealthLab lab;
		lab.Wall(-9.0f, 0.3f);
		lab.Player.Transform().Translation = { 0.0f, 0.0f, 40.0f }; // far away and behind
		lab.Start();
		PerceptionSystem& perception = *lab.SceneInstance.GetPerceptionSystem();
		auto busyFor = [&](float loudness) {
			PerceptionSystem::Calm(lab.Perception());
			perception.EmitNoise({ 0.0f, 1.65f, -1.4f }, loudness, UUID::Invalid); // heard at full loudness
			float time = 0.0f;
			lab.SceneInstance.OnUpdateRuntime(Timestep(0.1f));
			while (lab.Perception().RuntimeState != AwarenessState::Unaware && time < 200.0f)
			{
				lab.SceneInstance.OnUpdateRuntime(Timestep(0.1f));
				time += 0.1f;
			}
			return time;
		};
		const float quiet = busyFor(3.0f);
		const float loud = busyFor(9.0f);
		CHECK(quiet > 6.0f);
		CHECK(quiet < 10.0f);  // 90 * 0.3^2 = 8.1 s
		CHECK(loud > 65.0f);   // 90 * 0.9^2 = 72.9 s
		CHECK(loud < 80.0f);
		CHECK(lab.Perception().RuntimeAlertness > 0.0f); // the loud one left him more alert

		// While reacting to a loud noise, quieter ones do not pull him around...
		PerceptionSystem::Calm(lab.Perception());
		perception.EmitNoise({ 0.0f, 1.65f, -1.4f }, 8.0f, UUID::Invalid, "Bucket");
		lab.SceneInstance.OnUpdateRuntime(Timestep(0.1f));
		perception.EmitNoise({ 1.4f, 1.65f, 0.0f }, 3.0f, UUID::Invalid, "Creak");
		lab.SceneInstance.OnUpdateRuntime(Timestep(0.1f));
		CHECK(lab.Perception().RuntimeStimulusCause == "Bucket");
		// ...and background noise masks quiet sounds completely.
		PerceptionSystem::Calm(lab.Perception());
		lab.SceneInstance.GetEnvironmentEntity().GetComponent<EnvironmentComponent>().AmbientNoise = 6.0f;
		perception.EmitNoise({ 0.0f, 1.65f, -1.4f }, 5.0f, UUID::Invalid);
		lab.SceneInstance.OnUpdateRuntime(Timestep(0.1f));
		CHECK(lab.Perception().RuntimeState == AwarenessState::Unaware);
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Targets behind the guard or out of range are never seen")
	{
		StealthLab lab;
		lab.Wall(15.0f, 0.3f, 2.0f);
		lab.Player.Transform().Translation = { 0.0f, 0.0f, 8.0f }; // behind
		lab.Start();
		lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(lab.Visibility().VisiblePoints == 0);
		lab.Player.Transform().Translation = { 0.0f, 0.0f, -40.0f }; // beyond ViewDistance
		CHECK(lab.Visibility().VisiblePoints == 0);
		lab.SceneInstance.OnRuntimeStop();
	}

	TEST_CASE("Analytic luminance follows lights, falloff and shadows")
	{
		StealthLab lab;
		lab.PointLight({ 0.0f, 2.0f, 0.0f }, 10.0f, 10.0f);
		lab.Start();
		lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		PerceptionSystem& perception = *lab.SceneInstance.GetPerceptionSystem();
		const float near = perception.SampleLuminance({ 0.0f, 1.0f, 0.0f });
		const float far = perception.SampleLuminance({ 0.0f, 1.0f, -6.0f });
		const float outOfRange = perception.SampleLuminance({ 0.0f, 1.0f, -15.0f });
		CHECK(near > far);
		CHECK(far > outOfRange);

		Test::CreateBox(lab.SceneInstance, "Blocker", { 0.0f, 1.5f, -3.0f }, { 2.0f, 2.0f, 0.1f });
		lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f)); // creates the blocker body
		CHECK(perception.SampleLuminance({ 0.0f, 1.0f, -6.0f }) < far * 0.5f);
		lab.SceneInstance.OnRuntimeStop();
	}
}

TEST_SUITE("Perception")
{
	// Run explicitly: PenumbraTests.exe -tc="Perception benchmark*" -nrs (not part of CI; prints timings).
	TEST_CASE("Perception benchmark: 8 guards, 12 torches" * doctest::skip())
	{
		StealthLab lab;
		lab.Wall(-14.0f, 0.3f, 0.5f, 30.0f);
		for (int i = 0; i < 12; i++)
		{
			Entity light = lab.PointLight({ -12.0f + 2.2f * static_cast<float>(i), 1.8f, -10.0f + static_cast<float>(i % 3) * 3.0f }, 12.0f, 9.0f);
			light.GetComponent<PointLightComponent>().Flicker = 0.1f;
		}
		for (int i = 0; i < 7; i++)
		{
			Entity guard = lab.SceneInstance.CreateEntity("Guard" + std::to_string(i));
			guard.Transform().Translation = { -9.0f + 3.0f * static_cast<float>(i), 0.0f, 2.0f };
			guard.AddComponent<PerceptionComponent>();
		}
		lab.Start();
		const float dt = 1.0f / 60.0f;
		Test::Simulate(lab.SceneInstance, 0.5f, dt);

		PerceptionSystem& perception = *lab.SceneInstance.GetPerceptionSystem();
		constexpr int Frames = 600;
		double total = 0.0, worst = 0.0;
		for (int frame = 0; frame < Frames; frame++)
		{
			lab.Player.Transform().Translation = { std::sin(frame * 0.05f) * 6.0f, 0.0f, -7.0f };
			const auto start = std::chrono::steady_clock::now();
			perception.Update(Timestep(dt));
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			total += ms;
			worst = std::max(worst, ms);
		}
		MESSAGE("perception update: average " << total / Frames << " ms, worst frame " << worst << " ms");
		lab.SceneInstance.OnRuntimeStop();
	}
}

TEST_SUITE("Perception")
{
	TEST_CASE("Light gem maps body luminance to 0..1 on a log scale")
	{
		CHECK(PerceptionModel::LightGem(0.0f) == 0.0f);
		CHECK(PerceptionModel::LightGem(0.002f) == doctest::Approx(0.0f).epsilon(0.01));
		CHECK(PerceptionModel::LightGem(10.0f) == 1.0f);
		CHECK(PerceptionModel::LightGem(0.01f) < PerceptionModel::LightGem(0.1f));
		CHECK(PerceptionModel::LightGem(0.035f) == doctest::Approx(0.5f).epsilon(0.05));
	}

	TEST_CASE("A player in a pool of overhead light on a bright floor is seen against a dark background")
	{
		// Marble-like floor, lamp straight above the player, dark hall behind him.
		auto run = [](bool lampOn) {
			StealthLab lab(0.004f); // an unlit hall at night: practically no ambient light
			lab.Wall(-12.0f, 0.03f);
			Test::CreateBox(lab.SceneInstance, "Marble", { 0.0f, 0.01f, -8.0f }, { 2.0f, 0.01f, 2.0f }, glm::vec4(0.85f, 0.85f, 0.85f, 1.0f));
			if (lampOn)
				lab.PointLight({ 0.0f, 3.2f, -8.0f }, 25.0f, 6.0f);
			lab.Start();
			const float time = lab.TimeToSuspicion(0.3f, 6.0f);
			const auto& target = lab.Player.GetComponent<PerceptionTargetComponent>();
			const float gem = target.RuntimeLightGem;
			lab.SceneInstance.OnRuntimeStop();
			return std::make_pair(time, gem);
		};
		const auto [litTime, litGem] = run(true);
		const auto [darkTime, darkGem] = run(false);
		CHECK(litTime > 0.0f);
		CHECK(litTime < 2.0f);
		CHECK(darkTime < 0.0f);
		CHECK(litGem > darkGem + 0.3f);
	}

	TEST_CASE("A rounded body lit from behind is still partly lit (rim light)")
	{
		PointLighting lighting;
		lighting.Ambient = 0.0f;
		lighting.Lights.push_back({ glm::vec3(0.0f, 0.0f, -1.0f), 10.0f });
		const glm::vec3 towardsViewer(0.0f, 0.0f, 1.0f);
		CHECK(lighting.Facing(towardsViewer) == 0.0f);              // flat wall facing away from the light: black
		CHECK(lighting.Rounded(towardsViewer) == 0.0f);             // exactly back-lit: only the rim, ~0
		CHECK(lighting.Rounded(glm::normalize(glm::vec3(1.0f, 0.0f, 1.0f))) > 1.0f); // side-ish view sees the lit half
		CHECK(lighting.Rounded(-towardsViewer) == doctest::Approx(10.0f));
		CHECK(lighting.Average() == doctest::Approx(5.0f));
	}

	TEST_CASE("Guards think on staggered frames and exposure does not flicker")
	{
		StealthLab lab;
		lab.Wall(-9.0f, 0.3f, 2.0f);
		Entity second = lab.SceneInstance.CreateEntity("Guard2");
		second.Transform().Translation = { 1.0f, 0.0f, 0.0f };
		second.AddComponent<PerceptionComponent>();
		lab.Start();
		Test::Simulate(lab.SceneInstance, 0.5f);
		for (int frame = 0; frame < 30; frame++)
		{
			lab.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			CHECK(lab.Player.GetComponent<PerceptionTargetComponent>().RuntimeExposure > 1.0f);
		}
		lab.SceneInstance.OnRuntimeStop();
	}
}
