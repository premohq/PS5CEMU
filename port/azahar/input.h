// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the DualSense as the 3DS, for Azahar's core: an input engine named "ps5" whose
// buttons, sticks and motion read the latest sample the game's loop took (Update), mapped as the
// launcher's 3DS controls say (controls.h). The touch screen is the touchpad: a finger moves a
// cursor over the bottom screen, and clicking the touchpad touches it there.

#pragma once

#include "../frontend/settings.h"

#include <string>

namespace Frontend
{
	class EmuWindow;
}

namespace ps5azahar::input
{
	// Registers the engine and puts the launcher's mapping into Azahar's input settings.
	void Configure(const ps5settings::N3ds& settings);
	void Shutdown();

	// Takes the controller's latest sample: the buttons the game sees and the touch screen (on
	// window, whose layout places the bottom screen). Call it a few hundred times a second. While
	// blocked (the in-game menu is up), the game sees the controller let go.
	void Update(Frontend::EmuWindow& window, bool blocked);

	// The cursor over the bottom screen, from 0 to 1 across it; true while a finger is on the
	// touchpad (EmuWindow::GetCursorInfo).
	bool Cursor(float& x, float& y);
}
