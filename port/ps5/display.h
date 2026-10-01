// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the output the launcher's video settings ask for, shared by the launcher's and Cemu's
// Vulkan surfaces (VideoOut scales anything smaller than its 3840x2160 mode up to it).

#pragma once

#include <cstdint>

namespace ps5display
{
	void SetOutput(uint32_t width, uint32_t height, bool highFrameRate);
	void OutputSize(uint32_t& width, uint32_t& height);
	bool HighFrameRate();
}
