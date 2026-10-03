// SPDX-License-Identifier: GPL-3.0-or-later
#include "input.h"
#include "controls.h"
#include "../ps5/pad.h"

#include "common/settings.h"
#include "common/vector_math.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/input.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace ps5azahar::input
{
	namespace
	{
		using ps5emu::PadInput;

		// The controller as the game sees it: the buttons past the port's shortcuts, the sticks
		// from -1 to 1 (up positive), the motion in the 3DS's axes.
		struct Sample
		{
			uint32_t buttons = 0;
			float leftX = 0, leftY = 0, rightX = 0, rightY = 0;
			Common::Vec3<float> acceleration{0.0f, -1.0f, 0.0f}; // g
			Common::Vec3<float> rotation{};						 // degrees per second
		};

		std::mutex s_mutex;
		Sample s_sample;
		bool s_cursorVisible = false;
		float s_cursorX = 0, s_cursorY = 0; // 0 to 1 over the bottom screen
		bool s_touching = false;

		float Stick(uint8_t value)
		{
			return std::clamp((value - 128.0f) / 127.0f, -1.0f, 1.0f);
		}

		bool Has(const Sample& sample, uint32_t button)
		{
			return (sample.buttons & button) != 0;
		}

		// How far an input is pressed, 0 to 1.
		float Amount(const Sample& sample, PadInput input)
		{
			switch (input)
			{
			case PadInput::Cross: return Has(sample, ps5pad::kCross);
			case PadInput::Circle: return Has(sample, ps5pad::kCircle);
			case PadInput::Square: return Has(sample, ps5pad::kSquare);
			case PadInput::Triangle: return Has(sample, ps5pad::kTriangle);
			case PadInput::L1: return Has(sample, ps5pad::kL1);
			case PadInput::R1: return Has(sample, ps5pad::kR1);
			case PadInput::L2: return Has(sample, ps5pad::kL2);
			case PadInput::R2: return Has(sample, ps5pad::kR2);
			case PadInput::L3: return Has(sample, ps5pad::kL3);
			case PadInput::R3: return Has(sample, ps5pad::kR3);
			case PadInput::Create: return Has(sample, ps5pad::kCreate);
			case PadInput::Options: return Has(sample, ps5pad::kOptions);
			case PadInput::Up: return Has(sample, ps5pad::kUp);
			case PadInput::Down: return Has(sample, ps5pad::kDown);
			case PadInput::Left: return Has(sample, ps5pad::kLeft);
			case PadInput::Right: return Has(sample, ps5pad::kRight);
			case PadInput::LeftStickUp: return std::max(0.0f, sample.leftY);
			case PadInput::LeftStickDown: return std::max(0.0f, -sample.leftY);
			case PadInput::LeftStickLeft: return std::max(0.0f, -sample.leftX);
			case PadInput::LeftStickRight: return std::max(0.0f, sample.leftX);
			case PadInput::RightStickUp: return std::max(0.0f, sample.rightY);
			case PadInput::RightStickDown: return std::max(0.0f, -sample.rightY);
			case PadInput::RightStickLeft: return std::max(0.0f, -sample.rightX);
			case PadInput::RightStickRight: return std::max(0.0f, sample.rightX);
			case PadInput::None: break;
			}
			return 0.0f;
		}

		Sample Latest()
		{
			std::lock_guard lock(s_mutex);
			return s_sample;
		}

		class PadButton final : public Input::ButtonDevice
		{
		public:
			explicit PadButton(PadInput input) : m_input(input) {}
			bool GetStatus() const override
			{
				return Amount(Latest(), m_input) > 0.5f;
			}

		private:
			PadInput m_input;
		};

		// The circle pad or the C-stick from the four inputs its directions are mapped to: a stick's
		// direction gives its own travel, a button all of it.
		class PadAnalog final : public Input::AnalogDevice
		{
		public:
			PadAnalog(PadInput up, PadInput down, PadInput left, PadInput right, float deadzone)
				: m_up(up), m_down(down), m_left(left), m_right(right), m_deadzone(std::clamp(deadzone, 0.0f, 0.9f))
			{
			}

			std::tuple<float, float> GetStatus() const override
			{
				const Sample sample = Latest();
				float x = Amount(sample, m_right) - Amount(sample, m_left);
				float y = Amount(sample, m_up) - Amount(sample, m_down);
				const float length = std::sqrt(x * x + y * y);
				if (length <= m_deadzone)
					return {0.0f, 0.0f};
				// the travel past the deadzone over the whole range, within the unit circle
				const float scaled = std::min(1.0f, (length - m_deadzone) / (1.0f - m_deadzone));
				return {x / length * scaled, y / length * scaled};
			}

		private:
			PadInput m_up, m_down, m_left, m_right;
			float m_deadzone;
		};

		class PadMotion final : public Input::MotionDevice
		{
		public:
			explicit PadMotion(bool still) : m_still(still) {}
			std::tuple<Common::Vec3<float>, Common::Vec3<float>> GetStatus() const override
			{
				if (m_still)
					return {{0.0f, -1.0f, 0.0f}, {}}; // lying still, as Azahar's motion_emu at rest
				const Sample sample = Latest();
				return {sample.acceleration, sample.rotation};
			}

		private:
			bool m_still;
		};

		PadInput InputParam(const Common::ParamPackage& params, const std::string& name)
		{
			const int value = params.Get(name, 0);
			return value > 0 && value <= (int)PadInput::RightStickRight ? (PadInput)value : PadInput::None;
		}

		class ButtonFactory final : public Input::Factory<Input::ButtonDevice>
		{
		public:
			std::unique_ptr<Input::ButtonDevice> Create(const Common::ParamPackage& params) override
			{
				return std::make_unique<PadButton>(InputParam(params, "input"));
			}
		};

		class AnalogFactory final : public Input::Factory<Input::AnalogDevice>
		{
		public:
			std::unique_ptr<Input::AnalogDevice> Create(const Common::ParamPackage& params) override
			{
				return std::make_unique<PadAnalog>(InputParam(params, "up"), InputParam(params, "down"), InputParam(params, "left"),
					InputParam(params, "right"), params.Get("deadzone", 0.15f));
			}
		};

		class MotionFactory final : public Input::Factory<Input::MotionDevice>
		{
		public:
			std::unique_ptr<Input::MotionDevice> Create(const Common::ParamPackage& params) override
			{
				return std::make_unique<PadMotion>(params.Get("still", 0) != 0);
			}
		};

		bool s_registered = false;

		std::string ButtonParams(const ps5settings::N3ds& settings, Button button)
		{
			const PadInput input = MappedInput(settings, button);
			if (input == PadInput::None)
				return {};
			return "engine:ps5,input:" + std::to_string((int)input);
		}

		std::string AnalogParams(const ps5settings::N3ds& settings, Button up, Button down, Button left, Button right)
		{
			return "engine:ps5,up:" + std::to_string((int)MappedInput(settings, up)) +
				",down:" + std::to_string((int)MappedInput(settings, down)) +
				",left:" + std::to_string((int)MappedInput(settings, left)) +
				",right:" + std::to_string((int)MappedInput(settings, right)) +
				",deadzone:" + std::to_string(std::clamp(settings.deadzone, 0, 90) / 100.0f);
		}
	}

	void Configure(const ps5settings::N3ds& settings)
	{
		if (!s_registered)
		{
			Input::RegisterFactory<Input::ButtonDevice>("ps5", std::make_shared<ButtonFactory>());
			Input::RegisterFactory<Input::AnalogDevice>("ps5", std::make_shared<AnalogFactory>());
			Input::RegisterFactory<Input::MotionDevice>("ps5", std::make_shared<MotionFactory>());
			s_registered = true;
		}

		auto& profile = Settings::values.current_input_profile;
		using namespace Settings::NativeButton;
		constexpr std::pair<Values, Button> kButtons[] = {
			{A, Button::A}, {B, Button::B}, {X, Button::X}, {Y, Button::Y},
			{Up, Button::Up}, {Down, Button::Down}, {Left, Button::Left}, {Right, Button::Right},
			{L, Button::L}, {R, Button::R}, {Start, Button::Start}, {Select, Button::Select},
			{ZL, Button::ZL}, {ZR, Button::ZR}, {Home, Button::Home},
		};
		for (auto& button : profile.buttons)
			button.clear();
		for (const auto& [native, button] : kButtons)
			profile.buttons[native] = ButtonParams(settings, button);
		profile.analogs[Settings::NativeAnalog::CirclePad] =
			AnalogParams(settings, Button::CircleUp, Button::CircleDown, Button::CircleLeft, Button::CircleRight);
		profile.analogs[Settings::NativeAnalog::CStick] =
			AnalogParams(settings, Button::CStickUp, Button::CStickDown, Button::CStickLeft, Button::CStickRight);
		profile.motion_device = settings.motion ? "engine:ps5" : "engine:ps5,still:1";
		profile.touch_device = "engine:emu_window";
		profile.use_touchpad = false;
		profile.use_touch_from_button = false;
	}

	void Shutdown()
	{
		if (!s_registered)
			return;
		Input::UnregisterFactory<Input::ButtonDevice>("ps5");
		Input::UnregisterFactory<Input::AnalogDevice>("ps5");
		Input::UnregisterFactory<Input::MotionDevice>("ps5");
		s_registered = false;
	}

	void Update(Frontend::EmuWindow& window, bool blocked)
	{
		ps5pad::Data data{};
		Sample sample;
		bool fingerDown = false, touch = false;
		float fingerX = 0, fingerY = 0;
		if (ps5pad::Read(0, data) && !(data.buttons & ps5pad::kIntercepted))
		{
			const ps5pad::Filtered filtered = ps5pad::FilterShortcuts(0, data.buttons);
			sample.buttons = filtered.buttons;
			touch = filtered.touch;
			sample.leftX = Stick(data.leftX);
			sample.leftY = -Stick(data.leftY);
			sample.rightX = Stick(data.rightX);
			sample.rightY = -Stick(data.rightY);
			// the DualSense's axes as SDL's (x right, y up, z towards the player), into the 3DS's
			// as Azahar's SDL input turns them (x left, y out of the touch screen, z up)
			sample.acceleration = {data.acceleration[0], -data.acceleration[1], data.acceleration[2]};
			constexpr float kDegrees = 180.0f / 3.14159265f;
			sample.rotation = {-data.angularVelocity[0] * kDegrees, data.angularVelocity[1] * kDegrees,
				-data.angularVelocity[2] * kDegrees};
			if (blocked)
			{
				// the shortcuts still seen (the menu's own closes it), nothing for the game
				sample = Sample{};
				touch = false;
			}
			else if (data.touchCount > 0)
			{
				float width = 1, height = 1;
				ps5pad::TouchResolution(0, width, height);
				fingerDown = true;
				fingerX = std::clamp(data.touch[0].x / std::max(width, 1.0f), 0.0f, 1.0f);
				fingerY = std::clamp(data.touch[0].y / std::max(height, 1.0f), 0.0f, 1.0f);
			}
		}

		// the click touches only a bottom screen that is shown
		const auto& layout = window.GetFramebufferLayout();
		const auto& bottom = layout.bottom_screen;
		if (!layout.bottom_screen_enabled)
			touch = false;

		float cursorX, cursorY;
		bool wasTouching;
		{
			std::lock_guard lock(s_mutex);
			s_sample = sample;
			s_cursorVisible = fingerDown;
			if (fingerDown)
			{
				s_cursorX = fingerX;
				s_cursorY = fingerY;
			}
			cursorX = s_cursorX;
			cursorY = s_cursorY;
			wasTouching = s_touching;
			s_touching = touch;
		}

		// where the cursor is (where it last was, without a finger)
		const unsigned x = bottom.left + (unsigned)(cursorX * std::max(1u, bottom.GetWidth() - 1));
		const unsigned y = bottom.top + (unsigned)(cursorY * std::max(1u, bottom.GetHeight() - 1));
		if (touch && !wasTouching)
			window.TouchPressed(x, y);
		else if (touch)
			window.TouchMoved(x, y);
		else if (wasTouching)
			window.TouchReleased();
	}

	bool Cursor(float& x, float& y)
	{
		std::lock_guard lock(s_mutex);
		x = s_cursorX;
		y = s_cursorY;
		return s_cursorVisible;
	}
}
