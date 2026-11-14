#include "pnpch.h"
#include "Penumbra/Renderer/RenderDevice.h"

#include "Penumbra/Core/CommandLine.h"
#include "Penumbra/Renderer/RenderBackend.h"

#include <nvrhi/validation.h>

#include <cctype>
#include <cstdlib>

namespace Penumbra {

	namespace Detail {

		void MessageCallback::message(nvrhi::MessageSeverity severity, const char* text)
		{
			switch (severity)
			{
				case nvrhi::MessageSeverity::Info:
					PN_CORE_TRACE("[NVRHI] {}", text);
					break;
				case nvrhi::MessageSeverity::Warning:
					PN_CORE_WARN("[NVRHI] {}", text);
					break;
				case nvrhi::MessageSeverity::Error:
				case nvrhi::MessageSeverity::Fatal:
					PN_CORE_ERROR("[NVRHI] {}", text);
					ErrorCount++;
					break;
			}
		}

	}

	const char* GraphicsApiToString(GraphicsApi api)
	{
		switch (api)
		{
			case GraphicsApi::D3D12:  return "D3D12";
			case GraphicsApi::Vulkan: return "Vulkan";
		}
		return "unknown";
	}

	std::optional<GraphicsApi> GraphicsApiFromString(std::string_view text)
	{
		std::string lower(text);
		for (char& c : lower)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		if (lower == "d3d12" || lower == "dx12")
			return GraphicsApi::D3D12;
		if (lower == "vulkan" || lower == "vk")
			return GraphicsApi::Vulkan;
		return std::nullopt;
	}

	GraphicsApi GetDefaultGraphicsApi()
	{
		if (const char* value = std::getenv("PENUMBRA_GRAPHICS_API"))
		{
			const std::optional<GraphicsApi> api = GraphicsApiFromString(value);
			if (api && RenderDevice::IsGraphicsApiAvailable(*api))
				return *api;
		}
#ifdef _WIN32
		return GraphicsApi::D3D12;
#else
		return GraphicsApi::Vulkan;
#endif
	}

	bool RenderDevice::IsGraphicsApiAvailable(GraphicsApi api)
	{
#ifdef _WIN32
		return api == GraphicsApi::D3D12 || api == GraphicsApi::Vulkan;
#else
		return api == GraphicsApi::Vulkan;
#endif
	}

	GraphicsApi RenderDevice::ChooseGraphicsApi(const CommandLine& arguments)
	{
		std::optional<GraphicsApi> requested;
		if (arguments.HasFlag("vulkan"))
			requested = GraphicsApi::Vulkan;
		else if (arguments.HasFlag("d3d12"))
			requested = GraphicsApi::D3D12;
		if (requested && !IsGraphicsApiAvailable(*requested))
		{
			PN_CORE_WARN("{} is not available on this platform; using {}", GraphicsApiToString(*requested), GraphicsApiToString(GetDefaultGraphicsApi()));
			requested.reset();
		}
		return requested.value_or(GetDefaultGraphicsApi());
	}

	void RenderDevice::PrepareWindowSystem(GraphicsApi api)
	{
		if (api == GraphicsApi::Vulkan && !Detail::PrepareVulkanWindowSystem())
			PN_CORE_ERROR("No Vulkan library found: windows will have no Vulkan surface");
	}

	struct RenderDevice::Impl
	{
		RenderDeviceSpecification Specification;
		Detail::MessageCallback Messages;
		Scope<Detail::RenderBackend> Backend;

		nvrhi::DeviceHandle NativeDevice;
		nvrhi::DeviceHandle Device; // possibly wrapped by the validation layer
		std::vector<nvrhi::TextureHandle> BackBuffers;
		std::vector<nvrhi::FramebufferHandle> Framebuffers;
		uint32_t BackBufferIndex = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;
		bool DeviceLost = false;
		bool SwapChainOutOfDate = false;

		std::array<nvrhi::EventQueryHandle, MaxFramesInFlight> FrameQueries;
		uint64_t FrameCount = 0;
	};

	RenderDevice::RenderDevice()
		: m_Impl(CreateScope<Impl>())
	{
	}

	RenderDevice::~RenderDevice()
	{
		if (m_Impl->Device)
			m_Impl->Device->waitForIdle();
		ReleaseBackBuffers();
		for (auto& query : m_Impl->FrameQueries)
			query = nullptr;
		if (m_Impl->Device)
			m_Impl->Device->runGarbageCollection();
		m_Impl->Device = nullptr;
		m_Impl->NativeDevice = nullptr;
		if (m_Impl->Backend)
			m_Impl->Backend->Shutdown();
	}

	Scope<RenderDevice> RenderDevice::Create(const RenderDeviceSpecification& specification)
	{
		Scope<RenderDevice> device(new RenderDevice());
		Impl& impl = *device->m_Impl;
		impl.Specification = specification;
		impl.Width = std::max(specification.BackBufferWidth, 1u);
		impl.Height = std::max(specification.BackBufferHeight, 1u);

		if (!IsGraphicsApiAvailable(specification.Api))
		{
			PN_CORE_CRITICAL("{} is not available on this platform", GraphicsApiToString(specification.Api));
			return nullptr;
		}
#ifdef _WIN32
		impl.Backend = specification.Api == GraphicsApi::D3D12 ? Detail::CreateD3D12Backend() : Detail::CreateVulkanBackend();
#else
		impl.Backend = Detail::CreateVulkanBackend();
#endif

		impl.NativeDevice = impl.Backend->CreateDevice(specification, impl.Messages);
		if (!impl.NativeDevice)
			return nullptr;
		impl.Device = specification.EnableDebugLayer ? nvrhi::validation::createValidationLayer(impl.NativeDevice) : impl.NativeDevice;
		PN_CORE_INFO("Render device: {} ({}{}{})", impl.Backend->AdapterName, GraphicsApiToString(specification.Api), impl.Backend->Warp ? ", WARP" : "",
			impl.Device->queryFeatureSupport(nvrhi::Feature::RayQuery) ? ", ray tracing" : "");

		if (specification.Window)
		{
			if (!impl.Backend->CreateSwapChain(specification.Window, impl.Width, impl.Height, std::max(specification.SwapChainBufferCount, 2u), specification.VSync))
			{
				PN_CORE_CRITICAL("Cannot create the swap chain");
				return nullptr;
			}
			device->CreateBackBuffers();
		}

		for (auto& query : impl.FrameQueries)
			query = impl.Device->createEventQuery();
		return device;
	}

	void RenderDevice::CreateBackBuffers()
	{
		Impl& impl = *m_Impl;
		impl.Width = impl.Backend->GetSwapChainWidth();
		impl.Height = impl.Backend->GetSwapChainHeight();
		impl.BackBuffers = impl.Backend->CreateBackBufferTextures(impl.NativeDevice);
		for (const nvrhi::TextureHandle& texture : impl.BackBuffers)
			impl.Framebuffers.push_back(impl.Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));
		impl.BackBufferIndex = 0;
	}

	void RenderDevice::ReleaseBackBuffers()
	{
		m_Impl->Framebuffers.clear();
		m_Impl->BackBuffers.clear();
	}

	bool RenderDevice::RecreateSwapChain(uint32_t width, uint32_t height)
	{
		Impl& impl = *m_Impl;
		impl.Device->waitForIdle();
		ReleaseBackBuffers();
		impl.Device->runGarbageCollection();
		if (!impl.Backend->ResizeSwapChain(width, height, impl.Specification.VSync))
		{
			PN_CORE_ERROR("Swap chain resize to {}x{} failed", width, height);
			impl.DeviceLost = true;
			return false;
		}
		CreateBackBuffers();
		impl.SwapChainOutOfDate = false;
		return true;
	}

	nvrhi::IDevice* RenderDevice::GetDevice() const { return m_Impl->Device; }
	GraphicsApi RenderDevice::GetGraphicsApi() const { return m_Impl->Specification.Api; }
	bool RenderDevice::HasSwapChain() const { return m_Impl->Backend && m_Impl->Backend->HasSwapChain(); }

	nvrhi::ITexture* RenderDevice::GetBackBuffer() const
	{
		return m_Impl->BackBuffers.empty() ? nullptr : m_Impl->BackBuffers[m_Impl->BackBufferIndex].Get();
	}

	nvrhi::IFramebuffer* RenderDevice::GetBackBufferFramebuffer() const
	{
		return m_Impl->Framebuffers.empty() ? nullptr : m_Impl->Framebuffers[m_Impl->BackBufferIndex].Get();
	}

	nvrhi::Format RenderDevice::GetBackBufferFormat() const
	{
		return HasSwapChain() ? m_Impl->Backend->GetBackBufferFormat() : nvrhi::Format::RGBA8_UNORM;
	}

	uint32_t RenderDevice::GetBackBufferWidth() const { return m_Impl->Width; }
	uint32_t RenderDevice::GetBackBufferHeight() const { return m_Impl->Height; }
	void RenderDevice::SetVSync(bool enabled) { m_Impl->Specification.VSync = enabled; }
	const std::string& RenderDevice::GetAdapterName() const { return m_Impl->Backend->AdapterName; }
	const std::string& RenderDevice::GetDriverName() const { return m_Impl->Backend->DriverName; }
	bool RenderDevice::IsWarp() const { return m_Impl->Backend->Warp; }
	bool RenderDevice::IsDeviceLost() const { return m_Impl->DeviceLost; }
	uint32_t RenderDevice::GetErrorCount() const { return m_Impl->Messages.ErrorCount; }

	bool RenderDevice::IsRayTracingSupported() const
	{
		return m_Impl->Device && m_Impl->Device->queryFeatureSupport(nvrhi::Feature::RayQuery);
	}

	void RenderDevice::WaitForIdle()
	{
		if (m_Impl->Device)
			m_Impl->Device->waitForIdle();
	}

	void RenderDevice::ResizeSwapChain(uint32_t width, uint32_t height)
	{
		if (!HasSwapChain() || width == 0 || height == 0 || (width == m_Impl->Width && height == m_Impl->Height && !m_Impl->SwapChainOutOfDate))
			return;
		RecreateSwapChain(width, height);
	}

	bool RenderDevice::BeginFrame()
	{
		Impl& impl = *m_Impl;
		if (impl.DeviceLost)
			return false;

		nvrhi::EventQueryHandle& query = impl.FrameQueries[impl.FrameCount % MaxFramesInFlight];
		if (impl.FrameCount >= MaxFramesInFlight)
		{
			impl.Device->waitEventQuery(query);
			impl.Device->resetEventQuery(query);
		}

		if (HasSwapChain())
		{
			for (int attempt = 0; attempt < 2; attempt++)
			{
				const Detail::AcquireResult result = impl.Backend->AcquireNextImage(impl.NativeDevice, impl.BackBufferIndex);
				if (result == Detail::AcquireResult::Success)
					break;
				if (result == Detail::AcquireResult::DeviceLost)
				{
					PN_CORE_CRITICAL("GPU device lost while acquiring a back buffer");
					impl.DeviceLost = true;
					return false;
				}
				// Out of date (window resized or moved to another display): rebuild at the window's size.
				impl.SwapChainOutOfDate = true;
				if (attempt == 1 || !RecreateSwapChain(impl.Width, impl.Height))
					return false;
			}
		}
		return true;
	}

	void RenderDevice::EndFrame()
	{
		Impl& impl = *m_Impl;
		nvrhi::EventQueryHandle& query = impl.FrameQueries[impl.FrameCount % MaxFramesInFlight];
		impl.Device->setEventQuery(query, nvrhi::CommandQueue::Graphics);

		if (HasSwapChain() && !impl.Backend->Present(impl.NativeDevice, impl.BackBufferIndex, impl.Specification.VSync))
			impl.DeviceLost = true;

		impl.Device->runGarbageCollection();
		impl.FrameCount++;
	}

}
