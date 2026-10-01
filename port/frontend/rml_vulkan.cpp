// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Vulkan's functions for RmlUi's renderer (patches/rmlui: RMLUI_VK_EXTERNAL_LOADER).
//
// Upstream, RmlUi's glad loader dlopens libvulkan. On the PS5 the driver is linked into the title,
// so glad gets every function from RADV's vkGetInstanceProcAddr (and, once there is a device, its
// vkGetDeviceProcAddr), as the loader would hand them out.

#include "RmlUi_Include_Vulkan.h"

extern "C" void* PS5Vk_GetInstanceProcAddrRaw();

namespace
{
	struct Scope
	{
		VkInstance instance;
		VkDevice device;
		PFN_vkGetInstanceProcAddr getInstanceProcAddr;
		PFN_vkGetDeviceProcAddr getDeviceProcAddr;
	};

	GLADapiproc Load(void* user, const char* name)
	{
		const auto* scope = static_cast<const Scope*>(user);
		if (scope->device && scope->getDeviceProcAddr)
			if (const auto function = scope->getDeviceProcAddr(scope->device, name))
				return reinterpret_cast<GLADapiproc>(function);
		return reinterpret_cast<GLADapiproc>(scope->getInstanceProcAddr(scope->instance, name));
	}
}

int RmlUiVk_LoadVulkan(VkInstance instance, VkPhysicalDevice physical_device, VkDevice device)
{
	Scope scope{instance, device, reinterpret_cast<PFN_vkGetInstanceProcAddr>(PS5Vk_GetInstanceProcAddrRaw()), nullptr};
	if (instance)
		scope.getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(scope.getInstanceProcAddr(instance, "vkGetDeviceProcAddr"));
	return gladLoadVulkanUserPtr(physical_device, &Load, &scope);
}
