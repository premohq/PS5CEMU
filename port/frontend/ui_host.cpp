// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher's drawing (ui_host.h).
//
// RmlUi draws through SDL's software renderer into a 1920x1080 window surface, which SDL's PS5
// video driver (pacbrew's SDL2) tiles into VideoOut's buffers and flips at the display's pace. That
// is how ProsperoEden draws this launcher on the console, and its render interface
// (headless/prosperoeden/frontend.cpp, GPL-3.0-or-later, by BlackBearReloaded) is adapted here.
// The GPU stays Cemu's: RmlUi's Vulkan renderer on RADV presented frames, but nothing it drew
// reached them. SDL closes VideoOut again before a game starts, so Cemu's renderer finds it free.
// Under the page is Cemu's background, the Wii U Homebrew Launcher's with its bubbles rising
// (bubbles.h), Azahar's, a yellow 3DS Homebrew Launcher's waves (wave.h), or the two side by side,
// drawn each frame before RmlUi draws.

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "ui_host.h"
#include "bubbles.h"
#include "wave.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include "bitmap_font_engine.h" // ProsperoEden's (headless/prosperoeden)

#include <RmlUi/Core.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <utility>
#include <vector>

extern "C" uint64_t sceKernelGetProcessTime();

// SDL's PS5 video driver opens the keyboard and an OpenGL loader as it starts. The launcher reads
// the DualSense itself (ps5/pad.h) and draws in software, so these stand in for them: neither the
// keyboard and IME dialog modules nor a dynamic loader become imports of the app.
extern "C"
{
	int PS5_Keyboard_Init(void) { return 0; }
	int PS5_Keyboard_Open(void) { return 0; }
	int PS5_Keyboard_PumpEvents(void) { return 0; }
	int PS5_Keyboard_Close(void) { return 0; }
	SDL_bool PS5_HasScreenKeyboardSupport(void*) { return SDL_FALSE; }
	SDL_bool PS5_IsScreenKeyboardShown(void*, SDL_Window*) { return SDL_FALSE; }
	void PS5_ShowScreenKeyboard(void*, SDL_Window*) {}
	void PS5_HideScreenKeyboard(void*, SDL_Window*) {}
	void PS5_OSMesa_InitDevice(void*) {}
}

namespace
{
	constexpr int kWidth = 1920, kHeight = 1080; // main.rml's body, and the window's size
	constexpr const char* kFonts[] = {"20", "24", "28", "32", "36", "40", "48"};

	class SystemInterface final : public Rml::SystemInterface
	{
	public:
		double GetElapsedTime() override
		{
			return sceKernelGetProcessTime() / 1000000.0;
		}

		bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
		{
			if (type <= Rml::Log::LT_WARNING)
				ps5log::Line("[ui] {}", message);
			return true;
		}

		// RmlUi takes a path that starts with '/' as relative to the application and strips the
		// '/'; here it is a path on the console (the covers in /data/ps5cemu/covers).
		void JoinPath(Rml::String& translated, const Rml::String& document, const Rml::String& path) override
		{
			if (!path.empty() && path[0] == '/')
				translated = path;
			else
				Rml::SystemInterface::JoinPath(translated, document, path);
		}
	};

	// Files by absolute path, or relative to the launcher's folder.
	class FileInterface final : public Rml::FileInterface
	{
	public:
		Rml::FileHandle Open(const Rml::String& path) override
		{
			const std::string resolved = !path.empty() && path[0] == '/' ? path : ps5ui::AssetPath(path);
			std::FILE* file = std::fopen(resolved.c_str(), "rb");
			if (!file)
				ps5log::Line("[ui] cannot open {}", resolved);
			return reinterpret_cast<Rml::FileHandle>(file);
		}

		void Close(Rml::FileHandle file) override
		{
			if (file)
				std::fclose(reinterpret_cast<std::FILE*>(file));
		}

		size_t Read(void* buffer, size_t size, Rml::FileHandle file) override
		{
			return std::fread(buffer, 1, size, reinterpret_cast<std::FILE*>(file));
		}

		bool Seek(Rml::FileHandle file, long offset, int origin) override
		{
			return fseeko(reinterpret_cast<std::FILE*>(file), offset, origin) == 0;
		}

		size_t Tell(Rml::FileHandle file) override
		{
			return static_cast<size_t>(ftello(reinterpret_cast<std::FILE*>(file)));
		}
	};

	// RmlUi's geometry through SDL's software renderer, as ProsperoEden's SdlRenderInterface draws it:
	// quads on whole pixels (most of the launcher: images and glyphs) are copied rather than
	// rasterised, and the bitmap fonts' glyphs are composited onto the window surface pixel for pixel.
	class RenderInterface final : public Rml::RenderInterfaceCompatibility
	{
	public:
		void Attach(SDL_Renderer* renderer, SDL_Surface* surface)
		{
			m_renderer = renderer;
			m_surface = surface;
			SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
		}

		void RenderGeometry(Rml::Vertex* vertices, int vertexCount, int* indices, int indexCount,
			Rml::TextureHandle handle, const Rml::Vector2f& translation) override
		{
			Texture* texture = reinterpret_cast<Texture*>(handle);
			if (texture && CopyPixelAlignedQuads(vertices, vertexCount, indices, indexCount, *texture, translation))
				return;
			std::vector<SDL_Vertex> converted(vertexCount);
			for (int i = 0; i < vertexCount; i++)
			{
				const Rml::Vertex& vertex = vertices[i];
				converted[i].position = {vertex.position.x + translation.x, vertex.position.y + translation.y};
				converted[i].color = {vertex.colour.red, vertex.colour.green, vertex.colour.blue, vertex.colour.alpha};
				converted[i].tex_coord = {vertex.tex_coord.x, vertex.tex_coord.y};
			}
			SDL_RenderGeometry(m_renderer, texture ? texture->texture : nullptr, converted.data(), vertexCount, indices, indexCount);
		}

		// The launcher's images are 32-bit top-down TGAs: ProsperoEden's artwork and fonts, the
		// port's icons (tools/render-icons.py) and the covers (app/covers.cpp).
		bool LoadTexture(Rml::TextureHandle& handle, Rml::Vector2i& dimensions, const Rml::String& source) override
		{
			handle = {};
			dimensions = {};
			Rml::FileInterface* files = Rml::GetFileInterface();
			const Rml::FileHandle file = files->Open(source);
			if (!file)
				return false;
			files->Seek(file, 0, SEEK_END);
			std::vector<Rml::byte> data(files->Tell(file));
			files->Seek(file, 0, SEEK_SET);
			const bool read = files->Read(data.data(), data.size(), file) == data.size();
			files->Close(file);
			if (!read || data.size() < 18)
				return false;
			const int width = data[12] | data[13] << 8, height = data[14] | data[15] << 8;
			const size_t bytes = (size_t)width * height * 4;
			if (data[0] != 0 || data[1] != 0 || data[2] != 2 || data[16] != 32 || (data[17] & 0x0f) != 8 ||
				(data[17] & 0x30) != 0x20 || width <= 0 || height <= 0 || data.size() < 18 + bytes)
			{
				ps5log::Line("[ui] {} is not a 32-bit top-down TGA", source);
				return false;
			}
			// pictures are scaled smoothly; the rest is drawn at its own size
			const bool art = source.find("/covers/") != Rml::String::npos;
			const bool glyphs = source.find("lvgl-bitmap") != Rml::String::npos;
			Texture* texture = Create(data.data() + 18, width, height, SDL_PIXELFORMAT_BGRA32,
				art ? SDL_ScaleModeLinear : SDL_ScaleModeNearest, glyphs);
			if (!texture)
				return false;
			handle = reinterpret_cast<Rml::TextureHandle>(texture);
			dimensions = {width, height};
			return true;
		}

		bool GenerateTexture(Rml::TextureHandle& handle, const Rml::byte* source, const Rml::Vector2i& dimensions) override
		{
			Texture* texture = Create(source, dimensions.x, dimensions.y, SDL_PIXELFORMAT_RGBA32, SDL_ScaleModeNearest, false);
			handle = reinterpret_cast<Rml::TextureHandle>(texture);
			return texture != nullptr;
		}

		void ReleaseTexture(Rml::TextureHandle handle) override
		{
			Release(reinterpret_cast<Texture*>(handle));
		}

		void EnableScissorRegion(bool enable) override
		{
			m_scissorEnabled = enable;
			SDL_RenderSetClipRect(m_renderer, enable ? &m_scissor : nullptr);
		}

		void SetScissorRegion(int x, int y, int width, int height) override
		{
			m_scissor = {x, y, width, height};
			if (m_scissorEnabled)
				SDL_RenderSetClipRect(m_renderer, &m_scissor);
		}

	private:
		struct Texture
		{
			SDL_Texture* texture = nullptr;
			int width = 0, height = 0;
			// glyph atlases also as a surface, blitted straight onto the window's
			SDL_Surface* surface = nullptr;
			std::vector<Rml::byte> pixels;
		};

		struct PixelCopy
		{
			SDL_Rect source, destination;
			Rml::ColourbPremultiplied colour;
		};

		Texture* Create(const Rml::byte* pixels, int width, int height, Uint32 format, SDL_ScaleMode scale, bool glyphs)
		{
			SDL_Surface* staging = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<Rml::byte*>(pixels), width, height, 32, width * 4, format);
			if (!staging)
				return nullptr;
			auto* texture = new Texture{SDL_CreateTextureFromSurface(m_renderer, staging), width, height};
			SDL_FreeSurface(staging);
			bool ok = texture->texture && SDL_SetTextureBlendMode(texture->texture, SDL_BLENDMODE_BLEND) == 0 &&
				SDL_SetTextureScaleMode(texture->texture, scale) == 0;
			if (ok && glyphs)
			{
				texture->pixels.assign(pixels, pixels + (size_t)width * height * 4);
				texture->surface = SDL_CreateRGBSurfaceWithFormatFrom(texture->pixels.data(), width, height, 32, width * 4, format);
				ok = texture->surface && SDL_SetSurfaceBlendMode(texture->surface, SDL_BLENDMODE_BLEND) == 0;
			}
			if (!ok)
			{
				Release(texture);
				return nullptr;
			}
			return texture;
		}

		static void Release(Texture* texture)
		{
			if (!texture)
				return;
			SDL_FreeSurface(texture->surface);
			if (texture->texture)
				SDL_DestroyTexture(texture->texture);
			delete texture;
		}

		static int RoundPixel(float value)
		{
			return static_cast<int>(value + (value >= 0.0f ? 0.5f : -0.5f));
		}

		static bool SameColour(const Rml::Vertex& a, const Rml::Vertex& b)
		{
			return a.colour.red == b.colour.red && a.colour.green == b.colour.green && a.colour.blue == b.colour.blue &&
				a.colour.alpha == b.colour.alpha;
		}

		// Quads RmlUi makes for images and glyphs (two triangles each, axis-aligned, one colour) as
		// rectangle copies. False when the geometry is anything else.
		bool CopyPixelAlignedQuads(Rml::Vertex* vertices, int vertexCount, int* indices, int indexCount, Texture& texture,
			const Rml::Vector2f& translation)
		{
			if (vertexCount <= 0 || indexCount <= 0 || indexCount % 6 != 0)
				return false;
			std::vector<PixelCopy> copies;
			copies.reserve(indexCount / 6);
			for (int index = 0; index < indexCount; index += 6)
			{
				const int i0 = indices[index], i3 = indices[index + 1], i1 = indices[index + 2], i2 = indices[index + 5];
				if (i0 < 0 || i0 >= vertexCount || i1 < 0 || i1 >= vertexCount || i2 < 0 || i2 >= vertexCount || i3 < 0 ||
					i3 >= vertexCount || indices[index + 3] != i1 || indices[index + 4] != i3 || i0 == i1 || i0 == i2 ||
					i0 == i3 || i1 == i2 || i1 == i3 || i2 == i3)
					return false;
				const Rml::Vertex &v0 = vertices[i0], &v1 = vertices[i1], &v2 = vertices[i2], &v3 = vertices[i3];
				if (v0.position.y != v1.position.y || v1.position.x != v2.position.x || v2.position.y != v3.position.y ||
					v3.position.x != v0.position.x || v0.tex_coord.y != v1.tex_coord.y || v1.tex_coord.x != v2.tex_coord.x ||
					v2.tex_coord.y != v3.tex_coord.y || v3.tex_coord.x != v0.tex_coord.x || !SameColour(v0, v1) ||
					!SameColour(v0, v2) || !SameColour(v0, v3))
					return false;
				const SDL_Rect source{RoundPixel(v0.tex_coord.x * texture.width), RoundPixel(v0.tex_coord.y * texture.height),
					RoundPixel((v1.tex_coord.x - v0.tex_coord.x) * texture.width),
					RoundPixel((v3.tex_coord.y - v0.tex_coord.y) * texture.height)};
				const SDL_Rect destination{RoundPixel(v0.position.x + translation.x), RoundPixel(v0.position.y + translation.y),
					RoundPixel(v1.position.x - v0.position.x), RoundPixel(v3.position.y - v0.position.y)};
				if (source.w <= 0 || source.h <= 0 || destination.w <= 0 || destination.h <= 0 || source.x < 0 || source.y < 0 ||
					source.x + source.w > texture.width || source.y + source.h > texture.height)
					return false;
				copies.push_back({source, destination, v0.colour});
			}
			if (texture.surface)
				return CompositeGlyphs(texture, copies);
			for (const PixelCopy& copy : copies)
			{
				SDL_SetTextureColorMod(texture.texture, copy.colour.red, copy.colour.green, copy.colour.blue);
				SDL_SetTextureAlphaMod(texture.texture, copy.colour.alpha);
				SDL_RenderCopy(m_renderer, texture.texture, &copy.source, &copy.destination);
			}
			SDL_SetTextureColorMod(texture.texture, 255, 255, 255);
			SDL_SetTextureAlphaMod(texture.texture, 255);
			return true;
		}

		bool CompositeGlyphs(Texture& texture, const std::vector<PixelCopy>& copies)
		{
			if (!m_surface)
				return false;
			// what the renderer has queued lands on the surface first
			SDL_RenderFlush(m_renderer);
			SDL_Rect previousClip{};
			SDL_GetClipRect(m_surface, &previousClip);
			SDL_SetClipRect(m_surface, m_scissorEnabled ? &m_scissor : nullptr);
			for (const PixelCopy& copy : copies)
			{
				SDL_SetSurfaceColorMod(texture.surface, copy.colour.red, copy.colour.green, copy.colour.blue);
				SDL_SetSurfaceAlphaMod(texture.surface, copy.colour.alpha);
				SDL_Rect destination = copy.destination;
				SDL_BlitSurface(texture.surface, &copy.source, m_surface, &destination);
			}
			SDL_SetSurfaceColorMod(texture.surface, 255, 255, 255);
			SDL_SetSurfaceAlphaMod(texture.surface, 255);
			SDL_SetClipRect(m_surface, &previousClip);
			return true;
		}

		SDL_Renderer* m_renderer = nullptr;
		SDL_Surface* m_surface = nullptr;
		SDL_Rect m_scissor{};
		bool m_scissorEnabled = false;
	};

	// The backgrounds. Cemu's: its gradient, and one picture of a bubble for each radius, drawn where
	// each bubble is with its alpha. Azahar's: its gradient, and each wave's picture, drawn from
	// where the wave has got to.
	class Background
	{
	public:
		bool Create(SDL_Renderer* renderer)
		{
			const auto gradient = ps5ui::Bubbles::Gradient();
			m_gradient = Texture(renderer, gradient.data(), ps5ui::Bubbles::kWidth, ps5ui::Bubbles::kHeight, SDL_BLENDMODE_NONE);
			const auto waveGradient = ps5ui::Wave::Gradient();
			m_waveGradient = Texture(renderer, waveGradient.data(), ps5ui::Wave::kWidth, ps5ui::Wave::kHeight, SDL_BLENDMODE_NONE);
			if (!m_gradient || !m_waveGradient)
				return false;
			for (int layer = 0; layer < ps5ui::Wave::kLayers; layer++)
			{
				const auto picture = ps5ui::Wave::Picture(layer);
				m_waves[layer] = Texture(renderer, picture.data(), ps5ui::Wave::PictureWidth(layer), ps5ui::Wave::LayerOf(layer).height,
					SDL_BLENDMODE_BLEND);
				if (!m_waves[layer])
					return false;
			}
			for (int radius = 1; radius <= ps5ui::Bubbles::kMaxRadius; radius++)
			{
				const int size = ps5ui::Bubbles::DiscSize(radius);
				const auto coverage = ps5ui::Bubbles::Disc(radius);
				std::vector<uint8_t> pixels((size_t)size * size * 4);
				for (size_t i = 0; i < coverage.size(); i++)
				{
					std::copy(ps5ui::Bubbles::kColour, ps5ui::Bubbles::kColour + 3, &pixels[i * 4]);
					pixels[i * 4 + 3] = coverage[i];
				}
				m_discs[radius] = Texture(renderer, pixels.data(), size, size, SDL_BLENDMODE_BLEND);
				if (!m_discs[radius])
					return false;
			}
			return true;
		}

		// Both: the bubbles on the left half, the waves on the right.
		void Draw(SDL_Renderer* renderer, double now, ps5ui::Scene scene)
		{
			const double step = m_last > 0.0 ? now - m_last : 0.0;
			m_last = now;
			m_bubbles.Advance(step);
			m_wave.Advance(step);
			const SDL_Rect left{0, 0, ps5ui::Bubbles::kWidth / 2, ps5ui::Bubbles::kHeight};
			const SDL_Rect right{ps5ui::Bubbles::kWidth / 2, 0, ps5ui::Bubbles::kWidth / 2, ps5ui::Bubbles::kHeight};
			if (scene != ps5ui::Scene::Wave)
			{
				if (scene == ps5ui::Scene::Both)
					SDL_RenderSetClipRect(renderer, &left);
				DrawBubbles(renderer);
			}
			if (scene != ps5ui::Scene::Bubbles)
			{
				if (scene == ps5ui::Scene::Both)
					SDL_RenderSetClipRect(renderer, &right);
				DrawWave(renderer);
			}
			SDL_RenderSetClipRect(renderer, nullptr);
		}

		void Destroy()
		{
			for (SDL_Texture** texture : {&m_gradient, &m_waveGradient})
				if (*texture)
					SDL_DestroyTexture(std::exchange(*texture, nullptr));
			for (SDL_Texture*& disc : m_discs)
				if (disc)
					SDL_DestroyTexture(std::exchange(disc, nullptr));
			for (SDL_Texture*& wave : m_waves)
				if (wave)
					SDL_DestroyTexture(std::exchange(wave, nullptr));
		}

	private:
		void DrawBubbles(SDL_Renderer* renderer)
		{
			SDL_RenderCopy(renderer, m_gradient, nullptr, nullptr);
			for (const auto& bubble : m_bubbles.List())
			{
				SDL_Texture* disc = m_discs[bubble.radius];
				const int size = ps5ui::Bubbles::DiscSize(bubble.radius);
				const SDL_Rect where{(int)std::lround(bubble.x) - size / 2, (int)std::lround(bubble.y) - size / 2, size, size};
				SDL_SetTextureAlphaMod(disc, (Uint8)std::lround(bubble.alpha * 255.0f));
				SDL_RenderCopy(renderer, disc, nullptr, &where);
			}
		}

		void DrawWave(SDL_Renderer* renderer)
		{
			SDL_RenderCopy(renderer, m_waveGradient, nullptr, nullptr);
			for (int layer = 0; layer < ps5ui::Wave::kLayers; layer++)
			{
				const auto& wave = ps5ui::Wave::LayerOf(layer);
				const SDL_Rect from{m_wave.Offset(layer), 0, ps5ui::Wave::kWidth, wave.height};
				const SDL_Rect to{0, wave.top, ps5ui::Wave::kWidth, wave.height};
				SDL_RenderCopy(renderer, m_waves[layer], &from, &to);
			}
		}

		static SDL_Texture* Texture(SDL_Renderer* renderer, const uint8_t* bgra, int width, int height, SDL_BlendMode blend)
		{
			SDL_Surface* staging = SDL_CreateRGBSurfaceWithFormatFrom(const_cast<uint8_t*>(bgra), width, height, 32, width * 4, SDL_PIXELFORMAT_BGRA32);
			if (!staging)
				return nullptr;
			SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, staging);
			SDL_FreeSurface(staging);
			if (texture)
				SDL_SetTextureBlendMode(texture, blend);
			return texture;
		}

		ps5ui::Bubbles m_bubbles;
		ps5ui::Wave m_wave;
		double m_last = 0.0;
		SDL_Texture* m_gradient = nullptr;
		std::array<SDL_Texture*, ps5ui::Bubbles::kMaxRadius + 1> m_discs{};
		SDL_Texture* m_waveGradient = nullptr;
		std::array<SDL_Texture*, ps5ui::Wave::kLayers> m_waves{};
	};

	struct Host
	{
		SystemInterface system;
		FileInterface files;
		BitmapFontEngine fonts;
		RenderInterface render;
		Background background;
		bool sdl = false;
		SDL_Window* window = nullptr;
		SDL_Surface* surface = nullptr;
		SDL_Renderer* renderer = nullptr;
		bool rml = false;
		Rml::Context* context = nullptr;
		std::map<std::string, Rml::ElementDocument*> documents;
		ps5ui::Scene scene = ps5ui::Scene::Both;
		uint64_t frames = 0;
	};
	Host* s_host = nullptr;
}

namespace ps5ui
{
	std::string AssetPath(const std::string& relative)
	{
		return ps5paths::Assets() + "/ui/" + relative;
	}

	bool Start(std::string& error)
	{
		if (s_host)
			return true;
		s_host = new Host();
		Host& host = *s_host;
		// first, so that what RmlUi reports while it starts reaches the boot log
		Rml::SetSystemInterface(&host.system);
		SDL_SetMainReady();
		if (SDL_Init(SDL_INIT_VIDEO) != 0)
		{
			error = fmt::format("SDL's video did not start: {}", SDL_GetError());
			Stop();
			return false;
		}
		host.sdl = true;
		SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
		SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
		host.window = SDL_CreateWindow("PS5Cemu", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, kWidth, kHeight, SDL_WINDOW_SHOWN);
		host.surface = host.window ? SDL_GetWindowSurface(host.window) : nullptr;
		host.renderer = host.surface ? SDL_CreateSoftwareRenderer(host.surface) : nullptr;
		if (!host.renderer)
		{
			error = fmt::format("the launcher's window did not open: {}", SDL_GetError());
			Stop();
			return false;
		}
		host.render.Attach(host.renderer, host.surface);
		if (!host.background.Create(host.renderer))
		{
			error = fmt::format("the launcher's background could not be made: {}", SDL_GetError());
			Stop();
			return false;
		}

		Rml::SetFileInterface(&host.files);
		Rml::SetRenderInterface(host.render.GetAdaptedInterface());
		Rml::SetFontEngineInterface(&host.fonts);
		if (!Rml::Initialise())
		{
			error = "RmlUi did not start";
			Stop();
			return false;
		}
		host.rml = true;
		for (const char* size : kFonts)
		{
			if (!Rml::LoadFontFace(AssetPath(fmt::format("fonts/lvgl-bitmap/Montserrat-{}.fnt", size))))
			{
				error = fmt::format("the launcher's font Montserrat-{} is missing", size);
				Stop();
				return false;
			}
		}
		host.context = Rml::CreateContext("ps5cemu", {kWidth, kHeight});
		if (!host.context)
		{
			error = "RmlUi's context could not be made";
			Stop();
			return false;
		}
		ps5log::Line("[ui] launcher started (SDL video driver: {})", SDL_GetCurrentVideoDriver());
		return true;
	}

	Rml::ElementDocument* Show(const std::string& name)
	{
		if (!s_host || !s_host->context)
			return nullptr;
		auto& documents = s_host->documents;
		auto it = documents.find(name);
		if (it == documents.end())
		{
			Rml::ElementDocument* document = s_host->context->LoadDocument(AssetPath(name));
			if (!document)
			{
				ps5log::Line("[ui] the launcher's layout {} did not load", name);
				return nullptr;
			}
			it = documents.emplace(name, document).first;
		}
		for (auto& [other, document] : documents)
			if (other != name)
				document->Hide();
		it->second->Show();
		return it->second;
	}

	void SetScene(Scene scene)
	{
		if (s_host)
			s_host->scene = scene;
	}

	void Frame()
	{
		if (!s_host || !s_host->context)
			return;
		Host& host = *s_host;
		// The first frame step by step, then how long the first frames and one a minute take: a
		// launcher that shows nothing says where it stopped.
		const bool first = host.frames == 0;
		const uint64_t start = sceKernelGetProcessTime();
		auto step = [first](const char* what) {
			if (first)
				ps5log::Line("[ui] first frame: {}", what);
		};
		step("layout");
		host.context->Update();
		step("draw");
		host.background.Draw(host.renderer, start / 1000000.0, host.scene);
		host.context->Render();
		SDL_RenderFlush(host.renderer);
		step("present");
		// returns once VideoOut has flipped to it, which paces the launcher
		if (SDL_UpdateWindowSurface(host.window) != 0 && first)
			ps5log::Line("[ui] the frame did not reach VideoOut: {}", SDL_GetError());
		// the video driver's events; the launcher reads the DualSense itself
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
		}
		const uint64_t frame = ++host.frames;
		if (frame <= 3 || frame % 3600 == 0)
			ps5log::Line("[ui] frame {} took {} ms", frame, (sceKernelGetProcessTime() - start) / 1000);
	}

	void Stop()
	{
		if (!s_host)
			return;
		Host& host = *s_host;
		for (auto& [name, document] : host.documents)
			document->Close();
		if (host.context)
			Rml::RemoveContext("ps5cemu");
		// RmlUi releases its textures through the renderer: SDL goes after it
		if (host.rml)
			Rml::Shutdown();
		Rml::SetSystemInterface(nullptr);
		host.background.Destroy();
		if (host.renderer)
			SDL_DestroyRenderer(host.renderer);
		if (host.window)
			SDL_DestroyWindow(host.window);
		// closes VideoOut, which Cemu's renderer opens next
		if (host.sdl)
			SDL_Quit();
		delete s_host;
		s_host = nullptr;
		ps5log::Line("[ui] launcher stopped");
	}
}
