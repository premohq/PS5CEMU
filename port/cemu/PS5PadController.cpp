// SPDX-License-Identifier: GPL-3.0-or-later
#include "PS5PadController.h"
#include "../ps5/pad.h"

namespace
{
	float StickAxis(uint8 raw)
	{
		// 0..255 with 128 at rest, down and right positive (as SDL reports them)
		return std::clamp((static_cast<int>(raw) - 128) / (raw < 128 ? 128.0f : 127.0f), -1.0f, 1.0f);
	}
}

PS5PadController::PS5PadController(int player)
	: base_type(fmt::format("{}", player), fmt::format("DualSense (player {})", player + 1)), m_player(player)
{
}

PS5PadController::PS5PadController(std::string_view uuid, std::string_view display_name)
	: base_type(uuid, display_name), m_player(std::clamp(ConvertString<int>(uuid), 0, ps5pad::kMaxPlayers - 1))
{
}

bool PS5PadController::is_connected()
{
	return ps5pad::IsConnected(m_player);
}

ControllerState PS5PadController::raw_state()
{
	ControllerState result{};
	ps5pad::Data data;
	if (!ps5pad::Read(m_player, data))
	{
		m_touching = false;
		return result;
	}

	const uint32 buttons = ps5pad::FilterShortcuts(m_player, data.buttons);
	static constexpr std::pair<uint32, uint64> kButtons[] = {
		{ps5pad::kCross, kCross}, {ps5pad::kCircle, kCircle}, {ps5pad::kSquare, kSquare}, {ps5pad::kTriangle, kTriangle},
		{ps5pad::kCreate, kCreate}, {ps5pad::kOptions, kOptions}, {ps5pad::kL3, kL3}, {ps5pad::kR3, kR3},
		{ps5pad::kL1, kL1}, {ps5pad::kR1, kR1}, {ps5pad::kUp, kDpadUp}, {ps5pad::kDown, kDpadDown},
		{ps5pad::kLeft, kDpadLeft}, {ps5pad::kRight, kDpadRight},
	};
	for (const auto& [mask, id] : kButtons)
		result.buttons.SetButtonState((uint32)id, (buttons & mask) != 0);

	result.axis = {StickAxis(data.leftX), StickAxis(data.leftY)};
	result.rotation = {StickAxis(data.rightX), StickAxis(data.rightY)};
	result.trigger = {data.l2 / 255.0f, data.r2 / 255.0f};

	// the touchpad as the GamePad's touch screen
	m_previousTouch = m_touch;
	m_touching = data.touchCount > 0;
	if (m_touching)
	{
		float width, height;
		ps5pad::TouchResolution(m_player, width, height);
		m_touch = {std::clamp(data.touch[0].x / width, 0.0f, 1.0f), std::clamp(data.touch[0].y / height, 0.0f, 1.0f)};
	}

	// motion, in the axes and units Cemu's SDL gamepads use (acceleration in g, rotation in
	// radians per second). The DualSense axes as libScePad reports them need checking on a console.
	if (data.timestampUs != m_lastMotionTimestamp)
	{
		std::lock_guard lock(m_motionMutex);
		const float deltaTime = m_lastMotionTimestamp ? (data.timestampUs - m_lastMotionTimestamp) / 1000000.0f : 0.0f;
		m_lastMotionTimestamp = data.timestampUs;
		if (deltaTime > 0.0f && deltaTime < 0.5f)
		{
			const glm::vec3 acc{-data.acceleration[0], -data.acceleration[1], -data.acceleration[2]};
			const glm::vec3 gyro{data.angularVelocity[0], -data.angularVelocity[1], -data.angularVelocity[2]};
			m_motion.processMotionSample(deltaTime, gyro.x, gyro.y, gyro.z, acc.x, -acc.y, -acc.z);
			m_motionSample = m_motion.getMotionSample();
		}
	}
	return result;
}

MotionSample PS5PadController::get_motion_sample()
{
	std::lock_guard lock(m_motionMutex);
	return m_motionSample;
}

bool PS5PadController::has_position()
{
	return m_touching;
}

glm::vec2 PS5PadController::get_position()
{
	return m_touch;
}

PositionVisibility PS5PadController::GetPositionVisibility()
{
	return m_touching ? PositionVisibility::FULL : PositionVisibility::NONE;
}

void PS5PadController::start_rumble()
{
	const float strength = std::clamp(get_settings().rumble, 0.0f, 1.0f);
	if (strength <= 0.0f)
		return;
	const auto motor = static_cast<uint8>(strength * 255.0f);
	ps5pad::SetVibration(m_player, motor, motor);
}

void PS5PadController::stop_rumble()
{
	ps5pad::SetVibration(m_player, 0, 0);
}

std::string PS5PadController::get_button_name(uint64 button) const
{
	switch (button)
	{
	case kCross: return "Cross";
	case kCircle: return "Circle";
	case kSquare: return "Square";
	case kTriangle: return "Triangle";
	case kCreate: return "Create";
	case kOptions: return "Options";
	case kL3: return "L3";
	case kR3: return "R3";
	case kL1: return "L1";
	case kR1: return "R1";
	case kDpadUp: return "D-pad up";
	case kDpadDown: return "D-pad down";
	case kDpadLeft: return "D-pad left";
	case kDpadRight: return "D-pad right";
	case kTriggerXP: return "L2";
	case kTriggerYP: return "R2";
	default: return ControllerBase::get_button_name(button);
	}
}
