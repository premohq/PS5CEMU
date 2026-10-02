// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: Cemu's WindowSystem on the PS5. There is one window, the TV, at VideoOut's 3840x2160
// (display.h); no separate GamePad window (the GamePad's picture can be the main one, or go in a
// corner: app/ingame.h); no keyboard. Error dialogs go to the boot log, a system notification and the launcher.

#include "WindowSystem.h"
#include "window.h"
#include "display.h"
#include "log.h"
#include "notify.h"

#include <mutex>

namespace
{
	WindowSystem::WindowInfo s_windowInfo;
	std::mutex s_errorMutex;
	std::string s_lastError;

	void UpdateSizes()
	{
		s_windowInfo.width = (int32_t)ps5display::kWidth;
		s_windowInfo.height = (int32_t)ps5display::kHeight;
		s_windowInfo.phys_width = (int32_t)ps5display::kWidth;
		s_windowInfo.phys_height = (int32_t)ps5display::kHeight;
		s_windowInfo.dpi_scale = ps5display::kHeight / 1080.0;
	}
}

namespace ps5window
{
	void Initialize()
	{
		UpdateSizes();
		s_windowInfo.app_active = true;
		s_windowInfo.is_fullscreen = true;
		s_windowInfo.pad_open = false;
		s_windowInfo.window_main.backend = WindowSystem::WindowHandleInfo::Backend::PS5VideoOut;
		s_windowInfo.canvas_main.backend = WindowSystem::WindowHandleInfo::Backend::PS5VideoOut;
	}

	std::string TakeLastError()
	{
		std::lock_guard lock(s_errorMutex);
		return std::exchange(s_lastError, {});
	}
}

namespace WindowSystem
{
	void ShowErrorDialog(std::string_view message, std::string_view title, std::optional<ErrorCategory> errorCategory)
	{
		ps5log::Line("[error] {}{}{}", title, title.empty() ? "" : ": ", message);
		ps5notify::Send(std::string(message));
		std::lock_guard lock(s_errorMutex);
		s_lastError = std::string(message);
	}

	void Create()
	{
		// the wx entry point; the PS5 port starts in main_ps5.cpp instead
	}

	WindowInfo& GetWindowInfo()
	{
		return s_windowInfo;
	}

	void UpdateWindowTitles(bool isIdle, bool isLoading, double fps) {}

	void GetWindowSize(int& w, int& h)
	{
		UpdateSizes();
		w = s_windowInfo.width;
		h = s_windowInfo.height;
	}

	void GetPadWindowSize(int& w, int& h)
	{
		w = 0;
		h = 0;
	}

	void GetWindowPhysSize(int& w, int& h)
	{
		UpdateSizes();
		w = s_windowInfo.phys_width;
		h = s_windowInfo.phys_height;
	}

	void GetPadWindowPhysSize(int& w, int& h)
	{
		w = 0;
		h = 0;
	}

	double GetWindowDPIScale()
	{
		return s_windowInfo.dpi_scale;
	}

	double GetPadDPIScale()
	{
		return 1.0;
	}

	bool IsPadWindowOpen()
	{
		return false;
	}

	bool IsKeyDown(uint32 key)
	{
		return false;
	}

	bool IsKeyDown(PlatformKeyCodes key)
	{
		return false;
	}

	std::string GetKeyCodeName(uint32 key)
	{
		return {};
	}

	bool InputConfigWindowHasFocus()
	{
		return false;
	}

	void NotifyGameLoaded() {}
	void NotifyGameExited() {}
	void RefreshGameList() {}

	bool IsFullScreen()
	{
		return true;
	}

	void CaptureInput(const ControllerState& currentState, const ControllerState& lastState) {}
}
