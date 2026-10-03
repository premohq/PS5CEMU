// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: azahar.h for a build without Azahar's core. The launcher's 3DS side works (its
// library, settings and controls), and a game says why it cannot start.

#include "azahar.h"

namespace ps5azahar
{
	namespace
	{
		constexpr const char* kMissing = "Azahar's core is not part of this build yet.";
	}

	bool Available()
	{
		return false;
	}

	bool LaunchGame(const ps5emu::Game&, const ps5settings::N3ds&, std::string& error)
	{
		error = kMissing;
		return false;
	}

	void RunGame()
	{
	}

	bool CoreTouched()
	{
		return false;
	}

	bool StartInstall(const std::string&, std::string& error)
	{
		error = kMissing;
		return false;
	}

	ps5emu::InstallStatus GetInstallStatus()
	{
		return {};
	}

	void CancelInstall()
	{
	}
}
