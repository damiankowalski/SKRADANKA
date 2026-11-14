#pragma once

#include "Penumbra/Core/Base.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Choosing among Vulkan devices, kept free of Vulkan headers so it can be unit tested. A Mac can show the
// same GPU twice, once per installed driver (MoltenVK and Mesa's KosmicKrisp): the engine prefers the
// driver it is tested on, and PENUMBRA_VULKAN_DRIVER forces another one.

namespace Penumbra {

	enum class VulkanDeviceKind
	{
		Other,
		Integrated,
		Discrete,
	};

	struct VulkanDeviceCandidate
	{
		std::string DeviceName;
		std::string DriverName; // VkPhysicalDeviceDriverProperties::driverName, e.g. "MoltenVK", "KosmicKrisp"
		VulkanDeviceKind Kind = VulkanDeviceKind::Other;
		bool Suitable = false; // has every feature and queue the renderer needs
	};

	// Drivers preferred when the same kind of device is offered by several (the ones CI runs on).
	inline constexpr std::string_view PreferredVulkanDrivers[] = { "MoltenVK" };

	// The environment variable that forces a driver (case-insensitive part of its name, e.g. "kosmickrisp").
	inline constexpr const char* VulkanDriverEnvironmentVariable = "PENUMBRA_VULKAN_DRIVER";

	struct VulkanDeviceChoice
	{
		std::optional<size_t> Index; // empty when no candidate is suitable
		bool Forced = false; // the requested driver was found and chosen
		bool RequestIgnored = false; // a driver was requested but no suitable device uses it
	};

	// Suitable devices only. A requested driver (empty: none) wins when a suitable device uses it;
	// otherwise discrete beats integrated beats other, then a preferred driver, then the earlier device.
	VulkanDeviceChoice ChooseVulkanDevice(const std::vector<VulkanDeviceCandidate>& candidates, std::string_view requestedDriver);

	// Case-insensitive: `driverName` contains `request`.
	bool VulkanDriverMatches(std::string_view driverName, std::string_view request);

}
