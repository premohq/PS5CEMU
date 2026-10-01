// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher's drawing (ui_host.h).
//
// This file sees Vulkan through RmlUi's glad header, which may not meet Vulkan's own headers in one
// file, so the display surface comes from vulkan_display.cpp through its type-free entry point.

#include "ui_host.h"
#include "../app/paths.h"
#include "../ps5/log.h"

#include "RmlUi_Renderer_VK.h"
#include "bitmap_font_engine.h" // ProsperoEden's (headless/prosperoeden)

#include <RmlUi/Core.h>

#include <cstdio>

extern "C" bool PS5Vk_CreateDisplaySurfaceRaw(void* instance, uint64_t* surfaceOut);
extern "C" uint64_t sceKernelGetProcessTime();

namespace
{
	constexpr int kWidth = 1920, kHeight = 1080; // main.rml's body
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

	bool CreateSurface(VkInstance instance, VkSurfaceKHR* surface)
	{
		uint64_t handle = 0;
		if (!PS5Vk_CreateDisplaySurfaceRaw(instance, &handle))
			return false;
		*surface = reinterpret_cast<VkSurfaceKHR>(handle);
		return true;
	}

	struct Host
	{
		SystemInterface system;
		FileInterface files;
		BitmapFontEngine fonts;
		RenderInterface_VK render;
		bool vulkan = false;
		bool rml = false;
		Rml::Context* context = nullptr;
		Rml::ElementDocument* document = nullptr;
	};
	Host* s_host = nullptr;
}

namespace ps5ui
{
	std::string AssetPath(const std::string& relative)
	{
		return std::string(ps5paths::kAssets) + "/ui/" + relative;
	}

	bool Start(std::string& error)
	{
		if (s_host)
			return true;
		s_host = new Host();
		Host& host = *s_host;
		// the instance needs the display extension for VideoOut's VK_KHR_display plane
		if (!host.render.Initialize({"VK_KHR_surface", "VK_KHR_display"}, &CreateSurface))
		{
			error = "the launcher's Vulkan renderer did not start";
			Stop();
			return false;
		}
		host.vulkan = true;
		// laid out at 1920x1080, drawn on VideoOut's 3840x2160 swapchain (patches/rmlui/0003)
		host.render.SetLayoutSize(kWidth, kHeight);
		host.render.SetViewport(kWidth, kHeight);
		if (!host.render.IsSwapchainValid())
		{
			error = "VideoOut did not accept the launcher's swapchain";
			Stop();
			return false;
		}

		Rml::SetSystemInterface(&host.system);
		Rml::SetFileInterface(&host.files);
		Rml::SetRenderInterface(&host.render);
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
		host.document = host.context ? host.context->LoadDocument(AssetPath("main.rml")) : nullptr;
		if (!host.document)
		{
			error = "the launcher's layout (assets/ui/main.rml) did not load";
			Stop();
			return false;
		}
		host.document->Show();
		ps5log::Line("[ui] launcher started");
		return true;
	}

	Rml::ElementDocument* Document()
	{
		return s_host ? s_host->document : nullptr;
	}

	void Frame()
	{
		if (!s_host || !s_host->context)
			return;
		s_host->context->Update();
		s_host->render.BeginFrame();
		s_host->context->Render();
		s_host->render.EndFrame();
	}

	void Stop()
	{
		if (!s_host)
			return;
		Host& host = *s_host;
		if (host.document)
			host.document->Close();
		if (host.context)
			Rml::RemoveContext("ps5cemu");
		if (host.rml)
			Rml::Shutdown();
		// RmlUi releases its textures through the renderer: Vulkan goes last
		if (host.vulkan)
			host.render.Shutdown();
		delete s_host;
		s_host = nullptr;
		ps5log::Line("[ui] launcher stopped");
	}
}
