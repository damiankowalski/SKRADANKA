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

		const FogMedium medium = FogMedium::FromScene(scene);
		CHECK(medium.Density == doctest::Approx(0.05f));
		CHECK(medium.Height == doctest::Approx(2.0f));
		CHECK(medium.HeightFalloff == doctest::Approx(0.3f));
		REQUIRE(medium.Boxes.size() == 1);
		CHECK(Test::Near(medium.Boxes[0].Center, { 1.0f, 0.5f, 2.0f }));
		CHECK(medium.DensityAt({ 1.0f, 0.5f, 2.0f }) == doctest::Approx(0.25f));
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
		CHECK(clear.Atmosphere > 0.9f);
		CHECK(mist.Atmosphere < 0.05f);
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
		CHECK(high.Atmosphere > low.Atmosphere * 2.0f);
	}
}
