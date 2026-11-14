#pragma once

#include "Penumbra/Core/Layer.h"
#include "Penumbra/Renderer/SceneRenderer.h"
#include "Penumbra/Scene/Scene.h"

#include <filesystem>
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
	};

	// Plays a project's scene: runtime simulation, rendering to the window (or offscreen when
	// headless), scene switching and quitting requested by scripts, scripted test runs.
	class RuntimeLayer final : public Layer
	{
	public:
		explicit RuntimeLayer(RuntimeOptions options);

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

	private:
		RuntimeOptions m_Options;
		Ref<Scene> m_Scene;
		Scope<SceneRenderer> m_Renderer;
		bool m_Finished = false;
	};

}
