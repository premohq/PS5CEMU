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
	namespace
	{
		using Writer = rapidjson::PrettyWriter<rapidjson::StringBuffer>;

		std::string Hex(uint64_t value)
		{
			char text[17];
			std::snprintf(text, sizeof(text), "%016llx", (unsigned long long)value);
			return text;
		}

		void ReadString(const rapidjson::Value& json, const char* key, std::string& out)
		{
			if (json.HasMember(key) && json[key].IsString())
				out = json[key].GetString();
		}

		void ReadInt(const rapidjson::Value& json, const char* key, int& out, int low, int high)
		{
			if (json.HasMember(key) && json[key].IsInt())
				out = std::clamp(json[key].GetInt(), low, high);
		}

		void ReadBool(const rapidjson::Value& json, const char* key, bool& out)
		{
			if (json.HasMember(key) && json[key].IsBool())
				out = json[key].GetBool();
		}

		void ReadGames(const rapidjson::Value& json, uint64_t& lastGame, std::vector<uint64_t>& recent)
		{
			if (json.HasMember("lastGame") && json["lastGame"].IsString())
				lastGame = std::strtoull(json["lastGame"].GetString(), nullptr, 16);
			if (json.HasMember("recent") && json["recent"].IsArray())
				for (const auto& entry : json["recent"].GetArray())
					if (entry.IsString() && recent.size() < 4)
						recent.push_back(std::strtoull(entry.GetString(), nullptr, 16));
		}

		void WriteGames(Writer& writer, uint64_t lastGame, const std::vector<uint64_t>& recent)
		{
			writer.Key("lastGame");
			writer.String(Hex(lastGame).c_str());
			writer.Key("recent");
			writer.StartArray();
			for (uint64_t titleId : recent)
				writer.String(Hex(titleId).c_str());
			writer.EndArray();
		}

		void ReadN3ds(const rapidjson::Value& json, N3ds& n3ds)
		{
			ReadString(json, "gamesFolder", n3ds.gamesFolder);
			ReadInt(json, "resolution", n3ds.resolution, 1, 10);
			ReadInt(json, "layout", n3ds.layout, 0, 3);
			ReadInt(json, "textureFilter", n3ds.textureFilter, 0, 5);
			ReadInt(json, "volume", n3ds.volume, 0, 100);
			ReadBool(json, "motion", n3ds.motion);
			ReadInt(json, "deadzone", n3ds.deadzone, 0, 50);
			ReadBool(json, "performance", n3ds.performance);
			if (json.HasMember("buttons") && json["buttons"].IsObject())
				for (const auto& member : json["buttons"].GetObject())
					if (member.value.IsString())
						n3ds.buttons[member.name.GetString()] = member.value.GetString();
			ReadGames(json, n3ds.lastGame, n3ds.recent);
		}

		void WriteN3ds(Writer& writer, const N3ds& n3ds)
		{
			writer.StartObject();
			writer.Key("gamesFolder");
			writer.String(n3ds.gamesFolder.c_str());
			writer.Key("resolution");
			writer.Int(n3ds.resolution);
			writer.Key("layout");
			writer.Int(n3ds.layout);
			writer.Key("textureFilter");
			writer.Int(n3ds.textureFilter);
			writer.Key("volume");
			writer.Int(n3ds.volume);
			writer.Key("motion");
			writer.Bool(n3ds.motion);
			writer.Key("deadzone");
			writer.Int(n3ds.deadzone);
			writer.Key("performance");
			writer.Bool(n3ds.performance);
			writer.Key("buttons");
			writer.StartObject();
			for (const auto& [button, input] : n3ds.buttons)
			{
				writer.Key(button.c_str());
				writer.String(input.c_str());
			}
			writer.EndObject();
			WriteGames(writer, n3ds.lastGame, n3ds.recent);
			writer.EndObject();
		}
	}

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
		ReadString(json, "gamesFolder", settings.gamesFolder);
		ReadInt(json, "upscaleFilter", settings.upscaleFilter, 0, 3);
		ReadBool(json, "highFrameRate", settings.highFrameRate);
		ReadBool(json, "overlay", settings.overlay);
		ReadBool(json, "rumble", settings.rumble);
		ReadBool(json, "pinCpuThreads", settings.pinCpuThreads);
		ReadInt(json, "volume", settings.volume, 0, 100);
		ReadString(json, "launchError", settings.launchError);
		ReadString(json, "side", settings.side);
		ReadGames(json, settings.lastGame, settings.recent);
		if (json.HasMember("n3ds") && json["n3ds"].IsObject())
			ReadN3ds(json["n3ds"], settings.n3ds);
		return settings;
	}

	bool Save(const Launcher& settings)
	{
		rapidjson::StringBuffer buffer;
		Writer writer(buffer);
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
		writer.Key("pinCpuThreads");
		writer.Bool(settings.pinCpuThreads);
		writer.Key("volume");
		writer.Int(settings.volume);
		WriteGames(writer, settings.lastGame, settings.recent);
		writer.Key("n3ds");
		WriteN3ds(writer, settings.n3ds);
		writer.Key("side");
		writer.String(settings.side.c_str());
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

	void AddRecent(uint64_t& lastGame, std::vector<uint64_t>& recent, uint64_t titleId)
	{
		lastGame = titleId;
		std::erase(recent, titleId);
		recent.insert(recent.begin(), titleId);
		if (recent.size() > 4)
			recent.resize(4);
	}
}
