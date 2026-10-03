// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the launcher's drawing. RmlUi lays out the app's assets/ui layouts (ProsperoEden's
// layout, fonts and artwork, adapted: start.rml, the choice of emulator; main.rml, Cemu's;
// azahar.rml, Azahar's) at 1920x1080 and draws them in software on VideoOut through SDL, as
// ProsperoEden does, over a moving background. Everything is torn down before a game starts, so the
// emulator's renderer finds VideoOut free.

#pragma once

#include <string>

namespace Rml
{
	class ElementDocument;
}

namespace ps5ui
{
	// What is under the page: Cemu's bubbles (bubbles.h), Azahar's waves (wave.h), or both, the
	// bubbles on the left half and the waves on the right.
	enum class Scene
	{
		Bubbles,
		Wave,
		Both,
	};

	// SDL's window, RmlUi and the fonts. False, with a reason, when the launcher cannot show.
	bool Start(std::string& error);
	// A layout (assets/ui/<name>), loaded the first time, shown with the others hidden. Null when
	// it cannot be loaded.
	Rml::ElementDocument* Show(const std::string& name);
	void SetScene(Scene scene);
	// Lays out and draws one frame; returns once VideoOut has it (the display paces the launcher).
	void Frame();
	// Everything Start made, Vulkan included.
	void Stop();
	// A file of the launcher's (fonts, artwork, styles) from its path below the app's assets/ui.
	std::string AssetPath(const std::string& relative);
}
