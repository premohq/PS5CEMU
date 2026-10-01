// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: what takes RADV's place in a link check (tools/link.sh, PS5CEMU_LINK_CHECK=1): the
// driver's one entry point, answering no function at all, so the launcher and Cemu's renderer
// report that Vulkan did not start. A link check's ELF is never packaged.

#include <stddef.h>

typedef void (*PFN_vkVoidFunction)(void);

PFN_vkVoidFunction vk_icdGetInstanceProcAddr(void* instance, const char* name)
{
	(void)instance;
	(void)name;
	return NULL;
}

// The platform layer's start-up hook that PS5_Vulkan's CRT calls (IEEE floating point for RADV).
void ps5_fp_ieee(void)
{
}
