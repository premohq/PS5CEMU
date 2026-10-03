// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: Azahar's core on the PS5 (azahar.h), built in Azahar's tree against its libraries
// (tools/build-azahar.sh) and linked into the app with Cemu.
//
//  - its data under kRoot: the 3DS's storage (sdmc, nand), sysdata (where aes_keys.txt goes for
//    encrypted games), the shader cache, its log (log/azahar_log.txt);
//  - the window is VideoOut, a VK_KHR_display surface (ps5/vulkan_display.h) for Azahar's own
//    Vulkan renderer, whose instance takes RADV's vkGetInstanceProcAddr (the hooks below, which
//    patches/azahar adds to vk_platform.cpp);
//  - the emulation runs on a thread of its own; the game's loop on the main thread reads the
//    DualSense (input.h) and the port's shortcuts (ps5/pad.h), and applies what the in-game menu
//    changes (app/ingame3ds.h, which Azahar's renderer draws over its screens).

// first: Azahar's Vulkan headers, with its options, before the port's, which include vulkan.h plainly
#include "video_core/renderer_vulkan/vk_frontend_overlay.h"

#include "azahar.h"
#include "controls.h"
#include "input.h"
#include "../app/ingame3ds.h"
#include "../ps5/display.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"
#include "../ps5/pad.h"
#include "../ps5/vulkan_display.h"

#include "audio_core/sink_details.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/settings.h"
#include "common/thread.h"
#include "core/core.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/emu_window.h"
#include "core/frontend/image_interface.h"
#include "core/hle/service/am/am.h"
#include "core/hle/service/apt/applet_manager.h"
#include "core/hle/service/apt/apt.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/ir/ir_rst.h"
#include "core/hle/service/ir/ir_user.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/loader/loader.h"
#include "video_core/gpu.h"
#include "video_core/rasterizer_interface.h"
#include "video_core/renderer_base.h"

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>

extern "C" int sceKernelUsleep(unsigned int microseconds);

// What Azahar's vk_platform.cpp asks the PS5 frontend for (patches/azahar).
PFN_vkGetInstanceProcAddr AzaharPS5_GetInstanceProcAddr()
{
	return ps5vk::GetInstanceProcAddr();
}

VkSurfaceKHR AzaharPS5_CreateSurface(VkInstance instance)
{
	std::string error;
	const VkSurfaceKHR surface = ps5vk::CreateDisplaySurface(instance, error);
	if (surface == VK_NULL_HANDLE)
		throw std::runtime_error("PS5: " + error);
	return surface;
}

namespace ps5azahar
{
	namespace
	{
		// VideoOut, whole: the layout places the 3DS's screens on the 4K picture.
		class Window final : public Frontend::EmuWindow
		{
		public:
			Window()
			{
				window_info.type = Frontend::WindowSystemType::PS5;
				window_info.render_surface = this; // not headless: there is a surface
				window_info.render_surface_scale = 1.0f;
				UpdateLayout();
			}

			void UpdateLayout()
			{
				UpdateCurrentFramebufferLayout(ps5display::kWidth, ps5display::kHeight);
			}

			void PollEvents() override {}
			void MakeCurrent() override {}
			void DoneCurrent() override {}

			CursorInfo GetCursorInfo() const override
			{
				CursorInfo cursor;
				float x, y;
				const auto& layout = GetFramebufferLayout();
				// only over a bottom screen that is shown (not with the top screen alone)
				cursor.visible = input::Cursor(x, y) && !ps5ingame3ds::MenuOpen() && layout.bottom_screen_enabled &&
					layout.bottom_screen.GetWidth() > 0;
				// in the bottom screen's pixels on the TV, from its top left corner, as Azahar draws
				// its crosshair: where the touch lands
				cursor.projected_x = x * layout.bottom_screen.GetWidth();
				cursor.projected_y = y * layout.bottom_screen.GetHeight();
				return cursor;
			}
		};

		std::once_flag s_setUp;
		std::unique_ptr<Window> s_window;
		std::thread s_emulation;
		std::atomic_bool s_stop = false;
		std::atomic_bool s_running = false;
		ps5settings::N3ds s_settings; // the game's loop's: as the menu leaves them
		std::string s_name;
		uint64_t s_titleId = 0;
		bool s_coreTouched = false; // LaunchGame got as far as starting Azahar's core

		// Azahar's paths and log, once, before anything of Azahar's runs (a CIA install from the
		// launcher, or a game).
		void SetUp()
		{
			std::call_once(s_setUp, [] {
				FileUtil::SetUserPath(std::string(kRoot) + "/");
				Common::Log::Filter filter;
				filter.ParseFilterString("*:Info");
				Common::Log::Initialize();
				Common::Log::SetGlobalFilter(filter);
				Common::Log::Start();
				// every 3DS system service emulated at a high level, as Azahar's frontends set it up
				// (the services look themselves up in this table when the 3DS starts)
				for (const auto& module : Service::service_module_map)
					Settings::values.lle_modules.emplace(module.name, false);
				ps5log::Line("[azahar] data in {}", FileUtil::GetUserPath(FileUtil::UserPath::UserDir));
			});
		}

		Settings::LayoutOption LayoutOf(int layout)
		{
			switch ((Layout)layout)
			{
			case Layout::Stacked: return Settings::LayoutOption::Default;
			case Layout::TopOnly: return Settings::LayoutOption::SingleScreen;
			case Layout::LargeTop: return Settings::LayoutOption::LargeScreen;
			case Layout::SideBySide: return Settings::LayoutOption::SideScreen;
			}
			return Settings::LayoutOption::LargeScreen;
		}

		// The launcher's 3DS settings in Azahar's.
		void ApplySettings(const ps5settings::N3ds& settings)
		{
			auto& values = Settings::values;
			values.graphics_api = Settings::GraphicsAPI::Vulkan;
			values.physical_device = 0;
			values.use_hw_shader = true;
			values.use_shader_jit = true;
			values.use_disk_shader_cache = true;
			values.async_shader_compilation = true; // a draw waits for no shader: no stutter
			values.async_presentation = true;
			values.use_vsync = true;
			values.use_skip_duplicate_frames = false; // every frame drawn, so the menu is too
			values.frame_limit = 100;
			values.resolution_factor = (u32)std::clamp(settings.resolution, 1, 10);
			values.texture_filter = (Settings::TextureFilter)std::clamp(settings.textureFilter, 0, 5);
			values.layout_option = LayoutOf(settings.layout);
			values.swap_screen = false;
			values.use_cpu_jit = true;
			values.cpu_clock_percentage = 100;
			values.is_new_3ds = true;
			values.output_type = AudioCore::SinkType::PS5;
			values.audio_emulation = Settings::AudioEmulation::HLE;
			values.enable_audio_stretching = true;
			values.volume = std::clamp(settings.volume, 0, 100) / 100.0f;
			values.input_type = AudioCore::InputType::Null;
			values.camera_name.fill("blank"); // the 3DS cameras see nothing: the PS5 has none to lend
			input::Configure(settings);
		}

		// The menu's view of the settings, and the settings it changed
		ps5ingame3ds::Settings MenuSettings(const ps5settings::N3ds& settings)
		{
			ps5ingame3ds::Settings menu;
			menu.layout = settings.layout;
			menu.swapScreens = Settings::values.swap_screen.GetValue();
			menu.resolution = settings.resolution;
			menu.textureFilter = settings.textureFilter;
			menu.volume = settings.volume;
			menu.performance = settings.performance;
			menu.motion = settings.motion;
			menu.deadzone = settings.deadzone;
			menu.aOnCircle = MappedInput(settings, Button::A) == ps5emu::PadInput::Circle;
			return menu;
		}

		void ShowInMenu()
		{
			ps5ingame3ds::Start(s_name, s_titleId, MenuSettings(s_settings));
		}

		// The 3DS's controls again, after a change: as Azahar's ApplySettings reloads them, without
		// its sound output, which it would open again
		void ReloadControls()
		{
			input::Configure(s_settings);
			Core::System& system = Core::System::GetInstance();
			if (!system.IsPoweredOn())
				return;
			if (auto hid = Service::HID::GetModule(system))
				hid->ReloadInputDevices();
			if (auto apt = Service::APT::GetModule(system))
				apt->GetAppletManager()->ReloadInputDevices();
			auto& services = system.ServiceManager();
			if (auto irUser = services.GetService<Service::IR::IR_USER>("ir:USER"))
				irUser->ReloadInputDevices();
			if (auto irRst = services.GetService<Service::IR::IR_RST>("ir:rst"))
				irRst->ReloadInputDevices();
		}

		// What the menu changed, while the game runs; kept in the launcher's settings for the next
		// games
		void ApplyMenu(const ps5ingame3ds::Settings& menu)
		{
			auto& values = Settings::values;
			const bool controls = menu.motion != s_settings.motion || menu.deadzone != s_settings.deadzone ||
				menu.aOnCircle != (MappedInput(s_settings, Button::A) == ps5emu::PadInput::Circle);
			s_settings.layout = menu.layout;
			s_settings.resolution = menu.resolution;
			s_settings.textureFilter = menu.textureFilter;
			s_settings.volume = menu.volume;
			s_settings.performance = menu.performance;
			s_settings.motion = menu.motion;
			s_settings.deadzone = menu.deadzone;
			if (menu.aOnCircle != (MappedInput(s_settings, Button::A) == ps5emu::PadInput::Circle))
			{
				// A, B, X and Y: on Circle, Cross, Triangle and Square, or on Cross, Circle, Square and
				// Triangle
				using ps5emu::PadInput;
				SetMapping(s_settings, (size_t)Button::A, menu.aOnCircle ? PadInput::Circle : PadInput::Cross);
				SetMapping(s_settings, (size_t)Button::B, menu.aOnCircle ? PadInput::Cross : PadInput::Circle);
				SetMapping(s_settings, (size_t)Button::X, menu.aOnCircle ? PadInput::Triangle : PadInput::Square);
				SetMapping(s_settings, (size_t)Button::Y, menu.aOnCircle ? PadInput::Square : PadInput::Triangle);
			}
			// the renderer and the sound read these as they go
			values.layout_option = LayoutOf(menu.layout);
			values.swap_screen = menu.swapScreens;
			values.resolution_factor = (u32)std::clamp(menu.resolution, 1, 10);
			values.texture_filter = (Settings::TextureFilter)std::clamp(menu.textureFilter, 0, 5);
			values.volume = std::clamp(menu.volume, 0, 100) / 100.0f;
			s_window->UpdateLayout();
			if (controls)
				ReloadControls();

			ps5settings::Launcher all = ps5settings::Load();
			all.n3ds = s_settings;
			all.side = "3ds";
			ps5settings::Save(all);
		}

		const char* LoadError(Core::System::ResultStatus status)
		{
			using Status = Core::System::ResultStatus;
			switch (status)
			{
			case Status::ErrorGetLoader: return "This is not a 3DS game Azahar can start.";
			case Status::ErrorLoader: return "The game could not be loaded.";
			case Status::ErrorLoader_ErrorEncrypted:
				return "The game is encrypted: decrypt it, or put the 3DS's aes_keys.txt in /data/ps5cemu/azahar/sysdata.";
			case Status::ErrorLoader_ErrorInvalidFormat: return "The game's format is not supported.";
			case Status::ErrorLoader_ErrorGbaTitle: return "GBA Virtual Console games are not supported.";
			case Status::ErrorSystemMode: return "The game's system mode could not be found.";
			case Status::ErrorSystemFiles: return "The game needs 3DS system files Azahar does not have.";
			case Status::ErrorLoader_ErrorPatches:
			case Status::ErrorLoader_ErrorPatchesInvalidTitle: return "The game's patches could not be applied.";
			case Status::ErrorNotInitialized: return "Azahar's renderer or CPU did not start.";
			default: return "The game could not be started.";
			}
		}

		// Azahar's renderer, twice a frame: the menu, in its frame
		void DrawMenu(const Vulkan::FrontendOverlayTarget& target)
		{
			ps5ingame3ds::Record({target.instance, target.physical_device, target.device, target.queue_family, target.queue,
				target.render_pass, target.image_count, target.command_buffer, target.width, target.height,
				target.inside_render_pass});
		}

		void Emulate()
		{
			Common::SetCurrentThreadName("AzaharEmu");
			Core::System& system = Core::System::GetInstance();
			system.RegisterCoreLoopThreadId();
			while (!s_stop)
			{
				const auto result = system.RunLoop();
				if (result == Core::System::ResultStatus::ShutdownRequested)
				{
					ps5log::Line("[azahar] the game shut the 3DS down");
					break;
				}
				if (result != Core::System::ResultStatus::Success)
				{
					ps5log::Line("[azahar] the emulation stopped: {} ({})", (int)result, system.GetStatusDetails());
					ps5notify::Send("The 3DS game stopped: " + system.GetStatusDetails());
					break;
				}
			}
			s_running = false;
		}
	}

	bool Available()
	{
		return true;
	}

	bool CoreTouched()
	{
		return s_coreTouched;
	}

	bool LaunchGame(const ps5emu::Game& game, const ps5settings::N3ds& settings, std::string& error)
	{
		SetUp();
		std::string path = game.path.string();
		ps5log::Line("[azahar] launching {} ({:016x}) from {}", game.name, game.titleId, path);
		if (game.path.extension() == ".cia" || game.path.extension() == ".CIA")
		{
			// a CIA is installed, not played: its game starts from the 3DS's SD card
			path = Service::AM::GetTitleContentPath(Service::FS::MediaType::SDMC, game.titleId);
			if (!FileUtil::Exists(path))
			{
				error = "A CIA file is installed, not played: install it from Settings > Install CIA files, then start the game.";
				return false;
			}
			ps5log::Line("[azahar] the CIA's game is installed: {}", path);
		}
		s_settings = settings;
		ApplySettings(settings);

		Core::System& system = Core::System::GetInstance();
		Frontend::RegisterDefaultApplets(system);
		system.RegisterImageInterface(std::make_shared<Frontend::ImageInterface>());
		Vulkan::SetFrontendOverlay(&DrawMenu);

		s_window = std::make_unique<Window>();
		s_coreTouched = true;
		Core::System::ResultStatus status;
		try
		{
			status = system.Load(*s_window, path);
		}
		catch (const std::exception& ex)
		{
			error = fmt::format("Azahar did not start: {}", ex.what());
			return false;
		}
		if (status != Core::System::ResultStatus::Success)
		{
			error = LoadError(status);
			const std::string details = system.GetStatusDetails();
			ps5log::Line("[azahar] load failed: {} ({})", error, details);
			return false;
		}

		u64 programId = 0;
		system.GetAppLoader().ReadProgramId(programId);
		system.GPU().ApplyPerProgramSettings(programId);
		std::atomic_bool stopLoading = false;
		system.GPU().Renderer().Rasterizer()->LoadDefaultDiskResources(stopLoading, nullptr);

		s_name = game.name;
		s_titleId = programId ? programId : game.titleId;
		ShowInMenu();
		s_stop = false;
		s_running = true;
		s_emulation = std::thread(Emulate);
		ps5log::Line("[azahar] {:016x} is running", programId);
		return true;
	}

	void RunGame()
	{
		ps5notify::Send("Touchpad + Options: the PS5 AZAHAR menu. Touchpad click: the bottom screen");
		Core::System& system = Core::System::GetInstance();
		uint64_t polls = 0;
		ps5emu::LogMemory();
		while (s_running && !s_stop)
		{
			sceKernelUsleep(4000);
			input::Update(*s_window, ps5ingame3ds::MenuOpen());
			if (++polls % 500 == 0)
				ps5pad::Rescan(); // controllers joining or leaving, about every two seconds
			if (polls % 15000 == 0)
				ps5emu::LogMemory(); // about once a minute: what keeps growing is a leak
			if (polls % 250 == 0)
			{
				const auto stats = system.GetAndResetPerfStats();
				ps5ingame3ds::SetPerformance(stats.game_fps, stats.emulation_speed * 100.0);
			}
			switch (ps5pad::TakeShortcut())
			{
			case ps5pad::Shortcut::Menu:
				ps5ingame3ds::ToggleMenu();
				break;
			case ps5pad::Shortcut::SwapScreens:
				Settings::values.swap_screen = !Settings::values.swap_screen.GetValue();
				s_window->UpdateLayout();
				ShowInMenu();
				break;
			case ps5pad::Shortcut::CornerScreen:
			{
				// the next layout, as the menu's Screens item goes
				ps5ingame3ds::Settings menu = MenuSettings(s_settings);
				menu.layout = (menu.layout + 1) % 4;
				ApplyMenu(menu);
				ShowInMenu();
				break;
			}
			case ps5pad::Shortcut::None:
				break;
			}
			ps5ingame3ds::Settings menu;
			if (ps5ingame3ds::TakeChanges(menu))
				ApplyMenu(menu);
			if (ps5ingame3ds::TakeLibraryRequest())
			{
				ps5log::Line("[azahar] back to the library");
				s_stop = true;
			}
		}
		s_stop = true;
		if (s_emulation.joinable())
			s_emulation.join();
		Vulkan::SetFrontendOverlay(nullptr);
		// the 3DS's files closed (saves are written as the game writes them)
		system.Shutdown();
		input::Shutdown();
	}

	namespace
	{
		std::mutex s_installMutex;
		ps5emu::InstallStatus s_install;
		std::atomic_bool s_installing = false;
	}

	bool StartInstall(const std::string& cia, std::string& error)
	{
		if (s_installing)
		{
			error = "An install is already running.";
			return false;
		}
		SetUp();
		{
			std::lock_guard lock(s_installMutex);
			s_install = {};
			s_install.state = ps5emu::InstallStatus::State::Running;
		}
		s_installing = true;
		std::thread([cia] {
			ps5log::Line("[azahar] installing {}", cia);
			const auto result = Service::AM::InstallCIA(cia, [](std::size_t written, std::size_t total) {
				std::lock_guard lock(s_installMutex);
				s_install.copied = written;
				s_install.total = total;
			});
			std::lock_guard lock(s_installMutex);
			using Result = Service::AM::InstallStatus;
			switch (result)
			{
			case Result::Success: s_install.state = ps5emu::InstallStatus::State::Done; break;
			case Result::ErrorEncrypted:
				s_install.message = "it is encrypted: decrypt it, or put the 3DS's aes_keys.txt in /data/ps5cemu/azahar/sysdata";
				break;
			case Result::ErrorFileNotFound:
			case Result::ErrorFailedToOpenFile: s_install.message = "the file could not be read"; break;
			case Result::ErrorAborted: s_install.message = "it was stopped"; break;
			default: s_install.message = "it is not a CIA Azahar can install"; break;
			}
			if (result != Result::Success)
				s_install.state = ps5emu::InstallStatus::State::Failed;
			ps5log::Line("[azahar] install of {}: {}", cia, result == Result::Success ? "done" : s_install.message);
			s_installing = false;
		}).detach();
		return true;
	}

	ps5emu::InstallStatus GetInstallStatus()
	{
		std::lock_guard lock(s_installMutex);
		return s_install;
	}

	void CancelInstall()
	{
		// Azahar writes a CIA's contents as it reads them, with no way to stop part-way: it finishes
	}
}
