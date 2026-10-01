// SPDX-License-Identifier: GPL-3.0-or-later
// PS5Cemu: the launcher (launcher.h).
//
// Its screens follow ProsperoEden's launcher (headless/prosperoeden/eden_app.cpp, GPL-3.0-or-later,
// by BlackBearReloaded): the same element ids and classes ("open" shows a screen, "focused" the
// row under the cursor, "offscreen" hides unused rows), the same navigation on the home screen,
// and its folder browser, which reads folders with sceKernelGetdents as it does.

#include "launcher.h"
#include "ui_host.h"
#include "../app/paths.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"
#include "../ps5/pad.h"
#include "../ps5/privilege.h"

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <initializer_list>
#include <sys/stat.h>
#include <unistd.h>

namespace ps5launcher
{
	namespace
	{
		enum class Key
		{
			Up,
			Down,
			Left,
			Right,
			Cross,
			Circle,
			Triangle,
			Square,
			L1,
			R1,
		};

		// The DualSense as key presses: any connected controller, the left stick as the D-pad, and
		// the directions and L1/R1 repeating while held.
		class Input
		{
		public:
			std::vector<Key> Poll()
			{
				uint32_t buttons = 0;
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
				{
					ps5pad::Data data;
					if (!ps5pad::Read(player, data))
						continue;
					buttons |= data.buttons;
					if (data.leftY < 64)
						buttons |= ps5pad::kUp;
					else if (data.leftY > 192)
						buttons |= ps5pad::kDown;
					if (data.leftX < 64)
						buttons |= ps5pad::kLeft;
					else if (data.leftX > 192)
						buttons |= ps5pad::kRight;
				}
				const uint64_t now = sceKernelGetProcessTime();
				std::vector<Key> keys;
				for (const auto& [mask, key, repeats] : kMap)
				{
					const bool down = buttons & mask, was = m_held & mask;
					const size_t slot = (size_t)key;
					if (down && !was)
					{
						keys.push_back(key);
						m_repeatAt[slot] = now + kRepeatDelayUs;
					}
					else if (down && repeats && now >= m_repeatAt[slot])
					{
						keys.push_back(key);
						m_repeatAt[slot] = now + kRepeatRateUs;
					}
				}
				m_held = buttons;
				return keys;
			}

		private:
			static constexpr uint64_t kRepeatDelayUs = 400000, kRepeatRateUs = 90000;
			struct Mapping
			{
				uint32_t mask;
				Key key;
				bool repeats;
			};
			static constexpr Mapping kMap[] = {
				{ps5pad::kUp, Key::Up, true},
				{ps5pad::kDown, Key::Down, true},
				{ps5pad::kLeft, Key::Left, true},
				{ps5pad::kRight, Key::Right, true},
				{ps5pad::kCross, Key::Cross, false},
				{ps5pad::kCircle, Key::Circle, false},
				{ps5pad::kTriangle, Key::Triangle, false},
				{ps5pad::kSquare, Key::Square, false},
				{ps5pad::kL1, Key::L1, true},
				{ps5pad::kR1, Key::R1, true},
			};
			uint32_t m_held = ~0u; // nothing counts as pressed until it has been released once
			std::array<uint64_t, 10> m_repeatAt{};
		};

		void SetClass(Rml::ElementDocument* document, const std::string& id, const char* name, bool enabled)
		{
			if (Rml::Element* element = document->GetElementById(id))
				element->SetClass(name, enabled);
		}

		void SetText(Rml::ElementDocument* document, const std::string& id, const std::string& text)
		{
			if (Rml::Element* element = document->GetElementById(id))
				element->SetInnerRML(Rml::StringUtilities::EncodeRml(text));
		}

		void SetImage(Rml::ElementDocument* document, const std::string& id, const std::string& source)
		{
			if (Rml::Element* element = document->GetElementById(id))
				element->SetAttribute("src", source);
		}

		std::string Hex(uint64_t value)
		{
			return fmt::format("{:016X}", value);
		}

		// A path that fits a label: its end is what tells folders apart.
		std::string ShortPath(const std::string& path, size_t limit)
		{
			return path.size() <= limit ? path : "..." + path.substr(path.size() - (limit - 3));
		}

		std::string Plural(int count, const char* one, const char* many)
		{
			return fmt::format("{} {}", count, count == 1 ? one : many);
		}

		std::string Lower(std::string text)
		{
			for (char& c : text)
				c = (char)std::tolower((unsigned char)c);
			return text;
		}

		std::string JoinPath(const std::string& folder, const std::string& name)
		{
			return folder == "/" ? "/" + name : folder + "/" + name;
		}

		std::string ParentPath(const std::string& folder)
		{
			const size_t slash = folder.find_last_of('/');
			return slash == 0 || slash == std::string::npos ? "/" : folder.substr(0, slash);
		}

		bool IsFolder(const std::string& path)
		{
			struct stat info{};
			return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
		}

		bool IsFile(const std::string& path)
		{
			struct stat info{};
			return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode);
		}

		// The names of a folder's subfolders (folders) or files, sorted without regard to case. An
		// entry that cannot be read is skipped: the browser walks the whole console filesystem.
		std::vector<std::string> ListEntries(const std::string& path, bool folders, bool& ok)
		{
			ok = false;
			std::vector<std::string> names;
			const int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
			if (fd < 0)
				return names;
			std::vector<char> buffer(65536);
			for (;;)
			{
				const int count = sceKernelGetdents(fd, buffer.data(), (int)buffer.size());
				if (count == 0)
				{
					ok = true;
					break;
				}
				if (count < 0 || count > (int)buffer.size())
					break;
				for (size_t offset = 0; offset + offsetof(dirent, d_name) < (size_t)count;)
				{
					uint16_t length;
					uint8_t type;
					std::memcpy(&length, buffer.data() + offset + offsetof(dirent, d_reclen), sizeof(length));
					std::memcpy(&type, buffer.data() + offset + offsetof(dirent, d_type), sizeof(type));
					if (length <= offsetof(dirent, d_name) || offset + length > (size_t)count)
						break;
					const char* name = buffer.data() + offset + offsetof(dirent, d_name);
					const std::string entry(name, strnlen(name, length - offsetof(dirent, d_name)));
					offset += length;
					if (entry.empty() || entry == "." || entry == "..")
						continue;
					bool isFolder = type == DT_DIR, isFile = type == DT_REG;
					if (type == DT_UNKNOWN || type == DT_LNK)
					{
						const std::string full = JoinPath(path, entry);
						isFolder = IsFolder(full);
						isFile = IsFile(full);
					}
					if (folders ? isFolder : isFile)
						names.push_back(entry);
				}
			}
			close(fd);
			std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return Lower(a) < Lower(b); });
			return names;
		}

		// What Cemu would find in a folder: Wii U images and executables, and folders that hold an
		// unpacked game (code, content, meta) or an installable one (title.tmd). -1: unreadable.
		int CountGames(const std::string& folder)
		{
			bool ok = false;
			int count = 0;
			for (const auto& name : ListEntries(folder, false, ok))
			{
				const std::string lower = Lower(name);
				for (const char* extension : {".wua", ".wud", ".wux", ".rpx", ".elf", ".wuhb"})
					if (lower.size() > std::strlen(extension) && lower.ends_with(extension))
					{
						count++;
						break;
					}
			}
			if (!ok)
				return -1;
			for (const auto& name : ListEntries(folder, true, ok))
			{
				const std::string path = JoinPath(folder, name);
				if (IsFolder(path + "/code") || IsFile(path + "/title.tmd"))
					count++;
			}
			return count;
		}

		enum Screen
		{
			kHome,
			kLibrary,
			kSettings,
			kVideo,
			kAudio,
			kControls,
			kDiagnostics,
			kAbout,
			kFiles,
			kPacks,
			kLoading,
		};

		// The home screen's focusable elements, as ProsperoEden numbers them.
		enum HomeItem
		{
			kContinue = 0,
			kLibraryButton = 1,
			kSettingsButton = 2,
			kAboutButton = 3,
			kPacksButton = 4,
			kRecent0 = 5,
			kViewAll = 9,
		};

		constexpr int kLibraryRows = 7, kPackRows = 7, kFileRows = 6;
		constexpr const char* kHomeElements[] = {"header", "menu", "last-played-card", "recent-section", "startup-status", "footer"};
		constexpr const char* kSettingsPages[] = {"video-dialog", "audio-dialog", "controls-dialog", "diagnostics-dialog"};

		class Launcher
		{
		public:
			Launcher(Rml::ElementDocument* document, ps5settings::Launcher& settings, const Status& status)
				: m_document(document), m_settings(settings), m_status(status)
			{
			}

			void Initialize()
			{
				SetText(m_document, "brand-version", "WII U  /  PS5 EDITION  /  v" PS5CEMU_VERSION);
				SetText(m_document, "startup-status", m_status.notice);
				SetClass(m_document, "startup-status", "quiet", m_status.notice.empty());
				SetClass(m_document, "load-rom", "disabled", !m_status.coreReady);
				SetText(m_document, "about-games-path", ShortPath(m_settings.gamesFolder, 36));
				SetText(m_document, "about-keys-path", std::string(ps5paths::kRoot) + "/keys.txt");
				SetText(m_document, "about-mlc-path", ps5paths::kMlc);
				m_scanning = m_status.coreReady && ps5emu::Scanning();
				if (m_status.coreReady)
					m_games = ps5emu::ListGames();
				RefreshHome();
				m_selected = m_continueReady ? kContinue : m_status.coreReady ? kLibraryButton : kSettingsButton;
				UpdateHome();
				Poll();
			}

			bool Done() const { return m_launch.has_value() && m_screen == kLoading; }
			const std::optional<ps5emu::Game>& Choice() const { return m_launch; }

			// The clock, and the library once Cemu has finished looking for games.
			void Poll()
			{
				const std::time_t minute = std::time(nullptr) / 60;
				if (minute != m_shownMinute)
				{
					m_shownMinute = minute;
					const std::time_t now = minute * 60;
					char label[16]{};
					if (const std::tm* local = std::localtime(&now))
						std::strftime(label, sizeof(label), "%H:%M", local);
					SetText(m_document, "menu-clock", label);
				}
				if (m_scanning && !ps5emu::Scanning())
				{
					m_scanning = false;
					m_games = ps5emu::ListGames();
					ps5log::Line("[launcher] {} games", m_games.size());
					RefreshHome();
					UpdateHome();
					if (m_screen == kLibrary)
						UpdateLibrary();
				}
			}

			void HandleKey(Key key)
			{
				switch (m_screen)
				{
				case kHome: HomeKey(key); break;
				case kLibrary: LibraryKey(key); break;
				case kPacks: PacksKey(key); break;
				case kSettings: SettingsKey(key); break;
				case kVideo:
				case kAudio:
				case kControls:
				case kDiagnostics: SettingsPageKey(key); break;
				case kAbout:
					if (key == Key::Circle)
						CloseFullScreen();
					break;
				case kFiles: FilesKey(key); break;
				case kLoading: break;
				}
			}

		private:
			// -- home --------------------------------------------------------------------------

			int FindGame(uint64_t titleId) const
			{
				for (size_t i = 0; i < m_games.size(); i++)
					if (m_games[i].titleId == titleId)
						return (int)i;
				return -1;
			}

			std::string Cover(const ps5emu::Game& game) const
			{
				const std::string cover = ps5emu::CoverPath(game.titleId);
				return cover.empty() ? "icons/ps5cemu.tga" : cover;
			}

			void RefreshHome()
			{
				m_lastIndex = FindGame(m_settings.lastGame);
				m_continueReady = m_status.coreReady && m_lastIndex >= 0;
				if (m_lastIndex >= 0)
				{
					const auto& game = m_games[m_lastIndex];
					SetText(m_document, "last-played-title", game.name);
					SetText(m_document, "last-played-caption", fmt::format("Last game played  /  v{}{}", game.version,
						game.dlcCount ? fmt::format("  /  {} DLC", game.dlcCount) : ""));
					SetImage(m_document, "last-played-cover", Cover(game));
				}
				else if (m_settings.lastGame && m_status.coreReady && !m_scanning)
				{
					SetText(m_document, "last-played-title", "Your last game is not here");
					SetText(m_document, "last-played-caption", "It is no longer in the game files folder.");
				}
				SetText(m_document, "continue-copy", m_continueReady ? "Launch game" : "Open library");
				SetClass(m_document, "continue-game", "disabled", !m_status.coreReady);
				SetClass(m_document, "hero-options", "disabled", !m_continueReady);

				m_recent.clear();
				for (uint64_t titleId : m_settings.recent)
					if (const int index = FindGame(titleId); index >= 0)
						m_recent.push_back(index);
				for (int i = 0; i < 4; i++)
				{
					const std::string row = fmt::format("recent-{}", i);
					const bool present = i < (int)m_recent.size();
					SetClass(m_document, row, "library-hidden", !present);
					if (!present)
						continue;
					const auto& game = m_games[m_recent[i]];
					SetText(m_document, fmt::format("recent-title-{}", i), game.name);
					SetImage(m_document, fmt::format("recent-cover-{}", i), Cover(game));
				}
				SetClass(m_document, "recent-empty", "library-hidden", !m_recent.empty());

				std::string system;
				if (!m_status.coreReady)
					system = "Setup required";
				else if (m_scanning)
					system = "Looking for games";
				else
					system = Plural((int)m_games.size(), "game", "games");
				system += PS5_JitAvailable() ? "  /  Recompiler" : "  /  Interpreter (no JIT)";
				SetText(m_document, "system-status", system);
			}

			void UpdateHome()
			{
				SetClass(m_document, "continue-game", "focused", m_selected == kContinue);
				SetClass(m_document, "load-rom", "focused", m_selected == kLibraryButton);
				SetClass(m_document, "settings", "focused", m_selected == kSettingsButton);
				SetClass(m_document, "help-about", "focused", m_selected == kAboutButton);
				SetClass(m_document, "hero-options", "focused", m_selected == kPacksButton);
				SetClass(m_document, "view-all", "focused", m_selected == kViewAll);
				for (int i = 0; i < 4; i++)
					SetClass(m_document, fmt::format("recent-{}", i), "focused", m_selected == kRecent0 + i);
			}

			void HomeKey(Key key)
			{
				const int recentCount = (int)m_recent.size();
				const bool ready = m_status.coreReady;
				if (key == Key::Up)
				{
					if (m_selected == kContinue || m_selected == kPacksButton)
						m_selected = ready ? kLibraryButton : kSettingsButton;
					else if (m_selected >= kRecent0)
						m_selected = kContinue;
				}
				else if (key == Key::Down)
				{
					if (m_selected >= kLibraryButton && m_selected <= kAboutButton)
					{
						if (ready)
							m_selected = kContinue;
					}
					else if (m_selected == kContinue || m_selected == kPacksButton)
						m_selected = recentCount ? kRecent0 : kViewAll;
				}
				else if (key == Key::Left || key == Key::Right)
				{
					const int delta = key == Key::Right ? 1 : -1;
					if (m_selected >= kLibraryButton && m_selected <= kAboutButton)
					{
						m_selected = kLibraryButton + (m_selected - kLibraryButton + delta + 3) % 3;
						if (m_selected == kLibraryButton && !ready)
							m_selected = delta > 0 ? kSettingsButton : kAboutButton;
					}
					else if (m_selected == kContinue || m_selected == kPacksButton)
					{
						if (m_continueReady)
							m_selected = m_selected == kContinue ? kPacksButton : kContinue;
					}
					else if (m_selected >= kRecent0)
					{
						const int length = recentCount + 1;
						int position = m_selected == kViewAll ? recentCount : m_selected - kRecent0;
						position = (position + delta + length) % length;
						m_selected = position == recentCount ? kViewAll : kRecent0 + position;
					}
				}
				else if (key == Key::Triangle && m_continueReady && (m_selected == kContinue || m_selected == kPacksButton))
					OpenPacks(m_lastIndex, kHome);
				else if (key == Key::Cross)
				{
					if (m_selected == kContinue && m_continueReady)
						Launch(m_lastIndex);
					else if ((m_selected == kContinue || m_selected == kLibraryButton || m_selected == kViewAll) && ready)
						OpenLibrary(m_lastIndex);
					else if (m_selected == kSettingsButton)
						OpenSettings();
					else if (m_selected == kAboutButton)
						OpenFullScreen("about-dialog", kAbout);
					else if (m_selected == kPacksButton && m_continueReady)
						OpenPacks(m_lastIndex, kHome);
					else if (m_selected >= kRecent0 && m_selected < kRecent0 + recentCount)
						Launch(m_recent[m_selected - kRecent0]);
				}
				UpdateHome();
			}

			void OpenFullScreen(const char* id, Screen screen)
			{
				for (const char* element : kHomeElements)
					SetClass(m_document, element, "library-hidden", true);
				SetClass(m_document, id, "open", true);
				m_screen = screen;
			}

			void CloseFullScreen()
			{
				for (const char* id : {"about-dialog", "rom-dialog", "packs-dialog", "settings-dialog", "files-dialog"})
					SetClass(m_document, id, "open", false);
				for (const char* element : kHomeElements)
					SetClass(m_document, element, "library-hidden", false);
				m_screen = kHome;
				RefreshHome();
				UpdateHome();
			}

			void Launch(int index)
			{
				if (index < 0 || index >= (int)m_games.size())
					return;
				const auto& game = m_games[index];
				ps5settings::AddRecent(m_settings, game.titleId);
				ps5settings::Save(m_settings);
				SetText(m_document, "loading-title", game.name);
				SetText(m_document, "loading-caption", "Starting");
				SetImage(m_document, "loading-cover", Cover(game));
				SetClass(m_document, "loading-screen", "open", true);
				m_launch = game;
				m_screen = kLoading;
			}

			// -- library -----------------------------------------------------------------------

			void OpenLibrary(int select)
			{
				SetClass(m_document, "packs-dialog", "open", false);
				OpenFullScreen("rom-dialog", kLibrary);
				if (select >= 0)
					m_librarySelected = select;
				m_librarySelected = std::clamp(m_librarySelected, 0, std::max(0, (int)m_games.size() - 1));
				UpdateLibrary();
			}

			void LibraryKey(Key key)
			{
				const int count = (int)m_games.size();
				if (key == Key::Circle)
				{
					CloseFullScreen();
					return;
				}
				if (count == 0)
					return;
				if (key == Key::Up)
					m_librarySelected = (m_librarySelected + count - 1) % count;
				else if (key == Key::Down)
					m_librarySelected = (m_librarySelected + 1) % count;
				else if (key == Key::L1 || key == Key::Left)
					m_librarySelected = std::max(0, m_librarySelected - kLibraryRows);
				else if (key == Key::R1 || key == Key::Right)
					m_librarySelected = std::min(count - 1, m_librarySelected + kLibraryRows);
				else if (key == Key::Cross)
				{
					Launch(m_librarySelected);
					return;
				}
				else if (key == Key::Triangle)
				{
					OpenPacks(m_librarySelected, kLibrary);
					return;
				}
				UpdateLibrary();
			}

			void UpdateLibrary()
			{
				const int count = (int)m_games.size();
				const int scroll = m_librarySelected < kLibraryRows ? 0 : m_librarySelected - (kLibraryRows - 1);
				for (int row = 0; row < kLibraryRows; row++)
				{
					const int index = scroll + row;
					const std::string id = fmt::format("rom-row-{}", row);
					SetClass(m_document, id, "focused", index == m_librarySelected);
					SetClass(m_document, id, "offscreen", index >= count);
					SetText(m_document, fmt::format("rom-name-{}", row), index < count ? m_games[index].name : "");
					SetText(m_document, fmt::format("rom-format-{}", row), index < count ? m_games[index].format : "");
				}
				SetText(m_document, "library-empty", m_scanning ? "Looking for games..." : "No games found. Put them in the game files folder.");
				SetClass(m_document, "library-empty", "visible", count == 0);
				SetClass(m_document, "rom-scrollbar", "offscreen", count <= kLibraryRows);
				if (count > kLibraryRows)
					if (Rml::Element* thumb = m_document->GetElementById("rom-scrollbar-thumb"))
						thumb->SetProperty("top", fmt::format("{}px", 496 * scroll / (count - kLibraryRows)));
				SetText(m_document, "library-position", fmt::format("{} OF {}", count ? m_librarySelected + 1 : 0, count));
				if (count == 0)
				{
					SetText(m_document, "game-detail-title", "No game selected");
					for (const char* id : {"game-detail-format", "game-detail-size", "game-detail-dlc", "game-detail-path", "game-packs-value"})
						SetText(m_document, id, "-");
					SetImage(m_document, "game-cover", "icons/ps5cemu.tga");
					SetText(m_document, "cover-caption", "");
					return;
				}
				const auto& game = m_games[m_librarySelected];
				SetText(m_document, "game-detail-title", game.name);
				SetText(m_document, "game-detail-format", Hex(game.titleId));
				SetText(m_document, "game-detail-size", fmt::format("v{}{}", game.version, game.hasUpdate ? " (update installed)" : ""));
				SetText(m_document, "game-detail-dlc", game.dlcCount ? std::to_string(game.dlcCount) : "None");
				SetText(m_document, "game-detail-path", ShortPath(game.path.string(), 44));
				const std::string cover = ps5emu::CoverPath(game.titleId);
				SetImage(m_document, "game-cover", cover.empty() ? "icons/ps5cemu.tga" : cover);
				SetText(m_document, "cover-caption", cover.empty() ? "No icon" : "");
				const auto packs = ps5emu::ListGraphicPacks(game.titleId);
				const int enabled = ps5emu::EnabledGraphicPackCount(game.titleId);
				SetText(m_document, "game-packs-value", packs.empty() ? "None available" : fmt::format("{} of {} on", enabled, packs.size()));
			}

			// -- graphic packs -----------------------------------------------------------------

			void OpenPacks(int gameIndex, Screen from)
			{
				if (gameIndex < 0 || gameIndex >= (int)m_games.size())
					return;
				m_packsGame = gameIndex;
				m_packsFrom = from;
				m_packSelected = 0;
				SetClass(m_document, "rom-dialog", "open", false);
				OpenFullScreen("packs-dialog", kPacks);
				SetText(m_document, "packs-game", m_games[gameIndex].name);
				UpdatePacks();
			}

			void PacksKey(Key key)
			{
				const uint64_t titleId = m_games[m_packsGame].titleId;
				const int count = (int)ps5emu::ListGraphicPacks(titleId).size();
				if (key == Key::Circle)
				{
					SetClass(m_document, "packs-dialog", "open", false);
					if (m_packsFrom == kLibrary)
						OpenLibrary(m_packsGame);
					else
						CloseFullScreen();
					return;
				}
				if (count == 0)
					return;
				if (key == Key::Up)
					m_packSelected = (m_packSelected + count - 1) % count;
				else if (key == Key::Down)
					m_packSelected = (m_packSelected + 1) % count;
				else if (key == Key::L1)
					m_packSelected = std::max(0, m_packSelected - kPackRows);
				else if (key == Key::R1)
					m_packSelected = std::min(count - 1, m_packSelected + kPackRows);
				else if (key == Key::Cross)
					ps5emu::ToggleGraphicPack(titleId, m_packSelected);
				else if (key == Key::Left || key == Key::Right)
					ps5emu::CycleGraphicPackPreset(titleId, m_packSelected, key == Key::Right ? 1 : -1);
				UpdatePacks();
			}

			void UpdatePacks()
			{
				const auto packs = ps5emu::ListGraphicPacks(m_games[m_packsGame].titleId);
				const int count = (int)packs.size();
				m_packSelected = std::clamp(m_packSelected, 0, std::max(0, count - 1));
				const int scroll = m_packSelected < kPackRows ? 0 : m_packSelected - (kPackRows - 1);
				for (int row = 0; row < kPackRows; row++)
				{
					const int index = scroll + row;
					const std::string id = fmt::format("pack-row-{}", row);
					SetClass(m_document, id, "focused", index == m_packSelected);
					SetClass(m_document, id, "offscreen", index >= count);
					SetText(m_document, fmt::format("pack-name-{}", row), index < count ? packs[index].name : "");
					SetText(m_document, fmt::format("pack-state-{}", row), index < count ? (packs[index].enabled ? "ON" : "OFF") : "");
					SetClass(m_document, fmt::format("pack-state-{}", row), "on", index < count && packs[index].enabled);
				}
				SetClass(m_document, "packs-empty", "visible", count == 0);
				SetText(m_document, "packs-position", fmt::format("{} OF {}", count ? m_packSelected + 1 : 0, count));
				if (count == 0)
				{
					for (const char* id : {"pack-detail-title", "pack-detail-category", "pack-detail-preset", "pack-detail-description"})
						SetText(m_document, id, "");
					return;
				}
				const auto& pack = packs[m_packSelected];
				SetText(m_document, "pack-detail-title", pack.name);
				SetText(m_document, "pack-detail-category", pack.category.empty() ? "-" : pack.category);
				SetText(m_document, "pack-detail-preset", pack.hasPresets ? pack.preset + "  (LEFT / RIGHT)" : "-");
				SetText(m_document, "pack-detail-description", pack.description.empty() ? "Applies to the next game you start." : pack.description);
			}

			// -- settings ----------------------------------------------------------------------

			void OpenSettings()
			{
				OpenFullScreen("settings-dialog", kSettings);
				UpdateSettingsList();
			}

			void UpdateSettingsList()
			{
				for (int row = 0; row < 5; row++)
					SetClass(m_document, fmt::format("settings-row-{}", row), "focused", m_settingsSelected == row);
			}

			void SettingsKey(Key key)
			{
				if (key == Key::Circle)
					CloseFullScreen();
				else if (key == Key::Up)
					m_settingsSelected = (m_settingsSelected + 4) % 5;
				else if (key == Key::Down)
					m_settingsSelected = (m_settingsSelected + 1) % 5;
				else if (key == Key::Cross && m_settingsSelected == 4)
				{
					OpenFiles();
					return;
				}
				else if (key == Key::Cross)
				{
					SetClass(m_document, kSettingsPages[m_settingsSelected], "open", true);
					m_screen = (Screen)(kVideo + m_settingsSelected);
					m_option = 0;
					UpdateSettingsPage();
					return;
				}
				UpdateSettingsList();
			}

			void SettingsPageKey(Key key)
			{
				if (key == Key::Circle)
				{
					SetClass(m_document, kSettingsPages[m_screen - kVideo], "open", false);
					m_screen = kSettings;
					UpdateSettingsList();
					return;
				}
				const bool change = key == Key::Cross || key == Key::Left || key == Key::Right;
				bool changed = false;
				if (m_screen == kVideo)
				{
					if (key == Key::Up)
						m_option = (m_option + 2) % 3;
					else if (key == Key::Down)
						m_option = (m_option + 1) % 3;
					else if (change && m_option == 0)
					{
						m_settings.upscaleFilter = (m_settings.upscaleFilter + (key == Key::Left ? 3 : 1)) % 4;
						changed = true;
					}
					else if (change && m_option == 1)
						changed = (m_settings.highFrameRate = !m_settings.highFrameRate, true);
					else if (change && m_option == 2)
						changed = (m_settings.overlay = !m_settings.overlay, true);
				}
				else if (m_screen == kAudio && (key == Key::Left || key == Key::Right))
				{
					m_settings.volume = std::clamp(m_settings.volume + (key == Key::Right ? 10 : -10), 0, 100);
					changed = true;
				}
				else if (m_screen == kControls && change)
				{
					m_settings.rumble = !m_settings.rumble;
					ps5pad::SetVibrationEnabled(m_settings.rumble);
					changed = true;
				}
				if (changed && !ps5settings::Save(m_settings))
					ps5log::Line("[launcher] could not save {}", ps5paths::kLauncherSettings);
				UpdateSettingsPage();
			}

			void UpdateSettingsPage()
			{
				static constexpr const char* kFilters[] = {"Linear", "Bicubic", "Bicubic Hermite", "Nearest neighbour"};
				SetText(m_document, "video-output", fmt::format("Upscaling to 4K: {}", kFilters[std::clamp(m_settings.upscaleFilter, 0, 3)]));
				SetText(m_document, "video-hfr", fmt::format("120 Hz output: {}", m_settings.highFrameRate ? "On (where the TV takes it)" : "Off"));
				SetText(m_document, "video-overlay", fmt::format("Performance overlay: {}", m_settings.overlay ? "On" : "Off"));
				for (int row = 0; row < 3; row++)
					SetClass(m_document, fmt::format("video-row-{}", row), "focused", m_screen == kVideo && m_option == row);
				SetText(m_document, "audio-volume", fmt::format("Game volume: {}%", m_settings.volume));
				SetText(m_document, "controls-rumble", fmt::format("Vibration: {}", m_settings.rumble ? "On" : "Off"));
				std::string details;
				for (const auto& line : m_status.diagnostics)
					details += (details.empty() ? "" : "<br/>") + Rml::StringUtilities::EncodeRml(line);
				if (Rml::Element* element = m_document->GetElementById("setup-details"))
					element->SetInnerRML(details);
			}

			// -- settings > game files ---------------------------------------------------------

			void OpenFiles()
			{
				std::string start = m_settings.gamesFolder.empty() ? ps5paths::kGames : m_settings.gamesFolder;
				while (!BrowseTo(start) && start != "/")
					start = ParentPath(start);
				m_filesMessage.clear();
				SetClass(m_document, "files-dialog", "open", true);
				m_screen = kFiles;
				UpdateFiles();
			}

			bool BrowseTo(const std::string& folder)
			{
				bool ok = false;
				auto folders = ListEntries(folder, true, ok);
				if (!ok)
					return false;
				m_browseFolder = folder;
				m_browseEntries.clear();
				if (folder != "/")
					m_browseEntries.push_back("..");
				m_browseEntries.insert(m_browseEntries.end(), folders.begin(), folders.end());
				m_browseSelected = 0;
				return true;
			}

			void FilesKey(Key key)
			{
				const int count = (int)m_browseEntries.size();
				if (key == Key::Circle)
				{
					SetClass(m_document, "files-dialog", "open", false);
					m_screen = kSettings;
					UpdateSettingsList();
					return;
				}
				if (key == Key::Up && count)
					m_browseSelected = (m_browseSelected + count - 1) % count;
				else if (key == Key::Down && count)
					m_browseSelected = (m_browseSelected + 1) % count;
				else if ((key == Key::L1 || key == Key::R1) && count)
					m_browseSelected = std::clamp(m_browseSelected + (key == Key::R1 ? kFileRows : -kFileRows), 0, count - 1);
				else if (key == Key::Cross && count)
				{
					const std::string entry = m_browseEntries[m_browseSelected];
					const std::string from = m_browseFolder;
					const bool up = entry == "..";
					if (!BrowseTo(up ? ParentPath(m_browseFolder) : JoinPath(m_browseFolder, entry)))
						m_filesMessage = "This folder cannot be opened.";
					else
					{
						m_filesMessage.clear();
						if (up) // keep the folder just left in view
						{
							const std::string left = from.substr(from.find_last_of('/') + 1);
							for (int i = 0; i < (int)m_browseEntries.size(); i++)
								if (m_browseEntries[i] == left)
									m_browseSelected = i;
						}
					}
				}
				else if (key == Key::Triangle)
				{
					m_settings.gamesFolder = m_browseFolder;
					const bool saved = ps5settings::Save(m_settings);
					if (m_status.coreReady)
					{
						ps5emu::ApplyOptions({m_settings.gamesFolder, m_settings.overlay, m_settings.volume, m_settings.upscaleFilter});
						m_scanning = true;
						m_games.clear();
					}
					SetText(m_document, "about-games-path", ShortPath(m_settings.gamesFolder, 36));
					m_filesMessage = saved ? "Saved. PS5Cemu is looking for games there." : "The folder could not be saved. Please try again.";
				}
				else
					return;
				UpdateFiles();
			}

			void UpdateFiles()
			{
				const int count = (int)m_browseEntries.size();
				const int scroll = m_browseSelected < kFileRows ? 0 : m_browseSelected - (kFileRows - 1);
				for (int row = 0; row < kFileRows; row++)
				{
					const int index = scroll + row;
					const bool up = index < count && m_browseEntries[index] == "..";
					const std::string id = fmt::format("files-row-{}", row);
					SetClass(m_document, id, "focused", index == m_browseSelected);
					SetClass(m_document, id, "offscreen", index >= count);
					SetClass(m_document, id, "files-up", up);
					SetText(m_document, fmt::format("files-name-{}", row), index >= count ? "" : up ? "Parent folder" : m_browseEntries[index]);
				}
				SetClass(m_document, "files-empty", "visible", count == 0);
				SetText(m_document, "files-path", ShortPath(m_browseFolder, 52));
				SetText(m_document, "files-position", fmt::format("{} OF {}", count ? m_browseSelected + 1 : 0, count));

				// TRIANGLE uses the folder shown: show what Cemu would find in it
				SetText(m_document, "files-current", ShortPath(m_browseFolder, 64));
				const int games = CountGames(m_browseFolder);
				SetText(m_document, "files-games", games < 0 ? "Cannot be read" : Plural(games, "game", "games"));
				SetClass(m_document, "files-games", "ready", games > 0);
				const bool keys = IsFile(std::string(ps5paths::kRoot) + "/keys.txt");
				SetText(m_document, "files-keys", keys ? "Found in /data/ps5cemu" : "Missing (only .wud/.wux need it)");
				SetClass(m_document, "files-keys", "ready", keys);
				SetText(m_document, "files-in-use", ShortPath(m_settings.gamesFolder, 40));
				SetClass(m_document, "files-in-use", "ready", m_settings.gamesFolder == m_browseFolder);
				SetText(m_document, "files-message", m_filesMessage.empty() ?
					"Games can be .wua, .wud, .wux, or folders with code, content and meta. TRIANGLE uses the folder shown." :
					m_filesMessage);
			}

			Rml::ElementDocument* m_document;
			ps5settings::Launcher& m_settings;
			const Status& m_status;
			Screen m_screen = kHome;
			std::optional<ps5emu::Game> m_launch;

			std::vector<ps5emu::Game> m_games;
			bool m_scanning = false;
			std::time_t m_shownMinute = 0;

			int m_selected = kContinue;
			int m_lastIndex = -1;
			bool m_continueReady = false;
			std::vector<int> m_recent; // indices into m_games

			int m_librarySelected = 0;
			int m_packsGame = 0;
			Screen m_packsFrom = kHome;
			int m_packSelected = 0;
			int m_settingsSelected = 0;
			int m_option = 0;

			std::string m_browseFolder;
			std::vector<std::string> m_browseEntries; // ".." first unless at "/", then subfolders
			int m_browseSelected = 0;
			std::string m_filesMessage;
		};
	}

	std::optional<ps5emu::Game> Run(ps5settings::Launcher& settings, const Status& status)
	{
		std::string error;
		if (!ps5ui::Start(error))
		{
			ps5log::Line("[launcher] {}", error);
			ps5notify::Send("The launcher cannot show: " + error);
			return std::nullopt;
		}
		Launcher launcher(ps5ui::Document(), settings, status);
		launcher.Initialize();
		ps5ui::Frame();
		sceSystemServiceHideSplashScreen();
		Input input;
		uint64_t frames = 0;
		while (!launcher.Done())
		{
			if (++frames % 120 == 0)
				ps5pad::Rescan(); // controllers joining or leaving, about every two seconds
			for (const Key key : input.Poll())
				launcher.HandleKey(key);
			launcher.Poll();
			ps5ui::Frame();
		}
		// the loading screen stays on VideoOut while the launcher makes way for Cemu's renderer
		ps5ui::Frame();
		ps5ui::Stop();
		ps5log::Line("[launcher] starting {} ({:016x})", launcher.Choice()->name, launcher.Choice()->titleId);
		return launcher.Choice();
	}
}
