// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher. Home (continue playing, recently played), the library, each game's
// graphic packs, settings (video, audio, controls, diagnostics, the game files folder) and about,
// driven by the DualSense. Its screens and their element ids are ProsperoEden's (ui/main.rml).

#pragma once

#include "../app/emulator.h"
#include "settings.h"

#include <optional>
#include <string>
#include <vector>

namespace ps5launcher
{
	struct Status
	{
		bool coreReady = false;	 // Cemu started: the library can open
		std::string notice;		 // a problem to show on the home screen (empty: none)
		std::vector<std::string> diagnostics; // the lines Settings > Diagnostics shows
	};

	// Shows the launcher until a game is chosen, saving the settings it changes. Returns nothing when
	// the launcher could not show (the reason is in the boot log and a notification).
	std::optional<ps5emu::Game> Run(ps5settings::Launcher& settings, const Status& status);
}
