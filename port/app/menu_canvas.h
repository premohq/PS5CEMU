// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the in-game menus' look, the Wii U's (ingame.cpp) and the 3DS's (ingame3ds.cpp): the
// launcher's panels, rows, colours and controller hints (frontend/ui, in the colours
// tools/recolour-ui.py gives each side), drawn with ImGui on the launcher's 1920x1080 layout scaled
// to the screen, so a menu over a game is laid out as the launcher's screens are.

#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <cfloat>
#include <cstdint>
#include <cstring>
#include <string>

namespace ps5menu
{
	constexpr ImU32 Colour(uint32_t rgb, uint8_t alpha = 255)
	{
		return IM_COL32(rgb >> 16, (rgb >> 8) & 255, rgb & 255, alpha);
	}

	// A side's colours: the Wii U's dark blue, or the 3DS's dark gold
	struct Palette
	{
		ImU32 title, text, copy, accent, kicker, line;
		ImU32 panel, panelEdge, row, rowEdge, focus, focusEnd, focusEdge, dim;
	};
	constexpr Palette kBlue{Colour(0xf2f8ff), Colour(0xedf1f6), Colour(0xcad1d9), Colour(0xb8cfed), Colour(0x82ace2), Colour(0x454d59),
		Colour(0x070d18, 0xf5), Colour(0x34506f, 0xa8), Colour(0x0d1828, 0xe8), Colour(0x2a4462, 0x77), Colour(0x2a63a6),
		Colour(0x0d2140), Colour(0x5c9ce6, 0xa0), Colour(0x02060e, 0xb8)};
	constexpr Palette kGold{Colour(0xfff8ec), Colour(0xf6f1e8), Colour(0xd9d1c4), Colour(0xeddcb2), Colour(0xe2be6a), Colour(0x594f40),
		Colour(0x181207, 0xf5), Colour(0x6f5a2e, 0xa8), Colour(0x281e0d, 0xe8), Colour(0x62502a, 0x77), Colour(0xa67a1c),
		Colour(0x40290a), Colour(0xe6b44a, 0xa0), Colour(0x0e0902, 0xb8)};

	struct Canvas
	{
		ImDrawList* draw;
		float scale;
		ImVec2 origin;
		const Palette& colours = kBlue;

		ImVec2 At(float x, float y) const { return {origin.x + x * scale, origin.y + y * scale}; }

		void Panel(float x, float y, float width, float height) const
		{
			draw->AddRectFilled(At(x + 2, y + 2), At(x + width - 3, y + height - 3), colours.panel, 26 * scale);
			draw->AddRect(At(x + 2, y + 2), At(x + width - 3, y + height - 3), colours.panelEdge, 26 * scale, 0, scale);
		}

		// A row as the launcher's library rows: dark, or with the focus's gradient
		void Row(float x, float y, float width, float height, bool focused) const
		{
			const ImVec2 a = At(x + 2, y + 2), b = At(x + width - 3, y + height - 3);
			if (!focused)
			{
				draw->AddRectFilled(a, b, colours.row, 13 * scale);
				draw->AddRect(a, b, colours.rowEdge, 13 * scale, 0, scale);
				return;
			}
			const int start = draw->VtxBuffer.Size;
			draw->AddRectFilled(a, b, (colours.focus & 0x00ffffff) | (0x78u << IM_COL32_A_SHIFT), 13 * scale);
			ImGui::ShadeVertsLinearColorGradientKeepAlpha(draw, start, draw->VtxBuffer.Size, a, {b.x, a.y}, colours.focus, colours.focusEnd);
			draw->AddRect(a, b, colours.focusEdge, 13 * scale, 0, 1.5f * scale);
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
			const ImU32 colour = colours.copy;
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

	// The touchpad's cursor
	inline void DrawCursor(ImDrawList* draw, ImVec2 at, bool pressed, float scale)
	{
		const float radius = 12.0f * scale;
		if (pressed)
			draw->AddCircleFilled(at, radius, IM_COL32(255, 255, 255, 170));
		draw->AddCircle(at, radius + 2.0f * scale, IM_COL32(0, 0, 0, 200), 0, 3.0f * scale);
		draw->AddCircle(at, radius, IM_COL32(255, 255, 255, 255), 0, 2.5f * scale);
	}
}
