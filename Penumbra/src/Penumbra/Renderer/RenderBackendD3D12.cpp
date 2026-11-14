#include "pnpch.h"
#include "Penumbra/Renderer/RenderBackend.h"

#ifdef _WIN32

	#include <nvrhi/d3d12.h>

	#include <d3d12.h>
	#include <dxgi1_6.h>
	#include <wrl/client.h>

	#define GLFW_INCLUDE_NONE
	#define GLFW_EXPOSE_NATIVE_WIN32
	#include <GLFW/glfw3.h>
	#include <GLFW/glfw3native.h>

using Microsoft::WRL::ComPtr;

namespace Penumbra::Detail {

	namespace {

		std::string Narrow(const wchar_t* text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1)
				return {};
			std::string result(static_cast<size_t>(size - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
			return result;
		}

		class D3D12Backend final : public RenderBackend
		{
		public:
			nvrhi::DeviceHandle CreateDevice(const RenderDeviceSpecification& specification, MessageCallback& messages) override;
			bool CreateSwapChain(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t bufferCount, bool vsync) override;
			bool HasSwapChain() const override { return m_SwapChain != nullptr; }
			bool ResizeSwapChain(uint32_t width, uint32_t height, bool vsync) override;
			uint32_t GetSwapChainWidth() const override { return m_Width; }
			uint32_t GetSwapChainHeight() const override { return m_Height; }
			nvrhi::Format GetBackBufferFormat() const override { return nvrhi::Format::RGBA8_UNORM; }
			std::vector<nvrhi::TextureHandle> CreateBackBufferTextures(nvrhi::IDevice* nativeDevice) override;
			AcquireResult AcquireNextImage(nvrhi::IDevice* device, uint32_t& imageIndex) override;
			bool Present(nvrhi::IDevice* device, uint32_t imageIndex, bool vsync) override;
			void Shutdown() override;

		private:
			ComPtr<IDXGIFactory6> m_Factory;
			ComPtr<IDXGIAdapter1> m_Adapter;
			ComPtr<ID3D12Device> m_Device;
			ComPtr<ID3D12CommandQueue> m_GraphicsQueue;
			ComPtr<ID3D12CommandQueue> m_ComputeQueue;
			ComPtr<ID3D12CommandQueue> m_CopyQueue;
			ComPtr<IDXGISwapChain3> m_SwapChain;
			bool m_TearingSupported = false;
			uint32_t m_Width = 0;
			uint32_t m_Height = 0;
		};

		nvrhi::DeviceHandle D3D12Backend::CreateDevice(const RenderDeviceSpecification& specification, MessageCallback& messages)
		{
			if (specification.EnableDebugLayer)
			{
				ComPtr<ID3D12Debug> debug;
				if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
					debug->EnableDebugLayer();
				else
					PN_CORE_WARN("D3D12 debug layer requested but not available (install Graphics Tools)");
			}

			const UINT factoryFlags = specification.EnableDebugLayer ? DXGI_CREATE_FACTORY_DEBUG : 0;
			if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_Factory))) && FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_Factory))))
			{
				PN_CORE_CRITICAL("Cannot create a DXGI factory");
				return nullptr;
			}

			BOOL tearing = FALSE;
			if (SUCCEEDED(m_Factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))))
				m_TearingSupported = tearing == TRUE;

			auto tryAdapter = [&](ComPtr<IDXGIAdapter1> adapter) {
				if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_Device))))
					return false;
				DXGI_ADAPTER_DESC1 desc{};
				adapter->GetDesc1(&desc);
				AdapterName = Narrow(desc.Description);
				m_Adapter = adapter;
				return true;
			};

			if (!specification.UseWarp)
			{
				ComPtr<IDXGIAdapter1> adapter;
				for (UINT i = 0; m_Factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; i++)
				{
					DXGI_ADAPTER_DESC1 desc{};
					adapter->GetDesc1(&desc);
					if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
						continue;
					if (tryAdapter(adapter))
						break;
				}
			}
			if (!m_Device)
			{
				if (!specification.UseWarp)
					PN_CORE_WARN("No hardware D3D12 adapter available, falling back to WARP");
				ComPtr<IDXGIAdapter1> warp;
				if (FAILED(m_Factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))) || !tryAdapter(warp))
				{
					PN_CORE_CRITICAL("Cannot create a Direct3D 12 device (feature level 12_0 required)");
					return nullptr;
				}
				Warp = true;
			}

			if (specification.EnableDebugLayer)
			{
				ComPtr<ID3D12InfoQueue> infoQueue;
				if (SUCCEEDED(m_Device.As(&infoQueue)))
				{
					infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, IsDebuggerPresent());
					infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, IsDebuggerPresent());
				}
			}

			auto createQueue = [&](D3D12_COMMAND_LIST_TYPE type, ComPtr<ID3D12CommandQueue>& queue, const wchar_t* name) {
				D3D12_COMMAND_QUEUE_DESC desc{};
				desc.Type = type;
				desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				if (FAILED(m_Device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue))))
					return false;
				queue->SetName(name);
				return true;
			};
			if (!createQueue(D3D12_COMMAND_LIST_TYPE_DIRECT, m_GraphicsQueue, L"Graphics Queue") ||
				!createQueue(D3D12_COMMAND_LIST_TYPE_COMPUTE, m_ComputeQueue, L"Compute Queue") ||
				!createQueue(D3D12_COMMAND_LIST_TYPE_COPY, m_CopyQueue, L"Copy Queue"))
			{
				PN_CORE_CRITICAL("Cannot create D3D12 command queues");
				return nullptr;
			}

			nvrhi::d3d12::DeviceDesc deviceDesc;
			deviceDesc.errorCB = &messages;
			deviceDesc.pDevice = m_Device.Get();
			deviceDesc.pGraphicsCommandQueue = m_GraphicsQueue.Get();
			deviceDesc.pComputeCommandQueue = m_ComputeQueue.Get();
			deviceDesc.pCopyCommandQueue = m_CopyQueue.Get();
			nvrhi::DeviceHandle device = nvrhi::d3d12::createDevice(deviceDesc);
			if (!device)
				PN_CORE_CRITICAL("NVRHI failed to create the D3D12 device");
			return device;
		}

		bool D3D12Backend::CreateSwapChain(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t bufferCount, bool)
		{
			const HWND hwnd = glfwGetWin32Window(window);
			DXGI_SWAP_CHAIN_DESC1 desc{};
			desc.Width = width;
			desc.Height = height;
			desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.BufferCount = bufferCount;
			desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
			desc.Flags = m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

			ComPtr<IDXGISwapChain1> swapChain;
			if (FAILED(m_Factory->CreateSwapChainForHwnd(m_GraphicsQueue.Get(), hwnd, &desc, nullptr, nullptr, &swapChain)) ||
				FAILED(swapChain.As(&m_SwapChain)))
				return false;
			m_Factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);
			m_Width = width;
			m_Height = height;
			return true;
		}

		bool D3D12Backend::ResizeSwapChain(uint32_t width, uint32_t height, bool)
		{
			DXGI_SWAP_CHAIN_DESC1 desc{};
			m_SwapChain->GetDesc1(&desc);
			const HRESULT result = m_SwapChain->ResizeBuffers(desc.BufferCount, width, height, desc.Format, desc.Flags);
			if (FAILED(result))
			{
				PN_CORE_ERROR("Swap chain resize failed (0x{:08X})", static_cast<uint32_t>(result));
				return false;
			}
			m_Width = width;
			m_Height = height;
			return true;
		}

		std::vector<nvrhi::TextureHandle> D3D12Backend::CreateBackBufferTextures(nvrhi::IDevice* nativeDevice)
		{
			std::vector<nvrhi::TextureHandle> textures;
			DXGI_SWAP_CHAIN_DESC1 desc{};
			m_SwapChain->GetDesc1(&desc);
			for (UINT i = 0; i < desc.BufferCount; i++)
			{
				ComPtr<ID3D12Resource> buffer;
				m_SwapChain->GetBuffer(i, IID_PPV_ARGS(&buffer));

				nvrhi::TextureDesc textureDesc;
				textureDesc.width = desc.Width;
				textureDesc.height = desc.Height;
				textureDesc.format = nvrhi::Format::RGBA8_UNORM;
				textureDesc.isRenderTarget = true;
				textureDesc.initialState = nvrhi::ResourceStates::Present;
				textureDesc.keepInitialState = true;
				textureDesc.debugName = std::format("BackBuffer{}", i);
				textures.push_back(nativeDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(buffer.Get()), textureDesc));
			}
			return textures;
		}

		AcquireResult D3D12Backend::AcquireNextImage(nvrhi::IDevice*, uint32_t& imageIndex)
		{
			imageIndex = m_SwapChain->GetCurrentBackBufferIndex();
			return AcquireResult::Success;
		}

		bool D3D12Backend::Present(nvrhi::IDevice*, uint32_t, bool vsync)
		{
			const UINT flags = (!vsync && m_TearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
			const HRESULT result = m_SwapChain->Present(vsync ? 1 : 0, flags);
			if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
			{
				const HRESULT reason = m_Device->GetDeviceRemovedReason();
				PN_CORE_CRITICAL("GPU device lost (0x{:08X}, reason 0x{:08X})", static_cast<uint32_t>(result), static_cast<uint32_t>(reason));
				return false;
			}
			return true;
		}

		void D3D12Backend::Shutdown()
		{
			m_SwapChain.Reset();
		}

	}

	Scope<RenderBackend> CreateD3D12Backend() { return CreateScope<D3D12Backend>(); }

}

#endif
