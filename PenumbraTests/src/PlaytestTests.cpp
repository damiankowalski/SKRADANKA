#include "TestHelpers.h"

#include "Penumbra/Core/Input.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

using namespace Penumbra;

// The agent playtester (docs/Playtest.md) drives the thief through the same code as the keyboard.
// These tests pin that down: a scripted run of keys gives the same frame-by-frame trace of the thief
// (position, capsule, lean, silhouette, footsteps) and of the guards watching him.

namespace {

	// One input change at a frame: a key or mouse button down/up, or the mouse moved by (dx, dy).
	struct InputStep
	{
		int Frame = 0;
		const char* Key = nullptr; // a Lua key name ("W", "LeftShift"...), "Mouse:Right", or nullptr for a mouse move
		bool Down = false;
		float DX = 0.0f, DY = 0.0f;
	};

	// Walk, turn, crouch, lean both ways, run, jump, aim, use and swing: every control the thief has.
	const std::vector<InputStep>& Timeline()
	{
		static const std::vector<InputStep> steps = {
			{ 0, "W", true },
			{ 60, nullptr, false, -2.0f, 0.0f },
			{ 120, "C", true },
			{ 200, "W", false },
			{ 260, "E", true },
			{ 320, "E", false },
			{ 320, "C", false },
			{ 360, "W", true },
			{ 360, "LeftShift", true },
			{ 420, "Space", true },
			{ 425, "Space", false },
			{ 470, "LeftShift", false },
			{ 480, "W", false },
			{ 480, "Q", true },
			{ 540, "Q", false },
			{ 540, nullptr, false, 1.5f, 1.0f },
			{ 560, "Mouse:Right", true },
			{ 570, "F", true },
			{ 575, "F", false },
			{ 580, "V", true },
			{ 585, "V", false },
			{ 600, "Mouse:Right", false },
			{ 600, nullptr, false, 0.0f, 0.0f },
		};
		return steps;
	}

	constexpr int TimelineFrames = 700;

	KeyCode KeyFromName(const std::string& name)
	{
		static const std::unordered_map<std::string, KeyCode> keys = {
			{ "W", Key::W }, { "A", Key::A }, { "S", Key::S }, { "D", Key::D }, { "C", Key::C }, { "E", Key::E }, { "Q", Key::Q },
			{ "F", Key::F }, { "V", Key::V }, { "Space", Key::Space }, { "LeftShift", Key::LeftShift },
		};
		return keys.at(name);
	}

	// What the frame looks like: the thief and the guards, rounded so the trace compares exactly.
	constexpr const char* TraceLua = R"(
local p = scene.find('Player')
local s = p:script()
local pos = p:get_position()
local t = p:get('PerceptionTarget')
local head = t.SilhouettePoints[1]
local line = string.format('%.4f %.4f %.4f h%.3f v%.3f l%.3f y%.2f c%s s%.3f,%.3f g%.3f', pos.x, pos.y, pos.z,
	p:get('CharacterController').Height, s.visual_height, s.lean, s.yaw, tostring(s.crouched), head.x, head.y, t.LightGem or 0)
if s.last_step then line = line .. string.format(' step%.2f', s.last_step.loudness) end
for _, g in ipairs(scene.entities_with('Perception')) do
	line = line .. string.format(' | %s %s %.4f %.4f', g:get_name(), ai.state(g), ai.suspicion(g), g:get('Perception').Stimulus)
end
return line)";

	struct TraceLevel
	{
		Scene SceneInstance;

		explicit TraceLevel(const std::string& scenePath, uint64_t seed, const GameState& values = {})
		{
			std::string error;
			REQUIRE_MESSAGE(Project::Load(std::filesystem::path(PENUMBRA_DEMO_PROJECT_DIR) / "project.json", error), error);
			SceneInstance.SetAudioDeviceEnabled(false);
			REQUIRE_MESSAGE(SceneSerializer::Deserialize(SceneInstance, Project::GetAssetDirectory() / scenePath, error), error);
			SceneInstance.SetRandomSeed(seed);
			SceneInstance.GetGameState() = values;
			Input::Reset();
			SceneInstance.OnRuntimeStart();
		}

		~TraceLevel()
		{
			Input::Reset();
			SceneInstance.OnRuntimeStop();
			Project::Unload();
		}

		std::string Lua(const std::string& code)
		{
			std::string output;
			REQUIRE_MESSAGE(SceneInstance.GetScriptEngine()->Execute(code, output), output);
			return output;
		}
	};

	// Plays the timeline through the keyboard (Input) on the cloister and returns one line every 5 frames.
	std::string KeyboardTrace()
	{
		TraceLevel level("Scenes/Cloister.pscene", 7);
		const std::vector<InputStep>& steps = Timeline();
		size_t next = 0;
		glm::vec2 mouse(0.0f), mouseSpeed(0.0f);
		Input::SetMousePosition(mouse);
		std::string trace;
		for (int frame = 0; frame < TimelineFrames; frame++)
		{
			for (; next < steps.size() && steps[next].Frame == frame; next++)
			{
				const InputStep& step = steps[next];
				if (!step.Key)
					mouseSpeed = glm::vec2(step.DX, step.DY);
				else if (std::string(step.Key) == "Mouse:Right")
					Input::SetMouseButtonState(Mouse::ButtonRight, step.Down);
				else
					Input::SetKeyState(KeyFromName(step.Key), step.Down);
			}
			mouse += mouseSpeed;
			Input::SetMousePosition(mouse);
			level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			Input::EndFrame();
			if (frame % 5 == 4)
				trace += level.Lua(TraceLua) + "\n";
		}
		CHECK(level.SceneInstance.GetScriptEngine()->GetErrors().empty());
		return trace;
	}

	// The same timeline through a Lua controls object (what the playtester's bot is) instead of the keyboard.
	std::string ControlsTrace()
	{
		TraceLevel level("Scenes/Cloister.pscene", 7);
		std::string steps = "{";
		for (const InputStep& step : Timeline())
		{
			const std::string key = step.Key ? (std::string(step.Key) == "Mouse:Right" ? "MouseRight" : step.Key) : "";
			steps += std::format("{{ frame = {}, key = {}, down = {}, dx = {}, dy = {} }},", step.Frame,
				key.empty() ? std::string("nil") : "'" + key + "'", step.Down ? "true" : "false", step.DX, step.DY);
		}
		steps += "}";
		level.Lua(R"(
local steps = )" + steps + R"(
local frame, next, down, previous, dx, dy = 0, 1, {}, {}, 0, 0
local controls = {
	key = function(k) return down[k] == true end,
	key_pressed = function(k) return down[k] == true and not previous[k] end,
	key_released = function(k) return previous[k] == true and not down[k] end,
	mouse_button = function(b) return down['Mouse' .. b] == true end,
	mouse_pressed = function(b) return down['Mouse' .. b] == true and not previous['Mouse' .. b] end,
	mouse_delta = function() return dx, dy end,
}
controls.tick = function()
	previous = {}
	for k, v in pairs(down) do previous[k] = v end
	while steps[next] and steps[next].frame == frame do
		local s = steps[next]
		if s.key then down[s.key] = s.down else dx, dy = s.dx, s.dy end
		next = next + 1
	end
	frame = frame + 1
end
scene.find('Player'):script().controls = controls)");
		std::string trace;
		for (int frame = 0; frame < TimelineFrames; frame++)
		{
			level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			Input::EndFrame();
			if (frame % 5 == 4)
				trace += level.Lua(TraceLua) + "\n";
		}
		CHECK(level.SceneInstance.GetScriptEngine()->GetErrors().empty());
		return trace;
	}

	// One playtest run in process: the recorded JSONL lines. `setup` runs once before the first frame (e.g. to
	// shorten a route's time limit).
	std::vector<std::string> PlaytestRun(const std::string& scenePath, const std::string& route, int seed, float maxSeconds,
		const std::string& setup = "")
	{
		GameState values;
		values["playtest"] = route;
		values["playtest_seed"] = static_cast<double>(seed);
		TraceLevel level(scenePath, static_cast<uint64_t>(seed), values);
		std::vector<std::string> lines;
		level.SceneInstance.SetRecordSink([&lines](const std::string& line) { lines.push_back(line); });
		if (!setup.empty())
			level.Lua(setup);
		const int frames = static_cast<int>(maxSeconds * 60.0f);
		for (int frame = 0; frame < frames && !level.SceneInstance.GetRequestedQuit(); frame++)
		{
			level.SceneInstance.OnUpdateRuntime(Timestep(1.0f / 60.0f));
			Input::EndFrame();
		}
		CHECK(level.SceneInstance.GetRequestedQuit().has_value());
		CHECK(level.SceneInstance.GetScriptEngine()->GetErrors().empty());
		return lines;
	}

	std::string Outcome(const std::vector<std::string>& lines)
	{
		REQUIRE(!lines.empty());
		const size_t at = lines.back().find("\"outcome\":\"");
		REQUIRE(at != std::string::npos);
		const size_t start = at + 11;
		return lines.back().substr(start, lines.back().find('"', start) - start);
	}

	uint64_t Fnv1a(const std::string& text)
	{
		uint64_t hash = 0xcbf29ce484222325ull;
		for (const char c : text)
		{
			hash ^= static_cast<uint8_t>(c);
			hash *= 0x100000001b3ull;
		}
		return hash;
	}

}

TEST_SUITE("Playtest")
{
	TEST_CASE("Playtest: playtest.record writes one JSON line to the host's sink, nothing without one")
	{
		Scene scene;
		scene.OnRuntimeStart();
		std::string output;
		REQUIRE(scene.GetScriptEngine()->Execute("return tostring(playtest.enabled()) .. ' ' .. tostring(playtest.record({ a = 1 }))", output));
		CHECK(output == "\"false false\"");

		std::vector<std::string> lines;
		scene.SetRecordSink([&lines](const std::string& line) { lines.push_back(line); });
		REQUIRE(scene.GetScriptEngine()->Execute(
			"playtest.record({ type = 'sample', t = 1.25, guard = 'North', at = { 1.5, 0, -2 }, seen = true }); return playtest.enabled()", output));
		CHECK(output == "true");
		REQUIRE(lines.size() == 1);
		// Keys sorted, integers stay integers: the same record is the same bytes.
		CHECK(lines[0] == R"({"at":[1.5,0,-2],"guard":"North","seen":true,"t":1.25,"type":"sample"})");
		scene.OnRuntimeStop();
	}

	TEST_CASE("Playtest: the keyboard trace of the thief is reproducible")
	{
		const std::string first = KeyboardTrace();
		const std::string second = KeyboardTrace();
		MESSAGE("keyboard trace hash " << std::hex << Fnv1a(first) << std::dec << ", " << first.size() << " bytes");
		CHECK(first == second);
		CHECK(first.find("step") != std::string::npos); // he made footsteps
		CHECK(first.find("ctrue") != std::string::npos); // and crouched
	}

	TEST_CASE("Playtest: the bot's controls drive the thief exactly as the keyboard does")
	{
		const std::string keyboard = KeyboardTrace();
		const std::string controls = ControlsTrace();
		CHECK(keyboard == controls);
	}

	TEST_CASE("Playtest: the same level, route and seed record the same lines")
	{
		// A short walk past the campfire towards the sentry: footsteps, samples, his reaction.
		const std::string route = "package.loaded['Playtest/Test/Walk'] = { name = 'Walk', time_limit = 6, survive = true, steps = {"
								  "{ to = vec3(-3, 0, 3), gait = 'walk' }, { to = vec3(2, 0, 0), gait = 'crouch' }, { wait = 2 } } }";
		const std::vector<std::string> first = PlaytestRun("Scenes/PerceptionLab.pscene", "Playtest/Test/Walk", 3, 10.0f, route);
		const std::vector<std::string> second = PlaytestRun("Scenes/PerceptionLab.pscene", "Playtest/Test/Walk", 3, 10.0f, route);
		CHECK(first.size() > 30);
		CHECK(first == second);
		CHECK(first.front().find("\"type\":\"run\"") != std::string::npos);
		CHECK(Outcome(first) != "stuck");
		CHECK(Outcome(first) != "error");
	}

	TEST_CASE("Playtest: in full light before the sentry he is detected, in the dark behind him never")
	{
		CHECK(Outcome(PlaytestRun("Scenes/PerceptionLab.pscene", "Playtest/PerceptionLab/LitFront", 1, 5.0f)) == "detected");
		const std::vector<std::string> dark = PlaytestRun("Scenes/PerceptionLab.pscene", "Playtest/PerceptionLab/DarkBehind", 1, 12.0f,
			"require('Playtest/PerceptionLab/DarkBehind').time_limit = 10");
		CHECK(Outcome(dark) == "undetected");
	}
}
