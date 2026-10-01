// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: a DualSense as a Cemu controller.
//
// Buttons are numbered as below; the sticks are the axis (left) and rotation (right), and the
// analog L2/R2 the trigger. The touchpad is the GamePad's touch screen (a position while a
// finger rests on it), and the motion sensors feed Cemu's motion handler like an SDL gamepad's.

#pragma once

#include "input/api/Controller.h"
#include "input/motion/MotionHandler.h"
#include "PS5PadControllerProvider.h"

class PS5PadController : public Controller<PS5PadControllerProvider>
{
public:
	enum ButtonId : uint64
	{
		kCross = kButton0,
		kCircle = kButton1,
		kSquare = kButton2,
		kTriangle = kButton3,
		kCreate = kButton4,
		kOptions = kButton6,
		kL3 = kButton7,
		kR3 = kButton8,
		kL1 = kButton9,
		kR1 = kButton10,
		kDpadUp = kButton11,
		kDpadDown = kButton12,
		kDpadLeft = kButton13,
		kDpadRight = kButton14,
	};

	explicit PS5PadController(int player);
	PS5PadController(std::string_view uuid, std::string_view display_name);

	std::string_view api_name() const override
	{
		static_assert(to_string(InputAPI::PS5Pad) == "PS5Pad");
		return to_string(InputAPI::PS5Pad);
	}
	InputAPI::Type api() const override { return InputAPI::PS5Pad; }

	bool is_connected() override;

	bool has_motion() override { return true; }
	MotionSample get_motion_sample() override;

	bool has_position() override;
	glm::vec2 get_position() override;
	glm::vec2 get_prev_position() override { return m_previousTouch; }
	PositionVisibility GetPositionVisibility() override;

	bool has_rumble() override { return true; }
	void start_rumble() override;
	void stop_rumble() override;

	std::string get_button_name(uint64 button) const override;

protected:
	ControllerState raw_state() override;

private:
	int m_player;

	std::mutex m_motionMutex;
	WiiUMotionHandler m_motion;
	MotionSample m_motionSample;
	uint64 m_lastMotionTimestamp = 0;

	bool m_touching = false;
	glm::vec2 m_touch{}, m_previousTouch{};
};
