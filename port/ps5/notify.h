// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the system's notification toast (top right of the screen).

#pragma once

#include <string>

namespace ps5notify
{
	void Send(const std::string& message);
}
