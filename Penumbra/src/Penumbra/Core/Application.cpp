#include "pnpch.h"
#include "Penumbra/Core/Application.h"

#include "Penumbra/Core/Input.h"
#include "Penumbra/Core/Timer.h"
#include "Penumbra/Renderer/RenderDevice.h"

#include <thread>

namespace Penumbra {

	Application* Application::s_Instance = nullptr;

	Application::Application(const ApplicationSpecification& specification)
		: m_Specification(specification)
	{
		PN_CORE_VERIFY(s_Instance == nullptr, "Only one Application may exist");
		s_Instance = this;

		RenderDeviceSpecification deviceSpec;
		deviceSpec.Api = RenderDevice::ChooseGraphicsApi(specification.Arguments);
		deviceSpec.UseWarp = specification.UseWarp;
		deviceSpec.EnableDebugLayer = specification.EnableGraphicsDebugLayer;
		deviceSpec.VSync = specification.VSync;

		if (!specification.Headless)
		{
			RenderDevice::PrepareWindowSystem(deviceSpec.Api);
			WindowSpecification windowSpec;
			windowSpec.Title = specification.Name;
			windowSpec.Width = specification.Width;
			windowSpec.Height = specification.Height;
			windowSpec.Maximized = specification.Maximized;
			m_Window = CreateScope<Window>(windowSpec);
			m_Window->SetEventCallback([this](Event& event) { OnEvent(event); });

			deviceSpec.Window = m_Window->GetNativeWindow();
			deviceSpec.BackBufferWidth = m_Window->GetWidth();
			deviceSpec.BackBufferHeight = m_Window->GetHeight();
		}

		m_RenderDevice = RenderDevice::Create(deviceSpec);
		PN_CORE_VERIFY(m_RenderDevice != nullptr, "Failed to create the render device");
	}

	Application::~Application()
	{
		if (m_RenderDevice)
			m_RenderDevice->WaitForIdle();

		for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
		{
			(*it)->OnDetach();
			delete *it;
		}
		m_Layers.clear();

		m_RenderDevice.reset();
		m_Window.reset();
		s_Instance = nullptr;
	}

	Application& Application::Get()
	{
		PN_CORE_ASSERT(s_Instance, "No Application instance");
		return *s_Instance;
	}

	bool Application::Exists() { return s_Instance != nullptr; }

	void Application::PushLayer(Layer* layer)
	{
		m_Layers.insert(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), layer);
		m_LayerInsertIndex++;
		layer->OnAttach();
	}

	void Application::PushOverlay(Layer* layer)
	{
		m_Layers.push_back(layer);
		layer->OnAttach();
	}

	void Application::Close(int exitCode)
	{
		m_ExitCode = exitCode;
		m_Running = false;
	}

	void Application::OnEvent(Event& event)
	{
		if (event.Type == EventType::WindowClose)
		{
			Close(m_ExitCode);
			return;
		}
		if (event.Type == EventType::WindowResize && event.Width > 0 && event.Height > 0)
			m_RenderDevice->ResizeSwapChain(event.Width, event.Height);

		for (auto it = m_Layers.rbegin(); it != m_Layers.rend() && !event.Handled; ++it)
			(*it)->OnEvent(event);
	}

	void Application::Run()
	{
		Timer frameTimer;
		while (m_Running)
		{
			if (m_Window)
				m_Window->PollEvents();

			float deltaTime = frameTimer.Elapsed();
			frameTimer.Reset();
			if (m_Specification.FixedTimestep > 0.0f)
				deltaTime = m_Specification.FixedTimestep;
			// Clamp long stalls (debugger breaks, window drags) so the simulation stays stable.
			deltaTime = std::min(deltaTime, 0.1f);

			if (m_Window && m_Window->IsMinimized())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				continue;
			}

			for (Layer* layer : m_Layers)
				layer->OnUpdate(Timestep(deltaTime));

			if (m_RenderDevice->BeginFrame())
			{
				for (Layer* layer : m_Layers)
					layer->OnRender();
				m_RenderDevice->EndFrame();
			}
			if (m_RenderDevice->IsDeviceLost())
			{
				PN_CORE_CRITICAL("Shutting down: the GPU device was lost");
				Close(1);
			}

			Input::EndFrame();
			m_FrameIndex++;

			if (m_Specification.MaxFrames > 0 && m_FrameIndex >= m_Specification.MaxFrames)
				m_Running = false;
		}
	}

}
