#include "TestHelpers.h"

#include "Penumbra/AI/PerceptionSystem.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/Core/Platform.h"
#include "Penumbra/Debug/DebugOverlays.h"
#include "Penumbra/Debug/FreeCamera.h"
#include "Penumbra/Debug/NoiseProbe.h"
#include "Penumbra/Debug/PerformanceMonitor.h"
#include "Penumbra/Renderer/DebugDraw.h"

#include <algorithm>

using namespace Penumbra;

// The runtime's testing tools (PenumbraRuntime/src/RuntimeDebugPanel): the performance monitor's
// bookkeeping, the freelook camera, game input blocked while the panel is up, the debug view of
// what guards see (fans cut by walls, sight lines to the thief), test noises and "guards ignore
// the thief". The ImGui panel itself is only glue over these.

namespace {

	// A guard at the origin looking down -Z, a wall 3 m in front of him (2 m wide, centred on
	// x = 0) and the thief wherever the test puts him. Dark: nothing here is about light.
	struct GuardAndWall
	{
		Test::TemporaryProject Project;
		Scene SceneInstance;
		Entity Guard;
		Entity Thief;

		// firstFrame: run one frame after starting (systems have seen the scene once).
		explicit GuardAndWall(const glm::vec3& thiefPosition, bool firstFrame = true)
		{
			SceneInstance.SetAudioDeviceEnabled(false);
			SceneInstance.CreateEntity("Environment").AddComponent<EnvironmentComponent>().AmbientColor = glm::vec3(0.0f);
			Test::CreateBox(SceneInstance, "Floor", { 0.0f, -0.5f, 0.0f }, { 30.0f, 0.5f, 30.0f });
			Test::CreateBox(SceneInstance, "Wall", { 0.0f, 1.5f, -3.1f }, { 1.0f, 1.5f, 0.1f });

			Guard = SceneInstance.CreateEntity("Guard");
			Guard.Transform().Translation = { 0.0f, 0.05f, 0.0f };
			Guard.AddComponent<PerceptionComponent>();
			Guard.AddComponent<CharacterControllerComponent>();

			Thief = SceneInstance.CreateEntity("Thief");
			Thief.Transform().Translation = thiefPosition + glm::vec3(0.0f, 0.05f, 0.0f);
			Thief.AddComponent<PerceptionTargetComponent>();
			Thief.AddComponent<CharacterControllerComponent>();
			SceneInstance.OnRuntimeStart();
			if (firstFrame)
				SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		}

		~GuardAndWall() { SceneInstance.OnRuntimeStop(); }

		PerceptionComponent& Perception() { return Guard.GetComponent<PerceptionComponent>(); }
	};

	glm::vec3 YawForward(float yawDegrees)
	{
		return glm::angleAxis(glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::vec3(0.0f, 0.0f, -1.0f);
	}

}

TEST_SUITE("Debug")
{
	TEST_CASE("The performance monitor averages frames over half a second and measures CPU from process time")
	{
		PerformanceMonitor monitor(4);
		Platform::ProcessUsage usage;
		usage.ResidentBytes = 300ull << 20;
		usage.PeakResidentBytes = 320ull << 20;
		double wall = 100.0;
		PerformanceSample sample;
		sample.FrameMs = 1000.0f / 60.0f;
		sample.UpdateMs = 4.0f;
		sample.RenderMs = 2.0f;
		sample.GpuMs = 5.0f;
		sample.Systems.Physics = 1.5f;
		sample.Systems.Perception = 0.5f;

		monitor.AddFrame(sample, wall, usage); // starts the clocks
		CHECK(monitor.GetSummary().Frames == 0);
		for (int frame = 0; frame < 30; ++frame) // half a second at 60 FPS, the process half as busy as one core
		{
			wall = 100.0 + (frame + 1) / 60.0;
			usage.CpuSeconds = 0.5 * (frame + 1) / 60.0;
			sample.FrameMs = frame == 10 ? 30.0f : 1000.0f / 60.0f; // one hitch
			monitor.AddFrame(sample, wall, usage);
		}
		const PerformanceMonitor::Summary& summary = monitor.GetSummary();
		CHECK(summary.Frames == 30);
		CHECK(summary.Fps == doctest::Approx(60.0f).epsilon(0.01));
		CHECK(summary.FrameMsMax == doctest::Approx(30.0f));
		CHECK(summary.FrameMsMin == doctest::Approx(1000.0f / 60.0f));
		CHECK(summary.FrameMsAverage > 1000.0f / 60.0f);
		CHECK(summary.CpuCorePercent == doctest::Approx(50.0f).epsilon(0.01));
		CHECK(summary.CpuMachinePercent == doctest::Approx(12.5f).epsilon(0.01)); // of four cores
		CHECK(summary.UpdateMsAverage == doctest::Approx(4.0f));
		CHECK(summary.RenderMsAverage == doctest::Approx(2.0f));
		REQUIRE(summary.GpuMsAverage.has_value());
		CHECK(*summary.GpuMsAverage == doctest::Approx(5.0f));
		CHECK(summary.Systems.Physics == doctest::Approx(1.5f));
		CHECK(summary.Systems.Total() == doctest::Approx(2.0f));
		CHECK(summary.ResidentBytes == 300ull << 20);

		// The graph keeps the last frame times, oldest first from the offset.
		const auto& history = monitor.GetFrameHistory();
		const size_t newest = (monitor.GetHistoryOffset() + PerformanceMonitor::HistorySize - 1) % PerformanceMonitor::HistorySize;
		CHECK(history[newest] == doctest::Approx(1000.0f / 60.0f));
		CHECK(std::count(history.begin(), history.end(), 30.0f) == 1);

		// Frames without a GPU time (no timer yet) report none.
		PerformanceMonitor noGpu(1);
		sample.GpuMs.reset();
		for (int frame = 0; frame < 40; ++frame)
			noGpu.AddFrame(sample, frame / 60.0, usage);
		CHECK_FALSE(noGpu.GetSummary().GpuMsAverage.has_value());
	}

	TEST_CASE("The process's CPU time and memory are read from the operating system")
	{
		const Platform::ProcessUsage before = Platform::GetProcessUsage();
		CHECK(before.ResidentBytes > 1ull << 20);
		CHECK(before.PeakResidentBytes >= before.ResidentBytes);
		volatile double sink = 0.0;
		for (int i = 0; i < 20'000'000; ++i)
			sink = sink + std::sqrt(static_cast<double>(i));
		CHECK(Platform::GetProcessUsage().CpuSeconds > before.CpuSeconds);
		CHECK(Platform::GetLogicalCoreCount() >= 1);
		CHECK(Platform::GetPhysicalMemoryBytes() > 256ull << 20);
	}

	TEST_CASE("Game input is blocked while the debug panel or freelook has it")
	{
		Input::Reset();
		Input::SetKeyState(Key::W, true);
		Input::SetMouseButtonState(Mouse::ButtonLeft, true);
		Input::SetMousePosition({ 10.0f, 10.0f });
		Input::SetMousePosition({ 30.0f, 10.0f });
		CHECK(Input::IsKeyPressed(Key::W));

		Input::SetGameInputBlocked(true);
		CHECK(Input::IsGameInputBlocked());
		CHECK_FALSE(Input::IsKeyDown(Key::W));
		CHECK_FALSE(Input::IsKeyPressed(Key::W));
		CHECK_FALSE(Input::IsMouseButtonDown(Mouse::ButtonLeft));
		CHECK(Input::GetMouseDelta() == glm::vec2(0.0f));
		Input::EndFrame();

		// Unblocked with the key still held: held, but not pressed again.
		Input::SetGameInputBlocked(false);
		CHECK(Input::IsKeyDown(Key::W));
		CHECK_FALSE(Input::IsKeyPressed(Key::W));
		Input::Reset();
	}

	TEST_CASE("The free camera starts where the game camera is and flies where it looks")
	{
		const glm::vec3 start(2.0f, 3.0f, 4.0f);
		const glm::quat rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)) *
								   glm::angleAxis(glm::radians(-20.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		const RenderCamera game = RenderCamera::Perspective(start, rotation, 55.0f, 16.0f / 9.0f, 0.1f, 300.0f);

		FreeCamera camera;
		camera.StartFrom(game);
		CHECK(Test::Near(camera.GetPosition(), start));
		CHECK(camera.GetYaw() == doctest::Approx(30.0f).epsilon(0.001));
		CHECK(camera.GetPitch() == doctest::Approx(-20.0f).epsilon(0.001));
		const RenderCamera same = camera.GetRenderCamera(16.0f / 9.0f);
		CHECK(same.VerticalFOV == doctest::Approx(55.0f));
		for (int column = 0; column < 4; ++column)
			CHECK(Test::Near(glm::vec3(same.View[column]), glm::vec3(game.View[column]), 1e-4f));

		// One second of W at 6 m/s along the view; Shift is four times faster.
		camera.SetPose({ 0.0f, 1.0f, 0.0f }, 90.0f, 0.0f); // looking down -X
		camera.SetSpeed(6.0f);
		FreeCameraInput input;
		input.Move = { 0.0f, 0.0f, 1.0f };
		camera.Update(input, 1.0f);
		CHECK(Test::Near(camera.GetPosition(), glm::vec3(-6.0f, 1.0f, 0.0f), 1e-3f));
		input.Move = { 0.0f, 1.0f, 0.0f };
		input.Fast = true;
		camera.Update(input, 0.5f);
		CHECK(camera.GetPosition().y == doctest::Approx(13.0f));

		// The mouse turns it (right turns right, down looks down); pitch stops short of straight down.
		FreeCameraInput look;
		look.Look = { 100.0f, 0.0f };
		camera.Update(look, 0.016f);
		CHECK(camera.GetYaw() == doctest::Approx(90.0f - 100.0f * FreeCamera::LookSensitivity));
		look.Look = { 0.0f, 10000.0f };
		camera.Update(look, 0.016f);
		CHECK(camera.GetPitch() == doctest::Approx(-89.0f));

		// The wheel changes the speed by 20% a step.
		FreeCameraInput wheel;
		wheel.SpeedSteps = 2.0f;
		camera.Update(wheel, 0.016f);
		CHECK(camera.GetSpeed() == doctest::Approx(6.0f * 1.44f));
	}

	TEST_CASE("A click on the screen becomes a ray into the world, and world points land on the pixel they are drawn at")
	{
		const RenderCamera camera =
			RenderCamera::Perspective({ 0.0f, 2.0f, 0.0f }, glm::angleAxis(glm::radians(40.0f), glm::vec3(0.0f, 1.0f, 0.0f)), 60.0f, 2.0f, 0.1f, 100.0f);
		const glm::vec2 viewport(1600.0f, 800.0f);
		CHECK(Test::Near(ScreenRayDirection(camera, viewport * 0.5f, viewport), YawForward(40.0f), 1e-4f));

		const glm::vec3 point(3.0f, 0.5f, -7.0f);
		glm::vec2 pixel;
		REQUIRE(WorldToScreen(camera, point, viewport, pixel));
		const glm::vec3 direction = ScreenRayDirection(camera, pixel, viewport);
		CHECK(Test::Near(direction, glm::normalize(point - camera.Position), 1e-4f));
		CHECK_FALSE(WorldToScreen(camera, camera.Position - YawForward(40.0f), viewport, pixel)); // behind
	}

	TEST_CASE("A guard's view fan covers his whole field of view and stops at walls")
	{
		GuardAndWall level({ 6.0f, 0.0f, 0.0f });
		const PerceptionComponent& perception = level.Perception();
		const std::vector<VisionFanRay> fan = DebugOverlays::ComputeVisionFan(level.SceneInstance, level.Guard, 5.0f);
		REQUIRE(fan.size() == 27); // 130 degrees in 5 degree steps, both edges included

		const glm::vec3 eye(0.0f, 0.05f + perception.EyeHeight, 0.0f);
		size_t focus = 0;
		for (const VisionFanRay& ray : fan)
			focus += ray.Focus ? 1 : 0;
		CHECK(focus == 19); // 90 degrees of focus: -45..45 in 5 degree steps
		CHECK_FALSE(fan.front().Focus);
		CHECK(fan[13].Focus);

		// Straight ahead the wall stops the eye at its face (z = -3); far to the side it sees 25 m.
		CHECK(fan[13].Blocked);
		CHECK(fan[13].End.z == doctest::Approx(-3.0f).epsilon(0.01));
		CHECK_FALSE(fan.front().Blocked);
		CHECK(glm::distance(fan.front().End, eye) == doctest::Approx(perception.ViewDistance));
		// The edges are 65 degrees off his forward direction, on the left and the right.
		const glm::vec3 left = glm::normalize(fan.front().End - eye);
		CHECK(glm::degrees(std::acos(glm::dot(left, glm::vec3(0.0f, 0.0f, -1.0f)))) == doctest::Approx(65.0f).epsilon(0.01));

		// The thief standing in the fan does not cut it (only solid things do).
		level.Thief.Transform().Translation = { 0.0f, 0.05f, -1.5f };
		level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(DebugOverlays::ComputeVisionFan(level.SceneInstance, level.Guard, 5.0f)[13].End.z == doctest::Approx(-3.0f).epsilon(0.01));

		// Drawing it adds lines.
		DebugDraw debug;
		DebugOverlays::DrawVision(level.SceneInstance, debug);
		CHECK(debug.GetVertices().size() > fan.size() * 2);
	}

	TEST_CASE("Sight lines tell which of the thief's body points a guard has in view")
	{
		auto lines = [](const glm::vec3& thief) {
			GuardAndWall level(thief);
			return DebugOverlays::ComputeSightLines(level.SceneInstance, level.Guard, level.Thief);
		};
		auto all = [](const std::vector<SightLine>& found, SightLine kind) {
			return !found.empty() && std::all_of(found.begin(), found.end(), [kind](SightLine line) { return line == kind; });
		};

		CHECK(all(lines({ 0.0f, 0.0f, -2.0f }), SightLine::Focus));     // in front, before the wall
		CHECK(all(lines({ 0.0f, 0.0f, -6.0f }), SightLine::Occluded));  // behind the wall
		CHECK(all(lines({ 0.0f, 0.0f, 3.0f }), SightLine::OutOfView));  // behind his back
		const float side = glm::radians(55.0f); // between the focus (45) and the edge (65)
		CHECK(all(lines({ 2.5f * std::sin(side), 0.0f, -2.5f * std::cos(side) }), SightLine::Peripheral));
	}

	TEST_CASE("A test noise reports who hears it and how they will react, then they do")
	{
		GuardAndWall level({ 10.0f, 0.0f, 10.0f });
		// 4 m away in the open (4.3 m to his ears): 6 - 2 * log2(4.3 / 1.4) = 2.8, he stops and looks (Suspicious).
		std::vector<NoiseHearing> heard = NoiseProbe::Emit(level.SceneInstance, { 4.0f, 0.2f, 0.0f }, 6.0f);
		REQUIRE(heard.size() == 1);
		CHECK(heard[0].Npc == level.Guard.GetUUID());
		CHECK(heard[0].Name == "Guard");
		CHECK(heard[0].Level == doctest::Approx(2.8f).epsilon(0.03));
		CHECK(heard[0].State == AwarenessState::Suspicious);
		level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(level.Perception().RuntimeState == AwarenessState::Suspicious);
		CHECK(level.Perception().RuntimeStimulusCause == "Debug");

		// Louder: he walks over (Investigating) and the reaction replaces the weaker one.
		heard = NoiseProbe::Emit(level.SceneInstance, { 4.0f, 0.2f, 0.0f }, 8.0f);
		REQUIRE(heard.size() == 1);
		CHECK(heard[0].State == AwarenessState::Investigating);
		level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(level.Perception().RuntimeState == AwarenessState::Investigating);

		// Too quiet to carry that far: nobody.
		CHECK(NoiseProbe::Emit(level.SceneInstance, { 20.0f, 0.2f, 0.0f }, 2.0f).empty());

		// The reference table runs from quiet to loud.
		const std::vector<NoiseReference>& references = NoiseProbe::References();
		REQUIRE(references.size() >= 8);
		CHECK(std::is_sorted(references.begin(), references.end(),
			[](const NoiseReference& a, const NoiseReference& b) { return a.Loudness < b.Loudness; }));
		CHECK(references.back().Loudness == 10.0f);
	}

	TEST_CASE("Guards can be told to ignore the thief: no sight, no touch, no footsteps; other noises still count")
	{
		// Right in front of him, close enough to be made out in the dark, and bumping into him.
		GuardAndWall level({ 0.0f, 0.0f, -0.65f }, false);
		PerceptionSystem& perception = *level.SceneInstance.GetPerceptionSystem();
		perception.SetIgnoreIntruders(true);
		CHECK(perception.IsIgnoringIntruders());
		perception.EmitNoise({ 0.0f, 0.1f, -0.65f }, 9.0f, level.Thief.GetUUID(), "Footstep"); // his own noise
		level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(perception.GetRecentNoises().size() == 1); // processed (and shown by the panel), heard by nobody
		Test::Simulate(level.SceneInstance, 3.0f);
		CHECK(level.Perception().RuntimeState == AwarenessState::Unaware);

		// A noise someone else made is heard as usual.
		perception.EmitNoise({ 2.0f, 0.1f, 0.0f }, 6.0f, UUID::Invalid, "Bucket");
		level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
		CHECK(level.Perception().RuntimeState != AwarenessState::Unaware);
		CHECK(level.Perception().RuntimeStimulusCause == "Bucket");

		// Back to normal: the thief at his nose is noticed.
		PerceptionSystem::Calm(level.Perception());
		perception.SetIgnoreIntruders(false);
		Test::Simulate(level.SceneInstance, 1.0f);
		CHECK(level.Perception().RuntimeState == AwarenessState::Alarmed);
	}

	TEST_CASE("The scene times each system of a runtime frame")
	{
		GuardAndWall level({ 0.0f, 0.0f, 5.0f });
		Test::Simulate(level.SceneInstance, 0.25f);
		const SceneFrameProfile& profile = level.SceneInstance.GetFrameProfile();
		CHECK(profile.Physics >= 0.0f);
		CHECK(profile.Perception >= 0.0f);
		CHECK(profile.Total() > 0.0f);
		CHECK(profile.Total() == doctest::Approx(profile.Scripts + profile.Navigation + profile.Physics + profile.Contacts + profile.Stims +
												 profile.Perception + profile.Smoke + profile.Audio + profile.Ragdolls + profile.Animation +
												 profile.Destruction));
	}
}
