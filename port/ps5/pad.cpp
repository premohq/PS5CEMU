// SPDX-License-Identifier: GPL-3.0-or-later
#include "pad.h"
#include "kernel.h"
#include "log.h"

#include <algorithm>
#include <atomic>
#include <mutex>

extern "C"
{
	struct LoginUserIdList
	{
		int32_t userId[4]; // unused entries are -1
	};
	struct PadControllerInformation
	{
		float touchPixelDensity;
		uint16_t touchResolutionX, touchResolutionY;
		uint8_t deadZoneLeft, deadZoneRight;
		uint8_t connectionType, connectedCount;
		int32_t connected;
		int32_t deviceClass;
		uint8_t reserved[8];
	};
	struct PadVibration
	{
		uint8_t largeMotor, smallMotor;
	};
	struct PadColor
	{
		uint8_t r, g, b, reserved;
	};

	int32_t sceUserServiceInitialize(void* parameters);
	int32_t sceUserServiceGetForegroundUser(int32_t* userId);
	int32_t sceUserServiceGetInitialUser(int32_t* userId);
	int32_t sceUserServiceGetLoginUserIdList(LoginUserIdList* list);
	int32_t scePadInit();
	int32_t scePadOpen(int32_t userId, int32_t type, int32_t index, const void* parameters);
	int32_t scePadGetHandle(int32_t userId, int32_t type, int32_t index);
	int32_t scePadClose(int32_t handle);
	int32_t scePadReadState(int32_t handle, ps5pad::Data* data);
	int32_t scePadGetControllerInformation(int32_t handle, PadControllerInformation* information);
	int32_t scePadSetVibration(int32_t handle, const PadVibration* vibration);
	int32_t scePadSetLightBar(int32_t handle, const PadColor* color);
	int32_t scePadSetMotionSensorState(int32_t handle, bool enabled);
}
static_assert(sizeof(PadControllerInformation) == 28);

namespace ps5pad
{
	namespace
	{
		constexpr int32_t kPortTypeStandard = 0;

		struct Slot
		{
			int32_t user = -1;
			int32_t handle = -1;
			float touchWidth = 1920, touchHeight = 1080;
			uint32_t lastButtons = 0;
			// the touchpad's click: when it began, whether it made a shortcut or touched the
			// GamePad's screen, and until when a short click's tap lasts
			uint64_t clickedAt = 0, tapUntil = 0;
			bool chordUsed = false, touched = false;
		};

		std::mutex s_mutex;
		std::array<Slot, kMaxPlayers> s_slots;
		bool s_initialized = false;
		std::atomic<bool> s_vibrationEnabled{true};
		std::atomic<Shortcut> s_pendingShortcut{Shortcut::None};

		void OpenSlot(int player, int32_t user)
		{
			int32_t handle = scePadOpen(user, kPortTypeStandard, 0, nullptr);
			if (handle < 0) // already open in this process
				handle = scePadGetHandle(user, kPortTypeStandard, 0);
			if (handle < 0)
			{
				ps5log::Line("[pad] no controller for user {:#x} (player {}): {:#x}", user, player + 1, (uint32_t)handle);
				return;
			}
			Slot& slot = s_slots[player];
			slot = {};
			slot.user = user;
			slot.handle = handle;
			PadControllerInformation info{};
			if (scePadGetControllerInformation(handle, &info) == 0 && info.touchResolutionX && info.touchResolutionY)
			{
				slot.touchWidth = info.touchResolutionX;
				slot.touchHeight = info.touchResolutionY;
			}
			scePadSetMotionSensorState(handle, true);
			ps5log::Line("[pad] player {} is user {:#x} (handle {})", player + 1, user, handle);
		}

		void CloseSlot(int player)
		{
			Slot& slot = s_slots[player];
			if (slot.handle >= 0)
				scePadClose(slot.handle);
			slot = {};
		}
	}

	bool Init()
	{
		std::lock_guard lock(s_mutex);
		if (s_initialized)
			return s_slots[0].handle >= 0;
		sceUserServiceInitialize(nullptr);
		if (scePadInit() < 0)
		{
			ps5log::Line("[pad] scePadInit failed");
			return false;
		}
		s_initialized = true;
		int32_t user = -1;
		if (sceUserServiceGetForegroundUser(&user) < 0 || user < 0)
			sceUserServiceGetInitialUser(&user);
		if (user >= 0)
			OpenSlot(0, user);
		return s_slots[0].handle >= 0;
	}

	void Shutdown()
	{
		std::lock_guard lock(s_mutex);
		for (int player = 0; player < kMaxPlayers; player++)
			CloseSlot(player);
	}

	void Rescan()
	{
		std::lock_guard lock(s_mutex);
		if (!s_initialized)
			return;
		LoginUserIdList list;
		std::fill(std::begin(list.userId), std::end(list.userId), -1);
		if (sceUserServiceGetLoginUserIdList(&list) < 0)
			return;
		auto signedIn = [&](int32_t user) { return std::find(std::begin(list.userId), std::end(list.userId), user) != std::end(list.userId); };
		// player 1 stays with the user who started PS5Cemu; the others leave when they sign out
		for (int player = 1; player < kMaxPlayers; player++)
			if (s_slots[player].handle >= 0 && !signedIn(s_slots[player].user))
				CloseSlot(player);
		for (int32_t user : list.userId)
		{
			if (user < 0 || std::any_of(s_slots.begin(), s_slots.end(), [user](const Slot& s) { return s.handle >= 0 && s.user == user; }))
				continue;
			for (int player = 0; player < kMaxPlayers; player++)
			{
				if (s_slots[player].handle < 0)
				{
					OpenSlot(player, user);
					break;
				}
			}
		}
	}

	bool Read(int player, Data& out)
	{
		int32_t handle;
		{
			std::lock_guard lock(s_mutex);
			if (player < 0 || player >= kMaxPlayers || s_slots[player].handle < 0)
				return false;
			handle = s_slots[player].handle;
		}
		if (scePadReadState(handle, &out) < 0 || !out.connected)
			return false;
		if (out.buttons & kIntercepted)
		{
			// the system menu has the controller: report it at rest
			out.buttons = 0;
			out.leftX = out.leftY = out.rightX = out.rightY = 128;
			out.l2 = out.r2 = 0;
			out.touchCount = 0;
		}
		return true;
	}

	bool IsConnected(int player)
	{
		Data data;
		return Read(player, data);
	}

	void TouchResolution(int player, float& width, float& height)
	{
		std::lock_guard lock(s_mutex);
		const Slot& slot = s_slots[std::clamp(player, 0, kMaxPlayers - 1)];
		width = slot.touchWidth;
		height = slot.touchHeight;
	}

	void SetVibration(int player, uint8_t largeMotor, uint8_t smallMotor)
	{
		std::lock_guard lock(s_mutex);
		if (player < 0 || player >= kMaxPlayers || s_slots[player].handle < 0)
			return;
		const bool enabled = s_vibrationEnabled;
		const PadVibration vibration{enabled ? largeMotor : (uint8_t)0, enabled ? smallMotor : (uint8_t)0};
		scePadSetVibration(s_slots[player].handle, &vibration);
	}

	void SetVibrationEnabled(bool enabled)
	{
		s_vibrationEnabled = enabled;
	}

	void SetLightBar(int player, uint8_t r, uint8_t g, uint8_t b)
	{
		std::lock_guard lock(s_mutex);
		if (player < 0 || player >= kMaxPlayers || s_slots[player].handle < 0)
			return;
		const PadColor color{r, g, b, 0};
		scePadSetLightBar(s_slots[player].handle, &color);
	}

	Filtered FilterShortcuts(int player, uint32_t buttons)
	{
		constexpr uint64_t kTouchDelayUs = 150000; // a shortcut's button within this is no touch
		constexpr uint64_t kTapUs = 80000;		   // how long a shorter click touches, once released
		const uint64_t now = sceKernelGetProcessTime();
		std::lock_guard lock(s_mutex);
		Slot& slot = s_slots[std::clamp(player, 0, kMaxPlayers - 1)];
		const uint32_t previous = slot.lastButtons;
		slot.lastButtons = buttons;
		auto pressed = [&](uint32_t mask) { return (buttons & mask) && !(previous & mask); };
		Filtered result{buttons & ~kTouchPad, false};
		if (buttons & kTouchPad)
		{
			if (!(previous & kTouchPad))
			{
				slot.clickedAt = now;
				slot.chordUsed = slot.touched = false;
			}
			const Shortcut shortcut = pressed(kOptions) ? Shortcut::Menu
				: pressed(kL1)							? Shortcut::SwapScreens
				: pressed(kR1)							? Shortcut::CornerScreen
														: Shortcut::None;
			if (shortcut != Shortcut::None)
			{
				s_pendingShortcut = shortcut;
				slot.chordUsed = true;
			}
			result.buttons &= ~(kOptions | kL1 | kR1);
			result.touch = !slot.chordUsed && now - slot.clickedAt >= kTouchDelayUs;
			slot.touched |= result.touch;
		}
		else if ((previous & kTouchPad) && !slot.chordUsed && !slot.touched)
			slot.tapUntil = now + kTapUs;
		if (now < slot.tapUntil)
			result.touch = true;
		return result;
	}

	Shortcut TakeShortcut()
	{
		return s_pendingShortcut.exchange(Shortcut::None);
	}
}
