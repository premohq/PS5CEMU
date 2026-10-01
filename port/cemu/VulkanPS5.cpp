// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Cemu's Vulkan on the PS5's RADV (see port/ps5/vulkan_display.h): the driver's
// vkGetInstanceProcAddr stands in for a loader, and the window is a VK_KHR_display surface on
// VideoOut.

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "../ps5/vulkan_display.h"

PFN_vkVoidFunction PS5_VulkanGetGlobalProc(const char* name)
{
	const auto gipa = ps5vk::GetInstanceProcAddr();
	if (strcmp(name, "vkGetInstanceProcAddr") == 0)
		return reinterpret_cast<PFN_vkVoidFunction>(gipa);
	return gipa(VK_NULL_HANDLE, name);
}

VkSurfaceKHR PS5_CreateDisplaySurface(VkInstance instance)
{
	std::string error;
	const VkSurfaceKHR surface = ps5vk::CreateDisplaySurface(instance, error);
	if (surface == VK_NULL_HANDLE)
		throw std::runtime_error("PS5: " + error);
	return surface;
}
