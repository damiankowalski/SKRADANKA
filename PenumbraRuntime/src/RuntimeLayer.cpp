#include "RuntimeLayer.h"

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

	void RuntimeLayer::OnAttach()
	{
		Application& app = Application::Get();
		if (m_Options.Render)
		{
			m_Renderer = CreateScope<SceneRenderer>(app.GetRenderDevice());
			if (Window* window = app.GetWindow())
				m_Renderer->SetViewportSize(window->GetWidth(), window->GetHeight());
			else
				m_Renderer->SetViewportSize(m_Options.Width, m_Options.Height);
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
			m_Scene->OnRuntimeStop();
		m_Scene = scene;
		const bool headless = Application::Get().GetWindow() == nullptr;
		m_Scene->SetAudioDeviceEnabled(!headless);
		if (m_Renderer)
			m_Scene->OnViewportResize(m_Renderer->GetViewportWidth(), m_Renderer->GetViewportHeight());
		else
			m_Scene->OnViewportResize(m_Options.Width, m_Options.Height);
		if (m_Options.Seed)
			m_Scene->SetRandomSeed(*m_Options.Seed);
		else if (m_Options.RandomizeSeed)
			m_Scene->SetRandomSeed(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
		m_Scene->OnRuntimeStart();
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

		m_Scene->OnUpdateRuntime(ts);

		if (std::optional<bool> locked = m_Scene->ConsumeCursorLockRequest())
		{
			if (Window* window = Application::Get().GetWindow())
				window->SetCursorLocked(*locked);
		}

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
		const float aspect = static_cast<float>(m_Renderer->GetViewportWidth()) / static_cast<float>(m_Renderer->GetViewportHeight());
		m_Renderer->Render(*m_Scene, RenderCamera::FromScene(*m_Scene, aspect));

		RenderDevice& device = Application::Get().GetRenderDevice();
		if (device.HasSwapChain())
			m_Renderer->Present(device.GetBackBufferFramebuffer());
	}

	void RuntimeLayer::OnEvent(Event& event)
	{
		if (event.Type == EventType::WindowResize && m_Renderer && event.Width > 0 && event.Height > 0)
		{
			m_Renderer->SetViewportSize(event.Width, event.Height);
			if (m_Scene)
				m_Scene->OnViewportResize(event.Width, event.Height);
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
