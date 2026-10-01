// SPDX-License-Identifier: GPL-3.0-or-later
#include "notify.h"
#include "kernel.h"

#include <cstring>

namespace ps5notify
{
	void Send(const std::string& message)
	{
		SceNotificationRequest request{};
		std::strncpy(request.message, ("PS5Cemu: " + message).c_str(), sizeof(request.message) - 1);
		sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
	}
}
