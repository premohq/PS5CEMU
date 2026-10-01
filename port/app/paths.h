// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: where things live on the console.
//
//   /data/homebrew/PPSA99360/        the app (eboot.bin, sce_sys, assets) as ShadowMountPlus mounts it
//   /app0/assets/                    the same, read-only, from inside the app
//   /data/ps5cemu/                   everything PS5Cemu writes; survives app updates
//     settings.xml, ps5cemu.json     Cemu's settings and the launcher's
//     log.txt                        Cemu's log
//     controllerProfiles/            Cemu's controller profiles
//     mlc01/                         the Wii U's internal storage: installed games, updates, DLC, saves
//     games/                         the default game files folder (.wua, .wud/.wux, .rpx folders)
//     keys.txt                       disc keys for encrypted .wud/.wux dumps
//     graphicPacks/                  community packs (downloadedGraphicPacks/) and your own
//     cache/                         shader and pipeline caches
//     covers/                        game icons converted for the launcher
//     logs/                          boot.log (the port; boot.prev.log is the session before)

#pragma once

namespace ps5paths
{
	constexpr const char* kTitleId = "PPSA99360";
	constexpr const char* kEboot = "/app0/eboot.bin";
	constexpr const char* kMountedEboot = "/data/homebrew/PPSA99360/eboot.bin";
	constexpr const char* kAssets = "/app0/assets";
	constexpr const char* kCemuData = "/app0/assets/cemu"; // Cemu's read-only data (gameProfiles, resources)
	constexpr const char* kBundledGraphicPacks = "/app0/assets/graphicPacks";

	constexpr const char* kRoot = "/data/ps5cemu";
	constexpr const char* kMlc = "/data/ps5cemu/mlc01";
	constexpr const char* kGames = "/data/ps5cemu/games";
	constexpr const char* kCache = "/data/ps5cemu/cache";
	constexpr const char* kLogs = "/data/ps5cemu/logs";
	constexpr const char* kLauncherSettings = "/data/ps5cemu/ps5cemu.json";
	constexpr const char* kCovers = "/data/ps5cemu/covers";
}
