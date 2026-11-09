#include "pnpch.h"
#include "Penumbra/Renderer/RenderDevice.h"

#include <nvrhi/d3d12.h>
#include <nvrhi/validation.h>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Penumbra {

	namespace {

		class MessageCallback final : public nvrhi::IMessageCallback
		{
		public:
			void message(nvrhi::MessageSeverity severity, const char* text) override
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

			std::atomic<uint32_t> ErrorCount = 0;
		};

		std::string Narrow(const wchar_t* text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1)
				return {};
			std::string result(static_cast<size_t>(size - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
			return result;
		}

	}

	struct RenderDevice::Impl
	{
		RenderDeviceSpecification Specification;
		MessageCallback Messages;

		ComPtr<IDXGIFactory6> Factory;
		ComPtr<IDXGIAdapter1> Adapter;
		ComPtr<ID3D12Device> D3DDevice;
		ComPtr<ID3D12CommandQueue> GraphicsQueue;
		ComPtr<ID3D12CommandQueue> ComputeQueue;
		ComPtr<ID3D12CommandQueue> CopyQueue;
		ComPtr<IDXGISwapChain3> SwapChain;
		HWND Window = nullptr;
		bool TearingSupported = false;
		bool Warp = false;
		bool DeviceLost = false;
		std::string AdapterName;

		nvrhi::DeviceHandle NativeDevice;
		nvrhi::DeviceHandle Device; // possibly wrapped by the validation layer
		std::vector<nvrhi::TextureHandle> BackBuffers;
		std::vector<nvrhi::FramebufferHandle> Framebuffers;
		uint32_t BackBufferIndex = 0;
		uint32_t Width = 0;
		uint32_t Height = 0;

		std::array<nvrhi::EventQueryHandle, MaxFramesInFlight> FrameQueries;
		uint64_t FrameCount = 0;

		bool CreateDevice();
		bool CreateSwapChain();
		void CreateBackBuffers();
		void ReleaseBackBuffers();
	};

	bool RenderDevice::Impl::CreateDevice()
	{
		if (Specification.EnableDebugLayer)
		{
			ComPtr<ID3D12Debug> debug;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
				debug->EnableDebugLayer();
			else
				PN_CORE_WARN("D3D12 debug layer requested but not available (install Graphics Tools)");
		}

		const UINT factoryFlags = Specification.EnableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0;
		if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&Factory))) && FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&Factory))))
		{
			PN_CORE_CRITICAL("Cannot create a DXGI factory");
			return false;
		}

		BOOL tearing = FALSE;
		if (SUCCEEDED(Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
			TearingSupported = tearing == TRUE;

		auto tryAdapter = [&](ComPtr<IDXGIAdapter1> adapter) {
			if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&D3DDevice))))
				return false;
			DXGI_ADAPTER_DESC1 desc{};
			adapter->GetDesc1(&desc);
			AdapterName = Narrow(desc.Description);
			Adapter = adapter;
			return true;
		};

		if (!Specification.UseWarp)
		{
			ComPtr<IDXGIAdapter1> adapter;
			for (UINT i = 0; Factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; i++)
			{
				DXGI_ADAPTER_DESC1 desc{};
				adapter->GetDesc1(&desc);
				if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
					continue;
				if (tryAdapter(adapter))
					break;
			}
		}
		if (!D3DDevice)
		{
			if (!Specification.UseWarp)
				PN_CORE_WARN("No hardware D3D12 adapter available, falling back to WARP");
			ComPtr<IDXGIAdapter1> warp;
			if (FAILED(Factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) || !tryAdapter(warp))
			{
				PN_CORE_CRITICAL("Cannot create a Direct3D 12 device (feature level 12_0 required)");
				return false;
			}
			Warp = true;
		}

		if (Specification.EnableDebugLayer)
		{
			ComPtr<ID3D12InfoQueue> infoQueue;
			if (SUCCEEDED(D3DDevice.As(&infoQueue)))
			{
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, IsDebuggerPresent());
				infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, IsDebuggerPresent());
			}
		}

		auto createQueue = [&](D3D12_COMMAND_LIST_TYPE type, ComPtr<ID3D12CommandQueue>& queue, const wchar_t* name) {
			D3D12_COMMAND_QUEUE_DESC desc{};
			desc.Type = type;
			desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
			if (FAILED(D3DDevice->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))))
				return false;
			queue->SetName(name);
			return true;
		};
		if (!createQueue(D3D12_COMMAND_LIST_TYPE_DIRECT, GraphicsQueue, L"Graphics Queue") ||
			!createQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE, ComputeQueue, L"Compute Queue") ||
			!createQueue(D3D12_COMMAND_LIST_TYPE_COPY, CopyQueue, L"Copy Queue"))
		{
			PN_CORE_CRITICAL("Cannot create D3D12 command queues");
			return false;
		}

		nvrhi::d3d12::DeviceDesc deviceDesc;
		deviceDesc.errorCB = &Messages;
		deviceDesc.pDevice = D3DDevice.Get();
		deviceDesc.pGraphicsCommandQueue = GraphicsQueue.Get();
		deviceDesc.pComputeCommandQueue = ComputeQueue.Get();
		deviceDesc.pCopyCommandQueue = CopyQueue.Get();
		NativeDevice = nvrhi::d3d12::createDevice(deviceDesc);
		if (!NativeDevice)
		{
			PN_CORE_CRITICAL("NVRHI failed to create the D3D12 device");
			return false;
		}
		Device = Specification.EnableDebugLayer ? nvrhi::validation::createValidationLayer(NativeDevice) : NativeDevice;

		PN_CORE_INFO("Render device: {} (D3D12{}{})", AdapterName, Warp ? ", WARP" : "",
			Device->queryFeatureSupport(nvrhi::Feature::RayQuery) ? ", ray tracing" : "");
		return true;
	}

	bool RenderDevice::Impl::CreateSwapChain()
	{
		DXGI_SWAP_CHAIN_DESC1 desc{};
		desc.Width = Width;
		desc.Height = Height;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		desc.BufferCount = std::max(Specification.SwapChainBufferCount, 2u);
		desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		desc.Flags = TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		ComPtr<IDXGISwapChain1> swapChain;
		if (FAILED(Factory->CreateSwapChainForHwnd(GraphicsQueue.Get(), Window, &desc, nullptr, nullptr, &swapChain)) ||
			FAILED(swapChain.As(&SwapChain)))
		{
			PN_CORE_CRITICAL("Cannot create the swap chain");
			return false;
		}
		Factory->MakeWindowAssociation(Window, DXGI_MWA_NO_ALT_ENTER);
		CreateBackBuffers();
		return true;
	}

	void RenderDevice::Impl::CreateBackBuffers()
	{
		DXGI_SWAP_CHAIN_DESC1 desc{};
		SwapChain->GetDesc1(&desc);
		for (UINT i = 0; i < desc.BufferCount; i++)
		{
			ComPtr<ID3D12Resource> buffer;
			SwapChain->GetBuffer(i, IID_PPV_ARGS(&buffer));

			nvrhi::TextureDesc textureDesc;
			textureDesc.width = desc.Width;
			textureDesc.height = desc.Height;
			textureDesc.format = nvrhi::Format::RGBA8_UNORM;
			textureDesc.isRenderTarget = true;
			textureDesc.initialState = nvrhi::ResourceStates::Present;
			textureDesc.keepInitialState = true;
			textureDesc.debugName = std::format("BackBuffer{}", i);

			nvrhi::TextureHandle texture = NativeDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource,
				nvrhi::Object(buffer.Get()), textureDesc);
			BackBuffers.push_back(texture);
			Framebuffers.push_back(Device->createFramebuffer(nvrhi::FramebufferDesc().addColorAttachment(texture)));
		}
		BackBufferIndex = SwapChain->GetCurrentBackBufferIndex();
	}

	void RenderDevice::Impl::ReleaseBackBuffers()
	{
		Framebuffers.clear();
		BackBuffers.clear();
	}

	RenderDevice::RenderDevice()
		: m_Impl(CreateScope<Impl>())
	{
	}

	RenderDevice::~RenderDevice()
	{
		if (m_Impl->Device)
			m_Impl->Device->waitForIdle();
		m_Impl->ReleaseBackBuffers();
		for (auto& query : m_Impl->FrameQueries)
			query = nullptr;
		if (m_Impl->Device)
			m_Impl->Device->runGarbageCollection();
		m_Impl->Device = nullptr;
		m_Impl->NativeDevice = nullptr;
		m_Impl->SwapChain.Reset();
	}

	Scope<RenderDevice> RenderDevice::Create(const RenderDeviceSpecification& specification)
	{
		Scope<RenderDevice> device(new RenderDevice());
		Impl& impl = *device->m_Impl;
		impl.Specification = specification;
		impl.Window = static_cast<HWND>(specification.WindowHandle);
		impl.Width = std::max(specification.BackBufferWidth, 1u);
		impl.Height = std::max(specification.BackBufferHeight, 1u);

		if (!impl.CreateDevice())
			return nullptr;
		if (impl.Window && !impl.CreateSwapChain())
			return nullptr;

		for (auto& query : impl.FrameQueries)
			query = impl.Device->createEventQuery();
		return device;
	}

	nvrhi::IDevice* RenderDevice::GetDevice() const { return m_Impl->Device; }
	bool RenderDevice::HasSwapChain() const { return m_Impl->SwapChain != nullptr; }

	nvrhi::ITexture* RenderDevice::GetBackBuffer() const
	{
		return m_Impl->BackBuffers.empty() ? nullptr : m_Impl->BackBuffers[m_Impl->BackBufferIndex].Get();
	}

	nvrhi::IFramebuffer* RenderDevice::GetBackBufferFramebuffer() const
	{
		return m_Impl->Framebuffers.empty() ? nullptr : m_Impl->Framebuffers[m_Impl->BackBufferIndex].Get();
	}

	uint32_t RenderDevice::GetBackBufferWidth() const { return m_Impl->Width; }
	uint32_t RenderDevice::GetBackBufferHeight() const { return m_Impl->Height; }
	void RenderDevice::SetVSync(bool enabled) { m_Impl->Specification.VSync = enabled; }
	const std::string& RenderDevice::GetAdapterName() const { return m_Impl->AdapterName; }
	bool RenderDevice::IsWarp() const { return m_Impl->Warp; }
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
		if (!m_Impl->SwapChain || width == 0 || height == 0 || (width == m_Impl->Width && height == m_Impl->Height))
			return;

		m_Impl->Device->waitForIdle();
		m_Impl->ReleaseBackBuffers();
		m_Impl->Device->runGarbageCollection();

		DXGI_SWAP_CHAIN_DESC1 desc{};
		m_Impl->SwapChain->GetDesc1(&desc);
		const HRESULT result = m_Impl->SwapChain->ResizeBuffers(desc.BufferCount, width, height, desc.Format, desc.Flags);
		if (FAILED(result))
		{
			PN_CORE_ERROR("Swap chain resize failed (0x{:08X})", static_cast<uint32_t>(result));
			m_Impl->DeviceLost = true;
			return;
		}
		m_Impl->Width = width;
		m_Impl->Height = height;
		m_Impl->CreateBackBuffers();
	}

	bool RenderDevice::BeginFrame()
	{
		if (m_Impl->DeviceLost)
			return false;

		nvrhi::EventQueryHandle& query = m_Impl->FrameQueries[m_Impl->FrameCount % MaxFramesInFlight];
		if (m_Impl->FrameCount >= MaxFramesInFlight)
		{
			m_Impl->Device->waitEventQuery(query);
			m_Impl->Device->resetEventQuery(query);
		}

		if (m_Impl->SwapChain)
			m_Impl->BackBufferIndex = m_Impl->SwapChain->GetCurrentBackBufferIndex();
		return true;
	}

	void RenderDevice::EndFrame()
	{
		nvrhi::EventQueryHandle& query = m_Impl->FrameQueries[m_Impl->FrameCount % MaxFramesInFlight];
		m_Impl->Device->setEventQuery(query, nvrhi::CommandQueue::Graphics);

		if (m_Impl->SwapChain)
		{
			const bool vsync = m_Impl->Specification.VSync;
			const UINT flags = (!vsync && m_Impl->TearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
			const HRESULT result = m_Impl->SwapChain->Present(vsync ? 1 : 0, flags);
			if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
			{
				const HRESULT reason = m_Impl->D3DDevice->GetDeviceRemovedReason();
				PN_CORE_CRITICAL("GPU device lost (0x{:08X}, reason 0x{:08X})", static_cast<uint32_t>(result), static_cast<uint32_t>(reason));
				m_Impl->DeviceLost = true;
			}
		}

		m_Impl->Device->runGarbageCollection();
		m_Impl->FrameCount++;
	}

}
