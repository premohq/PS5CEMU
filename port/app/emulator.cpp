// SPDX-License-Identifier: MPL-2.0
// PS5Cemu: Cemu's core on the PS5 (emulator.h).
//
// CommonInit and CreateDefaultMlcFiles follow Cemu's CemuCommonInit (src/main.cpp) and
// CemuApp::CreateDefaultMLCFiles (src/gui/wxgui/CemuApp.cpp), and LaunchGame its
// MainWindow::FileLoad and VulkanCanvas; this file keeps their MPL-2.0 licence.

#include "emulator.h"
#include "paths.h"
#include "../ps5/display.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"
#include "../ps5/pad.h"
#include "../ps5/privilege.h"
#include "PS5PadController.h"

#include "audio/IAudioAPI.h"
#include "audio/IAudioInputAPI.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Cafe/HW/Espresso/PPCState.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/TitleList/SaveList.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cemu/ncrypto/ncrypto.h"
#include "Cemu/Logging/CemuLogging.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "config/NetworkSettings.h"
#include "input/InputManager.h"
#include "util/crypto/aes128.h"

#include <fstream>

extern "C" int32_t sceSystemServiceParamGetInt(int32_t paramId, int32_t* value);

namespace ps5emu
{
	namespace
	{
		bool s_firstStart = false;

		bool CreateDirectories(const fs::path& path)
		{
			std::error_code ec;
			return fs::exists(path, ec) || fs::create_directories(path, ec);
		}

		// As CemuApp::CreateDefaultMLCFiles.
		bool CreateDefaultMlcFiles(const fs::path& mlc)
		{
			const fs::path directories[] = {
				mlc,
				mlc / "sys",
				mlc / "usr",
				mlc / "usr/title/00050000", // base
				mlc / "usr/title/0005000c", // dlc
				mlc / "usr/title/0005000e", // update
				mlc / "usr/save/00050010/1004a000/user/common/db", // Mii Maker save folders
				mlc / "usr/save/00050010/1004a100/user/common/db",
				mlc / "usr/save/00050010/1004a200/user/common/db",
				mlc / "sys/title/0005001b/1005c000/content", // lang files
			};
			for (const auto& path : directories)
				if (!CreateDirectories(path))
					return false;
			try
			{
				const auto langDir = mlc / "sys/title/0005001b/1005c000/content";
				if (const auto langFile = langDir / "language.txt"; !fs::exists(langFile))
				{
					std::ofstream file(langFile);
					for (const char* lang : {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "zh"})
						file << fmt::format(R"("{}",)", lang) << std::endl;
				}
				if (const auto countryFile = langDir / "country.txt"; !fs::exists(countryFile))
				{
					std::ofstream file(countryFile);
					for (sint32 i = 0; i < NCrypto::GetCountryCount(); i++)
					{
						const char* countryCode = NCrypto::GetCountryAsString(i);
						if (boost::iequals(countryCode, "NN"))
							file << "NULL," << std::endl;
						else
							file << fmt::format(R"("{}",)", countryCode) << std::endl;
					}
				}
			}
			catch (const std::exception& ex)
			{
				ps5log::Line("[emu] cannot write the MLC's language files: {}", ex.what());
				return false;
			}
			return true;
		}

		// The console's language as the Wii U's (sceSystemServiceParamGetInt, parameter 1).
		CafeConsoleLanguage SystemLanguage()
		{
			int32_t language = 1;
			if (sceSystemServiceParamGetInt(1, &language) != 0)
				return CafeConsoleLanguage::EN;
			switch (language)
			{
			case 0: return CafeConsoleLanguage::JA;
			case 2: case 22: return CafeConsoleLanguage::FR;
			case 3: case 20: return CafeConsoleLanguage::ES;
			case 4: return CafeConsoleLanguage::DE;
			case 5: return CafeConsoleLanguage::IT;
			case 6: return CafeConsoleLanguage::NL;
			case 7: case 17: return CafeConsoleLanguage::PT;
			case 8: return CafeConsoleLanguage::RU;
			case 9: return CafeConsoleLanguage::KO;
			case 10: return CafeConsoleLanguage::TW;
			case 11: return CafeConsoleLanguage::ZH;
			default: return CafeConsoleLanguage::EN;
			}
		}

		// The settings the PS5 needs whatever the file says, and the defaults of a first start.
		void ApplyPlatformSettings()
		{
			auto& config = GetConfig();
			config.graphic_api = kVulkan;
			config.audio_api = IAudioAPI::PS5AudioOut;
			config.tv_device = L"ps5-audioout";
			config.tv_channels = kStereo;
			if (s_firstStart)
			{
				config.mlc_path = ps5paths::kMlc;
				config.game_paths = {ps5paths::kGames};
				config.tv_volume = 100;
				config.pad_device = L""; // no separate GamePad speaker
				config.console_language = SystemLanguage();
				config.async_compile = true;
				config.vsync = 1;
				config.overlay.position = ScreenPosition::kDisabled; // touchpad + R1 shows it
				config.notification.position = ScreenPosition::kTopLeft;
			}
			if (config.game_paths.empty())
				config.game_paths = {ps5paths::kGames};
		}

		// The community graphic packs bundled with the app (/app0/assets/graphicPacks, with its
		// version.txt) go where Cemu's own downloader puts them, once per bundled release. Packs of
		// your own elsewhere in graphicPacks/ are left alone.
		void InstallBundledGraphicPacks()
		{
			std::error_code ec;
			const fs::path bundled = ps5paths::kBundledGraphicPacks;
			const fs::path target = ActiveSettings::GetUserDataPath("graphicPacks/downloadedGraphicPacks");
			std::string bundledVersion, installedVersion;
			{
				std::ifstream file(bundled / "version.txt");
				std::getline(file, bundledVersion);
			}
			if (bundledVersion.empty())
				return;
			{
				std::ifstream file(target / "version.txt");
				std::getline(file, installedVersion);
			}
			if (installedVersion == bundledVersion)
				return;
			ps5log::Line("[emu] installing community graphic packs {} (had '{}')", bundledVersion, installedVersion);
			fs::remove_all(target, ec);
			fs::create_directories(target, ec);
			fs::copy(bundled, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
			if (ec)
				ps5log::Line("[emu] graphic packs: {}", ec.message());
		}

		// Player 1 is the GamePad on the DualSense of whoever started PS5Cemu, players 2-4 Pro
		// Controllers on the other signed-in users' DualSenses. Only when no profile exists yet.
		void DefaultControllers()
		{
			auto& input = InputManager::instance();
			for (size_t player = 0; player < 4; player++)
			{
				if (input.get_controller(player))
					continue;
				auto pad = std::make_shared<PS5PadController>((int)player);
				const auto type = player == 0 ? EmulatedController::Type::VPAD : EmulatedController::Type::Pro;
				if (auto emulated = input.set_controller(player, type, pad))
				{
					emulated->set_default_mapping(pad);
					input.save(player);
					ps5log::Line("[emu] player {}: {} on the DualSense", player + 1, player == 0 ? "GamePad" : "Pro Controller");
				}
			}
		}

		// As CemuCommonInit.
		void CommonInit()
		{
			AES128_init();
			PPCTimer_init();
			ExceptionHandler_Init();
			GetConfigHandle().Load();
			ApplyPlatformSettings();
			GetConfigHandle().Save();
			if (NetworkConfig::XMLExists())
				n_config.Load();
			IAudioAPI::InitializeStatic();
			IAudioInputAPI::InitializeStatic();
			GraphicPack2::LoadAll();
			InputManager::instance().load();
			DefaultControllers();
			CafeSystem::Initialize();
			CafeTitleList::Initialize(ActiveSettings::GetUserDataPath("title_list_cache.xml"));
			for (auto& it : GetConfig().game_paths)
				CafeTitleList::AddScanPath(_utf8ToPath(it));
			const fs::path mlcPath = ActiveSettings::GetMlcPath();
			if (!mlcPath.empty())
				CafeTitleList::SetMLCPath(mlcPath);
			CafeTitleList::Refresh();
			CafeSaveList::Initialize();
			if (!mlcPath.empty())
			{
				CafeSaveList::SetMLCPath(mlcPath);
				CafeSaveList::Refresh();
			}
		}

		bool s_menuArmed = false;
		uint64_t s_menuArmedAt = 0;
	}

	bool InitializeCore(std::string& error)
	{
		std::set<fs::path> failedWriteAccess;
		ActiveSettings::SetPaths(false, ps5paths::kEboot, ps5paths::kRoot, ps5paths::kRoot, ps5paths::kCache,
			ps5paths::kCemuData, failedWriteAccess);
		if (!failedWriteAccess.empty())
		{
			error = fmt::format("PS5Cemu cannot write to {}. Is the HEN loaded?", _pathToUtf8(*failedWriteAccess.begin()));
			return false;
		}
		cemuLog_createLogFile(false); // log.txt in /data/ps5cemu, as on the desktop
		CreateDirectories(ActiveSettings::GetConfigPath("controllerProfiles"));
		CreateDirectories(ps5paths::kGames);
		CreateDirectories(ps5paths::kLogs);

		GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("settings.xml").generic_wstring());
		std::error_code ec;
		s_firstStart = !fs::exists(ActiveSettings::GetConfigPath("settings.xml"), ec);
		NetworkConfig::LoadOnce();
		if (s_firstStart)
		{
			ApplyPlatformSettings();
			GetConfigHandle().Save();
		}
		if (!CreateDefaultMlcFiles(ActiveSettings::GetMlcPath()))
		{
			error = fmt::format("PS5Cemu cannot create the MLC folder {}", _pathToUtf8(ActiveSettings::GetMlcPath()));
			return false;
		}
		InstallBundledGraphicPacks();
		ActiveSettings::Init();
		LatteOverlay_init();
		CommonInit();
		if (!InitializeGlobalVulkan())
		{
			error = "the PS5 Vulkan driver did not start";
			return false;
		}
		ps5log::Line("[emu] Cemu is ready: {} game folders, MLC {}", GetConfig().game_paths.size(), _pathToUtf8(ActiveSettings::GetMlcPath()));
		return true;
	}

	void ApplyOptions(const Options& options)
	{
		auto& config = GetConfig();
		config.tv_volume = std::clamp(options.volume, 0, 100);
		config.upscale_filter = std::clamp(options.upscaleFilter, (int)kLinearFilter, (int)kNearestNeighborFilter);
		config.overlay.position = options.overlay ? ScreenPosition::kTopLeft : ScreenPosition::kDisabled;
		if (options.overlay)
			config.overlay.fps = config.overlay.cpu_usage = config.overlay.ram_usage = true;
		const bool newFolder = !options.gamesFolder.empty() &&
			(config.game_paths.size() != 1 || config.game_paths.front() != options.gamesFolder);
		if (newFolder)
		{
			config.game_paths = {options.gamesFolder};
			CafeTitleList::ClearScanPaths();
			CafeTitleList::AddScanPath(_utf8ToPath(options.gamesFolder));
			CafeTitleList::Refresh();
			ps5log::Line("[emu] games folder is now {}", options.gamesFolder);
		}
		GetConfigHandle().Save();
	}

	bool Scanning()
	{
		return CafeTitleList::IsScanning();
	}

	std::vector<Game> ListGames()
	{
		std::vector<Game> games;
		for (const TitleId titleId : CafeTitleList::GetAllTitleIds())
		{
			GameInfo2 info = CafeTitleList::GetGameInfo(titleId);
			if (!info.IsValid() || info.IsSystemDataTitle())
				continue;
			TitleInfo& base = info.GetBase();
			if (!base.IsValid() || base.GetAppTitleId() != titleId)
				continue; // updates and DLC are listed with their base
			Game game;
			game.titleId = titleId;
			game.name = base.GetMetaTitleName();
			if (game.name.empty())
				game.name = fmt::format("{:016x}", titleId);
			game.path = base.GetPath();
			game.hasUpdate = info.HasUpdate();
			game.version = game.hasUpdate ? info.GetUpdate().GetAppTitleVersion() : base.GetAppTitleVersion();
			game.dlcCount = (uint32_t)info.GetAOC().size();
			switch (base.GetFormat())
			{
			case TitleInfo::TitleDataFormat::WIIU_ARCHIVE: game.format = "WUA"; break;
			case TitleInfo::TitleDataFormat::WUD:
				game.format = boost::iequals(_pathToUtf8(game.path.extension()), ".wux") ? "WUX" : "WUD";
				break;
			case TitleInfo::TitleDataFormat::NUS: game.format = "NUS"; break;
			case TitleInfo::TitleDataFormat::WUHB: game.format = "WUHB"; break;
			default: game.format = "FOLDER"; break;
			}
			games.push_back(std::move(game));
		}
		std::sort(games.begin(), games.end(), [](const Game& a, const Game& b) { return boost::ilexicographical_compare(a.name, b.name); });
		return games;
	}

	bool LaunchGame(const Game& game, std::string& error)
	{
		ps5log::Line("[emu] launching {} ({:016x}) from {}", game.name, game.titleId, _pathToUtf8(game.path));
		TitleInfo launchTitle{game.path};
		if (launchTitle.IsValid())
		{
			CafeTitleList::AddTitleFromPath(game.path);
			TitleId baseTitleId;
			if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
			{
				error = "The game's base files were not found.";
				return false;
			}
			const auto status = CafeSystem::PrepareForegroundTitle(baseTitleId);
			if (status == CafeSystem::PREPARE_STATUS_CODE::UNABLE_TO_MOUNT)
			{
				error = "The game could not be mounted. Check that its files are still in the game files folder.";
				return false;
			}
			if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
			{
				error = "The game could not be started.";
				return false;
			}
		}
		else
		{
			const CafeTitleFileType fileType = DetermineCafeSystemFileType(game.path);
			if (fileType != CafeTitleFileType::RPX && fileType != CafeTitleFileType::ELF)
			{
				error = "This is not a Wii U game PS5Cemu can start.";
				if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_DISC_KEY)
					error += " Its disc key is missing from /data/ps5cemu/keys.txt.";
				else if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_TITLE_TIK)
					error += " Its title.tik is missing.";
				return false;
			}
			if (CafeSystem::PrepareForegroundTitleFromStandaloneRPX(game.path) != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
			{
				error = "The executable could not be started.";
				return false;
			}
		}

		// as VulkanCanvas: the renderer, then the surface for the main window
		try
		{
			g_renderer = std::make_unique<VulkanRenderer>();
			VulkanRenderer::GetInstance()->InitializeSurface({(sint32)ps5display::kWidth, (sint32)ps5display::kHeight}, true);
		}
		catch (const std::exception& ex)
		{
			error = fmt::format("The Vulkan renderer did not start: {}", ex.what());
			return false;
		}
		CafeSystem::LaunchForegroundTitle();
		ps5log::Line("[emu] {} is running ({})", CafeSystem::GetForegroundTitleName(),
			ActiveSettings::GetCPUMode() == CPUMode::SinglecoreInterpreter ? "interpreter" : "recompiler");
		return true;
	}

	bool RendererStarted()
	{
		return g_renderer != nullptr;
	}

	void RunGame()
	{
		uint64_t polls = 0;
		for (;;)
		{
			sceKernelUsleep(16000);
			if (++polls % 120 == 0)
				ps5pad::Rescan(); // controllers joining or leaving, about every two seconds
			const uint64_t now = sceKernelGetProcessTime();
			if (s_menuArmed && now - s_menuArmedAt > 4000000)
				s_menuArmed = false;
			switch (ps5pad::TakeShortcut())
			{
			case ps5pad::Shortcut::Menu:
				if (s_menuArmed)
					return; // confirmed: back to the library
				s_menuArmed = true;
				s_menuArmedAt = now;
				ps5notify::Send("Press touchpad + L1 again to go back to the library");
				break;
			case ps5pad::Shortcut::Overlay:
			{
				auto& overlay = GetConfig().overlay;
				overlay.position = overlay.position == ScreenPosition::kDisabled ? ScreenPosition::kTopLeft : ScreenPosition::kDisabled;
				overlay.fps = overlay.cpu_usage = overlay.ram_usage = true;
				break;
			}
			case ps5pad::Shortcut::SwapScreens:
				LatteGPUState.isDRCPrimary = !LatteGPUState.isDRCPrimary;
				break;
			case ps5pad::Shortcut::None:
				break;
			}
		}
	}

	void RestartToLibrary()
	{
		ps5log::Line("[emu] back to the library: starting PS5Cemu over");
		GetConfigHandle().Save();
		std::error_code ec;
		const char* eboot = fs::exists(ps5paths::kMountedEboot, ec) ? ps5paths::kMountedEboot : ps5paths::kEboot;
		const int result = sceSystemServiceLoadExec(eboot, nullptr);
		// it does not come back when it works; allow for one that returns before ending the process
		if (result == 0)
			for (int i = 0; i < 100; i++)
				sceKernelUsleep(100000);
		ps5log::Line("[emu] LoadExec({}) returned {:#x}", eboot, (uint32_t)result);
	}
}
