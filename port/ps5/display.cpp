// SPDX-License-Identifier: GPL-3.0-or-later
#include "display.h"

#include <atomic>

namespace
{
	std::atomic<bool> s_highFrameRate{false};
}

namespace ps5display
{
	void SetHighFrameRate(bool highFrameRate)
	{
		s_highFrameRate = highFrameRate;
	}

	bool HighFrameRate()
	{
		return s_highFrameRate;
	}
}
