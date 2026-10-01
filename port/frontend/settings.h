// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher's settings, /data/ps5cemu/ps5cemu.json. Cemu's own settings stay in its
// settings.xml; the launcher writes the few it manages (game folder, volume, overlay) into both.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5settings
{
	struct Launcher
	{
		std::string gamesFolder = "/data/ps5cemu/games";
		bool output4k = true;		  // 3840x2160, else 1920x1080 (VideoOut scales it up)
		bool highFrameRate = false;	  // the 119.88 Hz mode where the display has it
		bool overlay = false;		  // Cemu's performance overlay from the start
		bool rumble = true;
		int volume = 100;			  // the TV sound, in percent
		uint64_t lastGame = 0;		  // title ID
		std::vector<uint64_t> recent; // newest first, at most four
		// Why the last game did not start, when that needed a fresh process to show (the launcher
		// shows it once, then clears it).
		std::string launchError;
	};

	Launcher Load();
	bool Save(const Launcher& settings);
	void AddRecent(Launcher& settings, uint64_t titleId);
}
