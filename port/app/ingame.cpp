// SPDX-License-Identifier: GPL-3.0-or-later
#include "ingame.h"
#include "emulator.h"
#include "menu_canvas.h"
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
	// The menu's pages: its main one, and the controllers'
	enum class Page
	{
		Main,
		Controls,
	};
	Page s_page = Page::Main;
	bool s_pageChanged = false;
	int s_controlsPlayer = 0;

	float UiScale()
	{
		// VideoOut is 3840x2160: the overlays at the size they have on a 1080p screen
		return std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f);
	}

	using ps5menu::DrawCursor;

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

	// The launcher's look, in Cemu's dark blue (menu_canvas.h)
	using ps5menu::Canvas;
	using ps5menu::Colour;
	constexpr ImU32 kTitle = ps5menu::kBlue.title, kText = ps5menu::kBlue.text, kCopy = ps5menu::kBlue.copy,
		kAccent = ps5menu::kBlue.accent, kKicker = ps5menu::kBlue.kicker, kLine = ps5menu::kBlue.line;

	const char* TypeName(ps5emu::EmulatedType type)
	{
		switch (type)
		{
		case ps5emu::EmulatedType::GamePad: return "Wii U GamePad";
		case ps5emu::EmulatedType::Pro: return "Pro Controller";
		case ps5emu::EmulatedType::Classic: return "Classic Controller";
		case ps5emu::EmulatedType::Wiimote: return "Wii Remote";
		case ps5emu::EmulatedType::Nunchuk: return "Wii Remote + Nunchuk";
		case ps5emu::EmulatedType::None: break;
		}
		return "None";
	}

	// The emulated controllers a player can have: Cemu has two GamePads at most.
	std::vector<ps5emu::EmulatedType> TypesFor(int player)
	{
		int otherGamePads = 0;
		for (int other = 0; other < ps5pad::kMaxPlayers; other++)
			if (other != player && ps5emu::GetPlayerControls(other).type == ps5emu::EmulatedType::GamePad)
				otherGamePads++;
		std::vector<ps5emu::EmulatedType> types;
		if (otherGamePads < 2)
			types.push_back(ps5emu::EmulatedType::GamePad);
		for (auto type : {ps5emu::EmulatedType::Pro, ps5emu::EmulatedType::Classic, ps5emu::EmulatedType::Wiimote, ps5emu::EmulatedType::Nunchuk})
			types.push_back(type);
		return types;
	}

	void ShowPage(Page page)
	{
		s_page = page;
		s_pageChanged = true;
		s_confirmLibrary = false;
	}

	void DrawMenu(float scale)
	{
		ImFont* titleFont = ImGui_GetFont(48.0f * scale);
		ImFont* headFont = ImGui_GetFont(32.0f * scale);
		ImFont* rowFont = ImGui_GetFont(24.0f * scale);
		ImFont* smallFont = ImGui_GetFont(20.0f * scale);
		if (!titleFont || !headFont || !rowFont || !smallFont)
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
				s_page = Page::Main; // the menu opens on its first page
				s_gameName = CafeSystem::GetForegroundTitleName();
			}
			const Canvas canvas{ImGui::GetWindowDrawList(), scale, origin};
			canvas.draw->AddRectFilled({0, 0}, io.DisplaySize, Colour(0x02060e, 0xb8)); // the game, dimmed

			canvas.Text(titleFont, 48, 108, 62, kTitle, "PS5 CEMU");
			canvas.Text(smallFont, 20, 110, 132, kCopy, s_gameName);
			canvas.Panel(108, 188, 820, 720);
			canvas.Text(smallFont, 20, 138, 208, kKicker, s_page == Page::Controls ? "CONTROLS" : "IN THE GAME");
			canvas.Panel(980, 188, 820, 720);

			struct Item
			{
				const char* id;
				std::string label, value;
				bool setting; // Left and Right change it
				const char* help;
			};
			std::vector<Item> items;
			const bool controlsPage = s_page == Page::Controls;
			static const char* kFilters[] = {"Bilinear", "Bicubic", "Bicubic Hermite", "Nearest neighbour"};
			const bool gamePadMain = LatteGPUState.isDRCPrimary;
			const bool stretch = config.fullscreen_scaling == kStretch;
			const bool overlay = config.overlay.position != ScreenPosition::kDisabled;
			const int filter = std::clamp((int)config.upscale_filter, 0, 3);
			const int player = s_controlsPlayer;
			const auto controls = ps5emu::GetPlayerControls(player);
			const auto mappings = ps5emu::ListMappings(player);
			// A, B, X and Y come first on the controllers that have them: on Circle where the Wii U has
			// A, or on Cross
			const bool faceButtons = mappings.size() > 3 && mappings[0].button == "A" && mappings[3].button == "Y";
			const bool aOnCircle = faceButtons && mappings[0].input == "Circle";
			if (!controlsPage)
				items = {
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
					{"controls", "Controls", "", false,
						"Each player's controller: the emulated one, motion controls, vibration, the sticks' deadzones and where A and B are. "
						"They are kept for the next games too."},
					{"library", "Back to the library", s_confirmLibrary ? "Press Cross again" : "", false,
						"Leaves the game for the library. What you have not saved in the game is lost."},
				};
			else
				items = {
					{"player", "Player", fmt::format("{}{}", player + 1, controls.connected ? "" : "  (no DualSense)"), true,
						"Whose controller the settings below are: Left and Right choose the player."},
					{"type", "Emulated controller", TypeName(controls.type), true,
						"What the game sees in this player's hands. Most games want the Wii U GamePad for player 1; a game may only "
						"notice a new controller when it next looks for one. Cemu has two GamePads at most."},
					{"motion", "Motion controls", !controls.hasMotion ? "None on this one" : controls.motion ? "On" : "Off", true,
						"The DualSense's gyroscope and accelerometer as the controller's own, for the games that aim or steer by tilting."},
					{"rumble", "Vibration", controls.rumble ? fmt::format("{}%", controls.rumble) : "Off", true,
						"How strongly the DualSense rumbles when the game makes the controller vibrate. Left and Right change it by 10%."},
					{"left", "Left stick deadzone", fmt::format("{}%", controls.leftDeadzone), true,
						"How far the left stick moves before the game sees it. Raise it if a character drifts when you let go."},
					{"right", "Right stick deadzone", fmt::format("{}%", controls.rightDeadzone), true,
						"How far the right stick moves before the game sees it. Raise it if the camera drifts when you let go."},
					{"layout", "A and B", !faceButtons ? "-" : aOnCircle ? "A on Circle" : "A on Cross", true,
						"A on Circle and B on Cross, where the Wii U has them, or A on Cross and B on Circle, with X and Y swapped to "
						"match. Every button can be set in the launcher's Settings > Controls."},
					{"back", "Back", "", false, "To the menu's first page."},
				};

			int focused = 0;
			const float spacing = 72, height = 64;
			for (int i = 0; i < (int)items.size(); i++)
			{
				const Item& item = items[i];
				const float y = 250 + i * spacing;
				ImGui::SetCursorScreenPos(canvas.At(138, y));
				const bool chosen = ImGui::InvisibleButton(item.id, {760 * scale, height * scale});
				if (i == 0 && (appearing || s_pageChanged))
				{
					ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
					ImGui::GetCurrentContext()->NavDisableHighlight = false;
					s_pageChanged = false; // a page chosen below, this frame, gets its focus next frame
				}
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
				canvas.Row(138, y, 760, height, isFocused);
				canvas.Text(rowFont, 24, 164, y + 17, kText, item.label);
				canvas.TextRight(rowFont, 22, 872, y + 19, kAccent, item.value);
				if (change == 0)
					continue;
				const std::string id = item.id;
				if (id == "resume")
					CloseMenu();
				else if (id == "main")
					ps5ingame::SwapScreens();
				else if (id == "corner")
					ps5ingame::ToggleCornerScreen();
				else if (id == "upscaling")
					config.upscale_filter = (filter + (change < 0 ? 3 : 1)) % 4;
				else if (id == "scaling")
					config.fullscreen_scaling = stretch ? kKeepAspectRatio : kStretch;
				else if (id == "overlay")
				{
					config.overlay.position = overlay ? ScreenPosition::kDisabled : ScreenPosition::kTopLeft;
					config.overlay.fps = config.overlay.cpu_usage = config.overlay.ram_usage = true;
				}
				else if (id == "volume")
					// Cross goes up by 10, and from 100 back to 0
					SetVolume(chosen && config.tv_volume >= 100 ? 0 : config.tv_volume + change * 10);
				else if (id == "controls")
					ShowPage(Page::Controls);
				else if (id == "library")
				{
					if (s_confirmLibrary)
						s_libraryRequested = true;
					s_confirmLibrary = true;
				}
				else if (id == "player")
					s_controlsPlayer = (player + change + ps5pad::kMaxPlayers) % ps5pad::kMaxPlayers;
				else if (id == "type")
				{
					const auto types = TypesFor(player);
					int at = 0;
					for (int t = 0; t < (int)types.size(); t++)
						if (types[t] == controls.type)
							at = t;
					ps5emu::SetEmulatedType(player, types[(at + change + (int)types.size()) % types.size()]);
				}
				else if (id == "motion" && controls.hasMotion)
					ps5emu::SetMotion(player, !controls.motion);
				else if (id == "rumble")
				{
					int rumble = controls.rumble + change * 10;
					if (chosen && rumble > 100)
						rumble = 0;
					ps5emu::SetRumble(player, std::clamp(rumble, 0, 100));
					if (rumble > 0)
						ps5pad::SetVibrationEnabled(true);
				}
				else if (id == "left" || id == "right")
				{
					const bool leftStick = id == "left";
					int value = (leftStick ? controls.leftDeadzone : controls.rightDeadzone) + change * 5;
					if (chosen && value > 50)
						value = 0;
					value = std::clamp(value, 0, 50);
					ps5emu::SetDeadzones(player, leftStick ? value : controls.leftDeadzone, leftStick ? controls.rightDeadzone : value);
				}
				else if (id == "layout" && faceButtons)
				{
					using ps5emu::PadInput;
					const PadInput a = aOnCircle ? PadInput::Cross : PadInput::Circle, b = aOnCircle ? PadInput::Circle : PadInput::Cross;
					const PadInput x = aOnCircle ? PadInput::Square : PadInput::Triangle, y = aOnCircle ? PadInput::Triangle : PadInput::Square;
					ps5emu::SetMapping(player, 0, a);
					ps5emu::SetMapping(player, 1, b);
					ps5emu::SetMapping(player, 2, x);
					ps5emu::SetMapping(player, 3, y);
				}
				else if (id == "back")
					ShowPage(Page::Main);
			}
			if (items[focused].id != std::string("library"))
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
			x = canvas.Hint(smallFont, x, 973, "circle", s_page == Page::Controls ? "Back" : "Back to the game");
			canvas.Hint(smallFont, x, 973, "touchpad", "Touchpad click + L1 / R1: the screens, in the game");
		}
		ImGui::End();
		ImGui::PopStyleColor();

		// Circle or Options closes it, but not the press of Options that opened it; on the controls'
		// page, Circle goes back to the first
		const bool settled = sceKernelGetProcessTime() - s_menuOpenedAt > 300000;
		if (settled && (s_pressed & (ps5pad::kCircle | ps5pad::kOptions)) && !(s_buttons & ps5pad::kTouchPad))
		{
			if (s_page == Page::Controls && (s_pressed & ps5pad::kCircle))
				ShowPage(Page::Main);
			else
				CloseMenu();
		}
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
