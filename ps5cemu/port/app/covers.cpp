// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: game icons for the launcher (emulator.h, CoverPath).
//
// The title is mounted and meta/iconTex.tga read as Cemu's game list does it
// (wxGameList::AsyncWorkerThread), so this file keeps Cemu's MPL-2.0 licence. The launcher's
// renderer (RmlUi's Vulkan backend) reads only uncompressed 24/32-bit TGAs, while icons may be
// run-length encoded or bottom-up, so each is rewritten once as a 32-bit top-down TGA.

#include "emulator.h"
#include "paths.h"
#include "../ps5/log.h"

#include "Cafe/Filesystem/fsc.h"
#include "Cafe/TitleList/TitleList.h"
#include "util/helpers/helpers.h"

#include <fstream>

namespace ps5emu
{
	namespace
	{
		struct Image
		{
			uint32_t width = 0, height = 0;
			std::vector<uint8_t> bgra; // top-down
		};

		// TGA types 2 (uncompressed) and 10 (run-length encoded), true colour, 24 or 32 bits.
		bool DecodeTga(const std::vector<uint8_t>& data, Image& out)
		{
			if (data.size() < 18)
				return false;
			const uint8_t idLength = data[0], colourMapType = data[1], type = data[2];
			const uint32_t width = data[12] | (data[13] << 8), height = data[14] | (data[15] << 8);
			const uint32_t bits = data[16];
			const bool topDown = data[17] & 0x20;
			if (colourMapType != 0 || (type != 2 && type != 10) || (bits != 24 && bits != 32) || !width || !height ||
				width > 1024 || height > 1024)
				return false;
			const uint32_t bytesPerPixel = bits / 8;
			size_t offset = 18 + idLength;
			std::vector<uint8_t> pixels((size_t)width * height * 4);
			auto put = [&](size_t index, const uint8_t* source) {
				pixels[index * 4 + 0] = source[0];
				pixels[index * 4 + 1] = source[1];
				pixels[index * 4 + 2] = source[2];
				pixels[index * 4 + 3] = bytesPerPixel == 4 ? source[3] : 0xff;
			};
			const size_t count = (size_t)width * height;
			if (type == 2)
			{
				if (data.size() < offset + count * bytesPerPixel)
					return false;
				for (size_t i = 0; i < count; i++)
					put(i, &data[offset + i * bytesPerPixel]);
			}
			else
			{
				size_t i = 0;
				while (i < count)
				{
					if (offset >= data.size())
						return false;
					const uint8_t header = data[offset++];
					const size_t run = (header & 0x7f) + 1;
					if (i + run > count)
						return false;
					if (header & 0x80)
					{
						if (offset + bytesPerPixel > data.size())
							return false;
						for (size_t j = 0; j < run; j++)
							put(i + j, &data[offset]);
						offset += bytesPerPixel;
					}
					else
					{
						if (offset + run * bytesPerPixel > data.size())
							return false;
						for (size_t j = 0; j < run; j++)
							put(i + j, &data[offset + j * bytesPerPixel]);
						offset += run * bytesPerPixel;
					}
					i += run;
				}
			}
			out.width = width;
			out.height = height;
			out.bgra.resize(pixels.size());
			const size_t row = (size_t)width * 4;
			for (uint32_t y = 0; y < height; y++)
			{
				const uint32_t sourceRow = topDown ? y : height - 1 - y;
				std::memcpy(&out.bgra[y * row], &pixels[sourceRow * row], row);
			}
			return true;
		}

		bool WriteTga(const fs::path& path, const Image& image)
		{
			uint8_t header[18]{};
			header[2] = 2;
			header[12] = image.width & 0xff;
			header[13] = image.width >> 8;
			header[14] = image.height & 0xff;
			header[15] = image.height >> 8;
			header[16] = 32;
			header[17] = 0x28; // 8 alpha bits, top-down
			const fs::path temporary = path.string() + ".tmp";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				if (!file)
					return false;
				file.write(reinterpret_cast<const char*>(header), sizeof(header));
				file.write(reinterpret_cast<const char*>(image.bgra.data()), (std::streamsize)image.bgra.size());
				if (!file)
					return false;
			}
			std::error_code ec;
			fs::rename(temporary, path, ec);
			return !ec;
		}

		std::optional<std::vector<uint8>> ReadIcon(uint64_t titleId)
		{
			TitleInfo titleInfo;
			if (!CafeTitleList::GetFirstByTitleId(titleId, titleInfo))
				return std::nullopt;
			const std::string mountPath = TitleInfo::GetUniqueTempMountingPath();
			if (!titleInfo.Mount(mountPath, "", FSC_PRIORITY_BASE))
				return std::nullopt;
			auto data = fsc_extractFile((mountPath + "/meta/iconTex.tga").c_str());
			if (!data)
			{
				data = fsc_extractFile((mountPath + "/meta/iconTex.tga.gz").c_str());
				if (data)
					data = zlibDecompress(*data, 70 * 1024);
			}
			titleInfo.Unmount(mountPath);
			return data;
		}
	}

	std::string CoverPath(uint64_t titleId)
	{
		static std::unordered_map<uint64_t, std::string> s_known; // per process: a failed one is not retried
		if (const auto it = s_known.find(titleId); it != s_known.end())
			return it->second;
		std::error_code ec;
		const fs::path folder = ps5paths::kCovers;
		const fs::path cover = folder / fmt::format("{:016x}.tga", titleId);
		std::string result;
		if (fs::exists(cover, ec))
			result = _pathToUtf8(cover);
		else if (const auto data = ReadIcon(titleId))
		{
			Image image;
			fs::create_directories(folder, ec);
			if (DecodeTga(*data, image) && WriteTga(cover, image))
				result = _pathToUtf8(cover);
			else
				ps5log::Line("[covers] {:016x}: the icon could not be converted", titleId);
		}
		s_known.emplace(titleId, result);
		return result;
	}
}
