#pragma once

#include "Penumbra/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace Penumbra {

	class CommandLine;

	// Direct3D 12 is the shipping API on Windows. Vulkan runs on macOS (through MoltenVK) and, with
	// --vulkan, on Windows too, so both backends can be tested on one machine.
	enum class GraphicsApi
	{
		D3D12,
		Vulkan,
	};

	const char* GraphicsApiToString(GraphicsApi api);
	std::optional<GraphicsApi> GraphicsApiFromString(std::string_view text);

	// The platform's default, overridden by the PENUMBRA_GRAPHICS_API environment variable
	// ("d3d12" / "vulkan"), which lets a whole test run use the other backend.
	GraphicsApi GetDefaultGraphicsApi();

	struct RenderDeviceSpecification
	{
		GraphicsApi Api = GetDefaultGraphicsApi();
		GLFWwindow* Window = nullptr; // null creates a headless device (no swap chain)
		uint32_t BackBufferWidth = 1280;
		uint32_t BackBufferHeight = 720;
		uint32_t SwapChainBufferCount = 3;
		bool VSync = true;
		bool UseWarp = false; // D3D12 software rasterizer; ignored by Vulkan
		bool EnableDebugLayer = false;
	};

	namespace Detail {
		class RenderBackend;
	}

	// Graphics memory as the driver reports it (the runtime's performance monitor).
	struct VideoMemoryInfo
	{
		uint64_t UsedBytes = 0;   // used by this process; 0 when the driver does not say
		uint64_t BudgetBytes = 0; // what the process may use before the driver starts paging (or the heap size)
		bool HasUsage = false;
	};

	// Owns the graphics device (through NVRHI), the swap chain and frame pacing.
	class RenderDevice
	{
	public:
		static constexpr uint32_t MaxFramesInFlight = 2;

		// --vulkan / --d3d12 on the command line, else GetDefaultGraphicsApi(). Logs and falls back to the
		// default when the requested API is not available on this platform.
		static GraphicsApi ChooseGraphicsApi(const CommandLine& arguments);
		static bool IsGraphicsApiAvailable(GraphicsApi api);
		// Must run before the first window is created: Vulkan hands its loader to GLFW.
		static void PrepareWindowSystem(GraphicsApi api);

		// Returns nullptr (after logging why) when no usable device exists.
		static Scope<RenderDevice> Create(const RenderDeviceSpecification& specification);
		~RenderDevice();

		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;

		nvrhi::IDevice* GetDevice() const;
		GraphicsApi GetGraphicsApi() const;

		// Waits for a free frame slot and acquires the next back buffer.
		bool BeginFrame();
		// Presents (when there is a swap chain) and recycles resources.
		void EndFrame();

		bool HasSwapChain() const;
		nvrhi::ITexture* GetBackBuffer() const;
		nvrhi::IFramebuffer* GetBackBufferFramebuffer() const;
		nvrhi::Format GetBackBufferFormat() const;
		uint32_t GetBackBufferWidth() const;
		uint32_t GetBackBufferHeight() const;
		void ResizeSwapChain(uint32_t width, uint32_t height);
		// Copies the current back buffer (the window's whole picture, UI included) into tightly packed RGBA8.
		// Call after every layer has rendered and before EndFrame. False when there is no swap chain.
		bool ReadBackBuffer(std::vector<uint8_t>& pixels, uint32_t& width, uint32_t& height);
		void SetVSync(bool enabled);

		void WaitForIdle();

		const std::string& GetAdapterName() const;
		// The Vulkan driver chosen for the adapter (empty on D3D12). See VulkanDeviceSelection.h.
		const std::string& GetDriverName() const;
		bool IsWarp() const;
		bool IsRayTracingSupported() const;
		bool IsDeviceLost() const;
		bool QueryVideoMemory(VideoMemoryInfo& info) const;
		// Number of errors reported by the graphics runtime / NVRHI validation since creation.
		uint32_t GetErrorCount() const;

	private:
		RenderDevice();
		bool RecreateSwapChain(uint32_t width, uint32_t height);
		void CreateBackBuffers();
		void ReleaseBackBuffers();

		struct Impl;
		Scope<Impl> m_Impl;
	};

}
