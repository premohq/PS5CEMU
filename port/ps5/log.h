// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the port's boot log, /data/ps5cemu/logs/boot.log (the previous session's is kept as
// boot.prev.log). Every line also goes to stdout, which the console's klog shows. Cemu's own log
// (log.txt) is separate and lives in /data/ps5cemu.

#pragma once

#include <fmt/format.h>
#include <string_view>

namespace ps5log
{
	void Open(const char* folder);
	void Write(std::string_view line);
	const char* Path();

	template<typename... Args>
	void Line(fmt::format_string<Args...> format, Args&&... args)
	{
		Write(fmt::format(format, std::forward<Args>(args)...));
	}
}
