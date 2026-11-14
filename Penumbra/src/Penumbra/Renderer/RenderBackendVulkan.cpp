#include "pnpch.h"
#include "Penumbra/Renderer/RenderBackend.h"

#include "Penumbra/Core/FileSystem.h"
#include "Penumbra/Renderer/VulkanDeviceSelection.h"

#include <nvrhi/vulkan.h>
#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h> // after vulkan.h: declares glfwCreateWindowSurface and glfwInitVulkanLoader

#ifdef _WIN32
	#include <Windows.h>
#else
	#include <dlfcn.h>
#endif

#include <cstdlib>

// NVRHI's Vulkan backend is a static library built for vulkan.hpp's dynamic dispatcher: the program
// defines the dispatcher once (here) and initializes it with the loader it opens at run time. Nothing
// links against a Vulkan library, so a missing one is an error message, not a failure to start.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Penumbra::Detail {

	namespace {

		auto& Dispatch() { return VULKAN_HPP_DEFAULT_DISPATCHER; }

		// --------------------------------------------------------------------------------------------
		// Loader
		// --------------------------------------------------------------------------------------------

		struct VulkanLibrary
		{
			void* Handle = nullptr;
			PFN_vkGetInstanceProcAddr GetInstanceProcAddr = nullptr;
			bool Tried = false;
		};

		VulkanLibrary& Library()
		{
			static VulkanLibrary s_Library;
			return s_Library;
		}

		std::vector<std::string> LibraryCandidates()
		{
			std::vector<std::string> candidates;
#if defined(_WIN32)
			candidates.push_back("vulkan-1.dll");
#elif defined(__APPLE__)
			// Next to the executable first (an exported game can carry its own loader and MoltenVK), then
			// the LunarG SDK (VULKAN_SDK or its system-wide install in /usr/local), then Homebrew.
			const std::string executableDirectory = FileSystem::GetExecutableDirectory().string();
			candidates.push_back(executableDirectory + "/libvulkan.1.dylib");
			if (const char* sdk = std::getenv("VULKAN_SDK"))
				candidates.push_back(std::string(sdk) + "/lib/libvulkan.1.dylib");
			candidates.push_back("libvulkan.1.dylib");
			candidates.push_back("/usr/local/lib/libvulkan.1.dylib");
			candidates.push_back("/opt/homebrew/lib/libvulkan.1.dylib");
			// MoltenVK alone also works (a single implicit device, no layers).
			candidates.push_back("libMoltenVK.dylib");
			candidates.push_back("/usr/local/lib/libMoltenVK.dylib");
			candidates.push_back("/opt/homebrew/lib/libMoltenVK.dylib");
#else
			candidates.push_back("libvulkan.so.1");
			candidates.push_back("libvulkan.so");
#endif
			return candidates;
		}

		bool LoadVulkanLibrary()
		{
			VulkanLibrary& library = Library();
			if (library.GetInstanceProcAddr || library.Tried)
				return library.GetInstanceProcAddr != nullptr;
			library.Tried = true;

			for (const std::string& candidate : LibraryCandidates())
			{
#ifdef _WIN32
				HMODULE dll = LoadLibraryA(candidate.c_str());
				if (!dll)
					continue;
				auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void*>(GetProcAddress(dll, "vkGetInstanceProcAddr")));
				void* handle = dll;
#else
				void* handle = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
				if (!handle)
					continue;
				auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(handle, "vkGetInstanceProcAddr"));
#endif
				if (!proc)
					continue;
				library.Handle = handle;
				library.GetInstanceProcAddr = proc;
				Dispatch().init(proc);
				PN_CORE_TRACE("Vulkan loader: {}", candidate);
				return true;
			}
			return false;
		}

		// --------------------------------------------------------------------------------------------
		// Helpers
		// --------------------------------------------------------------------------------------------

		const char* ResultToString(VkResult result) { return nvrhi::vulkan::resultToString(result); }

		bool Contains(const std::vector<VkExtensionProperties>& extensions, const char* name)
		{
			return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessenger(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
			const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
		{
			auto* messages = static_cast<MessageCallback*>(user);
			const char* text = data && data->pMessage ? data->pMessage : "";
			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
			{
				PN_CORE_ERROR("[Vulkan] {}", text);
				messages->ErrorCount++;
			}
			else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
			{
				PN_CORE_WARN("[Vulkan] {}", text);
			}
			return VK_FALSE;
		}

		// --------------------------------------------------------------------------------------------
		// Backend
		// --------------------------------------------------------------------------------------------

		class VulkanBackend final : public RenderBackend
		{
		public:
			nvrhi::DeviceHandle CreateDevice(const RenderDeviceSpecification& specification, MessageCallback& messages) override;
			bool CreateSwapChain(GLFWwindow* window, uint32_t width, uint32_t height, uint32_t bufferCount, bool vsync) override;
			bool HasSwapChain() const override { return m_SwapChain != VK_NULL_HANDLE; }
			bool ResizeSwapChain(uint32_t width, uint32_t height, bool vsync) override { return BuildSwapChain(width, height, vsync); }
			uint32_t GetSwapChainWidth() const override { return m_Extent.width; }
			uint32_t GetSwapChainHeight() const override { return m_Extent.height; }
			nvrhi::Format GetBackBufferFormat() const override { return m_Format; }
			std::vector<nvrhi::TextureHandle> CreateBackBufferTextures(nvrhi::IDevice* nativeDevice) override;
			AcquireResult AcquireNextImage(nvrhi::IDevice* device, uint32_t& imageIndex) override;
			bool Present(nvrhi::IDevice* device, uint32_t imageIndex, bool vsync) override;
			void Shutdown() override;

		private:
			bool CreateInstance(const RenderDeviceSpecification& specification, MessageCallback& messages);
			bool PickPhysicalDevice();
			bool CreateLogicalDevice();
			bool BuildSwapChain(uint32_t width, uint32_t height, bool vsync);
			void DestroySwapChainSemaphores();

			VkInstance m_Instance = VK_NULL_HANDLE;
			VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
			VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
			VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
			VkDevice m_Device = VK_NULL_HANDLE;
			VkQueue m_Queue = VK_NULL_HANDLE;
			uint32_t m_QueueFamily = 0;
			uint32_t m_DeviceApiVersion = 0;
			std::vector<const char*> m_InstanceExtensions;
			std::vector<const char*> m_DeviceExtensions;

			VkSwapchainKHR m_SwapChain = VK_NULL_HANDLE;
			std::vector<VkImage> m_Images;
			std::vector<VkSemaphore> m_AcquireSemaphores; // ring: one more than images
			std::vector<VkSemaphore> m_PresentSemaphores; // one per image
			uint32_t m_AcquireIndex = 0;
			VkExtent2D m_Extent{};
			VkFormat m_VkFormat = VK_FORMAT_UNDEFINED;
			nvrhi::Format m_Format = nvrhi::Format::BGRA8_UNORM;
			uint32_t m_DesiredImages = 3;
			bool m_VSync = true;
			bool m_NeedsRebuild = false;
			nvrhi::CommandListHandle m_PresentCommandList;
		};

		bool VulkanBackend::CreateInstance(const RenderDeviceSpecification& specification, MessageCallback& messages)
		{
			auto& api = Dispatch();
			uint32_t count = 0;
			api.vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
			std::vector<VkExtensionProperties> available(count);
			api.vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());

			auto add = [&](const char* name) {
				if (std::find_if(m_InstanceExtensions.begin(), m_InstanceExtensions.end(), [name](const char* e) { return std::strcmp(e, name) == 0; }) == m_InstanceExtensions.end())
					m_InstanceExtensions.push_back(name);
			};
			if (specification.Window)
			{
				uint32_t glfwCount = 0;
				const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwCount);
				if (!glfwExtensions)
				{
					PN_CORE_CRITICAL("GLFW found no Vulkan surface support (was RenderDevice::PrepareWindowSystem called?)");
					return false;
				}
				for (uint32_t i = 0; i < glfwCount; i++)
					add(glfwExtensions[i]);
			}
			VkInstanceCreateFlags flags = 0;
			// MoltenVK is a "portability" implementation: it is only listed when the application opts in.
			if (Contains(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
			{
				add(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
				flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
			}
			const bool debugUtils = specification.EnableDebugLayer && Contains(available, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			if (debugUtils)
				add(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

			std::vector<const char*> layers;
			if (specification.EnableDebugLayer)
			{
				api.vkEnumerateInstanceLayerProperties(&count, nullptr);
				std::vector<VkLayerProperties> availableLayers(count);
				api.vkEnumerateInstanceLayerProperties(&count, availableLayers.data());
				const bool hasValidation = std::any_of(availableLayers.begin(), availableLayers.end(),
					[](const VkLayerProperties& layer) { return std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0; });
				if (hasValidation)
					layers.push_back("VK_LAYER_KHRONOS_validation");
				else
					PN_CORE_WARN("Vulkan validation layer requested but not installed (it comes with the LunarG Vulkan SDK)");
			}

			VkApplicationInfo application{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
			application.pApplicationName = "Penumbra";
			application.pEngineName = "Penumbra";
			application.apiVersion = VK_API_VERSION_1_3;

			VkInstanceCreateInfo info{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
			info.flags = flags;
			info.pApplicationInfo = &application;
			info.enabledExtensionCount = static_cast<uint32_t>(m_InstanceExtensions.size());
			info.ppEnabledExtensionNames = m_InstanceExtensions.data();
			info.enabledLayerCount = static_cast<uint32_t>(layers.size());
			info.ppEnabledLayerNames = layers.data();
			const VkResult result = api.vkCreateInstance(&info, nullptr, &m_Instance);
			if (result != VK_SUCCESS)
			{
				PN_CORE_CRITICAL("Cannot create a Vulkan instance ({})", ResultToString(result));
				if (result == VK_ERROR_INCOMPATIBLE_DRIVER)
					PN_CORE_ERROR("The Vulkan loader found no driver. On macOS install the LunarG SDK system-wide or source its setup-env.sh, which points the loader at MoltenVK (docs/macOS.md)");
				return false;
			}
			api.init(vk::Instance(m_Instance));

			if (debugUtils)
			{
				VkDebugUtilsMessengerCreateInfoEXT messenger{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
				messenger.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
				messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
				messenger.pfnUserCallback = DebugMessenger;
				messenger.pUserData = &messages;
				api.vkCreateDebugUtilsMessengerEXT(m_Instance, &messenger, nullptr, &m_Messenger);
			}

			if (specification.Window)
			{
				const VkResult surface = glfwCreateWindowSurface(m_Instance, specification.Window, nullptr, &m_Surface);
				if (surface != VK_SUCCESS)
				{
					PN_CORE_CRITICAL("Cannot create a Vulkan surface for the window ({})", ResultToString(surface));
					return false;
				}
			}
			return true;
		}

		bool VulkanBackend::PickPhysicalDevice()
		{
			auto& api = Dispatch();
			uint32_t count = 0;
			api.vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
			std::vector<VkPhysicalDevice> devices(count);
			api.vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());

			struct Details
			{
				uint32_t QueueFamily = 0;
				uint32_t ApiVersion = 0;
			};
			std::vector<VulkanDeviceCandidate> candidates;
			std::vector<Details> details;
			for (VkPhysicalDevice device : devices)
			{
				VkPhysicalDeviceDriverProperties driver{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES };
				VkPhysicalDeviceProperties2 properties2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
				VkPhysicalDeviceProperties& properties = properties2.properties;
				api.vkGetPhysicalDeviceProperties(device, &properties);
				if (properties.apiVersion >= VK_API_VERSION_1_2) // driver properties are core in 1.2
				{
					properties2.pNext = &driver;
					api.vkGetPhysicalDeviceProperties2(device, &properties2);
				}

				VulkanDeviceCandidate& candidate = candidates.emplace_back();
				Details& detail = details.emplace_back();
				candidate.DeviceName = properties.deviceName;
				candidate.DriverName = driver.driverName[0] ? driver.driverName : "unknown driver";
				candidate.Kind = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? VulkanDeviceKind::Discrete
					: properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU		   ? VulkanDeviceKind::Integrated
																							   : VulkanDeviceKind::Other;
				detail.ApiVersion = properties.apiVersion;
				if (properties.apiVersion < VK_API_VERSION_1_2)
					continue;

				uint32_t extensionCount = 0;
				api.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
				std::vector<VkExtensionProperties> extensions(extensionCount);
				api.vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, extensions.data());
				const bool vulkan13 = properties.apiVersion >= VK_API_VERSION_1_3;
				if (!vulkan13 && (!Contains(extensions, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) || !Contains(extensions, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)))
					continue;
				if (m_Surface && !Contains(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
					continue;

				uint32_t familyCount = 0;
				api.vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, nullptr);
				std::vector<VkQueueFamilyProperties> families(familyCount);
				api.vkGetPhysicalDeviceQueueFamilyProperties(device, &familyCount, families.data());
				std::optional<uint32_t> family;
				for (uint32_t i = 0; i < familyCount && !family; i++)
				{
					const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
					if ((families[i].queueFlags & wanted) != wanted)
						continue;
					VkBool32 present = VK_TRUE;
					if (m_Surface)
						api.vkGetPhysicalDeviceSurfaceSupportKHR(device, i, m_Surface, &present);
					if (present)
						family = i;
				}
				if (!family)
					continue;
				detail.QueueFamily = *family;
				candidate.Suitable = true;
			}

			for (const VulkanDeviceCandidate& candidate : candidates)
				PN_CORE_TRACE("Vulkan device: {} ({}){}", candidate.DeviceName, candidate.DriverName, candidate.Suitable ? "" : ", unsuitable");

			const char* requested = std::getenv(VulkanDriverEnvironmentVariable);
			const VulkanDeviceChoice choice = ChooseVulkanDevice(candidates, requested ? requested : "");
			if (choice.RequestIgnored)
				PN_CORE_WARN("{}={}: no suitable Vulkan device uses that driver; choosing the default", VulkanDriverEnvironmentVariable, requested);
			if (!choice.Index)
			{
				PN_CORE_CRITICAL("No Vulkan 1.2 GPU with dynamic rendering, synchronization2 and a graphics queue{}", m_Surface ? " that can present" : "");
				return false;
			}

			const size_t index = *choice.Index;
			m_PhysicalDevice = devices[index];
			m_QueueFamily = details[index].QueueFamily;
			m_DeviceApiVersion = details[index].ApiVersion;
			AdapterName = candidates[index].DeviceName;
			DriverName = candidates[index].DriverName;
			std::string others;
			for (size_t i = 0; i < candidates.size(); i++)
			{
				if (i != index && candidates[i].Suitable)
					others += std::format("{}{} ({})", others.empty() ? "" : ", ", candidates[i].DeviceName, candidates[i].DriverName);
			}
			PN_CORE_INFO("Vulkan driver: {} for {}{}{}", DriverName, AdapterName, choice.Forced ? std::format(", forced by {}", VulkanDriverEnvironmentVariable) : std::string(),
				others.empty() ? std::string() : std::format("; also available: {} (set {} to choose)", others, VulkanDriverEnvironmentVariable));
			return true;
		}

		bool VulkanBackend::CreateLogicalDevice()
		{
			auto& api = Dispatch();
			uint32_t extensionCount = 0;
			api.vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, nullptr);
			std::vector<VkExtensionProperties> extensions(extensionCount);
			api.vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extensionCount, extensions.data());

			const bool vulkan13 = m_DeviceApiVersion >= VK_API_VERSION_1_3;
			if (m_Surface)
				m_DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			if (!vulkan13)
			{
				m_DeviceExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
				m_DeviceExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
			}
			// Required by the spec whenever the implementation exposes it (MoltenVK does).
			if (Contains(extensions, "VK_KHR_portability_subset"))
				m_DeviceExtensions.push_back("VK_KHR_portability_subset");
			const bool depthClip = Contains(extensions, VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);
			if (depthClip)
				m_DeviceExtensions.push_back(VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME);

			// Every core feature the GPU has (anisotropy, cube arrays, depth clamp, typeless storage writes...)
			// except robust buffer access, which only costs speed.
			VkPhysicalDeviceFeatures supported{};
			api.vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &supported);
			supported.robustBufferAccess = VK_FALSE;

			VkPhysicalDeviceVulkan12Features vulkan12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			VkPhysicalDeviceVulkan13Features vulkan13Features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRendering{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
			VkPhysicalDeviceSynchronization2FeaturesKHR synchronization2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
			VkPhysicalDeviceDepthClipEnableFeaturesEXT depthClipFeatures{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT };

			// Ask what is supported, then enable exactly what is needed.
			VkPhysicalDeviceFeatures2 query{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			void** next = &query.pNext;
			auto chain = [&next](auto& structure) {
				*next = &structure;
				next = &structure.pNext;
			};
			chain(vulkan12);
			if (vulkan13)
				chain(vulkan13Features);
			else
			{
				chain(dynamicRendering);
				chain(synchronization2);
			}
			if (depthClip)
				chain(depthClipFeatures);
			api.vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &query);

			const bool hasDynamicRendering = vulkan13 ? vulkan13Features.dynamicRendering : dynamicRendering.dynamicRendering;
			const bool hasSynchronization2 = vulkan13 ? vulkan13Features.synchronization2 : synchronization2.synchronization2;
			if (!vulkan12.timelineSemaphore || !vulkan12.scalarBlockLayout || !hasDynamicRendering || !hasSynchronization2)
			{
				PN_CORE_CRITICAL("{} lacks timeline semaphores, scalar block layout, dynamic rendering or synchronization2", AdapterName);
				return false;
			}

			VkPhysicalDeviceVulkan12Features enable12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
			enable12.timelineSemaphore = VK_TRUE;
			enable12.scalarBlockLayout = VK_TRUE; // shaders use the D3D buffer layout (cmake/Shaders.cmake)
			VkPhysicalDeviceVulkan13Features enable13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
			enable13.dynamicRendering = VK_TRUE;
			enable13.synchronization2 = VK_TRUE;
			VkPhysicalDeviceDynamicRenderingFeaturesKHR enableDynamicRendering{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
			enableDynamicRendering.dynamicRendering = VK_TRUE;
			VkPhysicalDeviceSynchronization2FeaturesKHR enableSynchronization2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR };
			enableSynchronization2.synchronization2 = VK_TRUE;
			VkPhysicalDeviceDepthClipEnableFeaturesEXT enableDepthClip{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT };
			enableDepthClip.depthClipEnable = depthClipFeatures.depthClipEnable;

			VkPhysicalDeviceFeatures2 features{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
			features.features = supported;
			next = &features.pNext;
			chain(enable12);
			if (vulkan13)
				chain(enable13);
			else
			{
				chain(enableDynamicRendering);
				chain(enableSynchronization2);
			}
			if (depthClip)
				chain(enableDepthClip);

			const float priority = 1.0f;
			VkDeviceQueueCreateInfo queue{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
			queue.queueFamilyIndex = m_QueueFamily;
			queue.queueCount = 1;
			queue.pQueuePriorities = &priority;

			VkDeviceCreateInfo info{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
			info.pNext = &features;
			info.queueCreateInfoCount = 1;
			info.pQueueCreateInfos = &queue;
			info.enabledExtensionCount = static_cast<uint32_t>(m_DeviceExtensions.size());
			info.ppEnabledExtensionNames = m_DeviceExtensions.data();
			const VkResult result = api.vkCreateDevice(m_PhysicalDevice, &info, nullptr, &m_Device);
			if (result != VK_SUCCESS)
			{
				PN_CORE_CRITICAL("Cannot create the Vulkan device ({})", ResultToString(result));
				return false;
			}
			api.init(vk::Device(m_Device));
			api.vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
			return true;
		}

		nvrhi::DeviceHandle VulkanBackend::CreateDevice(const RenderDeviceSpecification& specification, MessageCallback& messages)
		{
			if (!LoadVulkanLibrary())
			{
#ifdef __APPLE__
				PN_CORE_CRITICAL("No Vulkan library found: install the LunarG Vulkan SDK (docs/macOS.md)");
#else
				PN_CORE_CRITICAL("No Vulkan library found (vulkan-1 comes with the GPU driver)");
#endif
				return nullptr;
			}
			if (!CreateInstance(specification, messages) || !PickPhysicalDevice() || !CreateLogicalDevice())
				return nullptr;

			nvrhi::vulkan::DeviceDesc desc;
			desc.errorCB = &messages;
			desc.instance = m_Instance;
			desc.physicalDevice = m_PhysicalDevice;
			desc.device = m_Device;
			desc.graphicsQueue = m_Queue;
			desc.graphicsQueueIndex = static_cast<int>(m_QueueFamily);
			desc.transferQueue = VK_NULL_HANDLE;
			desc.computeQueue = VK_NULL_HANDLE;
			desc.instanceExtensions = m_InstanceExtensions.data();
			desc.numInstanceExtensions = m_InstanceExtensions.size();
			desc.deviceExtensions = m_DeviceExtensions.data();
			desc.numDeviceExtensions = m_DeviceExtensions.size();
			nvrhi::DeviceHandle device = nvrhi::vulkan::createDevice(desc);
			if (!device)
				PN_CORE_CRITICAL("NVRHI failed to create the Vulkan device");
			return device;
		}

		bool VulkanBackend::CreateSwapChain(GLFWwindow*, uint32_t width, uint32_t height, uint32_t bufferCount, bool vsync)
		{
			// The surface was made with the device (a GPU is picked by whether it can present to it).
			if (!m_Surface)
				return false;
			m_DesiredImages = bufferCount;
			return BuildSwapChain(width, height, vsync);
		}

		void VulkanBackend::DestroySwapChainSemaphores()
		{
			auto& api = Dispatch();
			for (VkSemaphore semaphore : m_AcquireSemaphores)
				api.vkDestroySemaphore(m_Device, semaphore, nullptr);
			for (VkSemaphore semaphore : m_PresentSemaphores)
				api.vkDestroySemaphore(m_Device, semaphore, nullptr);
			m_AcquireSemaphores.clear();
			m_PresentSemaphores.clear();
			m_AcquireIndex = 0;
		}

		bool VulkanBackend::BuildSwapChain(uint32_t width, uint32_t height, bool vsync)
		{
			auto& api = Dispatch();
			VkSurfaceCapabilitiesKHR capabilities{};
			api.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_PhysicalDevice, m_Surface, &capabilities);

			VkExtent2D extent = capabilities.currentExtent;
			if (extent.width == UINT32_MAX)
			{
				extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
				extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
			}
			if (extent.width == 0 || extent.height == 0)
				return m_SwapChain != VK_NULL_HANDLE; // minimized: keep the old one until the window has a size again

			uint32_t count = 0;
			api.vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &count, nullptr);
			std::vector<VkSurfaceFormatKHR> formats(count);
			api.vkGetPhysicalDeviceSurfaceFormatsKHR(m_PhysicalDevice, m_Surface, &count, formats.data());
			// The tonemapper writes display-ready values: a UNORM (not sRGB) back buffer, as on D3D12.
			VkSurfaceFormatKHR chosen{ VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
			for (const VkSurfaceFormatKHR& format : formats)
			{
				if ((format.format == VK_FORMAT_B8G8R8A8_UNORM || format.format == VK_FORMAT_R8G8B8A8_UNORM) && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
				{
					chosen = format;
					break;
				}
			}
			if (chosen.format == VK_FORMAT_UNDEFINED)
			{
				PN_CORE_CRITICAL("The window surface offers no 8-bit UNORM format");
				return false;
			}

			api.vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &count, nullptr);
			std::vector<VkPresentModeKHR> modes(count);
			api.vkGetPhysicalDeviceSurfacePresentModesKHR(m_PhysicalDevice, m_Surface, &count, modes.data());
			VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // always available: vsync
			if (!vsync)
			{
				for (VkPresentModeKHR candidate : { VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR })
				{
					if (std::find(modes.begin(), modes.end(), candidate) != modes.end())
					{
						presentMode = candidate;
						break;
					}
				}
			}

			uint32_t imageCount = std::max(m_DesiredImages, capabilities.minImageCount);
			if (capabilities.maxImageCount > 0)
				imageCount = std::min(imageCount, capabilities.maxImageCount);

			VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			if (!(capabilities.supportedCompositeAlpha & compositeAlpha))
			{
				for (VkCompositeAlphaFlagBitsKHR candidate : { VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR })
				{
					if (capabilities.supportedCompositeAlpha & candidate)
					{
						compositeAlpha = candidate;
						break;
					}
				}
			}

			VkSwapchainCreateInfoKHR info{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
			info.surface = m_Surface;
			info.minImageCount = imageCount;
			info.imageFormat = chosen.format;
			info.imageColorSpace = chosen.colorSpace;
			info.imageExtent = extent;
			info.imageArrayLayers = 1;
			info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			info.preTransform = capabilities.currentTransform;
			info.compositeAlpha = compositeAlpha;
			info.presentMode = presentMode;
			info.clipped = VK_TRUE;
			info.oldSwapchain = m_SwapChain;
			VkSwapchainKHR swapChain = VK_NULL_HANDLE;
			const VkResult result = api.vkCreateSwapchainKHR(m_Device, &info, nullptr, &swapChain);
			if (result != VK_SUCCESS)
			{
				PN_CORE_CRITICAL("Cannot create the Vulkan swap chain ({})", ResultToString(result));
				return false;
			}
			// The caller waited for the GPU to go idle: the old swap chain and its semaphores are free.
			if (m_SwapChain)
				api.vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
			DestroySwapChainSemaphores();
			m_SwapChain = swapChain;
			m_Extent = extent;
			m_VkFormat = chosen.format;
			m_Format = chosen.format == VK_FORMAT_B8G8R8A8_UNORM ? nvrhi::Format::BGRA8_UNORM : nvrhi::Format::RGBA8_UNORM;
			m_VSync = vsync;
			m_NeedsRebuild = false;

			api.vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &count, nullptr);
			m_Images.resize(count);
			api.vkGetSwapchainImagesKHR(m_Device, m_SwapChain, &count, m_Images.data());

			VkSemaphoreCreateInfo semaphoreInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
			m_AcquireSemaphores.resize(m_Images.size() + 1);
			m_PresentSemaphores.resize(m_Images.size());
			for (VkSemaphore& semaphore : m_AcquireSemaphores)
				api.vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore);
			for (VkSemaphore& semaphore : m_PresentSemaphores)
				api.vkCreateSemaphore(m_Device, &semaphoreInfo, nullptr, &semaphore);
			return true;
		}

		std::vector<nvrhi::TextureHandle> VulkanBackend::CreateBackBufferTextures(nvrhi::IDevice* nativeDevice)
		{
			std::vector<nvrhi::TextureHandle> textures;
			for (size_t i = 0; i < m_Images.size(); i++)
			{
				nvrhi::TextureDesc desc;
				desc.width = m_Extent.width;
				desc.height = m_Extent.height;
				desc.format = m_Format;
				desc.isRenderTarget = true;
				desc.initialState = nvrhi::ResourceStates::Present;
				desc.keepInitialState = true;
				desc.debugName = std::format("BackBuffer{}", i);
				textures.push_back(nativeDevice->createHandleForNativeTexture(nvrhi::ObjectTypes::VK_Image, nvrhi::Object(m_Images[i]), desc));
			}
			return textures;
		}

		AcquireResult VulkanBackend::AcquireNextImage(nvrhi::IDevice* device, uint32_t& imageIndex)
		{
			if (m_NeedsRebuild)
				return AcquireResult::OutOfDate;
			const VkSemaphore semaphore = m_AcquireSemaphores[m_AcquireIndex];
			const VkResult result = Dispatch().vkAcquireNextImageKHR(m_Device, m_SwapChain, UINT64_MAX, semaphore, VK_NULL_HANDLE, &imageIndex);
			if (result == VK_ERROR_OUT_OF_DATE_KHR)
				return AcquireResult::OutOfDate;
			if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
			{
				PN_CORE_CRITICAL("vkAcquireNextImageKHR failed ({})", ResultToString(result));
				return AcquireResult::DeviceLost;
			}
			if (result == VK_SUBOPTIMAL_KHR)
				m_NeedsRebuild = true; // still usable this frame
			m_AcquireIndex = (m_AcquireIndex + 1) % static_cast<uint32_t>(m_AcquireSemaphores.size());
			// The frame's first submission waits until the image is really free.
			static_cast<nvrhi::vulkan::IDevice*>(device)->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
			return AcquireResult::Success;
		}

		bool VulkanBackend::Present(nvrhi::IDevice* device, uint32_t imageIndex, bool vsync)
		{
			const VkSemaphore semaphore = m_PresentSemaphores[imageIndex];
			// NVRHI signals semaphores with a submission: an empty command list carries the signal (and any
			// wait the frame did not consume).
			static_cast<nvrhi::vulkan::IDevice*>(device)->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, semaphore, 0);
			if (!m_PresentCommandList)
				m_PresentCommandList = device->createCommandList();
			m_PresentCommandList->open();
			m_PresentCommandList->close();
			device->executeCommandList(m_PresentCommandList);

			VkPresentInfoKHR info{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
			info.waitSemaphoreCount = 1;
			info.pWaitSemaphores = &semaphore;
			info.swapchainCount = 1;
			info.pSwapchains = &m_SwapChain;
			info.pImageIndices = &imageIndex;
			const VkResult result = Dispatch().vkQueuePresentKHR(m_Queue, &info);
			if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || vsync != m_VSync)
			{
				m_NeedsRebuild = true;
				return true;
			}
			if (result != VK_SUCCESS)
			{
				PN_CORE_CRITICAL("vkQueuePresentKHR failed ({})", ResultToString(result));
				return false;
			}
			return true;
		}

		void VulkanBackend::Shutdown()
		{
			auto& api = Dispatch();
			m_PresentCommandList = nullptr;
			if (m_Device)
			{
				api.vkDeviceWaitIdle(m_Device);
				DestroySwapChainSemaphores();
				if (m_SwapChain)
					api.vkDestroySwapchainKHR(m_Device, m_SwapChain, nullptr);
				api.vkDestroyDevice(m_Device, nullptr);
			}
			if (m_Instance)
			{
				if (m_Surface)
					api.vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
				if (m_Messenger)
					api.vkDestroyDebugUtilsMessengerEXT(m_Instance, m_Messenger, nullptr);
				api.vkDestroyInstance(m_Instance, nullptr);
			}
			m_SwapChain = VK_NULL_HANDLE;
			m_Surface = VK_NULL_HANDLE;
			m_Device = VK_NULL_HANDLE;
			m_Instance = VK_NULL_HANDLE;
		}

	}

	Scope<RenderBackend> CreateVulkanBackend() { return CreateScope<VulkanBackend>(); }

	bool PrepareVulkanWindowSystem()
	{
		if (!LoadVulkanLibrary())
			return false;
		glfwInitVulkanLoader(Library().GetInstanceProcAddr);
		return true;
	}

}
