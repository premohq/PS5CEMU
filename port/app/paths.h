// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: where things live on the console.
//
//   /data/homebrew/PPSA99360/        the app (eboot.bin, sce_sys, assets) as ShadowMountPlus mounts it
//   /app0/                           the same, read-only, from inside the app's sandbox (AppDir)
//   /data/ps5cemu/                   everything PS5Cemu writes; survives app updates
//     settings.xml, ps5cemu.json     Cemu's settings and the launcher's
//     log.txt                        Cemu's log
//     controllerProfiles/            Cemu's controller profiles
//     mlc01/                         the Wii U's internal storage: installed games, updates, DLC, saves
//     games/                         the default game files folder (.wua, .wud/.wux, .rpx folders)
//     keys.txt                       disc keys for encrypted .wud/.wux dumps
//     graphicPacks/                  community packs (downloadedGraphicPacks/) and your own
//     cache/                         shader and pipeline caches (radv/: RADV's own)
//     covers/                        game icons converted for the launcher
//     logs/                          boot.log (the port; boot.prev.log is the session before)

#pragma once

#include <initializer_list>
#include <string>
#include <sys/stat.h>

namespace ps5paths
{
	constexpr const char* kTitleId = "PPSA99360";
	constexpr const char* kInstallDir = "/data/homebrew/PPSA99360";
	constexpr const char* kMountedEboot = "/data/homebrew/PPSA99360/eboot.bin";

// everything PS5CEMU-HAR writes: on the console /data/ps5cemu; the launcher's preview on a PC
// (tools/preview-launcher.sh) gives a folder of its own
#ifndef PS5CEMU_DATA
#define PS5CEMU_DATA "/data/ps5cemu"
#endif
	constexpr const char* kRoot = PS5CEMU_DATA;
	constexpr const char* kMlc = PS5CEMU_DATA "/mlc01";
	constexpr const char* kGames = PS5CEMU_DATA "/games";
	constexpr const char* kCache = PS5CEMU_DATA "/cache";
	constexpr const char* kRadvCache = PS5CEMU_DATA "/cache/radv";
	constexpr const char* kLogs = PS5CEMU_DATA "/logs";
	constexpr const char* kLauncherSettings = PS5CEMU_DATA "/ps5cemu.json";
	constexpr const char* kCovers = PS5CEMU_DATA "/covers";

	// The app's own folder. The sandbox mounts it as /app0, but a process the HEN has jailbroken
	// sees the console's root, which has no /app0: there the app is read where it is installed, or
	// where the sandbox mounts it from (as ProsperoEden's storage_paths.h finds its own). Decided on
	// first use, which comes after ps5privilege::Acquire.
	inline const std::string& AppDir()
	{
		static const std::string directory = [] {
			for (const char* candidate : {"/app0", kInstallDir, "/mnt/sandbox/PPSA99360_000/app0"})
			{
				struct stat info{};
				if (stat((std::string(candidate) + "/eboot.bin").c_str(), &info) == 0 && S_ISREG(info.st_mode))
					return std::string(candidate);
			}
			return std::string(kInstallDir);
		}();
		return directory;
	}

	inline std::string Eboot() { return AppDir() + "/eboot.bin"; }
	inline std::string Assets() { return AppDir() + "/assets"; }
	// Cemu's read-only data (gameProfiles, resources)
	inline std::string CemuData() { return Assets() + "/cemu"; }
	inline std::string BundledGraphicPacks() { return Assets() + "/graphicPacks"; }
}
