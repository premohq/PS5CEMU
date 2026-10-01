// SPDX-License-Identifier: GPL-3.0-or-later
#include "PS5PadControllerProvider.h"
#include "PS5PadController.h"
#include "../ps5/pad.h"

PS5PadControllerProvider::PS5PadControllerProvider()
{
	ps5pad::Init();
}

std::vector<std::shared_ptr<ControllerBase>> PS5PadControllerProvider::get_controllers()
{
	ps5pad::Rescan();
	std::vector<std::shared_ptr<ControllerBase>> result;
	for (int player = 0; player < ps5pad::kMaxPlayers; player++)
	{
		if (ps5pad::IsConnected(player))
			result.emplace_back(std::make_shared<PS5PadController>(player));
	}
	return result;
}
