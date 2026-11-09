#include "TestHelpers.h"

#include "Penumbra/Core/CommandLine.h"
#include "Penumbra/Core/Input.h"
#include "Penumbra/Core/KeyCodes.h"
#include "Penumbra/Core/Log.h"
#include "Penumbra/Math/Math.h"

#include <unordered_set>

using namespace Penumbra;

TEST_SUITE("Core")
{
	TEST_CASE("UUIDs are non-zero and unique")
	{
		std::unordered_set<uint64_t> seen;
		for (int i = 0; i < 10000; i++)
		{
			const UUID uuid;
			CHECK(uuid.IsValid());
			CHECK(seen.insert(uuid).second);
		}
		CHECK_FALSE(UUID(UUID::Invalid).IsValid());
	}

	TEST_CASE("CommandLine parses flags and values")
	{
		const CommandLine args({ "--headless", "--frames", "120", "--scene=Scenes/Test.pscene", "--bad=x1", "--last" });
		CHECK(args.HasFlag("headless"));
		CHECK(args.HasFlag("scene"));
		CHECK_FALSE(args.HasFlag("head"));
		CHECK(args.GetValue("frames") == "120");
		CHECK(args.GetInt("frames") == 120);
		CHECK(args.GetValue("scene") == "Scenes/Test.pscene");
		CHECK_FALSE(args.GetInt("bad").has_value());
		CHECK_FALSE(args.GetValue("last").has_value());
		CHECK_FALSE(args.GetValue("missing").has_value());
	}

	TEST_CASE("Key names round-trip")
	{
		for (const char* name : { "A", "Z", "0", "9", "F1", "F12", "Space", "LeftShift", "Escape", "Enter" })
		{
			const std::optional<KeyCode> key = KeyCodeFromName(name);
			REQUIRE_MESSAGE(key.has_value(), name);
			CHECK(KeyCodeToName(*key) == name);
		}
		CHECK(KeyCodeFromName("w") == Key::W);
		CHECK_FALSE(KeyCodeFromName("F13").has_value());
		CHECK_FALSE(KeyCodeFromName("NotAKey").has_value());
		CHECK(MouseCodeFromName("Right") == Mouse::ButtonRight);
	}

	TEST_CASE("Input tracks edges per frame")
	{
		Input::Reset();
		Input::SetKeyState(Key::W, true);
		CHECK(Input::IsKeyDown(Key::W));
		CHECK(Input::IsKeyPressed(Key::W));
		Input::EndFrame();
		CHECK(Input::IsKeyDown(Key::W));
		CHECK_FALSE(Input::IsKeyPressed(Key::W));
		Input::SetKeyState(Key::W, false);
		CHECK(Input::IsKeyReleased(Key::W));
		Input::EndFrame();
		CHECK_FALSE(Input::IsKeyReleased(Key::W));

		Input::SetMousePosition({ 10.0f, 10.0f });
		Input::EndFrame();
		Input::SetMousePosition({ 15.0f, 7.0f });
		CHECK(Input::GetMouseDelta() == glm::vec2(5.0f, -3.0f));
		Input::Reset();
	}

	TEST_CASE("FileSystem writes atomically and reads back")
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "PenumbraTests" / "fs" / "file.txt";
		REQUIRE(FileSystem::WriteTextFile(path, "hello\nworld"));
		CHECK(FileSystem::ReadTextFile(path) == "hello\nworld");
		CHECK_FALSE(std::filesystem::exists(path.string() + ".tmp"));
		REQUIRE(FileSystem::WriteTextFile(path, "replaced"));
		CHECK(FileSystem::ReadTextFile(path) == "replaced");
		CHECK_FALSE(FileSystem::ReadTextFile(path.parent_path() / "missing.txt").has_value());
		std::filesystem::remove_all(path.parent_path());
	}

	TEST_CASE("Log sinks receive messages at or above the minimum level")
	{
		const LogLevel previous = Log::GetMinimumLevel();
		Log::SetMinimumLevel(LogLevel::Info);
		Log::SetConsoleOutputEnabled(false);
		std::vector<LogMessage> received;
		const Log::SinkHandle sink = Log::AddSink([&](const LogMessage& message) { received.push_back(message); });
		PN_CORE_TRACE("hidden");
		PN_CORE_WARN("value {}", 42);
		Log::RemoveSink(sink);
		PN_CORE_ERROR("not received");
		Log::SetConsoleOutputEnabled(true);
		Log::SetMinimumLevel(previous);

		REQUIRE(received.size() == 1);
		CHECK(received[0].Level == LogLevel::Warn);
		CHECK(received[0].Text == "value 42");
	}

	TEST_CASE("Math helpers")
	{
		const glm::vec3 euler(10.0f, 45.0f, -20.0f);
		CHECK(Test::Near(Math::EulerDegreesFromQuat(Math::QuatFromEulerDegrees(euler)), euler, 1e-3f));

		const glm::vec3 direction = glm::normalize(glm::vec3(1.0f, 0.2f, -3.0f));
		CHECK(Test::Near(Math::Forward(Math::LookRotation(direction)), direction, 1e-4f));
		CHECK(Test::Near(Math::Forward(Math::LookRotation(glm::vec3(0.0f, -1.0f, 0.0f))), glm::vec3(0.0f, -1.0f, 0.0f), 1e-4f));
		CHECK(Test::Near(Math::Forward(glm::quat(1.0f, 0.0f, 0.0f, 0.0f)), glm::vec3(0.0f, 0.0f, -1.0f)));

		glm::vec3 t, s;
		glm::quat r;
		const glm::mat4 m = Math::ComposeTransform({ 1.0f, 2.0f, 3.0f }, Math::QuatFromEulerDegrees({ 0.0f, 90.0f, 0.0f }), { 2.0f, 2.0f, 2.0f });
		REQUIRE(Math::DecomposeTransform(m, t, r, s));
		CHECK(Test::Near(t, { 1.0f, 2.0f, 3.0f }));
		CHECK(Test::Near(s, { 2.0f, 2.0f, 2.0f }));
		// Compare rotations, not Euler triplets: 90 degrees of yaw is a gimbal singularity.
		CHECK(std::abs(glm::dot(r, Math::QuatFromEulerDegrees({ 0.0f, 90.0f, 0.0f }))) > 0.9999f);

		CHECK(Test::Near(Math::SegmentSphereOverlap({ -10.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 20.0f, glm::vec3(0.0f), 2.0f), 4.0f));
		CHECK(Test::Near(Math::SegmentSphereOverlap({ -10.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 9.0f, glm::vec3(0.0f), 2.0f), 1.0f));
		CHECK(Math::SegmentSphereOverlap({ -10.0f, 5.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, 20.0f, glm::vec3(0.0f), 2.0f) == 0.0f);
		CHECK(Test::Near(Math::Luminance(glm::vec3(1.0f)), 1.0f));
	}
}
