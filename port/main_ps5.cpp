// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the entry point (Cemu's src/main.cpp on the desktop).
//
//  1. out of the sandbox: /data, and JIT memory for the recompiler (ps5/privilege.h);
//  2. the boot log, the DualSense and Cemu's core (settings, MLC, graphic packs, the game scan);
//  3. the launcher, until a game is chosen;
//  4. the game, on Cemu's Vulkan renderer, until touchpad + L1 (twice) asks for the library, which
//     starts PS5Cemu over (app/emulator.h, RestartToLibrary).

#include "app/emulator.h"
#include "app/paths.h"
#include "frontend/launcher.h"
#include "frontend/settings.h"
#include "ps5/display.h"
#include "ps5/kernel.h"
#include "ps5/log.h"
#include "ps5/notify.h"
#include "ps5/pad.h"
#include "ps5/privilege.h"
#include "ps5/window.h"

namespace
{
	std::vector<std::string> Diagnostics(const ps5privilege::Result& privileges)
	{
		return {
			fmt::format("PS5Cemu {}, Cemu {}.{}.{}", PS5CEMU_VERSION, EMULATOR_VERSION_MAJOR, EMULATOR_VERSION_MINOR, EMULATOR_VERSION_PATCH),
			privileges.summary,
			fmt::format("Boot log: {}", ps5log::Path()[0] ? ps5log::Path() : "not written (/data is unreachable)"),
			fmt::format("Cemu's log: {}/log.txt", ps5paths::kRoot),
		};
	}

	ps5emu::Options Options(const ps5settings::Launcher& settings)
	{
		return {settings.gamesFolder, settings.overlay, settings.volume, settings.upscaleFilter};
	}
}

// What Cemu's src/main.cpp defines for the rest of Cemu.
std::atomic_bool g_isGPUInitFinished = false;
// Cemu's command line asks for a console window with some options; there is none on the PS5.
void requireConsole() {}

int main(int argc, char* argv[])
{
	ps5log::Line("[main] PS5Cemu {} starting", PS5CEMU_VERSION);
	// before any thread starts: the HEN jailbreaks the process as it is
	const ps5privilege::Result privileges = ps5privilege::Acquire();
	if (privileges.filesystem)
		ps5log::Open(ps5paths::kLogs);
	ps5log::Line("[main] {}", privileges.summary);

	ps5settings::Launcher settings = ps5settings::Load();
	ps5pad::Init();
	ps5pad::SetVibrationEnabled(settings.rumble);
	ps5window::Initialize();

	ps5launcher::Status status;
	status.diagnostics = Diagnostics(privileges);
	std::string error;
	if (!privileges.filesystem)
		status.notice = "PS5Cemu cannot reach /data. Load etaHEN, add PPSA99360 to its app jailbreak list, then restart PS5Cemu.";
	else if (!ps5emu::InitializeCore(error))
		status.notice = "Cemu did not start: " + error;
	else
	{
		status.coreReady = true;
		ps5emu::ApplyOptions(Options(settings));
	}
	if (!status.notice.empty())
	{
		ps5log::Line("[main] {}", status.notice);
		ps5notify::Send(status.notice);
	}
	else if (!settings.launchError.empty())
	{
		// the last game failed after its renderer started, and the process was started over to show it
		status.notice = "The game could not start: " + settings.launchError;
		settings.launchError.clear();
		ps5settings::Save(settings);
	}
	if (!privileges.jit)
		ps5notify::Send("No JIT memory: games run on the interpreter, much slower. Is PPSA99360 in etaHEN's jailbreak list?");

	for (;;)
	{
		ps5display::SetHighFrameRate(false); // the launcher at 59.94 Hz
		const auto game = ps5launcher::Run(settings, status);
		if (!game)
		{
			// nothing to show it on: wait for the player to close the app from the PS5's menu
			for (;;)
				sceKernelUsleep(1000000);
		}

		ps5emu::ApplyOptions(Options(settings));
		ps5display::SetHighFrameRate(settings.highFrameRate);
		ps5window::Initialize();
		if (ps5emu::LaunchGame(*game, error))
		{
			ps5emu::RunGame();
			ps5emu::RestartToLibrary();
			return 0;
		}
		ps5log::Line("[main] {} did not start: {}", game->name, error);
		if (!ps5emu::RendererStarted())
		{
			status.notice = "The game could not start: " + error;
			continue;
		}
		// Cemu's renderer holds VideoOut: show the reason from a fresh process
		settings.launchError = error;
		ps5settings::Save(settings);
		ps5emu::RestartToLibrary();
		return 1;
	}
}
