// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Vulkan on the PS5's RADV, shared by the launcher and Cemu's renderer.
//
// The driver (Mesa's RADV with a PS5 winsys, PS5_Mesa) is linked into the title, so its
// vk_icdGetInstanceProcAddr stands in for a loader. The screen is VideoOut, which the driver
// exposes as VK_KHR_display: one display, one plane and a 3840x2160 mode (59.94 Hz, or 119.88 Hz
// where the title declares high-frame-rate output and the display takes it). A swapchain smaller
// than the mode is scaled up by VideoOut, so the plane surface's image size is the output
// resolution the launcher's video settings ask for (display.h).

#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#include <string>

namespace ps5vk
{
	// The driver's vkGetInstanceProcAddr.
	PFN_vkGetInstanceProcAddr GetInstanceProcAddr();

	// Instance extensions a display surface needs.
	constexpr const char* kSurfaceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME};

	// A VK_KHR_display plane surface on VideoOut at the configured output size. Returns
	// VK_NULL_HANDLE, with the reason in error, when it cannot.
	VkSurfaceKHR CreateDisplaySurface(VkInstance instance, std::string& error);
}

// The same, without Vulkan types, for code built against another copy of the Vulkan headers
// (RmlUi's glad loader in the launcher).
extern "C" void* PS5Vk_GetInstanceProcAddrRaw();
extern "C" bool PS5Vk_CreateDisplaySurfaceRaw(void* instance, uint64_t* surfaceOut);
