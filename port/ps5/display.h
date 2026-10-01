// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the output, shared by the launcher's and Cemu's Vulkan surfaces. VideoOut has one
// 3840x2160 mode (59.94 Hz, or 119.88 Hz where the title declares high-frame-rate output and the
// display takes it), and RADV's swapchains on it are that size (PS5_Mesa's wsi_common_videoout.c):
// Cemu scales the game's picture to it with its upscaling filter, and the launcher draws its
// 1920x1080 layout at twice the size.

#pragma once

#include <cstdint>

namespace ps5display
{
	constexpr uint32_t kWidth = 3840, kHeight = 2160;

	// The 119.88 Hz mode for the next surface, where the display has it.
	void SetHighFrameRate(bool highFrameRate);
	bool HighFrameRate();
}
