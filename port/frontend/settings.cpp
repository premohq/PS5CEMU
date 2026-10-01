// SPDX-License-Identifier: GPL-3.0-or-later
#include "settings.h"
#include "../app/paths.h"

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace ps5settings
{
	Launcher Load()
	{
		Launcher settings;
		std::ifstream file(ps5paths::kLauncherSettings);
		if (!file)
			return settings;
		std::stringstream text;
		text << file.rdbuf();
		rapidjson::Document json;
		if (json.Parse(text.str().c_str()).HasParseError() || !json.IsObject())
			return settings;
		if (json.HasMember("gamesFolder") && json["gamesFolder"].IsString())
			settings.gamesFolder = json["gamesFolder"].GetString();
		if (json.HasMember("upscaleFilter") && json["upscaleFilter"].IsInt())
			settings.upscaleFilter = std::clamp(json["upscaleFilter"].GetInt(), 0, 3);
		if (json.HasMember("highFrameRate") && json["highFrameRate"].IsBool())
			settings.highFrameRate = json["highFrameRate"].GetBool();
		if (json.HasMember("overlay") && json["overlay"].IsBool())
			settings.overlay = json["overlay"].GetBool();
		if (json.HasMember("rumble") && json["rumble"].IsBool())
			settings.rumble = json["rumble"].GetBool();
		if (json.HasMember("volume") && json["volume"].IsInt())
			settings.volume = std::clamp(json["volume"].GetInt(), 0, 100);
		if (json.HasMember("lastGame") && json["lastGame"].IsString())
			settings.lastGame = std::strtoull(json["lastGame"].GetString(), nullptr, 16);
		if (json.HasMember("launchError") && json["launchError"].IsString())
			settings.launchError = json["launchError"].GetString();
		if (json.HasMember("recent") && json["recent"].IsArray())
			for (const auto& entry : json["recent"].GetArray())
				if (entry.IsString() && settings.recent.size() < 4)
					settings.recent.push_back(std::strtoull(entry.GetString(), nullptr, 16));
		return settings;
	}

	bool Save(const Launcher& settings)
	{
		auto hex = [](uint64_t value) {
			char text[17];
			std::snprintf(text, sizeof(text), "%016llx", (unsigned long long)value);
			return std::string(text);
		};
		rapidjson::StringBuffer buffer;
		rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
		writer.StartObject();
		writer.Key("gamesFolder");
		writer.String(settings.gamesFolder.c_str());
		writer.Key("upscaleFilter");
		writer.Int(settings.upscaleFilter);
		writer.Key("highFrameRate");
		writer.Bool(settings.highFrameRate);
		writer.Key("overlay");
		writer.Bool(settings.overlay);
		writer.Key("rumble");
		writer.Bool(settings.rumble);
		writer.Key("volume");
		writer.Int(settings.volume);
		writer.Key("lastGame");
		writer.String(hex(settings.lastGame).c_str());
		writer.Key("recent");
		writer.StartArray();
		for (uint64_t titleId : settings.recent)
			writer.String(hex(titleId).c_str());
		writer.EndArray();
		writer.Key("launchError");
		writer.String(settings.launchError.c_str());
		writer.EndObject();
		const std::string temporary = std::string(ps5paths::kLauncherSettings) + ".tmp";
		{
			std::ofstream file(temporary, std::ios::trunc);
			if (!file)
				return false;
			file << buffer.GetString() << '\n';
		}
		return std::rename(temporary.c_str(), ps5paths::kLauncherSettings) == 0;
	}

	void AddRecent(Launcher& settings, uint64_t titleId)
	{
		settings.lastGame = titleId;
		std::erase(settings.recent, titleId);
		settings.recent.insert(settings.recent.begin(), titleId);
		if (settings.recent.size() > 4)
			settings.recent.resize(4);
	}
}
