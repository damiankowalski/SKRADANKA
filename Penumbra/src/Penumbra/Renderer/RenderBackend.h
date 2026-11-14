#pragma once

#include "Penumbra/Renderer/RenderDevice.h"

#include <atomic>

// Internal to the renderer: what RenderDevice needs from a graphics API. RenderDevice keeps everything
// the APIs share (NVRHI wrapping, back buffer textures, frame pacing); a backend owns the native device
// and the swap chain.

namespace Penumbra::Detail {

	class MessageCallback final : public nvrhi::IMessageCallback
	{
	public:
		void message(nvrhi::MessageSeverity severity, const char* text) override;

		std::atomic<uint32_t> ErrorCount = 0;
	};

	enum class AcquireResult
	{
		Success,
		OutOfDate, // the swap chain no longer matches the window: recreate it
		DeviceLost,
	};

	class RenderBackend
	{
	public:
		virtual ~RenderBackend() = default;

		// Creates the native device and wraps it in NVRHI (without the validation layer).
		virtual nvrhi::DeviceHandle CreateDevice(const RenderDeviceSpecification& specification, MessageCallback& messages) = 0;

		virtual bool CreateSwapChain(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t bufferCount, bool vsync) = 0;
		virtual bool HasSwapChain() const = 0;
		// Recreates the swap chain buffers at a new size. Every texture wrapping them must be released first.
		virtual bool ResizeSwapChain(uint32_t width, uint32_t height, bool vsync) = 0;
		// The swap chain's size, which can differ from the one asked for (the surface decides on Vulkan).
		virtual uint32_t GetSwapChainWidth() const = 0;
		virtual uint32_t GetSwapChainHeight() const = 0;
		virtual nvrhi::Format GetBackBufferFormat() const = 0;
		// The swap chain images as NVRHI textures (state Present between frames).
		virtual std::vector<nvrhi::TextureHandle> CreateBackBufferTextures(nvrhi::IDevice* nativeDevice) = 0;

		// Acquire and Present get the native NVRHI device (not the validation wrapper).
		virtual AcquireResult AcquireNextImage(nvrhi::IDevice* device, uint32_t& imageIndex) = 0;
		// Returns false when the device is lost; an out-of-date swap chain is reported by the next acquire.
		virtual bool Present(nvrhi::IDevice* device, uint32_t imageIndex, bool vsync) = 0;

		// Called after the NVRHI device is gone: releases the swap chain and the native device.
		virtual void Shutdown() = 0;

		std::string AdapterName;
		bool Warp = false;
	};

#ifdef _WIN32
	Scope<RenderBackend> CreateD3D12Backend();
#endif
	Scope<RenderBackend> CreateVulkanBackend();
	// Loads the Vulkan library and hands it to GLFW (glfwInitVulkanLoader). False when there is none.
	bool PrepareVulkanWindowSystem();

}
