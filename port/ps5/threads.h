// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: where the emulator's threads run. By default the system places every thread, as in
// 1.0: pinning made Breath of the Wild slower on a console (a pinned thread cannot be moved away
// from a busy core), so it is only an experiment, turned on with "pinCpuThreads": true in
// ps5cemu.json. Then the Wii U's three emulated CPU cores (Cemu's OSSched[core=0..2], named by
// util/helpers) get a core each, both of its logical CPUs, counted from the title's second core,
// and every other named thread all of the title's CPUs.

#pragma once

#include <string>

namespace ps5threads
{
	// Reads the CPUs the title runs on (the main thread's, which the threads it starts inherit).
	// Call it from the main thread before any other starts.
	void Initialize();
	// Those CPUs, for the boot log ("CPUs 0-13: 7 cores").
	std::string Describe();
	// The experiment above, from the launcher's settings: off, nothing is placed.
	void SetPinning(bool enabled);
	// Puts the calling thread on the CPUs a thread of that name gets, when pinning is on.
	void Place(const char* name);
}
