// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's core, started and driven the way its desktop frontend does (src/main.cpp's
// CemuCommonInit, gui/wxgui's CemuApp::OnInit and MainWindow::FileLoad), without wxWidgets.
//
// The launcher includes this header, so it names no Cemu type.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ps5emu
{
	struct Game
	{
		uint64_t titleId = 0;		 // the base title
		std::string name;			 // from meta.xml
		std::filesystem::path path;	 // what Cemu launches
		uint16_t version = 0;		 // the base's, or the update's when installed
		bool hasUpdate = false;
		uint32_t dlcCount = 0;
		std::string format;			 // WUA, WUD, WUX, folder (code/content/meta) or RPX
	};

	// What the launcher's settings change in Cemu's.
	struct Options
	{
		std::string gamesFolder;
		bool overlay = false; // the performance overlay from the start
		int volume = 100;	  // the TV's, in percent
		int upscaleFilter = 1; // Cemu's upscale_filter: 0 linear, 1 bicubic, 2 bicubic Hermite, 3 nearest
	};

	// Paths, settings (with PS5 defaults on first start), MLC, graphic packs, controllers and
	// Cemu's own initialisation. False, with a reason, when something essential is missing.
	bool InitializeCore(std::string& error);

	// Writes the launcher's settings into Cemu's (settings.xml). A new games folder is scanned.
	void ApplyOptions(const Options& options);

	// Whether Cemu is still looking for games (the library fills in when it is done).
	bool Scanning();

	// The games Cemu found in the game files folder and the MLC, sorted by name. Does not wait
	// for a scan in progress: it lists what was found so far.
	std::vector<Game> ListGames();

	// The Vulkan renderer on VideoOut, then the title. False, with a reason, when it cannot start.
	bool LaunchGame(const Game& game, std::string& error);

	// Whether LaunchGame got as far as the renderer: a failure after that point needs a fresh
	// process (RestartToLibrary) before the launcher can show again.
	bool RendererStarted();

	// Runs while the game does, handling the port's shortcuts. Returns when the player asks for the
	// library; the caller then restarts the app (RestartToLibrary).
	void RunGame();

	// Starts PS5Cemu over (sceSystemServiceLoadExec on its own eboot), which shows the library with
	// the last game selected. Cemu cannot yet shut a game down and start another reliably in one
	// process (MainWindow::EndEmulation says so), so leaving a game means a fresh process; PS5SX2
	// goes back to its shelf the same way. Returns only if the restart did not take.
	void RestartToLibrary();

	// A game's icon (meta/iconTex.tga) as an uncompressed top-down TGA in /data/ps5cemu/covers,
	// extracted on first use; empty when the game has none or it cannot be read.
	std::string CoverPath(uint64_t titleId);

	// The community graphic packs (graphic_packs.cpp): what Cemu's Graphic Packs window shows for
	// one game, with its choices saved to settings.xml as that window saves them.
	struct GraphicPackInfo
	{
		std::string name;		 // "Resolution"
		std::string category;	 // "Graphics", the folders between the game and the pack
		std::string description;
		bool enabled = false;
		std::string preset;		 // the active presets, "category: name" when there are categories
		bool hasPresets = false; // more than one preset to choose from
	};
	std::vector<GraphicPackInfo> ListGraphicPacks(uint64_t titleId);
	int EnabledGraphicPackCount(uint64_t titleId);
	// Turns a pack of the game (by its index in ListGraphicPacks) on or off; returns its new state.
	bool ToggleGraphicPack(uint64_t titleId, size_t index);
	// Moves the pack's first preset choice by delta (left -1, right +1).
	void CycleGraphicPackPreset(uint64_t titleId, size_t index, int delta);
}
