// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the launcher. It opens on the start screen, split down the middle: Cemu on the left,
// Azahar on the right. Each has the same screens, in its own colours: home (continue playing,
// recently played), the library, settings (video, audio, controls, the game files folder,
// installs, diagnostics) and about; Cemu's games have their graphic packs too. All of it is driven
// by the DualSense. Its screens and their element ids are ProsperoEden's (tools/render-layout.py).

#pragma once

#include "../app/emulator.h"
#include "settings.h"

#include <optional>
#include <string>
#include <vector>

namespace ps5launcher
{
	enum class System
	{
		WiiU, // Cemu
		N3ds, // Azahar
	};

	struct Status
	{
		bool coreReady = false;	 // Cemu started: its library can open
		std::string notice;		 // a problem to show on Cemu's home screen (empty: none)
		std::string notice3ds;	 // and on Azahar's
		std::vector<std::string> diagnostics; // the lines Settings > Diagnostics shows
	};

	struct Choice
	{
		System system;
		ps5emu::Game game;
	};

	// Shows the launcher until a game is chosen, saving the settings it changes (the emulator
	// chosen among them, which the launcher opens on next time). Returns nothing when the launcher
	// could not show (the reason is in the boot log and a notification).
	std::optional<Choice> Run(ps5settings::Launcher& settings, const Status& status);
}
