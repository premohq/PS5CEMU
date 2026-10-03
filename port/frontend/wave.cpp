// SPDX-License-Identifier: GPL-3.0-or-later
#include "wave.h"

#include <algorithm>
#include <cmath>

namespace ps5ui
{
	namespace
	{
		constexpr uint8_t kTop[3] = {255, 204, 64}, kBottom[3] = {236, 158, 22}; // RGB
		constexpr float kPi = 3.14159265358979f;

		constexpr Wave::Layer kWaves[Wave::kLayers] = {
			{580, 500, 1280, 36, 24.0f, {255, 228, 140}, 0.26f},
			{670, 410, 960, 28, -36.0f, {255, 236, 166}, 0.32f},
			{770, 310, 720, 22, 50.0f, {255, 244, 196}, 0.40f},
		};

		uint8_t Dark(float value)
		{
			return (uint8_t)std::lround(std::clamp(value, 0.0f, 255.0f) * (1.0f - Wave::kOverlay));
		}
	}

	const Wave::Layer& Wave::LayerOf(int layer)
	{
		return kWaves[layer];
	}

	void Wave::Advance(double seconds)
	{
		m_time += std::clamp(seconds, 0.0, 0.1); // a stall does not make them jump
	}

	int Wave::Offset(int layer) const
	{
		const Layer& wave = kWaves[layer];
		// the picture moves the way the wave goes: where the screen starts in it, the other way
		const double position = std::fmod(-m_time * wave.speed, (double)wave.wavelength);
		return (int)(position < 0 ? position + wave.wavelength : position);
	}

	std::vector<uint8_t> Wave::Gradient()
	{
		std::vector<uint8_t> pixels((size_t)kWidth * kHeight * 4);
		for (int y = 0; y < kHeight; y++)
		{
			const float t = y / (float)(kHeight - 1);
			const uint8_t bgra[4] = {Dark(kTop[2] + (kBottom[2] - kTop[2]) * t), Dark(kTop[1] + (kBottom[1] - kTop[1]) * t),
				Dark(kTop[0] + (kBottom[0] - kTop[0]) * t), 255};
			uint8_t* row = pixels.data() + (size_t)y * kWidth * 4;
			for (int x = 0; x < kWidth; x++)
				std::copy(bgra, bgra + 4, row + x * 4);
		}
		return pixels;
	}

	std::vector<uint8_t> Wave::Picture(int layer)
	{
		const Layer& wave = kWaves[layer];
		const int width = PictureWidth(layer);
		std::vector<uint8_t> pixels((size_t)width * wave.height * 4);
		const uint8_t bgr[3] = {Dark(wave.rgb[2]), Dark(wave.rgb[1]), Dark(wave.rgb[0])};
		for (int x = 0; x < width; x++)
		{
			// the surface, from the crest (0) down to the trough (2 * amplitude)
			const float surface = wave.amplitude * (1.0f - std::cos(2.0f * kPi * (x + 0.5f) / wave.wavelength));
			for (int y = 0; y < wave.height; y++)
			{
				const float depth = y + 0.5f - surface;
				// the body, its edge smoothed over a pixel, and a brighter crest just under it
				const float body = std::clamp(depth + 0.5f, 0.0f, 1.0f);
				const float crest = std::clamp(1.0f - std::fabs(depth - 1.5f) / 2.5f, 0.0f, 1.0f);
				const float alpha = std::min(1.0f, body * wave.alpha + crest * 0.35f);
				uint8_t* pixel = &pixels[((size_t)y * width + x) * 4];
				std::copy(bgr, bgr + 3, pixel);
				pixel[3] = (uint8_t)std::lround(alpha * 255.0f);
			}
		}
		return pixels;
	}
}
