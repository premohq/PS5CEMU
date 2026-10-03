// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: box art for the launcher's game details, from GameTDB (art.gametdb.com), which has
// the Wii U's and the 3DS's covers by the ID printed on each game's box (ALZE01 for a Wii U game,
// AREE for a 3DS one). They are fetched in the background over HTTP, as the Wii's homebrew loaders
// fetch theirs, decoded, scaled to the launcher's cover and kept as TGAs it shows:
// covers/boxart/<wiiu|3ds>/<ID>.tga. An ID GameTDB has no cover for is remembered (<ID>.none) and
// not asked for again.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps5boxart
{
	enum class System
	{
		WiiU,
		N3ds,
	};

	// The largest a cover is kept: the launcher's cover area at 4K
	constexpr int kMaxWidth = 576, kMaxHeight = 704;

	// The cover's TGA when it has been fetched, else empty.
	std::string Path(System system, const std::string& id);
	// Fetches the covers not fetched yet, one at a time, on a thread of its own.
	void Fetch(System system, const std::vector<std::string>& ids);
	// How many covers have arrived since the start: the launcher shows a new one when it changes.
	uint32_t Arrivals();
	// The launcher's setting: when off, nothing more is fetched.
	void SetEnabled(bool enabled);

	// A TGA's size (its header), for laying it out.
	bool ImageSize(const std::string& path, int& width, int& height);
}
