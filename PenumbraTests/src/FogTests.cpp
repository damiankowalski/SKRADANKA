#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Scene/FogUtils.h"

using namespace Penumbra;

// Fog density (Scene/FogUtils.h): the environment's fog thinning with height and local fog boxes
// (FogVolume). The renderer lights the same density (shaders/Fog.hlsl; GPU test in RendererTests.cpp)
// and guards see through it the same way (PerceptionSystem::OpticalDepth).

namespace {

	// Midpoint rule over many steps: the reference for the closed forms.
	float NumericOpticalDepth(const FogMedium& medium, const glm::vec3& from, const glm::vec3& to, int steps = 20000)
	{
		const glm::vec3 step = (to - from) / static_cast<float>(steps);
		float depth = 0.0f;
		for (int i = 0; i < steps; i++)
			depth += medium.DensityAt(from + step * (static_cast<float>(i) + 0.5f));
		return depth * glm::length(step);
	}

}

TEST_SUITE("Fog")
{
	TEST_CASE("Height fog is full below its height and thins exponentially above it")
	{
		FogMedium medium;
		medium.Density = 0.1f;
		medium.Height = 1.0f;
		medium.HeightFalloff = 0.5f;
		CHECK(medium.GlobalDensityAt(-3.0f) == doctest::Approx(0.1f));
		CHECK(medium.GlobalDensityAt(1.0f) == doctest::Approx(0.1f));
		CHECK(medium.GlobalDensityAt(3.0f) == doctest::Approx(0.1f * std::exp(-1.0f)));

		// Along a level line, a rising line and one crossing the fog's top: the closed form matches the integral.
		CHECK(medium.OpticalDepth({ 0, 0.5f, 0 }, { 10, 0.5f, 0 }) == doctest::Approx(1.0f));
		CHECK(medium.OpticalDepth({ 0, 4, 0 }, { 10, 4, 0 }) == doctest::Approx(1.0f * std::exp(-1.5f)));
		for (const auto& [from, to] : { std::pair{ glm::vec3(0, 0, 0), glm::vec3(8, 6, 0) }, std::pair{ glm::vec3(0, 7, 0), glm::vec3(5, -2, 3) } })
			CHECK(medium.OpticalDepth(from, to) == doctest::Approx(NumericOpticalDepth(medium, from, to)).epsilon(0.002));

		// No falloff: the same everywhere, as the fog always was.
		medium.HeightFalloff = 0.0f;
		CHECK(medium.OpticalDepth({ 0, 0, 0 }, { 0, 30, 40 }) == doctest::Approx(5.0f));
	}

	TEST_CASE("A fog box adds its density inside, fading in over its edge, turned with its entity")
	{
		FogMedium medium;
		FogBox box;
		box.Center = { 0.0f, 1.0f, 0.0f };
		box.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		box.HalfExtents = { 4.0f, 1.0f, 2.0f };
		box.Density = 0.5f;
		box.EdgeFade = 0.5f;
		medium.Boxes.push_back(box);

		CHECK(medium.DensityAt({ 0.0f, 1.0f, 0.0f }) == doctest::Approx(0.5f));
		CHECK(medium.DensityAt({ 0.0f, 1.75f, 0.0f }) == doctest::Approx(0.25f)); // half way through the top's fade
		CHECK(medium.DensityAt({ 0.0f, 2.5f, 0.0f }) == doctest::Approx(0.0f));
		// The box's long axis runs along the turned X: 3 m out that way is inside, 3 m along world X is not.
		const glm::vec3 alongBox = box.Rotation * glm::vec3(3.0f, 0.0f, 0.0f);
		CHECK(medium.DensityAt(box.Center + alongBox) > 0.4f);
		CHECK(medium.DensityAt(box.Center + glm::vec3(3.0f, 0.0f, 0.0f)) == doctest::Approx(0.0f));

		// Through it and past it, at a slant: the sampled integral matches a fine one.
		const glm::vec3 from(-6.0f, 0.5f, -5.0f), to(5.0f, 1.6f, 4.0f);
		CHECK(medium.OpticalDepth(from, to) == doctest::Approx(NumericOpticalDepth(medium, from, to)).epsilon(0.03));
		CHECK(medium.OpticalDepth({ -6.0f, 5.0f, 0.0f }, { 6.0f, 5.0f, 0.0f }) == doctest::Approx(0.0f)); // above it
	}

	TEST_CASE("A Clear volume keeps the environment's fog out of a room, creeping in over its edge; fog boxes inside stay")
	{
		FogMedium medium;
		medium.Density = 0.2f;
		medium.Height = 1.0f;
		medium.HeightFalloff = 0.4f;
		FogBox room;
		room.Center = { 0.0f, 2.0f, 0.0f };
		room.Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		room.HalfExtents = { 4.0f, 2.0f, 3.0f };
		room.EdgeFade = 1.0f;
		medium.Clears.push_back(room);

		CHECK(medium.ClearAt({ 0.0f, 2.0f, 0.0f }) == doctest::Approx(1.0f));
		CHECK(medium.DensityAt({ 0.0f, 1.0f, 0.0f }) == doctest::Approx(0.0f));
		// In the doorway: half a metre inside the side, half the fog.
		const glm::vec3 doorway = room.Center + room.Rotation * glm::vec3(3.5f, -1.0f, 0.0f);
		CHECK(medium.ClearAt(doorway) == doctest::Approx(0.5f));
		CHECK(medium.DensityAt(doorway) == doctest::Approx(0.5f * medium.GlobalDensityAt(doorway.y)));
		// Outside: untouched.
		CHECK(medium.DensityAt({ 10.0f, 0.5f, 0.0f }) == doctest::Approx(0.2f));
		CHECK_FALSE(medium.IsEmpty());

		// A haze box inside the room stays: the room keeps out only the level's fog.
		FogBox haze;
		haze.Center = { 0.0f, 1.0f, 0.0f };
		haze.HalfExtents = { 1.0f, 1.0f, 1.0f };
		haze.Density = 0.3f;
		haze.EdgeFade = 0.0f;
		medium.Boxes.push_back(haze);
		CHECK(medium.DensityAt({ 0.0f, 1.0f, 0.0f }) == doctest::Approx(0.3f));

		// A second room overlapping the first is not taken away twice; lines from outside, through the doorway,
		// through both rooms and along the floor: the sampled integral matches a fine one.
		FogBox annex = room;
		annex.Center = { 3.0f, 2.0f, 2.0f };
		annex.Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
		medium.Clears.push_back(annex);
		CHECK(medium.ClearAt({ 2.0f, 2.0f, 1.0f }) == doctest::Approx(1.0f));
		for (const auto& [from, to] : { std::pair{ glm::vec3(-12, 1.6f, -3), glm::vec3(10, 1.4f, 5) }, std::pair{ glm::vec3(0, 1.7f, 0), glm::vec3(15, 0.5f, -2) },
				 std::pair{ glm::vec3(-8, 0.2f, 0), glm::vec3(8, 0.2f, 0.5f) }, std::pair{ glm::vec3(1, 3.5f, 1), glm::vec3(-6, 5, -9) } })
		{
			CAPTURE(from);
			CAPTURE(to);
			CHECK(medium.OpticalDepth(from, to) == doctest::Approx(NumericOpticalDepth(medium, from, to)).epsilon(0.02));
		}
		// Wholly inside a room: only the haze box counts.
		CHECK(medium.OpticalDepth({ -2.0f, 3.0f, 0.0f }, { 2.0f, 3.0f, 0.0f }) == doctest::Approx(0.0f));
	}

	TEST_CASE("The scene's medium: environment fog and enabled fog volumes")
	{
		Test::TemporaryProject project;
		Scene scene;
		auto& environment = scene.CreateEntity("Environment").AddComponent<EnvironmentComponent>();
		environment.FogDensity = 0.05f;
		environment.FogHeight = 2.0f;
		environment.FogHeightFalloff = 0.3f;
		Entity mist = scene.CreateEntity("Mist");
		mist.Transform().Translation = { 1.0f, 0.5f, 2.0f };
		auto& volume = mist.AddComponent<FogVolumeComponent>();
		volume.Density = 0.2f;
		scene.CreateEntity("Off").AddComponent<FogVolumeComponent>().Enabled = false;
		Entity hall = scene.CreateEntity("Hall");
		hall.Transform().Translation = { 20.0f, 2.0f, 0.0f };
		auto& clear = hall.AddComponent<FogVolumeComponent>();
		clear.Mode = FogVolumeMode::Clear;
		clear.Density = 0.0f; // not used by a Clear volume
		clear.HalfExtents = { 3.0f, 2.0f, 3.0f };

		const FogMedium medium = FogMedium::FromScene(scene);
		REQUIRE(medium.Clears.size() == 1);
		CHECK(medium.DensityAt({ 20.0f, 1.0f, 0.0f }) == doctest::Approx(0.0f));
		CHECK(medium.Density == doctest::Approx(0.05f));
		CHECK(medium.Height == doctest::Approx(2.0f));
		CHECK(medium.HeightFalloff == doctest::Approx(0.3f));
		REQUIRE(medium.Boxes.size() == 1);
		CHECK(Test::Near(medium.Boxes[0].Center, { 1.0f, 0.5f, 2.0f }));
		CHECK(medium.DensityAt({ 1.0f, 0.5f, 2.0f }) == doctest::Approx(0.25f));
	}

	TEST_CASE("The fog's glow: an evenly lit fog adds its colour as it hides what is behind (S = C (1 - T))")
	{
		FogMedium medium;
		medium.Density = 0.1f;
		medium.ColorLuminance = 0.02f;
		const glm::vec3 from(0.0f, 1.0f, 0.0f), to(0.0f, 1.0f, -12.0f);
		const FogPath path = medium.Integrate(from, to, {});
		const float transmittance = std::exp(-0.1f * 12.0f);
		CHECK(path.Transmittance == doctest::Approx(transmittance).epsilon(1e-4));
		CHECK(path.InScattered == doctest::Approx(0.02f * (1.0f - transmittance)).epsilon(1e-4));

		// Lamps add FogScattering of their light where the fog is; nowhere without fog.
		medium.Scattering = 0.5f;
		int calls = 0;
		const FogPath lit = medium.Integrate(from, to, [&](const glm::vec3&) {
			calls++;
			return 1.0f;
		});
		CHECK(lit.InScattered == doctest::Approx((0.02f + 0.5f) * (1.0f - transmittance)).epsilon(1e-4));
		CHECK(calls > 0);

		// No fog: nothing on the way, and no lamp is asked.
		const FogMedium clear;
		CHECK(clear.IsEmpty());
		calls = 0;
		const FogPath none = clear.Integrate(from, to, [&](const glm::vec3&) {
			calls++;
			return 1.0f;
		});
		CHECK(none.Transmittance == 1.0f);
		CHECK(none.InScattered == 0.0f);
		CHECK(calls == 0);
	}

	TEST_CASE("The phase function scatters evenly at 0, forwards when positive, and keeps the light's total")
	{
		CHECK(FogPhase(0.3f, 0.0f) == doctest::Approx(1.0f));
		CHECK(FogPhase(-0.8f, 0.0f) == doctest::Approx(1.0f));
		CHECK(FogPhase(1.0f, 0.4f) > 1.0f);  // looking into the lamp: glare
		CHECK(FogPhase(-1.0f, 0.4f) < 1.0f); // the lamp behind the eye
		// Averaged over every direction it is 1, whatever the anisotropy.
		for (const float g : { 0.0f, 0.3f, 0.7f })
		{
			const int steps = 4000;
			float sum = 0.0f;
			for (int i = 0; i < steps; i++)
				sum += FogPhase(-1.0f + 2.0f * (static_cast<float>(i) + 0.5f) / static_cast<float>(steps), g);
			CHECK(sum / static_cast<float>(steps) == doctest::Approx(1.0f).epsilon(0.01));
		}
	}

	TEST_CASE("The medium reads how the fog is lit; without volumetric fog only the environment's fog counts, as on screen")
	{
		Test::TemporaryProject project;
		Scene scene;
		auto& environment = scene.CreateEntity("Environment").AddComponent<EnvironmentComponent>();
		environment.FogDensity = 0.05f;
		environment.FogColor = glm::vec3(0.1f, 0.2f, 0.3f);
		environment.FogScattering = 0.25f;
		environment.FogAnisotropy = 0.6f;
		scene.CreateEntity("Mist").AddComponent<FogVolumeComponent>().Density = 0.2f;

		FogMedium medium = FogMedium::FromScene(scene);
		CHECK(medium.ColorLuminance == doctest::Approx(Math::Luminance(glm::vec3(0.1f, 0.2f, 0.3f))));
		CHECK(medium.Scattering == doctest::Approx(0.25f));
		CHECK(medium.Anisotropy == doctest::Approx(0.6f));
		CHECK(medium.Volumetric);
		CHECK(medium.Boxes.size() == 1);

		environment.VolumetricFog = false;
		medium = FogMedium::FromScene(scene);
		CHECK_FALSE(medium.Volumetric);
		CHECK(medium.Boxes.empty());
		int calls = 0;
		medium.Integrate({ 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, -10.0f }, [&](const glm::vec3&) {
			calls++;
			return 1.0f;
		});
		CHECK(calls == 0); // lamps do not light it
	}

	TEST_CASE("Guards see through the fog the player sees: a bank of mist hides the thief, height fog does not reach a high one")
	{
		// A guard at the origin looking down -Z; the thief 12 m away, lit, in the open.
		auto visibility = [](const std::function<void(Scene&)>& setup, float thiefHeight = 0.0f) {
			Test::TemporaryProject project;
			Scene scene;
			scene.SetAudioDeviceEnabled(false);
			auto& environment = scene.CreateEntity("Environment").AddComponent<EnvironmentComponent>();
			environment.AmbientColor = glm::vec3(0.0f);
			Test::CreateBox(scene, "Floor", { 0.0f, -0.5f, 0.0f }, { 30.0f, 0.5f, 30.0f });
			Entity sun = scene.CreateEntity("Sun");
			sun.Transform().Rotation = Math::LookRotation(glm::vec3(0.2f, -1.0f, -0.3f));
			sun.AddComponent<DirectionalLightComponent>().Intensity = 3.0f;
			Entity guard = scene.CreateEntity("Guard");
			guard.Transform().Translation = { 0.0f, 0.05f, 0.0f };
			guard.AddComponent<PerceptionComponent>();
			Entity thief = scene.CreateEntity("Thief");
			thief.Transform().Translation = { 0.0f, 0.05f + thiefHeight, -12.0f };
			thief.AddComponent<PerceptionTargetComponent>();
			setup(scene);
			scene.OnRuntimeStart();
			scene.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			const VisibilityResult result = scene.GetPerceptionSystem()->ComputeVisibility(guard, thief);
			scene.OnRuntimeStop();
			return result;
		};

		const VisibilityResult clear = visibility([](Scene&) {});
		const VisibilityResult mist = visibility([](Scene& scene) {
			Entity bank = scene.CreateEntity("Mist");
			bank.Transform().Translation = { 0.0f, 1.0f, -6.0f };
			auto& volume = bank.AddComponent<FogVolumeComponent>();
			volume.HalfExtents = { 4.0f, 2.0f, 3.0f };
			volume.Density = 1.0f;
		});
		// The fog on the sight line (the glow model: Atmosphere is smoke's alone now).
		CHECK(clear.FogTransmittance > 0.9f);
		CHECK(mist.FogTransmittance < 0.05f);
		CHECK(mist.Stimulus < clear.Stimulus * 0.1f);

		// Ground fog up to 1 m, very thin above: thick between guard and thief on the ground, less so for a thief
		// standing 3 m up (his points sit above the layer, the guard's eye at 1.65 m is above it too).
		auto groundFog = [](Scene& scene) {
			auto& environment = scene.GetEnvironmentEntity().GetComponent<EnvironmentComponent>();
			environment.FogDensity = 0.3f;
			environment.FogHeight = 1.0f;
			environment.FogHeightFalloff = 3.0f;
		};
		const VisibilityResult low = visibility(groundFog);
		const VisibilityResult high = visibility(groundFog, 3.0f);
		CHECK(high.FogTransmittance > low.FogTransmittance * 2.0f);

		// Thick fog over the level, but guard and thief stand in a hall that keeps it out: he sees as in clear air.
		auto thickFog = [](Scene& scene) { scene.GetEnvironmentEntity().GetComponent<EnvironmentComponent>().FogDensity = 0.3f; };
		const VisibilityResult foggy = visibility(thickFog);
		const VisibilityResult indoors = visibility([&](Scene& scene) {
			thickFog(scene);
			Entity hall = scene.CreateEntity("Hall");
			hall.Transform().Translation = { 0.0f, 2.0f, -6.0f };
			auto& volume = hall.AddComponent<FogVolumeComponent>();
			volume.Mode = FogVolumeMode::Clear;
			volume.HalfExtents = { 5.0f, 2.5f, 9.0f };
		});
		CHECK(foggy.FogTransmittance < 0.05f);
		CHECK(indoors.FogTransmittance == doctest::Approx(1.0f).epsilon(0.001));
		// The sight line beyond him (to the sky behind the hall) still crosses the level's fog: within 10%.
		CHECK(indoors.Stimulus == doctest::Approx(clear.Stimulus).epsilon(0.1));
	}
}
