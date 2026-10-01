// SPDX-License-Identifier: GPL-3.0-or-later
#include "display.h"

#include <atomic>

namespace
{
	std::atomic<uint32_t> s_width{3840}, s_height{2160};
	std::atomic<bool> s_highFrameRate{false};
}

namespace ps5display
{
	void SetOutput(uint32_t width, uint32_t height, bool highFrameRate)
	{
		s_width = width;
		s_height = height;
		s_highFrameRate = highFrameRate;
	}

	void OutputSize(uint32_t& width, uint32_t& height)
	{
		width = s_width;
		height = s_height;
	}

	bool HighFrameRate()
	{
		return s_highFrameRate;
	}
}
