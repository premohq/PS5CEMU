// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Azahar's background, after the 3DS Homebrew Launcher's waves, made yellow: a golden
// gradient with three translucent waves rolling along its lower part, each its own length, height
// and speed, so they keep crossing as they go. Black at kOverlay's opacity covers it, as a darker
// black covers Cemu's bubbles (bubbles.h), so the launcher's light text reads as well on either.
//
// Each wave is a picture that repeats across: one wavelength more than the screen is wide, drawn
// from a point that moves on with time. This holds the pictures and those points; ui_host.cpp
// draws them under the launcher.

#pragma once

#include <cstdint>
#include <vector>

namespace ps5ui
{
	class Wave
	{
	public:
		static constexpr int kWidth = 1920, kHeight = 1080;
		static constexpr int kLayers = 3;
		static constexpr float kOverlay = 0.45f; // the dark overlay's opacity: less than the bubbles', so it stays yellow

		struct Layer
		{
			int top;		 // where its highest crest can be
			int height;		 // from there to the bottom of the screen
			int wavelength;	 // in pixels
			int amplitude;	 // crest to trough, halved
			float speed;	 // across, in pixels per second (negative: to the left)
			uint8_t rgb[3];	 // before the overlay
			float alpha;	 // its body; its crest is brighter
		};
		static const Layer& LayerOf(int layer);

		void Advance(double seconds);
		// Where in the layer's picture the screen's left edge is.
		int Offset(int layer) const;

		// The gradient under the waves, darkened: kWidth x kHeight BGRA pixels, top-down.
		static std::vector<uint8_t> Gradient();
		// A layer: (kWidth + wavelength) x height BGRA pixels, top-down, under the overlay.
		static std::vector<uint8_t> Picture(int layer);
		static int PictureWidth(int layer) { return kWidth + LayerOf(layer).wavelength; }

	private:
		double m_time = 0.0;
	};
}
