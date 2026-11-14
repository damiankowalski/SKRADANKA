#include "pnpch.h"
#include "Penumbra/Renderer/VulkanDeviceSelection.h"

#include <algorithm>
#include <cctype>

namespace Penumbra {

	namespace {

		char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

		int KindScore(VulkanDeviceKind kind)
		{
			switch (kind)
			{
				case VulkanDeviceKind::Discrete:   return 2;
				case VulkanDeviceKind::Integrated: return 1;
				case VulkanDeviceKind::Other:	   return 0;
			}
			return 0;
		}

		bool IsPreferredDriver(std::string_view driverName)
		{
			for (std::string_view preferred : PreferredVulkanDrivers)
			{
				if (driverName.size() == preferred.size()
					&& std::equal(driverName.begin(), driverName.end(), preferred.begin(), [](char a, char b) { return Lower(a) == Lower(b); }))
					return true;
			}
			return false;
		}

	}

	bool VulkanDriverMatches(std::string_view driverName, std::string_view request)
	{
		if (request.empty())
			return false;
		const auto found = std::search(driverName.begin(), driverName.end(), request.begin(), request.end(),
			[](char a, char b) { return Lower(a) == Lower(b); });
		return found != driverName.end();
	}

	VulkanDeviceChoice ChooseVulkanDevice(const std::vector<VulkanDeviceCandidate>& candidates, std::string_view requestedDriver)
	{
		VulkanDeviceChoice choice;
		if (!requestedDriver.empty())
		{
			for (size_t i = 0; i < candidates.size(); i++)
			{
				if (candidates[i].Suitable && VulkanDriverMatches(candidates[i].DriverName, requestedDriver))
				{
					choice.Index = i;
					choice.Forced = true;
					return choice;
				}
			}
			choice.RequestIgnored = true;
		}

		int bestScore = -1;
		for (size_t i = 0; i < candidates.size(); i++)
		{
			const VulkanDeviceCandidate& candidate = candidates[i];
			if (!candidate.Suitable)
				continue;
			const int score = KindScore(candidate.Kind) * 2 + (IsPreferredDriver(candidate.DriverName) ? 1 : 0);
			if (score > bestScore) // strictly greater: ties keep the earlier device
			{
				bestScore = score;
				choice.Index = i;
			}
		}
		return choice;
	}

}
