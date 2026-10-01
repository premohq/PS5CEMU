// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's WindowSystem on the PS5 (window_system.cpp): one full-screen window, the TV.

#pragma once

#include <string>

namespace ps5window
{
	// Before Cemu's renderer starts: the window's size is the output resolution (display.h).
	void Initialize();
	// The last error Cemu reported through WindowSystem::ShowErrorDialog, cleared by the call.
	std::string TakeLastError();
}
