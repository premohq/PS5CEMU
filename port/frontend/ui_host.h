// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher's drawing. RmlUi lays out /app0/assets/ui/main.rml (ProsperoEden's layout,
// fonts and artwork, adapted) at 1920x1080 and its Vulkan renderer draws it at twice the size on
// VideoOut through RADV, the driver Cemu's renderer uses after it. Everything is torn down before a game starts, so
// Cemu's renderer finds VideoOut free.

#pragma once

#include <string>

namespace Rml
{
	class ElementDocument;
}

namespace ps5ui
{
	// Vulkan, RmlUi, the fonts and the document. False, with a reason, when the launcher cannot show.
	bool Start(std::string& error);
	Rml::ElementDocument* Document();
	// Lays out and draws one frame; returns once VideoOut has it (the display paces the launcher).
	void Frame();
	// Everything Start made, Vulkan included.
	void Stop();
	// A file of the launcher's (fonts, artwork, styles) from its path below /app0/assets/ui.
	std::string AssetPath(const std::string& relative);
}
