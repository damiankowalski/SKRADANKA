#pragma once

#include "Penumbra/Core/Layer.h"
#include "Penumbra/Core/UserSettings.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Scene/Scene.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace Penumbra {

	struct RuntimeOptions
	{
		std::string ScenePath;           // overrides the project's start scene
		bool Render = true;
		bool ExitOnTestComplete = false; // exit with 0/1 when scripts call test.complete()
		float TestTimeout = 0.0f;        // seconds; 0 = none
		std::optional<std::filesystem::path> ScreenshotPath; // saved before exiting
		uint32_t Width = 1280;
		uint32_t Height = 720;
		// Random seed for every scene played. Without one, headless or fixed-timestep runs use the
		// scene's fixed default (reproducible) and interactive runs a new seed per scene load.
		std::optional<uint64_t> Seed;
		bool RandomizeSeed = false;
		GameState GameValues; // initial game.get values (--set)
		// The testing tools (F3 performance, F4 debug panel, F5 freelook, F6 pause): always in
		// development builds, with --dev in shipping (Dist) builds. Need a window.
		bool DebugTools = true;
		std::string DebugToolsOpen; // tools up from the start (--debug-ui performance,panel,freelook)
		// Where scripts' playtest.record lines go (the agent playtester, docs/Playtest.md); none: not recorded.
		std::optional<std::filesystem::path> PlaytestOut;
		// The 3D picture's resolution for this run (--render-scale 0.25-1), overriding the user's setting.
		std::optional<float> RenderScale;
		// Frame statistics as JSON lines, twice a second (--perf-out; Tools/Benchmark.py). Needs the testing tools.
		std::optional<std::filesystem::path> PerfOut;
	};

	// Plays a project's scene: runtime simulation, rendering to the window (or offscreen when
	// headless), scene switching and quitting requested by scripts, scripted test runs.
	class RuntimeDebugPanel;

	class RuntimeLayer final : public Layer
	{
	public:
		explicit RuntimeLayer(RuntimeOptions options);
		~RuntimeLayer() override;

		void OnAttach() override;
		void OnDetach() override;
		void OnUpdate(Timestep ts) override;
		void OnRender() override;
		void OnEvent(Event& event) override;

		// True once the layer ended the run itself (test finished, quit requested, load failure).
		bool IsFinished() const { return m_Finished; }

	private:
		bool LoadScene(const std::string& path);
		void Finish(int exitCode);
		void ApplyCursorLock();
		// Windowed: the renderer follows the window's pixels times the render scale.
		void UpdateViewportSize();

	private:
		RuntimeOptions m_Options;
		Ref<Scene> m_Scene;
		Scope<SceneRenderer> m_Renderer;
		Scope<RuntimeDebugPanel> m_DebugPanel;
		UserSettings m_Settings; // windowed runs only; headless runs never read or write the user's settings
		bool m_GameCursorLocked = false; // what the game's scripts asked for
		std::chrono::steady_clock::time_point m_LastFrame;
		bool m_Finished = false;
		std::ofstream m_PlaytestOut;
	};

}
