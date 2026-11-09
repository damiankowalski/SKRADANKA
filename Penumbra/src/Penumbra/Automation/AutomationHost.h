#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/UUID.h"

#include <filesystem>

namespace Penumbra {

	class Scene;
	class SceneRenderer;
	class RenderDevice;

	// What the automation command set needs from the application hosting it. The editor
	// implements it on top of its UI state; HeadlessAutomationHost is used by the CLI and tests.
	class AutomationHost
	{
	public:
		virtual ~AutomationHost() = default;

		virtual Ref<Scene> GetEditScene() = 0;
		virtual void SetEditScene(Ref<Scene> scene) = 0;

		// Non-null while play mode is active.
		virtual Ref<Scene> GetPlayScene() = 0;
		virtual bool StartPlay() = 0;
		virtual void StopPlay() = 0;

		// Rendering for screenshots; hosts without a GPU may return nullptr.
		virtual RenderDevice* GetRenderDevice() = 0;
		virtual SceneRenderer* GetRenderer() = 0;

		virtual void SetSelection(UUID entity) {}
		virtual std::filesystem::path GetRuntimeExecutable() const;

		// The scene commands act on: the play scene while playing, otherwise the edit scene.
		Ref<Scene> GetActiveScene();
	};

	class HeadlessAutomationHost : public AutomationHost
	{
	public:
		HeadlessAutomationHost();
		~HeadlessAutomationHost() override;

		Ref<Scene> GetEditScene() override { return m_EditScene; }
		void SetEditScene(Ref<Scene> scene) override;
		Ref<Scene> GetPlayScene() override { return m_PlayScene; }
		bool StartPlay() override;
		void StopPlay() override;
		RenderDevice* GetRenderDevice() override;
		SceneRenderer* GetRenderer() override;
		std::filesystem::path GetRuntimeExecutable() const override;

		// Headless hosts default to an audio engine without an output device and to WARP if no GPU exists.
		void SetAudioDeviceEnabled(bool enabled) { m_AudioDeviceEnabled = enabled; }
		void SetUseWarp(bool useWarp) { m_UseWarp = useWarp; }
		void SetRuntimeExecutable(const std::filesystem::path& path) { m_RuntimeExecutable = path; }

	private:
		Ref<Scene> m_EditScene;
		Ref<Scene> m_PlayScene;
		Scope<RenderDevice> m_RenderDevice;
		Scope<SceneRenderer> m_Renderer;
		bool m_AudioDeviceEnabled = false;
		bool m_UseWarp = false;
		bool m_RendererFailed = false;
		std::filesystem::path m_RuntimeExecutable;
	};

}
