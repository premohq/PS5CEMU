// SPDX-License-Identifier: GPL-3.0-or-later
#include "ingame.h"
#include "../ps5/kernel.h"
#include "../ps5/pad.h"

#include "audio/IAudioAPI.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/OS/libs/swkbd/swkbd.h"
#include "config/CemuConfig.h"
#include "imgui/imgui_extension.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace
{
	std::atomic<bool> s_menuOpen{false};
	std::atomic<uint64_t> s_menuOpenedAt{0}; // sceKernelGetProcessTime
	std::atomic<bool> s_cornerScreen{false};
	std::atomic<bool> s_saveRequested{false};
	std::atomic<bool> s_libraryRequested{false};
	std::atomic<bool> s_keyboardShift{false};

	struct Pointer
	{
		bool finger = false;
		float x = 0.5f, y = 0.5f;
		bool touching = false;
	};
	std::mutex s_pointerMutex;
	Pointer s_pointer;

	// The rest belongs to the render thread, which draws the overlays.
	struct Area
	{
		bool visible = false;
		float x = 0, y = 0, width = 0, height = 0;
	};
	Area s_gamePadArea; // where the GamePad's picture is this frame
	uint32_t s_buttons = 0, s_pressed = 0; // player 1's buttons, and those pressed since the last frame
	bool s_confirmLibrary = false;
	std::string s_gameName;

	float UiScale()
	{
		// VideoOut is 3840x2160: the overlays at the size they have on a 1080p screen
		return std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f);
	}

	void DrawCursor(ImDrawList* draw, ImVec2 at, bool pressed, float scale)
	{
		const float radius = 12.0f * scale;
		if (pressed)
			draw->AddCircleFilled(at, radius, IM_COL32(255, 255, 255, 170));
		draw->AddCircle(at, radius + 2.0f * scale, IM_COL32(0, 0, 0, 200), 0, 3.0f * scale);
		draw->AddCircle(at, radius, IM_COL32(255, 255, 255, 255), 0, 2.5f * scale);
	}

	void CloseMenu()
	{
		s_menuOpen = false;
		s_saveRequested = true;
	}

	void SetVolume(int volume)
	{
		GetConfig().tv_volume = std::clamp(volume, 0, 100);
		std::shared_lock lock(g_audioMutex);
		if (g_tvAudio)
			g_tvAudio->SetVolume(GetConfig().tv_volume);
	}

	void DrawMenu(float scale)
	{
		ImFont* titleFont = ImGui_GetFont(36.0f * scale);
		ImFont* textFont = ImGui_GetFont(26.0f * scale);
		if (!titleFont || !textFont)
			return; // ready next frame
		ImGuiIO& io = ImGui::GetIO();
		auto& config = GetConfig();

		ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(0, 0, 0, 140));
		ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
		ImGui::SetNextWindowSize({760.0f * scale, 0.0f}, ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.94f);
		ImGui::SetNextWindowFocus();
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {28.0f * scale, 24.0f * scale});
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {12.0f * scale, 10.0f * scale});
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {16.0f * scale, 10.0f * scale});
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f * scale);
		ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
		// its height fits the items (the size's 0)
		constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
		if (ImGui::Begin("PS5Cemu##InGameMenu", nullptr, kFlags))
		{
			if (ImGui::IsWindowAppearing())
			{
				ImGui::GetCurrentContext()->NavDisableHighlight = false; // Resume shows as selected
				s_confirmLibrary = false;
				s_gameName = CafeSystem::GetForegroundTitleName();
			}
			ImGui::PushFont(titleFont);
			ImGui::TextUnformatted("PS5Cemu");
			ImGui::PopFont();
			ImGui::PushFont(textFont);
			ImGui::TextDisabled("%s", s_gameName.c_str());
			ImGui::Separator();

			// An item: chosen with Cross, and a setting also changed with Left and Right. The IDs after
			// ### stay the same when the values in the labels change, so the selection stays.
			enum class Change { kNone, kChosen, kLeft, kRight };
			auto item = [](const std::string& label, bool setting) {
				if (ImGui::Button(label.c_str(), {-FLT_MIN, 0.0f}))
					return Change::kChosen;
				if (setting && ImGui::IsItemFocused())
				{
					if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft))
						return Change::kLeft;
					if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight))
						return Change::kRight;
				}
				return Change::kNone;
			};

			if (item("Back to the game###resume", false) != Change::kNone)
				CloseMenu();
			ImGui::SetItemDefaultFocus();

			const bool gamePadMain = LatteGPUState.isDRCPrimary;
			if (item(fmt::format("Main screen: {}###main", gamePadMain ? "GamePad" : "TV"), true) != Change::kNone)
				ps5ingame::SwapScreens();
			if (item(fmt::format("{} in a corner: {}###corner", gamePadMain ? "TV" : "GamePad", s_cornerScreen ? "On" : "Off"), true) != Change::kNone)
				ps5ingame::ToggleCornerScreen();

			static const char* kFilters[] = {"Bilinear", "Bicubic", "Bicubic Hermite", "Nearest neighbour"};
			const int filter = std::clamp((int)config.upscale_filter, 0, 3);
			if (const Change change = item(fmt::format("Upscaling: {}###upscaling", kFilters[filter]), true); change != Change::kNone)
				config.upscale_filter = (filter + (change == Change::kLeft ? 3 : 1)) % 4;
			const bool stretch = config.fullscreen_scaling == kStretch;
			if (item(fmt::format("Picture: {}###scaling", stretch ? "Stretched to the screen" : "Its own shape"), true) != Change::kNone)
				config.fullscreen_scaling = stretch ? kKeepAspectRatio : kStretch;
			const bool overlay = config.overlay.position != ScreenPosition::kDisabled;
			if (item(fmt::format("Performance overlay: {}###overlay", overlay ? "On" : "Off"), true) != Change::kNone)
			{
				config.overlay.position = overlay ? ScreenPosition::kDisabled : ScreenPosition::kTopLeft;
				config.overlay.fps = config.overlay.cpu_usage = config.overlay.ram_usage = true;
			}
			// Left and Right by 10; Cross goes up by 10, and from 100 back to 0
			switch (item(fmt::format("Volume: {}%###volume", config.tv_volume), true))
			{
			case Change::kChosen: SetVolume(config.tv_volume >= 100 ? 0 : config.tv_volume + 10); break;
			case Change::kLeft: SetVolume(config.tv_volume - 10); break;
			case Change::kRight: SetVolume(config.tv_volume + 10); break;
			case Change::kNone: break;
			}

			if (item(s_confirmLibrary ? "Press Cross again: unsaved progress is lost###library" : "Back to the library###library", false) != Change::kNone)
			{
				if (s_confirmLibrary)
					s_libraryRequested = true;
				s_confirmLibrary = true;
			}
			if (!ImGui::IsItemFocused())
				s_confirmLibrary = false;

			ImGui::Separator();
			ImGui::TextDisabled("Cross: choose    Left/Right: change    Circle: back to the game");
			ImGui::TextDisabled("Touchpad + L1: main screen    Touchpad + R1: screen in a corner");
			ImGui::PopFont();
		}
		ImGui::End();
		ImGui::PopStyleVar(5);

		// Circle or Options closes it, but not the press of Options that opened it
		const bool settled = sceKernelGetProcessTime() - s_menuOpenedAt > 300000;
		if (settled && (s_pressed & (ps5pad::kCircle | ps5pad::kOptions)) && !(s_buttons & ps5pad::kTouchPad))
			CloseMenu();
	}
}

namespace ps5ingame
{
	void SetGamePadPointer(bool finger, float x, float y, bool touching)
	{
		std::lock_guard lock(s_pointerMutex);
		s_pointer.finger = finger;
		if (finger || touching)
		{
			s_pointer.x = x;
			s_pointer.y = y;
		}
		s_pointer.touching = touching;
	}

	bool OverlayTakesInput()
	{
		return s_menuOpen || swkbd_hasKeyboardInputHook();
	}

	void ToggleMenu()
	{
		if (s_menuOpen)
		{
			CloseMenu();
			return;
		}
		s_menuOpenedAt = sceKernelGetProcessTime();
		s_menuOpen = true;
	}

	void SwapScreens()
	{
		LatteGPUState.isDRCPrimary = !LatteGPUState.isDRCPrimary;
	}

	void ToggleCornerScreen()
	{
		s_cornerScreen = !s_cornerScreen;
	}

	bool TakeSaveRequest()
	{
		return s_saveRequested.exchange(false);
	}

	bool TakeLibraryRequest()
	{
		return s_libraryRequested.exchange(false);
	}
}

// Called by Cemu's patched code (patches/cemu), on the render thread.

// ImGui's input, each frame (imgui_extension.cpp): the D-pad and the left stick move between items
// and Cross chooses, as with ImGui's gamepad navigation. Circle, Triangle and Options are read
// here instead: ImGui gives them uses that do not suit the overlays (Circle drops the selection).
// Over the menu or the keyboard, the touchpad is also the mouse.
void PS5Cemu_ImGuiInput()
{
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	ps5pad::Data data{};
	const bool connected = ps5pad::Read(0, data);
	const uint32_t buttons = connected ? data.buttons : 0;
	s_pressed = buttons & ~s_buttons;
	s_buttons = buttons;

	const bool touchpadHeld = buttons & ps5pad::kTouchPad; // a shortcut's, not the overlays'
	auto key = [&](ImGuiKey imguiKey, uint32_t mask) { io.AddKeyEvent(imguiKey, !touchpadHeld && (buttons & mask)); };
	key(ImGuiKey_GamepadDpadUp, ps5pad::kUp);
	key(ImGuiKey_GamepadDpadDown, ps5pad::kDown);
	key(ImGuiKey_GamepadDpadLeft, ps5pad::kLeft);
	key(ImGuiKey_GamepadDpadRight, ps5pad::kRight);
	key(ImGuiKey_GamepadFaceDown, ps5pad::kCross);
	auto stick = [&](ImGuiKey negative, ImGuiKey positive, uint8_t raw) {
		const float value = connected ? std::clamp((raw - 128) / 127.0f, -1.0f, 1.0f) : 0.0f;
		io.AddKeyAnalogEvent(negative, value < -0.5f, std::max(-value, 0.0f));
		io.AddKeyAnalogEvent(positive, value > 0.5f, std::max(value, 0.0f));
	};
	stick(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, data.leftX);
	stick(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown, data.leftY);

	// Cemu's keyboard reads these itself: Circle deletes, Options is done (swkbd.cpp)
	const bool keyboard = !s_menuOpen && swkbd_hasKeyboardInputHook();
	io.NavInputs[ImGuiNavInput_Cancel] = keyboard && !touchpadHeld && (buttons & ps5pad::kCircle) ? 1.0f : 0.0f;
	io.NavInputs[ImGuiNavInput_Input] = keyboard && !touchpadHeld && (buttons & ps5pad::kOptions) ? 1.0f : 0.0f;
	if (keyboard && (s_pressed & ps5pad::kTriangle))
		s_keyboardShift = true;

	io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
	io.MouseDown[0] = false;
	io.MouseDrawCursor = false; // RenderOverlay draws one the size of the rest
	if (ps5ingame::OverlayTakesInput() && connected && data.touchCount > 0)
	{
		float width, height;
		ps5pad::TouchResolution(0, width, height);
		io.MousePos = {std::clamp(data.touch[0].x / width, 0.0f, 1.0f) * io.DisplaySize.x,
			std::clamp(data.touch[0].y / height, 0.0f, 1.0f) * io.DisplaySize.y};
		io.MouseDown[0] = touchpadHeld;
	}
}

bool PS5Cemu_MenuOpen()
{
	return s_menuOpen;
}

// Triangle on Cemu's keyboard: shift (swkbd.cpp).
bool PS5Cemu_KeyboardShiftPressed()
{
	return s_keyboardShift.exchange(false);
}

// Whether the screen not shown whole goes in a corner (LatteRenderTarget.cpp).
bool PS5Cemu_SecondScreenInCorner(bool /*gamePadIsPrimary*/)
{
	return s_cornerScreen;
}

// Where the GamePad's picture is this frame, whole or in the corner, or that it is not shown.
void PS5Cemu_SetGamePadArea(bool visible, sint32 x, sint32 y, sint32 width, sint32 height)
{
	s_gamePadArea = {visible, (float)x, (float)y, (float)width, (float)height};
}

// Over the game's picture, in its ImGui frame: the menu, and the touchpad's cursor, on the menu or
// the keyboard while they are up, otherwise on the GamePad's picture where it is shown.
void PS5Cemu_RenderOverlay()
{
	const float scale = UiScale();
	if (s_menuOpen)
		DrawMenu(scale);
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	if (ps5ingame::OverlayTakesInput())
	{
		if (ImGui::IsMousePosValid())
			DrawCursor(draw, ImGui::GetIO().MousePos, ImGui::GetIO().MouseDown[0], scale);
		return;
	}
	Pointer pointer;
	{
		std::lock_guard lock(s_pointerMutex);
		pointer = s_pointer;
	}
	if ((pointer.finger || pointer.touching) && s_gamePadArea.visible)
		DrawCursor(draw, {s_gamePadArea.x + pointer.x * s_gamePadArea.width, s_gamePadArea.y + pointer.y * s_gamePadArea.height},
			pointer.touching, scale);
}
