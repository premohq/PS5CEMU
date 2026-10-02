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

#include <cstring>

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

	// The launcher's look (frontend/ui, in the dark blue tools/recolour-ui.py gives it): its panels,
	// rows, colours and controller hints, drawn here with ImGui on its 1920x1080 layout scaled to the
	// screen, so the menu over a game is laid out as the launcher's screens are.
	constexpr ImU32 Colour(uint32_t rgb, uint8_t alpha = 255)
	{
		return IM_COL32(rgb >> 16, (rgb >> 8) & 255, rgb & 255, alpha);
	}
	constexpr ImU32 kTitle = Colour(0xf2f8ff), kText = Colour(0xedf1f6), kCopy = Colour(0xcad1d9), kAccent = Colour(0xb8cfed),
		kKicker = Colour(0x82ace2), kLine = Colour(0x454d59);

	struct Canvas
	{
		ImDrawList* draw;
		float scale;
		ImVec2 origin;

		ImVec2 At(float x, float y) const { return {origin.x + x * scale, origin.y + y * scale}; }

		void Panel(float x, float y, float width, float height) const
		{
			draw->AddRectFilled(At(x + 2, y + 2), At(x + width - 3, y + height - 3), Colour(0x070d18, 0xf5), 26 * scale);
			draw->AddRect(At(x + 2, y + 2), At(x + width - 3, y + height - 3), Colour(0x34506f, 0xa8), 26 * scale, 0, scale);
		}

		// A row as the launcher's library rows: dark, or with the focus's deep blue gradient
		void Row(float x, float y, float width, float height, bool focused) const
		{
			const ImVec2 a = At(x + 2, y + 2), b = At(x + width - 3, y + height - 3);
			if (!focused)
			{
				draw->AddRectFilled(a, b, Colour(0x0d1828, 0xe8), 13 * scale);
				draw->AddRect(a, b, Colour(0x2a4462, 0x77), 13 * scale, 0, scale);
				return;
			}
			const int start = draw->VtxBuffer.Size;
			draw->AddRectFilled(a, b, Colour(0x2a63a6, 0x78), 13 * scale);
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size, a, {b.x, a.y}, Colour(0x2a63a6), Colour(0x0d2140));
			draw->AddRect(a, b, Colour(0x5c9ce6, 0xa0), 13 * scale, 0, 1.5f * scale);
		}

		void Text(ImFont* font, float size, float x, float y, ImU32 colour, const std::string& text, float wrap = 0.0f) const
		{
			draw->AddText(font, size * scale, At(x, y), colour, text.c_str(), nullptr, wrap * scale);
		}

		void TextRight(ImFont* font, float size, float right, float y, ImU32 colour, const std::string& text) const
		{
			const float width = font->CalcTextSizeA(size * scale, FLT_MAX, 0.0f, text.c_str()).x / scale;
			Text(font, size, right - width, y, colour, text);
		}

		// A controller hint: its button's mark, as the launcher's mono icons have it, and what it
		// does. Returns where the next one goes.
		float Hint(ImFont* font, float x, float y, const char* button, const std::string& label) const
		{
			const ImU32 colour = kCopy;
			const float thick = 2.2f * scale;
			const ImVec2 centre = At(x + 13, y + 14);
			const float r = 10 * scale;
			if (std::strcmp(button, "cross") == 0)
			{
				draw->AddLine({centre.x - r, centre.y - r}, {centre.x + r, centre.y + r}, colour, thick);
				draw->AddLine({centre.x - r, centre.y + r}, {centre.x + r, centre.y - r}, colour, thick);
			}
			else if (std::strcmp(button, "circle") == 0)
				draw->AddCircle(centre, r, colour, 0, thick);
			else if (std::strcmp(button, "leftright") == 0)
			{
				draw->AddLine({centre.x - r - 2 * scale, centre.y}, {centre.x + r + 2 * scale, centre.y}, colour, thick);
				for (const float side : {-1.0f, 1.0f})
				{
					const ImVec2 tip{centre.x + side * (r + 2 * scale), centre.y};
					draw->AddLine(tip, {tip.x - side * 6 * scale, centre.y - 6 * scale}, colour, thick);
					draw->AddLine(tip, {tip.x - side * 6 * scale, centre.y + 6 * scale}, colour, thick);
				}
			}
			else if (std::strcmp(button, "touchpad") == 0)
				draw->AddRect({centre.x - r - 3 * scale, centre.y - r + 3 * scale}, {centre.x + r + 3 * scale, centre.y + r - 3 * scale},
					colour, 3 * scale, 0, thick);
			Text(font, 20, x + 38, y + 2, colour, label);
			return x + 38 + font->CalcTextSizeA(20 * scale, FLT_MAX, 0.0f, label.c_str()).x / scale + 44;
		}
	};

	void DrawMenu(float scale)
	{
		ImFont* titleFont = ImGui_GetFont(48.0f * scale);
		ImFont* headFont = ImGui_GetFont(32.0f * scale);
		ImFont* rowFont = ImGui_GetFont(24.0f * scale);
		ImFont* smallFont = ImGui_GetFont(20.0f * scale);
		if (!titleFont || !headFont || !rowFont || !smallFont)
	void DrawMenu(float scale)
	{
		ImFont* titleFont = ImGui_GetFont(36.0f * scale);
		ImFont* textFont = ImGui_GetFont(26.0f * scale);
		if (!titleFont || !textFont)
			return; // ready next frame
		ImGuiIO& io = ImGui::GetIO();
		auto& config = GetConfig();

		// the launcher's 1920x1080 layout, scaled to the screen and centred on it
		const ImVec2 origin{(io.DisplaySize.x - 1920.0f * scale) * 0.5f, (io.DisplaySize.y - 1080.0f * scale) * 0.5f};
		ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
		ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
		ImGui::SetNextWindowFocus();
		ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(0, 0, 0, 0)); // the rows show the focus
		constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
		if (ImGui::Begin("PS5Cemu##InGameMenu", nullptr, kFlags))
		{
			const bool appearing = ImGui::IsWindowAppearing();
			if (appearing)
			{
				ImGui::GetCurrentContext()->NavDisableHighlight = false; // Back to the game shows as selected
				s_confirmLibrary = false;
				s_gameName = CafeSystem::GetForegroundTitleName();
			}
			const Canvas canvas{ImGui::GetWindowDrawList(), scale, origin};
			canvas.draw->AddRectFilled({0, 0}, io.DisplaySize, Colour(0x02060e, 0xb8)); // the game, dimmed

			canvas.Text(titleFont, 48, 108, 62, kTitle, "PS5Cemu");
			canvas.Text(smallFont, 20, 110, 132, kCopy, s_gameName);
			canvas.Panel(108, 188, 820, 720);
			canvas.Text(smallFont, 20, 138, 208, kKicker, "IN THE GAME");
			canvas.Panel(980, 188, 820, 720);

			static const char* kFilters[] = {"Bilinear", "Bicubic", "Bicubic Hermite", "Nearest neighbour"};
			const bool gamePadMain = LatteGPUState.isDRCPrimary;
			const bool stretch = config.fullscreen_scaling == kStretch;
			const bool overlay = config.overlay.position != ScreenPosition::kDisabled;
			const int filter = std::clamp((int)config.upscale_filter, 0, 3);
			struct Item
			{
				const char* id;
				std::string label, value;
				bool setting; // Left and Right change it
				const char* help;
			};
			const Item items[] = {
				{"resume", "Back to the game", "", false, "Closes this menu: the game carries on where it is."},
				{"main", "Main screen", gamePadMain ? "GamePad" : "TV", true,
					"Which picture fills the TV: the TV's or the GamePad's.\nIn the game, touchpad click + L1 swaps them."},
				{"corner", fmt::format("{} in a corner", gamePadMain ? "TV" : "GamePad"), s_cornerScreen ? "On" : "Off", true,
					"The other screen, small in the bottom right corner, so both can be seen.\nIn the game, touchpad click + R1."},
				{"upscaling", "Upscaling to 4K", kFilters[filter], true,
					"How the game's picture is scaled to the screen. Bicubic is sharp, Bicubic Hermite a little softer, Bilinear "
					"softer still; Nearest neighbour keeps pixels square."},
				{"scaling", "Picture", stretch ? "Stretched" : "Its own shape", true,
					"Its own shape keeps the picture's proportions, with bars where they differ from the screen's; Stretched "
					"fills the screen."},
				{"overlay", "Performance overlay", overlay ? "On" : "Off", true,
					"Frames per second, CPU and memory use in the top left corner, as Cemu shows them."},
				{"volume", "Volume", fmt::format("{}%", config.tv_volume), true, "The game's sound. Left and Right change it by 10%."},
				{"library", "Back to the library", s_confirmLibrary ? "Press Cross again" : "", false,
					"Leaves the game for the library. What you have not saved in the game is lost."},
			};

			int focused = 0;
			for (int i = 0; i < (int)std::size(items); i++)
			{
				const Item& item = items[i];
				const float y = 250 + i * 80;
				ImGui::SetCursorScreenPos(canvas.At(138, y));
				const bool chosen = ImGui::InvisibleButton(item.id, {760 * scale, 72 * scale});
				if (i == 0 && appearing)
					ImGui::SetItemDefaultFocus();
				const bool isFocused = ImGui::IsItemFocused();
				if (isFocused)
					focused = i;
				int change = chosen ? 1 : 0; // Cross moves a setting on, Left and Right either way
				if (isFocused && item.setting)
				{
					if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft))
						change = -1;
					else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight))
						change = 1;
				}
				canvas.Row(138, y, 760, 72, isFocused);
				canvas.Text(rowFont, 24, 164, y + 21, kText, item.label);
				canvas.TextRight(rowFont, 22, 872, y + 23, kAccent, item.value);
				if (change == 0)
					continue;
				switch (i)
				{
				case 0: CloseMenu(); break;
				case 1: ps5ingame::SwapScreens(); break;
				case 2: ps5ingame::ToggleCornerScreen(); break;
				case 3: config.upscale_filter = (filter + (change < 0 ? 3 : 1)) % 4; break;
				case 4: config.fullscreen_scaling = stretch ? kKeepAspectRatio : kStretch; break;
				case 5:
					config.overlay.position = overlay ? ScreenPosition::kDisabled : ScreenPosition::kTopLeft;
					config.overlay.fps = config.overlay.cpu_usage = config.overlay.ram_usage = true;
					break;
				case 6:
					// Cross goes up by 10, and from 100 back to 0
					SetVolume(chosen && config.tv_volume >= 100 ? 0 : config.tv_volume + change * 10);
					break;
				case 7:
					if (s_confirmLibrary)
						s_libraryRequested = true;
					s_confirmLibrary = true;
					break;
				}
			}
			if (focused != 7)
				s_confirmLibrary = false;

			// the right-hand panel: the game, and what the focused item does
			canvas.Text(smallFont, 20, 1016, 208, kKicker, "THIS GAME");
			canvas.Text(headFont, 32, 1016, 248, kTitle, s_gameName, 748);
			canvas.Text(smallFont, 20, 1016, 378, kKicker, "TITLE ID");
			canvas.TextRight(smallFont, 20, 1764, 378, kAccent, fmt::format("{:016X}", CafeSystem::GetForegroundTitleId()));
			canvas.Text(smallFont, 20, 1016, 422, kKicker, "VERSION");
			canvas.TextRight(smallFont, 20, 1764, 422, kAccent, fmt::format("v{}", CafeSystem::GetForegroundTitleVersion()));
			canvas.draw->AddLine(canvas.At(1016, 478), canvas.At(1764, 478), kLine, scale);
			canvas.Text(headFont, 32, 1016, 500, kTitle, items[focused].label);
			canvas.Text(rowFont, 22, 1016, 552, kCopy, items[focused].help, 748);

			// the controller hints, along the bottom
			canvas.draw->AddLine(canvas.At(108, 955), canvas.At(1812, 955), kLine, scale);
			float x = 108;
			x = canvas.Hint(smallFont, x, 973, "cross", "Choose");
			x = canvas.Hint(smallFont, x, 973, "leftright", "Change");
			x = canvas.Hint(smallFont, x, 973, "circle", "Back to the game");
			canvas.Hint(smallFont, x, 973, "touchpad", "Touchpad click + L1 / R1: the screens, in the game");
		}
		ImGui::End();
		ImGui::PopStyleColor();
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
