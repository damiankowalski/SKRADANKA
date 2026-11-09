#pragma once

#include "Penumbra/Core/Base.h"

#include <nvrhi/nvrhi.h>

#include <string>
#include <vector>

namespace Penumbra {

	struct RenderDeviceSpecification
	{
		void* WindowHandle = nullptr; // HWND; null creates a headless device (no swap chain)
		uint32_t BackBufferWidth = 1280;
		uint32_t BackBufferHeight = 720;
		uint32_t SwapChainBufferCount = 3;
		bool VSync = true;
		bool UseWarp = false;
		bool EnableDebugLayer = false;
	};

	// Owns the D3D12 device (through NVRHI), the swap chain and frame pacing.
	class RenderDevice
	{
	public:
		static constexpr uint32_t MaxFramesInFlight = 2;

		// Returns nullptr (after logging why) when no usable D3D12 device exists.
		static Scope<RenderDevice> Create(const RenderDeviceSpecification& specification);
		~RenderDevice();

		RenderDevice(const RenderDevice&) = delete;
		RenderDevice& operator=(const RenderDevice&) = delete;

		nvrhi::IDevice* GetDevice() const;

		// Waits for a free frame slot and acquires the next back buffer.
		bool BeginFrame();
		// Presents (when there is a swap chain) and recycles resources.
		void EndFrame();

		bool HasSwapChain() const;
		nvrhi::ITexture* GetBackBuffer() const;
		nvrhi::IFramebuffer* GetBackBufferFramebuffer() const;
		nvrhi::Format GetBackBufferFormat() const { return nvrhi::Format::RGBA8_UNORM; }
		uint32_t GetBackBufferWidth() const;
		uint32_t GetBackBufferHeight() const;
		void ResizeSwapChain(uint32_t width, uint32_t height);
		void SetVSync(bool enabled);

		void WaitForIdle();

		const std::string& GetAdapterName() const;
		bool IsWarp() const;
		bool IsRayTracingSupported() const;
		bool IsDeviceLost() const;
		// Number of errors reported by the D3D12 runtime / NVRHI validation since creation.
		uint32_t GetErrorCount() const;

	private:
		RenderDevice();

		struct Impl;
		Scope<Impl> m_Impl;
	};

}
