#pragma once

#include "Penumbra/Core/Base.h"
#include "Penumbra/Core/CommandLine.h"
#include "Penumbra/Core/Layer.h"
#include "Penumbra/Core/Window.h"

#include <vector>

namespace Penumbra {

	class RenderDevice;

	struct ApplicationSpecification
	{
		std::string Name = "Penumbra";
		uint32_t Width = 1600;
		uint32_t Height = 900;
		bool Maximized = false;
		bool VSync = true;
		// Headless applications have no window and no swap chain; rendering still works offscreen.
		bool Headless = false;
		// Forces the WARP software rasterizer (CI machines without a GPU).
		bool UseWarp = false;
		bool EnableGraphicsDebugLayer = false;
		// Stops the main loop after this many frames (0 = unlimited). Used by automated runs.
		uint64_t MaxFrames = 0;
		// When set, every frame advances by exactly this many seconds (deterministic automation).
		float FixedTimestep = 0.0f;
		CommandLine Arguments;
	};

	class Application
	{
	public:
		explicit Application(const ApplicationSpecification& specification);
		virtual ~Application();

		Application(const Application&) = delete;
		Application& operator=(const Application&) = delete;

		void Run();
		void Close(int exitCode = 0);

		// The application takes ownership of pushed layers.
		void PushLayer(Layer* layer);
		void PushOverlay(Layer* layer);

		Window* GetWindow() const { return m_Window.get(); }
		RenderDevice& GetRenderDevice() const { return *m_RenderDevice; }
		const ApplicationSpecification& GetSpecification() const { return m_Specification; }
		uint64_t GetFrameIndex() const { return m_FrameIndex; }
		int GetExitCode() const { return m_ExitCode; }

		static Application& Get();
		static bool Exists();

	private:
		void OnEvent(Event& event);

	private:
		ApplicationSpecification m_Specification;
		Scope<Window> m_Window;
		Scope<RenderDevice> m_RenderDevice;
		std::vector<Layer*> m_Layers;
		size_t m_LayerInsertIndex = 0;
		bool m_Running = true;
		int m_ExitCode = 0;
		uint64_t m_FrameIndex = 0;

		static Application* s_Instance;
	};

}
