#include "RuntimeLayer.h"

#include "RuntimeDebugPanel.h"

#include "Penumbra/Core/Application.h"
#include "Penumbra/Project/Project.h"
#include "Penumbra/Renderer/RenderDevice.h"
#include "Penumbra/Scene/SceneSerializer.h"
#include "Penumbra/Scripting/ScriptEngine.h"

#include <chrono>

namespace Penumbra {

	RuntimeLayer::RuntimeLayer(RuntimeOptions options)
		: Layer("Runtime"), m_Options(std::move(options))
	{
	}

	RuntimeLayer::~RuntimeLayer() = default;

	void RuntimeLayer::OnAttach()
	{
		Application& app = Application::Get();
		if (m_Options.Render)
		{
			m_Renderer = CreateScope<SceneRenderer>(app.GetRenderDevice());
			if (app.GetWindow())
			{
				m_Settings = UserSettings::LoadDefault();
				UpdateViewportSize();
			}
			else
				m_Renderer->SetViewportSize(m_Options.Width, m_Options.Height);
			if (Window* window = app.GetWindow(); window && m_Options.DebugTools)
			{
				m_DebugPanel = CreateScope<RuntimeDebugPanel>(window->GetNativeWindow(), app.GetRenderDevice().GetDevice(), m_Settings);
				m_DebugPanel->Open(m_Options.DebugToolsOpen);
				if (m_Options.PerfOut && !m_DebugPanel->StartPerformanceLog(*m_Options.PerfOut))
				{
					Finish(1);
					return;
				}
			}
		}
		if (m_Options.PerfOut && !m_DebugPanel)
		{
			PN_CORE_CRITICAL("--perf-out needs a window and the testing tools (a development build, or --dev)");
			Finish(1);
			return;
		}
		m_LastFrame = std::chrono::steady_clock::now();

		if (m_Options.PlaytestOut)
		{
			std::error_code ignored;
			if (m_Options.PlaytestOut->has_parent_path())
				std::filesystem::create_directories(m_Options.PlaytestOut->parent_path(), ignored);
			m_PlaytestOut.open(*m_Options.PlaytestOut, std::ios::binary | std::ios::trunc);
			if (!m_PlaytestOut)
			{
				PN_CORE_CRITICAL("Cannot write {}", m_Options.PlaytestOut->string());
				Finish(1);
				return;
			}
		}

		const std::string scene = m_Options.ScenePath.empty() ? Project::GetConfig().StartScene : m_Options.ScenePath;
		if (!LoadScene(scene))
			Finish(1);
	}

	void RuntimeLayer::OnDetach()
	{
		if (m_Options.ScreenshotPath && m_Renderer)
		{
			if (m_Renderer->SaveScreenshot(*m_Options.ScreenshotPath))
				PN_CORE_INFO("Saved screenshot {}", m_Options.ScreenshotPath->string());
			else
				PN_CORE_ERROR("Cannot save screenshot {}", m_Options.ScreenshotPath->string());
		}
		if (m_Scene)
			m_Scene->OnRuntimeStop();
		m_Scene.reset();
		m_DebugPanel.reset();
		m_Renderer.reset();
	}

	bool RuntimeLayer::LoadScene(const std::string& path)
	{
		Ref<Scene> scene = CreateRef<Scene>();
		std::string error;
		if (!SceneSerializer::Deserialize(*scene, Project::ResolveAssetPath(path), error))
		{
			PN_CORE_CRITICAL("Cannot load scene: {}", error);
			return false;
		}
		if (m_Scene)
		{
			m_Scene->OnRuntimeStop();
			scene->SetGameState(m_Scene->GetGameStateRef()); // the game goes on in the next scene
		}
		else
		{
			scene->GetGameState() = m_Options.GameValues;
		}
		m_Scene = scene;
		const bool headless = Application::Get().GetWindow() == nullptr;
		m_Scene->SetAudioDeviceEnabled(!headless);
		if (m_Renderer)
			m_Scene->OnViewportResize(m_Renderer->GetViewportWidth(), m_Renderer->GetViewportHeight());
		else
			m_Scene->OnViewportResize(m_Options.Width, m_Options.Height);
		if (m_PlaytestOut.is_open())
			m_Scene->SetRecordSink([this](const std::string& line) { m_PlaytestOut << line << '\n'; });
		if (m_Options.Seed)
			m_Scene->SetRandomSeed(*m_Options.Seed);
		else if (m_Options.RandomizeSeed)
			m_Scene->SetRandomSeed(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		m_Scene->OnRuntimeStart();
		if (m_DebugPanel)
			m_DebugPanel->OnSceneLoaded(*m_Scene);
		PN_CORE_INFO("Playing scene '{}' (seed {})", path, m_Scene->GetRandomSeed());
		return true;
	}

	void RuntimeLayer::Finish(int exitCode)
	{
		if (m_Finished)
			return;
		m_Finished = true;
		Application::Get().Close(exitCode);
	}

	void RuntimeLayer::OnUpdate(Timestep ts)
	{
		if (!m_Scene || m_Finished)
			return;

		using Clock = std::chrono::steady_clock;
		const Clock::time_point frameStart = Clock::now();
		const float frameMs = std::chrono::duration<float, std::milli>(frameStart - m_LastFrame).count();
		m_LastFrame = frameStart;

		m_Scene->OnUpdateRuntime(m_DebugPanel ? m_DebugPanel->ScaleTimestep(ts) : ts);

		if (std::optional<bool> locked = m_Scene->ConsumeCursorLockRequest())
			m_GameCursorLocked = *locked;
		ApplyCursorLock();
		if (m_DebugPanel)
			m_DebugPanel->AfterUpdate(*m_Scene, frameMs, std::chrono::duration<float, std::milli>(Clock::now() - frameStart).count());

		const TestReport& report = m_Scene->GetTestReport();
		if (m_Options.ExitOnTestComplete && report.Completed)
		{
			const size_t scriptErrors = m_Scene->GetScriptEngine()->GetErrors().size();
			const bool passed = report.Passed && scriptErrors == 0;
			PN_CORE_INFO("Test scene finished: {} ({} assertions, {} failures, {} script errors)", passed ? "PASSED" : "FAILED",
				report.Assertions, report.Failures, scriptErrors);
			for (const std::string& message : report.Messages)
				PN_CORE_ERROR("  {}", message);
			Finish(passed ? 0 : 1);
			return;
		}
		if (m_Options.ExitOnTestComplete && m_Options.TestTimeout > 0.0f && m_Scene->GetTime() >= m_Options.TestTimeout)
		{
			PN_CORE_ERROR("Test scene timed out after {:.1f} s without calling test.complete()", m_Scene->GetTime());
			Finish(1);
			return;
		}

		if (const std::optional<int> quit = m_Scene->GetRequestedQuit())
		{
			Finish(*quit);
			return;
		}
		if (std::optional<std::string> next = m_Scene->GetRequestedScene())
		{
			m_Scene->ClearRequestedScene();
			if (!LoadScene(*next))
				Finish(1);
		}
	}

	void RuntimeLayer::OnRender()
	{
		if (!m_Renderer || !m_Scene)
			return;
		UpdateViewportSize();
		const float aspect = static_cast<float>(m_Renderer->GetViewportWidth()) / static_cast<float>(m_Renderer->GetViewportHeight());
		RenderDevice& device = Application::Get().GetRenderDevice();
		if (!m_DebugPanel || !device.HasSwapChain())
		{
			m_Renderer->Render(*m_Scene, RenderCamera::FromScene(*m_Scene, aspect));
			if (device.HasSwapChain())
				m_Renderer->Present(device.GetBackBufferFramebuffer());
			return;
		}

		using Clock = std::chrono::steady_clock;
		const Clock::time_point start = Clock::now();
		const RenderCamera camera = m_DebugPanel->BeginFrame(*m_Scene, *m_Renderer, aspect);
		m_Renderer->Render(*m_Scene, camera);
		m_Renderer->Present(device.GetBackBufferFramebuffer());
		const float renderMs = std::chrono::duration<float, std::milli>(Clock::now() - start).count();
		m_DebugPanel->EndFrame(*m_Scene, *m_Renderer, device, camera, device.GetBackBufferFramebuffer(), renderMs);
	}

	void RuntimeLayer::UpdateViewportSize()
	{
		const Window* window = Application::Get().GetWindow();
		if (!m_Renderer || !window || window->IsMinimized())
			return;
		UserSettings settings = m_Settings;
		if (m_Options.RenderScale)
			settings.RenderScale = *m_Options.RenderScale;
		const float scale = settings.ResolveRenderScale(window->GetFramebufferScale());
		const uint32_t width = std::max(static_cast<uint32_t>(static_cast<float>(window->GetWidth()) * scale), 1u);
		const uint32_t height = std::max(static_cast<uint32_t>(static_cast<float>(window->GetHeight()) * scale), 1u);
		if (width != m_Renderer->GetViewportWidth() || height != m_Renderer->GetViewportHeight())
			m_Renderer->SetViewportSize(width, height);
		if (m_Scene)
			m_Scene->OnViewportResize(width, height);
	}

	void RuntimeLayer::ApplyCursorLock()
	{
		Window* window = Application::Get().GetWindow();
		if (!window)
			return;
		const bool locked = m_DebugPanel ? m_DebugPanel->GetCursorOverride().value_or(m_GameCursorLocked) : m_GameCursorLocked;
		if (window->IsCursorLocked() != locked)
			window->SetCursorLocked(locked);
	}

	void RuntimeLayer::OnEvent(Event& event)
	{
		if (event.Type == EventType::WindowResize && event.Width > 0 && event.Height > 0)
			UpdateViewportSize();
		if (event.Type == EventType::KeyPressed && m_DebugPanel && m_DebugPanel->OnKeyPressed(event.Key, m_Scene.get()))
		{
			ApplyCursorLock();
			return;
		}
		if (event.Type == EventType::KeyPressed && event.Key == Key::F12 && m_Renderer)
		{
			const std::filesystem::path path = Project::GetProjectDirectory() / "Screenshots" /
											   std::format("Screenshot_{}.png", Application::Get().GetFrameIndex());
			if (m_Renderer->SaveScreenshot(path))
				PN_CORE_INFO("Saved {}", path.string());
		}
	}

}
