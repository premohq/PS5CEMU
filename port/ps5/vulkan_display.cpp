// SPDX-License-Identifier: GPL-3.0-or-later
#include "vulkan_display.h"
#include "display.h"
#include "log.h"

#include <algorithm>
#include <vector>

extern "C" VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char* pName);

namespace ps5vk
{
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr()
	{
		return &vk_icdGetInstanceProcAddr;
	}

	VkSurfaceKHR CreateDisplaySurface(VkInstance instance, std::string& error)
	{
		auto gipa = GetInstanceProcAddr();
#define LOAD(name) auto name = reinterpret_cast<PFN_##name>(gipa(instance, #name))
		LOAD(vkEnumeratePhysicalDevices);
		LOAD(vkGetPhysicalDeviceDisplayPropertiesKHR);
		LOAD(vkGetDisplayModePropertiesKHR);
		LOAD(vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
		LOAD(vkGetDisplayPlaneSupportedDisplaysKHR);
		LOAD(vkCreateDisplayPlaneSurfaceKHR);
#undef LOAD
		if (!vkEnumeratePhysicalDevices || !vkGetPhysicalDeviceDisplayPropertiesKHR || !vkCreateDisplayPlaneSurfaceKHR)
		{
			error = "the Vulkan instance was made without VK_KHR_display";
			return VK_NULL_HANDLE;
		}

		uint32_t count = 0;
		vkEnumeratePhysicalDevices(instance, &count, nullptr);
		if (count == 0)
		{
			error = "the Vulkan driver reports no GPU";
			return VK_NULL_HANDLE;
		}
		std::vector<VkPhysicalDevice> devices(count);
		vkEnumeratePhysicalDevices(instance, &count, devices.data());
		const VkPhysicalDevice device = devices[0];

		count = 0;
		vkGetPhysicalDeviceDisplayPropertiesKHR(device, &count, nullptr);
		if (count == 0)
		{
			error = "VideoOut is not available as a Vulkan display";
			return VK_NULL_HANDLE;
		}
		std::vector<VkDisplayPropertiesKHR> displays(count);
		vkGetPhysicalDeviceDisplayPropertiesKHR(device, &count, displays.data());
		const VkDisplayKHR display = displays[0].display;

		// the high refresh rate mode only when the 120 Hz setting is on
		count = 0;
		vkGetDisplayModePropertiesKHR(device, display, &count, nullptr);
		std::vector<VkDisplayModePropertiesKHR> modes(count);
		vkGetDisplayModePropertiesKHR(device, display, &count, modes.data());
		if (modes.empty())
		{
			error = "VideoOut reports no display mode";
			return VK_NULL_HANDLE;
		}
		const bool highFrameRate = ps5display::HighFrameRate();
		const VkDisplayModePropertiesKHR* chosen = &modes[0];
		for (const auto& mode : modes)
		{
			if ((mode.parameters.refreshRate > 100000) == highFrameRate)
			{
				chosen = &mode;
				break;
			}
		}

		// the plane that can show this display
		uint32_t planeIndex = 0;
		count = 0;
		vkGetPhysicalDeviceDisplayPlanePropertiesKHR(device, &count, nullptr);
		for (uint32_t plane = 0; plane < count; plane++)
		{
			uint32_t supported = 0;
			vkGetDisplayPlaneSupportedDisplaysKHR(device, plane, &supported, nullptr);
			std::vector<VkDisplayKHR> planeDisplays(supported);
			vkGetDisplayPlaneSupportedDisplaysKHR(device, plane, &supported, planeDisplays.data());
			if (std::find(planeDisplays.begin(), planeDisplays.end(), display) != planeDisplays.end())
			{
				planeIndex = plane;
				break;
			}
		}

		// the mode's whole size: RADV's VideoOut swapchains are no other
		const uint32_t width = chosen->parameters.visibleRegion.width, height = chosen->parameters.visibleRegion.height;

		VkDisplaySurfaceCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR};
		createInfo.displayMode = chosen->displayMode;
		createInfo.planeIndex = planeIndex;
		createInfo.planeStackIndex = 0;
		createInfo.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
		createInfo.globalAlpha = 1.0f;
		createInfo.alphaMode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.imageExtent = {width, height};
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		const VkResult result = vkCreateDisplayPlaneSurfaceKHR(instance, &createInfo, nullptr, &surface);
		if (result != VK_SUCCESS)
		{
			error = "cannot create the VideoOut surface (VkResult " + std::to_string((int)result) + ")";
			return VK_NULL_HANDLE;
		}
		ps5log::Line("[vulkan] VideoOut surface {}x{} at {:.2f} Hz", width, height, chosen->parameters.refreshRate / 1000.0);
		return surface;
	}
}

extern "C" void* PS5Vk_GetInstanceProcAddrRaw()
{
	return reinterpret_cast<void*>(ps5vk::GetInstanceProcAddr());
}

extern "C" bool PS5Vk_CreateDisplaySurfaceRaw(void* instance, uint64_t* surfaceOut)
{
	std::string error;
	const VkSurfaceKHR surface = ps5vk::CreateDisplaySurface(static_cast<VkInstance>(instance), error);
	if (surface == VK_NULL_HANDLE)
	{
		ps5log::Line("[vulkan] {}", error);
		return false;
	}
	*surfaceOut = reinterpret_cast<uint64_t>(surface);
	return true;
}
