// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's controller provider for the DualSense (port/ps5/pad.h).

#pragma once

#include "input/api/ControllerProvider.h"

class PS5PadControllerProvider : public ControllerProviderBase
{
public:
	PS5PadControllerProvider();

	inline static InputAPI::Type kAPIType = InputAPI::PS5Pad;
	InputAPI::Type api() const override { return kAPIType; }

	// one controller per player slot
	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;
};
