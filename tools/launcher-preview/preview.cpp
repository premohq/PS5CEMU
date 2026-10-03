// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher on a PC, to see its layout without a console (tools/preview-launcher.sh).
//
// The launcher's own code (port/frontend: launcher.cpp, settings.cpp, bubbles.cpp, wave.cpp,
// ProsperoEden's bitmap fonts; port/azahar's library and controls) runs on RmlUi built for the PC,
// and draws in software here as SDL draws it on the console: textures modulated by RmlUi's vertex
// colours and blended without premultiplying, on whole pixels. In place of Cemu and the console it
// has sample Wii U games, graphic packs and controllers; the 3DS games are files the script makes
// (tools/launcher-preview/make-3ds-samples.py), read by Azahar's side's own library. A script
// presses the DualSense's buttons and saves frames as PNGs.
//
//   launcher-preview UI_FOLDER OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER
//
// The script has one command a line ('#' starts a comment):
//   press BUTTON...  each button down for 3 frames, then up for 3, one after another
//                    (touchpad+options: the two together)
//   hold BUTTON N    down for N frames, then up for 3
//   wait N           N frames
//   shot NAME        the frame on screen as OUTPUT_FOLDER/NAME.png
// Buttons: up down left right cross circle square triangle l1 r1 l2 r2 l3 r3 options create
// touchpad, and the sticks: ls-up ls-down ls-left ls-right rs-up rs-down rs-left rs-right.

#include "app/boxart.h"
#include "app/emulator.h"
#include "frontend/bubbles.h"
#include "frontend/launcher.h"
#include "frontend/wave.h"
#include "frontend/settings.h"
#include "frontend/ui_host.h"
#include "ps5/kernel.h"
#include "ps5/log.h"
#include "ps5/notify.h"
#include "ps5/pad.h"
#include "ps5/privilege.h"

#include "bitmap_font_engine.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <sys/syscall.h>
#include <unistd.h>

namespace
{
	constexpr int kWidth = 1920, kHeight = 1080;
	constexpr const char* kFonts[] = {"20", "24", "28", "32", "36", "40", "48"};

	// sticks, pressed like buttons in the script
	constexpr uint32_t kLeftUp = 1u << 24, kLeftDown = 1u << 25, kLeftLeft = 1u << 26, kLeftRight = 1u << 27;
	constexpr uint32_t kRightUp = 1u << 28, kRightDown = 1u << 29, kRightLeft = 1u << 30;
	constexpr uint64_t kRightRight = 1ull << 31; // shares a bit with kIntercepted, which the launcher ignores

	std::string s_ui, s_output;
	uint64_t s_timeUs = 1000000;
	uint32_t s_buttons = 0;

	// -- drawing ---------------------------------------------------------------------------------

	struct Texture
	{
		int width = 0, height = 0;
		std::vector<uint8_t> bgra;
	};

	std::vector<uint8_t> s_frame((size_t)kWidth * kHeight * 4); // BGRA

	void Blend(uint8_t* pixel, float b, float g, float r, float a)
	{
		// as SDL_BLENDMODE_BLEND: the colour weighted by its alpha, without premultiplying
		pixel[0] = (uint8_t)std::lround(b * a + pixel[0] * (1.0f - a));
		pixel[1] = (uint8_t)std::lround(g * a + pixel[1] * (1.0f - a));
		pixel[2] = (uint8_t)std::lround(r * a + pixel[2] * (1.0f - a));
	}

	class Renderer final : public Rml::RenderInterfaceCompatibility
	{
	public:
		void RenderGeometry(Rml::Vertex* vertices, int, int* indices, int indexCount, Rml::TextureHandle handle,
			const Rml::Vector2f& translation) override
		{
			const Texture* texture = reinterpret_cast<const Texture*>(handle);
			for (int i = 0; i + 2 < indexCount; i += 3)
				Triangle(vertices[indices[i]], vertices[indices[i + 1]], vertices[indices[i + 2]], texture, translation);
		}

		bool LoadTexture(Rml::TextureHandle& handle, Rml::Vector2i& dimensions, const Rml::String& source) override
		{
			Rml::FileInterface* files = Rml::GetFileInterface();
			const Rml::FileHandle file = files->Open(source);
			if (!file)
				return false;
			files->Seek(file, 0, SEEK_END);
			std::vector<uint8_t> data(files->Tell(file));
			files->Seek(file, 0, SEEK_SET);
			files->Read(data.data(), data.size(), file);
			files->Close(file);
			if (data.size() < 18 || data[2] != 2 || data[16] != 32 || !(data[17] & 0x20))
			{
				std::fprintf(stderr, "%s is not a 32-bit top-down TGA\n", source.c_str());
				return false;
			}
			auto* texture = new Texture{data[12] | data[13] << 8, data[14] | data[15] << 8};
			const size_t bytes = (size_t)texture->width * texture->height * 4;
			texture->bgra.assign(data.begin() + 18, data.begin() + 18 + std::min(bytes, data.size() - 18));
			texture->bgra.resize(bytes);
			handle = reinterpret_cast<Rml::TextureHandle>(texture);
			dimensions = {texture->width, texture->height};
			return true;
		}

		bool GenerateTexture(Rml::TextureHandle& handle, const Rml::byte* rgba, const Rml::Vector2i& dimensions) override
		{
			auto* texture = new Texture{dimensions.x, dimensions.y};
			texture->bgra.resize((size_t)dimensions.x * dimensions.y * 4);
			for (size_t i = 0; i < texture->bgra.size(); i += 4)
			{
				texture->bgra[i + 0] = rgba[i + 2];
				texture->bgra[i + 1] = rgba[i + 1];
				texture->bgra[i + 2] = rgba[i + 0];
				texture->bgra[i + 3] = rgba[i + 3];
			}
			handle = reinterpret_cast<Rml::TextureHandle>(texture);
			return true;
		}

		void ReleaseTexture(Rml::TextureHandle handle) override { delete reinterpret_cast<Texture*>(handle); }
		void EnableScissorRegion(bool enable) override { m_scissor = enable; }
		void SetScissorRegion(int x, int y, int width, int height) override { m_clip = {x, y, x + width, y + height}; }

	private:
		// Pixel centres inside the triangle, sampled a hair off the centre so that a pixel on the
		// edge two triangles share is drawn by exactly one of them.
		void Triangle(const Rml::Vertex& a, const Rml::Vertex& b, const Rml::Vertex& c, const Texture* texture, Rml::Vector2f t)
		{
			const double ax = a.position.x + t.x, ay = a.position.y + t.y, bx = b.position.x + t.x, by = b.position.y + t.y;
			const double cx = c.position.x + t.x, cy = c.position.y + t.y;
			const double area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
			if (area == 0.0)
				return;
			int left = (int)std::floor(std::min({ax, bx, cx})), right = (int)std::ceil(std::max({ax, bx, cx}));
			int top = (int)std::floor(std::min({ay, by, cy})), bottom = (int)std::ceil(std::max({ay, by, cy}));
			left = std::max(left, m_scissor ? m_clip[0] : 0);
			top = std::max(top, m_scissor ? m_clip[1] : 0);
			right = std::min(right, m_scissor ? m_clip[2] : kWidth);
			bottom = std::min(bottom, m_scissor ? m_clip[3] : kHeight);
			left = std::max(left, 0), top = std::max(top, 0), right = std::min(right, kWidth), bottom = std::min(bottom, kHeight);
			for (int y = top; y < bottom; y++)
				for (int x = left; x < right; x++)
				{
					const double px = x + 0.5 + 1e-5, py = y + 0.5 + 2e-5;
					const double wa = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
					const double wb = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
					const double wc = 1.0 - wa - wb;
					if (wa < 0.0 || wb < 0.0 || wc < 0.0)
						continue;
					auto mix = [&](float va, float vb, float vc) { return (float)(va * wa + vb * wb + vc * wc); };
					float r = mix(a.colour.red, b.colour.red, c.colour.red);
					float g = mix(a.colour.green, b.colour.green, c.colour.green);
					float bl = mix(a.colour.blue, b.colour.blue, c.colour.blue);
					float alpha = mix(a.colour.alpha, b.colour.alpha, c.colour.alpha) / 255.0f;
					if (texture)
					{
						const float u = mix(a.tex_coord.x, b.tex_coord.x, c.tex_coord.x), v = mix(a.tex_coord.y, b.tex_coord.y, c.tex_coord.y);
						const int tx = std::clamp((int)std::floor(u * texture->width), 0, texture->width - 1);
						const int ty = std::clamp((int)std::floor(v * texture->height), 0, texture->height - 1);
						const uint8_t* texel = &texture->bgra[((size_t)ty * texture->width + tx) * 4];
						bl *= texel[0] / 255.0f;
						g *= texel[1] / 255.0f;
						r *= texel[2] / 255.0f;
						alpha *= texel[3] / 255.0f;
					}
					if (alpha > 0.0f)
						Blend(&s_frame[((size_t)y * kWidth + x) * 4], bl, g, r, alpha);
				}
		}

		bool m_scissor = false;
		std::array<int, 4> m_clip{0, 0, kWidth, kHeight};
	};

	class System final : public Rml::SystemInterface
	{
	public:
		double GetElapsedTime() override { return s_timeUs / 1000000.0; }
		bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
		{
			if (type <= Rml::Log::LT_WARNING)
				std::fprintf(stderr, "[ui] %s\n", message.c_str());
			return true;
		}
		void JoinPath(Rml::String& translated, const Rml::String& document, const Rml::String& path) override
		{
			if (!path.empty() && path[0] == '/')
				translated = path;
			else
				Rml::SystemInterface::JoinPath(translated, document, path);
		}
	};

	class Files final : public Rml::FileInterface
	{
	public:
		Rml::FileHandle Open(const Rml::String& path) override
		{
			const std::string resolved = !path.empty() && path[0] == '/' ? path : ps5ui::AssetPath(path);
			std::FILE* file = std::fopen(resolved.c_str(), "rb");
			if (!file)
				std::fprintf(stderr, "[ui] cannot open %s\n", resolved.c_str());
			return reinterpret_cast<Rml::FileHandle>(file);
		}
		void Close(Rml::FileHandle file) override { std::fclose(reinterpret_cast<std::FILE*>(file)); }
		size_t Read(void* buffer, size_t size, Rml::FileHandle file) override { return std::fread(buffer, 1, size, reinterpret_cast<std::FILE*>(file)); }
		bool Seek(Rml::FileHandle file, long offset, int origin) override { return std::fseek(reinterpret_cast<std::FILE*>(file), offset, origin) == 0; }
		size_t Tell(Rml::FileHandle file) override { return (size_t)std::ftell(reinterpret_cast<std::FILE*>(file)); }
	};

	// The background in the columns from left to right: as ui_host.cpp draws it.
	void DrawBubbles(ps5ui::Bubbles& bubbles, int left, int right)
	{
		static const std::vector<uint8_t> gradient = ps5ui::Bubbles::Gradient();
		static std::map<int, std::vector<uint8_t>> discs;
		for (int y = 0; y < kHeight; y++)
			std::copy(&gradient[((size_t)y * kWidth + left) * 4], &gradient[((size_t)y * kWidth + right) * 4], &s_frame[((size_t)y * kWidth + left) * 4]);
		for (const auto& bubble : bubbles.List())
		{
			auto& disc = discs[bubble.radius];
			if (disc.empty())
				disc = ps5ui::Bubbles::Disc(bubble.radius);
			const int size = ps5ui::Bubbles::DiscSize(bubble.radius);
			const int bubbleLeft = (int)std::lround(bubble.x) - size / 2, top = (int)std::lround(bubble.y) - size / 2;
			for (int y = 0; y < size; y++)
				for (int x = 0; x < size; x++)
				{
					const int fx = bubbleLeft + x, fy = top + y;
					if (fx < left || fy < 0 || fx >= right || fy >= kHeight)
						continue;
					const float alpha = bubble.alpha * disc[(size_t)y * size + x] / 255.0f;
					const uint8_t* colour = ps5ui::Bubbles::kColour;
					if (alpha > 0.0f)
						Blend(&s_frame[((size_t)fy * kWidth + fx) * 4], colour[0], colour[1], colour[2], alpha);
				}
		}
	}

	void DrawWave(const ps5ui::Wave& wave, int left, int right)
	{
		static const std::vector<uint8_t> gradient = ps5ui::Wave::Gradient();
		static std::vector<std::vector<uint8_t>> pictures;
		if (pictures.empty())
			for (int layer = 0; layer < ps5ui::Wave::kLayers; layer++)
				pictures.push_back(ps5ui::Wave::Picture(layer));
		for (int y = 0; y < kHeight; y++)
			std::copy(&gradient[((size_t)y * kWidth + left) * 4], &gradient[((size_t)y * kWidth + right) * 4], &s_frame[((size_t)y * kWidth + left) * 4]);
		for (int layer = 0; layer < ps5ui::Wave::kLayers; layer++)
		{
			const auto& info = ps5ui::Wave::LayerOf(layer);
			const int width = ps5ui::Wave::PictureWidth(layer), offset = wave.Offset(layer);
			for (int y = 0; y < info.height; y++)
				for (int x = left; x < right; x++)
				{
					const uint8_t* p = &pictures[layer][((size_t)y * width + offset + x) * 4];
					if (p[3])
						Blend(&s_frame[((size_t)(info.top + y) * kWidth + x) * 4], p[0], p[1], p[2], p[3] / 255.0f);
				}
		}
	}

	void WritePng(const std::string& path)
	{
		std::vector<uint8_t> raw;
		raw.reserve((size_t)kHeight * (kWidth * 3 + 1));
		for (int y = 0; y < kHeight; y++)
		{
			raw.push_back(0);
			for (int x = 0; x < kWidth; x++)
			{
				const uint8_t* p = &s_frame[((size_t)y * kWidth + x) * 4];
				raw.insert(raw.end(), {p[2], p[1], p[0]});
			}
		}
		uLongf size = compressBound(raw.size());
		std::vector<uint8_t> packed(size);
		compress2(packed.data(), &size, raw.data(), raw.size(), 6);
		packed.resize(size);
		std::ofstream out(path, std::ios::binary);
		auto chunk = [&](const char* type, const std::vector<uint8_t>& data) {
			const uint8_t length[4] = {(uint8_t)(data.size() >> 24), (uint8_t)(data.size() >> 16), (uint8_t)(data.size() >> 8), (uint8_t)data.size()};
			out.write((const char*)length, 4);
			std::vector<uint8_t> body(type, type + 4);
			body.insert(body.end(), data.begin(), data.end());
			out.write((const char*)body.data(), body.size());
			const uLong crc = crc32(0, body.data(), body.size());
			const uint8_t crcBytes[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
			out.write((const char*)crcBytes, 4);
		};
		out.write("\x89PNG\r\n\x1a\n", 8);
		chunk("IHDR", {0, 0, kWidth >> 8, kWidth & 255, 0, 0, kHeight >> 8, kHeight & 255, 8, 2, 0, 0, 0});
		chunk("IDAT", packed);
		chunk("IEND", {});
		std::printf("%s\n", path.c_str());
	}

	// -- the script ------------------------------------------------------------------------------

	struct Step
	{
		enum Kind
		{
			Press,
			Hold,
			Wait,
			Shot,
		} kind;
		uint32_t buttons = 0;
		int frames = 0;
		std::string name;
	};
	std::vector<Step> s_script;
	size_t s_step = 0;
	int s_stepFrame = 0;

	uint32_t ButtonMask(const std::string& name)
	{
		static const std::map<std::string, uint32_t> kNames = {
			{"up", ps5pad::kUp}, {"down", ps5pad::kDown}, {"left", ps5pad::kLeft}, {"right", ps5pad::kRight},
			{"cross", ps5pad::kCross}, {"circle", ps5pad::kCircle}, {"square", ps5pad::kSquare}, {"triangle", ps5pad::kTriangle},
			{"l1", ps5pad::kL1}, {"r1", ps5pad::kR1}, {"l2", ps5pad::kL2}, {"r2", ps5pad::kR2}, {"l3", ps5pad::kL3},
			{"r3", ps5pad::kR3}, {"options", ps5pad::kOptions}, {"create", ps5pad::kCreate}, {"touchpad", ps5pad::kTouchPad},
			{"ls-up", kLeftUp}, {"ls-down", kLeftDown}, {"ls-left", kLeftLeft}, {"ls-right", kLeftRight},
			{"rs-up", kRightUp}, {"rs-down", kRightDown}, {"rs-left", kRightLeft}, {"rs-right", (uint32_t)kRightRight},
		};
		const auto it = kNames.find(name);
		if (it == kNames.end())
		{
			std::fprintf(stderr, "unknown button %s\n", name.c_str());
			std::exit(2);
		}
		return it->second;
	}

	void LoadScript(const std::string& path)
	{
		std::ifstream file(path);
		if (!file)
		{
			std::fprintf(stderr, "cannot read %s\n", path.c_str());
			std::exit(2);
		}
		std::string line;
		while (std::getline(file, line))
		{
			line = line.substr(0, line.find('#'));
			std::istringstream words(line);
			std::string command;
			if (!(words >> command))
				continue;
			Step step{};
			if (command == "press")
			{
				// one press after another; buttons joined by + together
				for (std::string buttons; words >> buttons;)
				{
					Step press{Step::Press};
					for (size_t start = 0; start <= buttons.size();)
					{
						const size_t plus = std::min(buttons.find('+', start), buttons.size());
						press.buttons |= ButtonMask(buttons.substr(start, plus - start));
						start = plus + 1;
					}
					s_script.push_back(press);
				}
				continue;
			}
			else if (command == "hold")
			{
				std::string button;
				words >> button >> step.frames;
				step.kind = Step::Hold;
				step.buttons = ButtonMask(button);
			}
			else if (command == "wait")
			{
				step.kind = Step::Wait;
				words >> step.frames;
			}
			else if (command == "shot")
			{
				step.kind = Step::Shot;
				words >> step.name;
			}
			else
			{
				std::fprintf(stderr, "unknown command %s\n", command.c_str());
				std::exit(2);
			}
			s_script.push_back(step);
		}
	}

	// After each frame: what the buttons are for the next, and the shots.
	void AdvanceScript()
	{
		for (;;)
		{
			if (s_step >= s_script.size())
				std::exit(0);
			const Step& step = s_script[s_step];
			if (step.kind == Step::Shot)
			{
				WritePng(s_output + "/" + step.name + ".png");
				s_step++;
				continue;
			}
			const int down = step.kind == Step::Press ? 3 : step.kind == Step::Hold ? step.frames : 0;
			const int total = step.kind == Step::Wait ? step.frames : down + 3;
			if (s_stepFrame >= total)
			{
				s_step++;
				s_stepFrame = 0;
				continue;
			}
			s_buttons = s_stepFrame < down ? step.buttons : 0;
			s_stepFrame++;
			return;
		}
	}

	struct Host
	{
		System system;
		Files files;
		BitmapFontEngine fonts;
		Renderer render;
		ps5ui::Bubbles bubbles;
		ps5ui::Wave wave;
		ps5ui::Scene scene = ps5ui::Scene::Both;
		Rml::Context* context = nullptr;
		std::map<std::string, Rml::ElementDocument*> documents;
	};
	Host* s_host = nullptr;
}

// -- the launcher's host (frontend/ui_host.h) ----------------------------------------------------

namespace ps5ui
{
	std::string AssetPath(const std::string& relative) { return s_ui + "/" + relative; }

	bool Start(std::string& error)
	{
		s_host = new Host();
		Rml::SetSystemInterface(&s_host->system);
		Rml::SetFileInterface(&s_host->files);
		Rml::SetRenderInterface(s_host->render.GetAdaptedInterface());
		Rml::SetFontEngineInterface(&s_host->fonts);
		Rml::Initialise();
		for (const char* size : kFonts)
			if (!Rml::LoadFontFace(AssetPath(fmt::format("fonts/lvgl-bitmap/Montserrat-{}.fnt", size))))
			{
				error = "missing font";
				return false;
			}
		s_host->context = Rml::CreateContext("preview", {kWidth, kHeight});
		return true;
	}

	Rml::ElementDocument* Show(const std::string& name)
	{
		auto it = s_host->documents.find(name);
		if (it == s_host->documents.end())
		{
			Rml::ElementDocument* document = s_host->context->LoadDocument(AssetPath(name));
			if (!document)
				return nullptr;
			it = s_host->documents.emplace(name, document).first;
		}
		for (auto& [other, document] : s_host->documents)
			if (other != name)
				document->Hide();
		it->second->Show();
		return it->second;
	}

	void SetScene(Scene scene) { s_host->scene = scene; }

	void Frame()
	{
		s_host->context->Update();
		s_host->bubbles.Advance(1.0 / 60.0);
		s_host->wave.Advance(1.0 / 60.0);
		const Scene scene = s_host->scene;
		if (scene != Scene::Wave)
			DrawBubbles(s_host->bubbles, 0, scene == Scene::Both ? kWidth / 2 : kWidth);
		if (scene != Scene::Bubbles)
			DrawWave(s_host->wave, scene == Scene::Both ? kWidth / 2 : 0, kWidth);
		s_host->context->Render();
		s_timeUs += 16667;
		AdvanceScript();
	}

	void Stop() {}
}

// -- the console, in brief -------------------------------------------------------------------------

extern "C"
{
	int32_t sceKernelGetdents(int fd, char* buffer, int length)
	{
		// Linux's getdents64 records are laid out as its struct dirent, which the launcher reads them as
		return (int32_t)syscall(SYS_getdents64, fd, buffer, length);
	}
	uint64_t sceKernelGetProcessTime() { return s_timeUs; }
	int32_t sceKernelUsleep(uint32_t) { return 0; }
	int32_t sceSystemServiceHideSplashScreen() { return 0; }
}

bool PS5_JitAvailable() { return true; }

namespace ps5log
{
	void Open(const char*) {}
	void Write(std::string_view line) { std::fprintf(stderr, "%.*s\n", (int)line.size(), line.data()); }
	const char* Path() { return ""; }
}

namespace ps5notify
{
	void Send(const std::string& message) { std::fprintf(stderr, "[notify] %s\n", message.c_str()); }
}

namespace ps5pad
{
	bool Init() { return true; }
	void Shutdown() {}
	void Rescan() {}
	bool Read(int player, Data& out)
	{
		if (player != 0)
			return false;
		out = {};
		out.buttons = s_buttons & 0x00ffffffu;
		out.leftX = s_buttons & kLeftLeft ? 0 : s_buttons & kLeftRight ? 255 : 128;
		out.leftY = s_buttons & kLeftUp ? 0 : s_buttons & kLeftDown ? 255 : 128;
		out.rightX = s_buttons & kRightLeft ? 0 : s_buttons & kRightRight ? 255 : 128;
		out.rightY = s_buttons & kRightUp ? 0 : s_buttons & kRightDown ? 255 : 128;
		out.l2 = s_buttons & kL2 ? 255 : 0;
		out.r2 = s_buttons & kR2 ? 255 : 0;
		out.connected = 1;
		return true;
	}
	bool IsConnected(int player) { return player == 0; }
	void TouchResolution(int, float& width, float& height) { width = 1920, height = 1070; }
	void SetVibration(int, uint8_t, uint8_t) {}
	void SetVibrationEnabled(bool) {}
	void SetLightBar(int, uint8_t, uint8_t, uint8_t) {}
	Filtered FilterShortcuts(int, uint32_t buttons) { return {buttons, false}; }
	Shortcut TakeShortcut() { return Shortcut::None; }
}

// -- GameTDB's box art, in brief: what the preview's own folder has ---------------------------------
// (build/preview/boxart/<wiiu|3ds>/<ID>.tga, put there by hand: GameTDB's covers are not ours to
// keep in the repository)

namespace ps5boxart
{
	std::string Path(System system, const std::string& id)
	{
		const std::string path = s_output + "/boxart/" + (system == System::WiiU ? "wiiu/" : "3ds/") + id + ".tga";
		return !id.empty() && std::ifstream(path).good() ? path : std::string();
	}
	void Fetch(System, const std::vector<std::string>&) {}
	uint32_t Arrivals() { return 0; }
	void SetEnabled(bool) {}
	bool ImageSize(const std::string& path, int& width, int& height)
	{
		unsigned char header[18];
		std::ifstream in(path, std::ios::binary);
		if (!in.read((char*)header, sizeof(header)))
			return false;
		width = header[12] | header[13] << 8;
		height = header[14] | header[15] << 8;
		return width > 0 && height > 0;
	}
}

// -- Cemu, in brief: sample games, packs and controllers ------------------------------------------

namespace ps5emu
{
	namespace
	{
		constexpr uint64_t kBreathOfTheWild = 0x00050000101C9400;

		struct SamplePreset
		{
			std::string category, name, needs; // needs: the aspect ratio it is listed for
		};
		struct SamplePack
		{
			GraphicPackInfo info;
			std::vector<SamplePreset> presets;
			std::map<std::string, std::string> active;
		};

		std::vector<SamplePack> MakePacks()
		{
			std::vector<SamplePack> packs;
			auto add = [&](std::string folder, std::string name, std::string description, bool enabled, std::vector<SamplePreset> presets = {}) {
				SamplePack pack;
				pack.info = {name, folder, description, enabled, {}};
				pack.presets = std::move(presets);
				for (const auto& preset : pack.presets)
					if (!pack.active.count(preset.category) && (preset.needs.empty() || preset.needs == "16:9 (Default)"))
						pack.active[preset.category] = preset.name;
				packs.push_back(std::move(pack));
			};
			std::vector<SamplePreset> graphics;
			for (const char* ratio : {"16:9 (Default)", "16:10", "21:9", "32:9", "4:3"})
				graphics.push_back({"Aspect Ratio", ratio, ""});
			for (const char* r : {"320x180", "640x360", "960x540", "1280x720 (HD, Default)", "1600x900 (HD+)", "1920x1080 (Full HD)",
					 "2560x1440 (2K)", "3200x1800", "3840x2160 (4K)", "5120x2880 (5K)", "7680x4320 (8K)"})
				graphics.push_back({"Resolution", r, "16:9 (Default)"});
			for (const char* r : {"1280x800", "1440x900", "1680x1050", "1920x1200", "2560x1600"})
				graphics.push_back({"Resolution", r, "16:10"});
			for (const char* r : {"1720x720", "2560x1080", "3440x1440", "5120x2160"})
				graphics.push_back({"Resolution", r, "21:9"});
			for (const char* r : {"3840x1080", "5120x1440"})
				graphics.push_back({"Resolution", r, "32:9"});
			for (const char* r : {"800x600", "1024x768", "1600x1200"})
				graphics.push_back({"Resolution", r, "4:3"});
			for (const char* s : {"Low (0.5x)", "Medium (100%, Default)", "High (200%)", "Very High (300%)", "Ultra (400%)"})
				graphics.push_back({"Shadows", s, ""});
			for (const char* a : {"Disabled", "Normal FXAA (Default)", "Enhanced FXAA", "Ultra FXAA"})
				graphics.push_back({"Anti-Aliasing", a, ""});
			add("", "Graphics", "Allows you to change the game resolution, shadow resolution and anti-aliasing.\nMade by Kiri, Skalfate, rajkosto and NAVras.", false, graphics);
			add("", "Enhancements", "Adjusts the lighting, the colours and the sharpness of the picture.\n", false,
				{{"Lighting", "Original (Default)", ""}, {"Lighting", "High", ""}, {"Contrast", "Original (Default)", ""}, {"Contrast", "Slightly higher", ""}, {"Saturation", "Original (Default)", ""}, {"Saturation", "Vibrant", ""}});
			add("Mods", "FPS++", "Unlocks the frame rate. Needs a powerful machine.\nMade by Epigramx, Xalphenos, rajkosto, Crementif and Exzap.", true,
				{{"Mode", "Advanced Settings", ""}, {"Mode", "Simple", ""}, {"Framerate Limit", "30FPS (ideal for 240/120/60Hz displays)", ""}, {"Framerate Limit", "60FPS (ideal for 240/120/60Hz displays)", ""}});
			add("Mods", "Extended Memory", "More memory for the game's heap, for mods that need it.\n", false);
			add("Mods", "Remove Fog", "Removes the fog in the distance.\n", false);
			add("Mods", "Draw Distance", "Draws more of the world in the distance.\n", false, {{"Distance", "Normal (Default)", ""}, {"Distance", "High", ""}, {"Distance", "Ultra", ""}});
			add("Mods", "Day Length", "Makes days in Hyrule longer.\n", false, {{"", "x2", ""}, {"", "x4", ""}});
			add("Workarounds", "AMD Shader Crash", "Fixes a crash on AMD graphics.\n", true);
			add("Workarounds", "Grass Workaround", "Fixes flickering grass.\n", false);
			add("Cheats", "Infinite Stamina", "Your stamina never runs out.\n", false);
			add("Cheats", "Infinite Hearts", "Your hearts never run out.\n", false);
			return packs;
		}

		std::vector<SamplePack>& Packs(uint64_t titleId)
		{
			static std::map<uint64_t, std::vector<SamplePack>> packs;
			auto& list = packs[titleId];
			if (list.empty() && titleId == kBreathOfTheWild)
				list = MakePacks();
			return list;
		}

		void FillChoices(SamplePack& pack)
		{
			pack.info.choices.clear();
			const std::string ratio = pack.active.count("Aspect Ratio") ? pack.active["Aspect Ratio"] : "";
			for (const auto& preset : pack.presets)
			{
				if (!preset.needs.empty() && preset.needs != ratio)
					continue;
				auto it = std::find_if(pack.info.choices.begin(), pack.info.choices.end(), [&](auto& c) { return c.category == preset.category; });
				if (it == pack.info.choices.end())
				{
					pack.info.choices.push_back({preset.category, {}, 0});
					it = pack.info.choices.end() - 1;
				}
				if (pack.active[preset.category] == preset.name)
					it->active = (int)it->presets.size();
				it->presets.push_back(preset.name);
			}
		}

		std::array<PlayerControls, 4> s_players = [] {
			std::array<PlayerControls, 4> players{};
			players[0] = {EmulatedType::GamePad, true, true, true, 50, 25, 25};
			for (int i = 1; i < 4; i++)
				players[i] = {EmulatedType::Pro, false, false, false, 0, 25, 25};
			return players;
		}();
		std::array<std::vector<ButtonMapping>, 4> s_mappings;

		std::vector<ButtonMapping>& Mappings(int player)
		{
			auto& list = s_mappings[player];
			if (list.empty())
				list = {{"A", "Circle"}, {"B", "Cross"}, {"X", "Triangle"}, {"Y", "Square"}, {"L", "L1"}, {"R", "R1"}, {"ZL", "L2"},
					{"ZR", "R2"}, {"+ (Start)", "Options"}, {"- (Select)", "Create"}, {"D-pad up", "D-pad up"}, {"D-pad down", "D-pad down"},
					{"D-pad left", "D-pad left"}, {"D-pad right", "D-pad right"}, {"Left stick up", "Left stick up"},
					{"Left stick down", "Left stick down"}, {"Left stick left", "Left stick left"}, {"Left stick right", "Left stick right"},
					{"Left stick click", "L3"}, {"Right stick up", "Right stick up"}, {"Right stick down", "Right stick down"},
					{"Right stick left", "Right stick left"}, {"Right stick right", "Right stick right"}, {"Right stick click", "R3"},
					{"Home", ""}, {"Blow into the mic", ""}, {"Show the GamePad's screen", ""}};
			return list;
		}

		InstallStatus s_install;
	}

	bool Scanning() { return false; }
	void ApplyOptions(const Options&) {}
	void Rescan() {}
	std::string CoverPath(uint64_t) { return ""; }

	std::vector<Game> ListGames()
	{
		std::vector<Game> games;
		auto add = [&](uint64_t id, const char* name, uint16_t version, bool update, uint32_t dlc, const char* format, const char* box = "") {
			games.push_back({id, name, std::string("/data/ps5cemu/games/") + name + ".wua", version, update, dlc, format});
			games.back().gameId = box;
		};
		add(0x0005000010110E00, "Bayonetta 2", 0, false, 0, "WUA");
		add(0x0005000010138300, "Donkey Kong Country: Tropical Freeze", 17, true, 0, "WUX");
		add(0x0005000010180700, "Captain Toad: Treasure Tracker", 0, false, 1, "FOLDER");
		add(0x0005000010145D00, "Super Mario 3D World", 0, false, 0, "WUA");
		add(0x000500001010EC00, "Mario Kart 8", 64, true, 2, "WUA", "AMKE01");
		add(0x0005000010101D00, "New Super Mario Bros. U + New Super Luigi U", 0, false, 1, "WUD");
		add(0x0005000010176900, "Splatoon", 288, true, 0, "WUA");
		add(kBreathOfTheWild, "The Legend of Zelda: Breath of the Wild", 208, true, 1, "WUA", "ALZE01");
		add(0x0005000010143500, "The Legend of Zelda: The Wind Waker HD", 0, false, 0, "FOLDER");
		add(0x000500001014B800, "Xenoblade Chronicles X", 33, true, 1, "WUX");
		add(0x0005000010172600, "Pikmin 3", 0, false, 0, "NUS");
		std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return a.name < b.name; });
		return games;
	}

	std::vector<GraphicPackInfo> ListGraphicPacks(uint64_t titleId)
	{
		std::vector<GraphicPackInfo> result;
		for (auto& pack : Packs(titleId))
		{
			FillChoices(pack);
			result.push_back(pack.info);
		}
		return result;
	}

	int EnabledGraphicPackCount(uint64_t titleId)
	{
		int count = 0;
		for (auto& pack : Packs(titleId))
			count += pack.info.enabled;
		return count;
	}

	bool ToggleGraphicPack(uint64_t titleId, size_t index)
	{
		auto& packs = Packs(titleId);
		return index < packs.size() && (packs[index].info.enabled = !packs[index].info.enabled);
	}

	void SetGraphicPackPreset(uint64_t titleId, size_t index, const std::string& category, const std::string& preset)
	{
		auto& packs = Packs(titleId);
		if (index >= packs.size())
			return;
		auto& pack = packs[index];
		pack.active[category] = preset;
		pack.info.enabled = true;
		if (category == "Aspect Ratio")
		{
			// as Cemu's ValidatePresetSelections: the first resolution the new ratio has
			pack.active.erase("Resolution");
			for (const auto& p : pack.presets)
				if (p.category == "Resolution" && p.needs == preset)
				{
					pack.active["Resolution"] = p.name;
					break;
				}
		}
	}

	PlayerControls GetPlayerControls(int player) { return s_players[player]; }
	void SetEmulatedType(int player, EmulatedType type)
	{
		s_players[player].type = type;
		s_players[player].hasMotion = type == EmulatedType::GamePad || type == EmulatedType::Wiimote || type == EmulatedType::Nunchuk;
		s_mappings[player].clear();
	}
	void SetMotion(int player, bool enabled) { s_players[player].motion = enabled; }
	void SetRumble(int player, int percent) { s_players[player].rumble = percent; }
	void SetDeadzones(int player, int left, int right) { s_players[player].leftDeadzone = left, s_players[player].rightDeadzone = right; }
	void ResetControls(int player) { s_mappings[player].clear(); }
	std::vector<ButtonMapping> ListMappings(int player) { return Mappings(player); }
	void SetMapping(int player, size_t index, PadInput input)
	{
		static const char* kNames[] = {"", "Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "L3", "R3", "Create", "Options",
			"D-pad up", "D-pad down", "D-pad left", "D-pad right", "Left stick up", "Left stick down", "Left stick left", "Left stick right",
			"Right stick up", "Right stick down", "Right stick left", "Right stick right"};
		if (index < Mappings(player).size())
			Mappings(player)[index].input = kNames[(int)input];
	}
	void ClearMapping(int player, size_t index)
	{
		if (index < Mappings(player).size())
			Mappings(player)[index].input.clear();
	}

	InstallCandidate InspectInstall(const std::string& folder)
	{
		InstallCandidate candidate;
		std::ifstream meta(folder + "/meta/meta.xml");
		if (!meta)
		{
			candidate.note = "There is no title here: a game, update or DLC to install is a folder with code, content and meta.";
			return candidate;
		}
		const std::string name = folder.substr(folder.find_last_of('/') + 1);
		candidate.name = "The Legend of Zelda: Breath of the Wild";
		candidate.titleId = name.find("DLC") != std::string::npos ? 0x0005000C101C9400 : 0x0005000E101C9400;
		candidate.kind = name.find("DLC") != std::string::npos ? InstallCandidate::Kind::Dlc : InstallCandidate::Kind::Update;
		candidate.version = candidate.kind == InstallCandidate::Kind::Dlc ? 80 : 208;
		candidate.installedVersion = candidate.kind == InstallCandidate::Kind::Update ? 192 : -1;
		return candidate;
	}

	bool StartInstall(const std::string&, std::string&)
	{
		s_install = {InstallStatus::State::Running, 0, 3200000000ull, ""};
		return true;
	}

	InstallStatus GetInstallStatus()
	{
		if (s_install.state == InstallStatus::State::Running)
		{
			s_install.copied += 40000000ull;
			if (s_install.copied >= s_install.total)
				s_install.state = InstallStatus::State::Done;
		}
		return s_install;
	}

	void CancelInstall() { s_install.state = InstallStatus::State::Cancelled; }
}

int main(int argc, char* argv[])
{
	if (argc != 6)
	{
		std::fprintf(stderr, "launcher-preview UI_FOLDER OUTPUT_FOLDER SCRIPT GAMES_FOLDER 3DS_GAMES_FOLDER\n");
		return 2;
	}
	s_ui = argv[1];
	s_output = argv[2];
	LoadScript(argv[3]);
	ps5settings::Launcher settings;
	settings.gamesFolder = argv[4];
	settings.lastGame = 0x00050000101C9400;
	settings.recent = {0x00050000101C9400, 0x0005000010143500, 0x000500001010EC00, 0x0005000010101D00};
	settings.n3ds.gamesFolder = argv[5];
	settings.n3ds.lastGame = 0x0004000000053F00;
	settings.n3ds.recent = {0x0004000000053F00, 0x0004000000030600, 0x000400000017C100};
	ps5launcher::Status status;
	status.coreReady = true;
	status.diagnostics = {"PS5CEMU-HAR preview: Cemu at 4e3c824, Azahar at 4598458", "Jailbroken by the HEN: /data reachable, JIT memory available",
		"Boot log: /data/ps5cemu/logs/boot.log", "Cemu's log: /data/ps5cemu/log.txt"};
	ps5launcher::Run(settings, status);
	// a game was chosen: the loading screen is on
	WritePng(s_output + "/launch.png");
	return 0;
}
