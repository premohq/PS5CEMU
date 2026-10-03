// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the launcher (launcher.h).
//
// Its screens follow ProsperoEden's launcher (headless/prosperoeden/eden_app.cpp, GPL-3.0-or-later,
// by BlackBearReloaded): the same element ids and classes ("open" shows a screen, "focused" the
// row under the cursor, "offscreen" hides unused rows), the same navigation on the home screen,
// and its folder browser, which reads folders with sceKernelGetdents as it does. The graphic
// packs, the controls and the installs are laid out as Cemu's own windows have them. One Launcher
// is Cemu's (main.rml) or Azahar's (azahar.rml); the start screen (start.rml) chooses which.

#include "launcher.h"
#include "ui_host.h"
#include "../app/boxart.h"
#include "../app/paths.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/notify.h"
#include "../ps5/pad.h"
#include "../ps5/privilege.h"
#include "../azahar/azahar.h"
#include "../azahar/controls.h"
#include "../azahar/library.h"

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <functional>
#include <initializer_list>
#include <map>
#include <set>
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

		// Text in what the launcher's fonts have (ProsperoEden's Montserrat atlases: ASCII, with ° and
		// •): accented letters as their plain ones, typographic quotes, dashes and marks as ASCII ones,
		// and anything else left out, so a name such as "Pokémon" shows as "Pokemon" and not "Pokmon".
		std::string Printable(const std::string& text)
		{
			static constexpr const char* kLatin1[64] = {
				"A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I", // C0
				"D", "N", "O", "O", "O", "O", "O", "x", "O", "U", "U", "U", "U", "Y", "Th", "ss", // D0
				"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i", // E0
				"d", "n", "o", "o", "o", "o", "o", "/", "o", "u", "u", "u", "u", "y", "th", "y", // F0
			};
			std::string out;
			for (size_t i = 0; i < text.size();)
			{
				const unsigned char lead = (unsigned char)text[i];
				const int length = lead < 0x80 ? 1 : lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
				uint32_t c = length == 1 ? lead : lead & (0xFF >> (length + 1));
				for (int k = 1; k < length && i + k < text.size(); k++)
					c = c << 6 | ((unsigned char)text[i + k] & 0x3F);
				const std::string_view whole(text.data() + i, std::min((size_t)length, text.size() - i));
				i += length;
				if (c < 0x80)
					out += (char)c;
				else if (c == 0xB0 || c == 0x2022) // the fonts have these
					out += whole;
				else if (c >= 0xC0 && c <= 0xFF)
					out += kLatin1[c - 0xC0];
				else if (c == 0x2018 || c == 0x2019 || c == 0xB4)
					out += '\'';
				else if (c == 0x201C || c == 0x201D)
					out += '"';
				else if (c == 0x2013 || c == 0x2014)
					out += '-';
				else if (c == 0x2026)
					out += "...";
				else if (c == 0x2122)
					out += "TM";
				else if (c == 0xAE)
					out += "(R)";
				else if (c == 0xA9)
					out += "(C)";
				else if (c == 0xA0)
					out += ' ';
			}
			return out;
		}

		void SetText(Rml::ElementDocument* document, const std::string& id, const std::string& text)
		{
			if (Rml::Element* element = document->GetElementById(id))
				element->SetInnerRML(Rml::StringUtilities::EncodeRml(Printable(text)));
		}

		// Text whose lines end with '\n', a line each (empty ones dropped).
		void SetLines(Rml::ElementDocument* document, const std::string& id, const std::string& text)
		{
			std::string rml;
			size_t start = 0;
			while (start < text.size())
			{
				size_t end = text.find('\n', start);
				if (end == std::string::npos)
					end = text.size();
				if (end > start)
					rml += (rml.empty() ? "" : "<br/>") + Rml::StringUtilities::EncodeRml(Printable(text.substr(start, end - start)));
				start = end + 1;
			}
			if (Rml::Element* element = document->GetElementById(id))
				element->SetInnerRML(rml);
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

		std::string Upper(std::string text)
		{
			for (char& c : text)
				c = (char)std::toupper((unsigned char)c);
			return text;
		}

		std::string Gigabytes(uint64_t bytes)
		{
			return fmt::format("{:.1f} GB", bytes / 1e9);
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

		bool Has3dsExtension(const std::string& name)
		{
			const std::string lower = Lower(name);
			for (const char* extension : {".3ds", ".cci", ".cxi", ".cia", ".3dsx", ".app", ".elf", ".axf", ".z3ds", ".zcci", ".zcxi", ".z3dsx"})
				if (lower.size() > std::strlen(extension) && lower.ends_with(extension))
					return true;
			return false;
		}

		// The 3DS games right in a folder (Azahar's library looks in the folders in it too). -1: unreadable.
		int Count3dsGames(const std::string& folder)
		{
			bool ok = false;
			const auto files = ListEntries(folder, false, ok);
			return ok ? (int)std::count_if(files.begin(), files.end(), Has3dsExtension) : -1;
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

		// The first row shown of a list with `visible` rows, keeping `selected` in view.
		int Scroll(int selected, int visible)
		{
			return selected < visible ? 0 : selected - (visible - 1);
		}

		// Up and Down move through a list, wrapping; L1 and R1 a page at a time. False for other keys.
		bool Browse(Key key, int& selected, int count, int page)
		{
			if (count <= 0)
				return false;
			if (key == Key::Up)
				selected = (selected + count - 1) % count;
			else if (key == Key::Down)
				selected = (selected + 1) % count;
			else if (key == Key::L1)
				selected = std::max(0, selected - page);
			else if (key == Key::R1)
				selected = std::min(count - 1, selected + page);
			else
				return false;
			return true;
		}

		const char* TypeName(ps5emu::EmulatedType type)
		{
			switch (type)
			{
			case ps5emu::EmulatedType::GamePad: return "Wii U GamePad";
			case ps5emu::EmulatedType::Pro: return "Wii U Pro Controller";
			case ps5emu::EmulatedType::Classic: return "Classic Controller";
			case ps5emu::EmulatedType::Wiimote: return "Wii Remote";
			case ps5emu::EmulatedType::Nunchuk: return "Wii Remote + Nunchuk";
			case ps5emu::EmulatedType::None: break;
			}
			return "No controller";
		}

		const char* KindName(ps5emu::InstallCandidate::Kind kind)
		{
			switch (kind)
			{
			case ps5emu::InstallCandidate::Kind::Game: return "Game";
			case ps5emu::InstallCandidate::Kind::Update: return "Update";
			case ps5emu::InstallCandidate::Kind::Dlc: return "DLC";
			case ps5emu::InstallCandidate::Kind::System: return "System title";
			case ps5emu::InstallCandidate::Kind::None: break;
			}
			return "";
		}

		enum Screen
		{
			kHome,
			kLibrary,
			kSettings,
			kVideo,
			kAudio,
			kDiagnostics,
			kAbout,
			kFiles,
			kPacks,
			kControls,
			kPlayer,
			kMapping,
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

		// Settings' list
		enum SettingsRow
		{
			kRowVideo,
			kRowAudio,
			kRowControls,
			kRowGameFiles,
			kRowInstall,
			kRowDiagnostics,
			kSettingsRows,
		};

		// The 3DS's controls (Azahar's side has one console, and these in place of a player's)
		enum ConsoleRow
		{
			kRowConsoleMotion,
			kRowConsoleDeadzone,
			kRowConsoleButtons,
			kRowConsoleReset,
			kConsoleRows,
		};

		constexpr const char* kResolutions[] = {"", "1x (400x240)", "2x (800x480)", "3x (1200x720)", "4x (1600x960)", "5x (2000x1200)",
			"6x (2400x1440)", "7x (2800x1680)", "8x (3200x1920)", "9x (3600x2160)", "10x (4000x2400)"};
		constexpr const char* kLayouts[] = {"Top above bottom", "Top screen only", "Large top screen", "Side by side"};
		constexpr const char* kTextureFilters[] = {"None", "Anime4K", "Bicubic", "ScaleForce", "xBRZ", "MMPX"};

		// A player's settings
		enum PlayerRow
		{
			kRowType,
			kRowMotion,
			kRowRumble,
			kRowLeftDeadzone,
			kRowRightDeadzone,
			kRowButtons,
			kRowReset,
			kPlayerRows,
		};

		constexpr int kListRows = 7, kFileRows = 6, kPresetRows = 4, kPickerRows = 6;
		constexpr const char* kHomeElements[] = {"header", "menu", "last-played-card", "recent-section", "startup-status", "footer"};
		// The screens that fill the display: one shows at a time, as the background shows through them
		constexpr const char* kFullScreens[] = {"rom-dialog", "packs-dialog", "settings-dialog", "controls-dialog", "player-dialog",
			"mapping-dialog", "files-dialog", "about-dialog", "loading-screen"};
		constexpr uint64_t kCaptureUs = 6000000; // how long a button mapping waits for a press

		class Launcher
		{
		public:
			Launcher(Rml::ElementDocument* document, System system, ps5settings::Launcher& settings, const Status& status)
				: m_document(document), m_system(system), m_settings(settings), m_status(status)
			{
			}

			void Initialize()
			{
				SetText(m_document, "brand-version", fmt::format("{}  /  PS5CEMU-HAR  /  v" PS5CEMU_VERSION, Is3ds() ? "NINTENDO 3DS" : "WII U"));
				const std::string& notice = Notice();
				SetText(m_document, "startup-status", notice);
				SetClass(m_document, "startup-status", "quiet", notice.empty());
				SetClass(m_document, "recent-section", "covered", !notice.empty());
				SetClass(m_document, "load-rom", "disabled", !CoreReady());
				SetText(m_document, "about-games-path", ShortPath(GamesFolder(), 36));
				if (!Is3ds())
				{
					SetText(m_document, "about-keys-path", std::string(ps5paths::kRoot) + "/keys.txt");
					SetText(m_document, "about-mlc-path", ps5paths::kMlc);
				}
				m_scanning = CoreReady() && SystemScanning();
				if (CoreReady())
					m_games = SystemGames();
				if (!m_scanning)
					FetchBoxArt();
				RefreshHome();
				m_selected = m_continueReady ? kContinue : CoreReady() ? kLibraryButton : kSettingsButton;
				UpdateHome();
				Poll();
			}

			bool Done() const { return m_launch.has_value() && m_screen == kLoading; }
			// The player went back to the start screen.
			bool Leaving() const { return m_leaving; }
			const std::optional<ps5emu::Game>& Choice() const { return m_launch; }

			// The clock, the library once Cemu has finished looking for games, a button being
			// mapped and an install.
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
				if (m_scanning && !SystemScanning())
				{
					m_scanning = false;
					m_games = SystemGames();
					ps5log::Line("[launcher] {} games", m_games.size());
					FetchBoxArt();
					RefreshHome();
					UpdateHome();
					if (m_screen == kLibrary)
						UpdateLibrary();
				}
				// a cover arrived: the selected game's may be it
				if (ps5boxart::Arrivals() != m_boxArrivals)
				{
					m_boxArrivals = ps5boxart::Arrivals();
					if (m_screen == kLibrary)
						UpdateLibrary();
				}
				if (m_capture.active)
					PollCapture();
				if (m_installing)
					PollInstall();
			}

			void HandleKey(Key key)
			{
				if (m_picker.open)
				{
					PickerKey(key);
					return;
				}
				switch (m_screen)
				{
				case kHome: HomeKey(key); break;
				case kLibrary: LibraryKey(key); break;
				case kPacks: PacksKey(key); break;
				case kSettings: SettingsKey(key); break;
				case kVideo:
				case kAudio:
				case kDiagnostics: SettingsPageKey(key); break;
				case kControls: ControlsKey(key); break;
				case kPlayer: PlayerKey(key); break;
				case kMapping: MappingKey(key); break;
				case kAbout:
					if (key == Key::Circle)
						GoHome();
					break;
				case kFiles: FilesKey(key); break;
				case kLoading: break;
				}
			}

		private:
			// -- the emulator this launcher is --------------------------------------------------

			bool Is3ds() const { return m_system == System::N3ds; }
			bool HasPacks() const { return !Is3ds(); }
			// Cemu's games need its core; Azahar's are read by the launcher itself
			bool CoreReady() const { return Is3ds() || m_status.coreReady; }
			const std::string& Notice() const { return Is3ds() ? m_status.notice3ds : m_status.notice; }
			bool SystemScanning() const { return Is3ds() ? ps5azahar::Scanning() : ps5emu::Scanning(); }
			std::vector<ps5emu::Game> SystemGames() const { return Is3ds() ? ps5azahar::ListGames() : ps5emu::ListGames(); }
			std::string& GamesFolder() { return Is3ds() ? m_settings.n3ds.gamesFolder : m_settings.gamesFolder; }
			uint64_t& LastGame() { return Is3ds() ? m_settings.n3ds.lastGame : m_settings.lastGame; }
			std::vector<uint64_t>& Recent() { return Is3ds() ? m_settings.n3ds.recent : m_settings.recent; }
			int& Volume() { return Is3ds() ? m_settings.n3ds.volume : m_settings.volume; }
			const char* Icon() const { return Is3ds() ? "icons/azahar.tga" : "icons/ps5cemu.tga"; }
			std::string CoverOf(uint64_t titleId) const { return Is3ds() ? ps5azahar::CoverPath(titleId) : ps5emu::CoverPath(titleId); }
			ps5boxart::System BoxSystem() const { return Is3ds() ? ps5boxart::System::N3ds : ps5boxart::System::WiiU; }

			// GameTDB's covers for the games that have none yet (boxart.h)
			void FetchBoxArt()
			{
				std::vector<std::string> ids;
				for (const auto& game : m_games)
					if (!game.gameId.empty())
						ids.push_back(game.gameId);
				ps5boxart::Fetch(BoxSystem(), ids);
			}

			// The library's details: the game's box art, fitted to the cover's area (288x352, at its
			// top, centred across), or its icon, square, as before covers came. Box art no longer
			// shown is let go of, so a long scroll through the library does not keep every one.
			void ShowCover(const ps5emu::Game* game)
			{
				Rml::Element* cover = m_document->GetElementById("game-cover");
				if (!cover)
					return;
				std::string source = Icon();
				float width = 288, height = 288;
				std::string caption;
				if (game)
				{
					const std::string boxArt = ps5boxart::Path(BoxSystem(), game->gameId);
					int w = 0, h = 0;
					if (!boxArt.empty() && ps5boxart::ImageSize(boxArt, w, h))
					{
						const float scale = std::min(288.0f / w, 352.0f / h);
						width = std::round(w * scale);
						height = std::round(h * scale);
						source = boxArt;
					}
					else
					{
						const std::string icon = CoverOf(game->titleId);
						if (!icon.empty())
							source = icon;
						else
							caption = "No icon";
					}
				}
				cover->SetAttribute("src", source);
				cover->SetProperty("width", fmt::format("{}px", width));
				cover->SetProperty("height", fmt::format("{}px", height));
				cover->SetProperty("left", fmt::format("{}px", 36 + (288 - width) / 2));
				SetText(m_document, "cover-caption", caption);
				if (!m_shownBoxArt.empty() && m_shownBoxArt != source)
					Rml::ReleaseTexture(m_shownBoxArt);
				m_shownBoxArt = source.find("/boxart/") != std::string::npos ? source : std::string();
			}

			// The rows' icons: those scrolled away are let go of once there are many
			void ReleaseRowIcons(const std::vector<std::string>& shown)
			{
				for (const auto& icon : shown)
					m_rowIcons.insert(icon);
				if (m_rowIcons.size() <= 48)
					return;
				for (const auto& icon : m_rowIcons)
					if (std::find(shown.begin(), shown.end(), icon) == shown.end())
						Rml::ReleaseTexture(icon);
				m_rowIcons = {shown.begin(), shown.end()};
			}

			// -- screens -----------------------------------------------------------------------

			// One full screen open (none: the home screen), the rest closed.
			void Show(const char* id, Screen screen)
			{
				for (const char* full : kFullScreens)
					SetClass(m_document, full, "open", id && std::strcmp(id, full) == 0);
				for (const char* element : kHomeElements)
					SetClass(m_document, element, "library-hidden", id != nullptr);
				m_screen = screen;
			}

			void GoHome()
			{
				Show(nullptr, kHome);
				RefreshHome();
				UpdateHome();
			}

			// -- a dropdown --------------------------------------------------------------------

			struct Picker
			{
				bool open = false;
				std::vector<std::string> options;
				int active = -1; // the one in use
				int selected = 0;
				std::function<void(int)> choose;
			};

			void OpenPicker(const std::string& kicker, const std::string& title, std::vector<std::string> options, int active,
				std::function<void(int)> choose)
			{
				if (options.empty())
					return;
				m_picker = {true, std::move(options), active, std::max(active, 0), std::move(choose)};
				SetText(m_document, "picker-kicker", Upper(kicker));
				SetText(m_document, "picker-title", title);
				SetClass(m_document, "picker", "open", true);
				UpdatePicker();
			}

			void PickerKey(Key key)
			{
				if (key == Key::Circle)
					m_picker.open = false;
				else if (key == Key::Cross)
				{
					m_picker.open = false;
					SetClass(m_document, "picker", "open", false);
					m_picker.choose(m_picker.selected);
					return;
				}
				else
					Browse(key, m_picker.selected, (int)m_picker.options.size(), kPickerRows);
				SetClass(m_document, "picker", "open", m_picker.open);
				UpdatePicker();
			}

			void UpdatePicker()
			{
				const int count = (int)m_picker.options.size();
				const int scroll = Scroll(m_picker.selected, kPickerRows);
				for (int row = 0; row < kPickerRows; row++)
				{
					const int index = scroll + row;
					SetClass(m_document, fmt::format("picker-row-{}", row), "focused", index == m_picker.selected);
					SetClass(m_document, fmt::format("picker-row-{}", row), "offscreen", index >= count);
					SetText(m_document, fmt::format("picker-name-{}", row), index < count ? m_picker.options[index] : "");
					SetText(m_document, fmt::format("picker-mark-{}", row), index == m_picker.active ? "IN USE" : "");
				}
				SetText(m_document, "picker-position", count > kPickerRows ? fmt::format("{} OF {}", m_picker.selected + 1, count) : "");
			}

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
				const std::string cover = CoverOf(game.titleId);
				return cover.empty() ? Icon() : cover;
			}

			// Recently played games, unless a notice has their place.
			int RecentCount() const
			{
				return Notice().empty() ? (int)m_recent.size() : 0;
			}

			void RefreshHome()
			{
				m_lastIndex = FindGame(LastGame());
				m_continueReady = CoreReady() && m_lastIndex >= 0;
				if (m_lastIndex >= 0)
				{
					const auto& game = m_games[m_lastIndex];
					SetText(m_document, "last-played-title", game.name);
					if (Is3ds())
						SetText(m_document, "last-played-caption", game.publisher.empty() ? "Last game played" : "Last game played  /  " + game.publisher);
					else
						SetText(m_document, "last-played-caption", fmt::format("Last game played  /  v{}{}", game.version,
							game.dlcCount ? "  /  DLC" : ""));
					SetImage(m_document, "last-played-cover", Cover(game));
				}
				else if (LastGame() && CoreReady() && !m_scanning)
				{
					SetText(m_document, "last-played-title", "Your last game is not here");
					SetText(m_document, "last-played-caption", "It is no longer in the game files folder.");
				}
				SetText(m_document, "continue-copy", m_continueReady ? "Launch game" : "Open library");
				SetClass(m_document, "continue-game", "disabled", !CoreReady());
				SetClass(m_document, "hero-options", "disabled", !m_continueReady);

				m_recent.clear();
				for (uint64_t titleId : Recent())
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
				if (!CoreReady())
					system = "Setup required";
				else if (m_scanning)
					system = "Looking for games";
				else
					system = Plural((int)m_games.size(), "game", "games");
				if (Is3ds())
					system += ps5azahar::Available() ? (PS5_JitAvailable() ? "  /  dynarmic JIT" : "  /  Interpreter (no JIT)") : "  /  Core not in this build";
				else
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
				const int recentCount = RecentCount();
				const bool ready = CoreReady();
				const bool recentShown = Notice().empty();
				if (key == Key::Circle)
				{
					m_leaving = true;
					return;
				}
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
					else if ((m_selected == kContinue || m_selected == kPacksButton) && recentShown)
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
						if (m_continueReady && HasPacks())
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
				else if (key == Key::Triangle && m_continueReady && HasPacks() && (m_selected == kContinue || m_selected == kPacksButton))
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
						Show("about-dialog", kAbout);
					else if (m_selected == kPacksButton && m_continueReady)
						OpenPacks(m_lastIndex, kHome);
					else if (m_selected >= kRecent0 && m_selected < kRecent0 + recentCount)
						Launch(m_recent[m_selected - kRecent0]);
				}
				UpdateHome();
			}

			void Launch(int index)
			{
				if (index < 0 || index >= (int)m_games.size())
					return;
				const auto& game = m_games[index];
				ps5settings::AddRecent(LastGame(), Recent(), game.titleId);
				ps5settings::Save(m_settings);
				SetText(m_document, "loading-title", game.name);
				SetText(m_document, "loading-caption", "Starting");
				SetImage(m_document, "loading-cover", Cover(game));
				Show("loading-screen", kLoading);
				m_launch = game;
			}

			// -- library -----------------------------------------------------------------------

			void OpenLibrary(int select)
			{
				Show("rom-dialog", kLibrary);
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
					GoHome();
					return;
				}
				if (count == 0)
					return;
				if (key == Key::Left)
					m_librarySelected = std::max(0, m_librarySelected - kListRows);
				else if (key == Key::Right)
					m_librarySelected = std::min(count - 1, m_librarySelected + kListRows);
				else if (key == Key::Cross)
				{
					Launch(m_librarySelected);
					return;
				}
				else if (key == Key::Triangle && HasPacks())
				{
					OpenPacks(m_librarySelected, kLibrary);
					return;
				}
				else
					Browse(key, m_librarySelected, count, kListRows);
				UpdateLibrary();
			}

			void UpdateLibrary()
			{
				const int count = (int)m_games.size();
				const int scroll = Scroll(m_librarySelected, kListRows);
				std::vector<std::string> icons;
				for (int row = 0; row < kListRows; row++)
				{
					const int index = scroll + row;
					const std::string id = fmt::format("rom-row-{}", row);
					SetClass(m_document, id, "focused", index == m_librarySelected);
					SetClass(m_document, id, "offscreen", index >= count);
					SetText(m_document, fmt::format("rom-name-{}", row), index < count ? m_games[index].name : "");
					SetText(m_document, fmt::format("rom-format-{}", row), index < count ? m_games[index].format : "");
					if (index < count)
					{
						icons.push_back(Cover(m_games[index]));
						SetImage(m_document, fmt::format("rom-icon-{}", row), icons.back());
					}
				}
				ReleaseRowIcons(icons);
				SetText(m_document, "library-empty", m_scanning ? "Looking for games..." : "No games found. Put them in the game files folder.");
				SetClass(m_document, "library-empty", "visible", count == 0);
				SetClass(m_document, "rom-scrollbar", "offscreen", count <= kListRows);
				if (count > kListRows)
					if (Rml::Element* thumb = m_document->GetElementById("rom-scrollbar-thumb"))
						thumb->SetProperty("top", fmt::format("{}px", 496 * scroll / (count - kListRows)));
				SetText(m_document, "library-position", fmt::format("{} OF {}", count ? m_librarySelected + 1 : 0, count));
				if (count == 0)
				{
					SetText(m_document, "game-detail-title", "No game selected");
					for (const char* id : {"game-detail-format", "game-detail-size", "game-detail-dlc", "game-detail-path", "game-packs-value"})
						SetText(m_document, id, "-");
					ShowCover(nullptr);
					return;
				}
				const auto& game = m_games[m_librarySelected];
				SetText(m_document, "game-detail-title", game.name);
				SetText(m_document, "game-detail-path", ShortPath(game.path.string(), 44));
				ShowCover(&game);
				if (Is3ds())
				{
					// a title ID made from the path is no 3DS's
					const bool own = (game.titleId >> 60) == 0xF;
					SetText(m_document, "game-detail-format", own ? "-" : Hex(game.titleId));
					SetText(m_document, "game-detail-size", game.publisher.empty() ? "-" : game.publisher);
					SetText(m_document, "game-detail-dlc", game.format);
					return;
				}
				SetText(m_document, "game-detail-format", Hex(game.titleId));
				SetText(m_document, "game-detail-size", game.hasUpdate ? fmt::format("v{} (update)", game.version) : fmt::format("v{}", game.version));
				SetText(m_document, "game-detail-dlc", game.dlcCount ? "Installed" : "None");
				const auto packs = ps5emu::ListGraphicPacks(game.titleId);
				const int enabled = ps5emu::EnabledGraphicPackCount(game.titleId);
				SetText(m_document, "game-packs-value", packs.empty() ? "None available" : fmt::format("{} of {} on", enabled, packs.size()));
			}

			// -- graphic packs -----------------------------------------------------------------

			// A row of the packs' list: a pack (its index in ListGraphicPacks), or the heading of a
			// folder of them, as Cemu's window shows them in a tree.
			struct PackItem
			{
				int pack = -1;
				std::string heading;
			};

			uint64_t PacksTitle() const { return m_games[m_packsGame].titleId; }

			// The packs as they are now; the ones in no folder first, then each folder's.
			void RefreshPacks()
			{
				m_packs = ps5emu::ListGraphicPacks(PacksTitle());
				const int selectedPack = m_packItem < (int)m_packItems.size() ? m_packItems[m_packItem].pack : -1;
				m_packItems.clear();
				std::vector<std::string> folders;
				for (int i = 0; i < (int)m_packs.size(); i++)
				{
					if (m_packs[i].folder.empty())
						m_packItems.push_back({i, {}});
					else if (std::find(folders.begin(), folders.end(), m_packs[i].folder) == folders.end())
						folders.push_back(m_packs[i].folder);
				}
				for (const auto& folder : folders)
				{
					m_packItems.push_back({-1, folder});
					for (int i = 0; i < (int)m_packs.size(); i++)
						if (m_packs[i].folder == folder)
							m_packItems.push_back({i, {}});
				}
				m_packItem = 0;
				for (int i = 0; i < (int)m_packItems.size() && selectedPack >= 0; i++)
					if (m_packItems[i].pack == selectedPack)
						m_packItem = i;
				SettlePackItem(1);
			}

			// Off a heading, onto the next pack in the direction given (or the other way at the end).
			void SettlePackItem(int direction)
			{
				const int count = (int)m_packItems.size();
				for (int tries = 0; tries < count && m_packItems[m_packItem].pack < 0; tries++)
				{
					const int next = m_packItem + direction;
					if (next < 0 || next >= count)
						direction = -direction;
					else
						m_packItem = next;
				}
			}

			const ps5emu::GraphicPackInfo* SelectedPack() const
			{
				if (m_packItem >= (int)m_packItems.size() || m_packItems[m_packItem].pack < 0)
					return nullptr;
				return &m_packs[m_packItems[m_packItem].pack];
			}

			void OpenPacks(int gameIndex, Screen from)
			{
				if (gameIndex < 0 || gameIndex >= (int)m_games.size())
					return;
				m_packsGame = gameIndex;
				m_packsFrom = from;
				m_packItem = 0;
				m_packItems.clear();
				m_presetsFocus = false;
				m_presetSelected = 0;
				Show("packs-dialog", kPacks);
				SetText(m_document, "packs-game", m_games[gameIndex].name);
				RefreshPacks();
				UpdatePacks();
			}

			void ChoosePreset(int choiceIndex, int presetIndex)
			{
				const auto* pack = SelectedPack();
				if (!pack || choiceIndex >= (int)pack->choices.size())
					return;
				const auto& choice = pack->choices[choiceIndex];
				if (presetIndex < 0 || presetIndex >= (int)choice.presets.size())
					return;
				ps5emu::SetGraphicPackPreset(PacksTitle(), m_packItems[m_packItem].pack, choice.category, choice.presets[presetIndex]);
				RefreshPacks();
				m_presetSelected = std::clamp(m_presetSelected, 0, std::max(0, (int)(SelectedPack() ? SelectedPack()->choices.size() : 1) - 1));
			}

			void PacksKey(Key key)
			{
				if (key == Key::Circle)
				{
					if (m_presetsFocus)
						m_presetsFocus = false;
					else if (m_packsFrom == kLibrary)
					{
						OpenLibrary(m_packsGame);
						return;
					}
					else
					{
						GoHome();
						return;
					}
				}
				const auto* pack = SelectedPack();
				if (!pack)
				{
					UpdatePacks();
					return;
				}
				if (!m_presetsFocus)
				{
					const int count = (int)m_packItems.size();
					if (key == Key::Up || key == Key::Down)
					{
						Browse(key, m_packItem, count, kListRows);
						SettlePackItem(key == Key::Up ? -1 : 1);
					}
					else if (key == Key::L1 || key == Key::R1)
					{
						Browse(key, m_packItem, count, kListRows);
						SettlePackItem(key == Key::L1 ? -1 : 1);
					}
					else if (key == Key::Cross)
					{
						ps5emu::ToggleGraphicPack(PacksTitle(), m_packItems[m_packItem].pack);
						RefreshPacks();
					}
					else if ((key == Key::Right || key == Key::Square) && !pack->choices.empty())
					{
						m_presetsFocus = true;
						m_presetSelected = 0;
					}
				}
				else if (pack->choices.empty())
					m_presetsFocus = false;
				else if (!Browse(key, m_presetSelected, (int)pack->choices.size(), kPresetRows))
				{
					const auto& choice = pack->choices[std::min(m_presetSelected, (int)pack->choices.size() - 1)];
					if (key == Key::Cross)
					{
						const int choiceIndex = m_presetSelected;
						OpenPicker(choice.category.empty() ? "Preset" : choice.category, pack->name, choice.presets, choice.active,
							[this, choiceIndex](int preset) {
								ChoosePreset(choiceIndex, preset);
								UpdatePacks();
							});
					}
					else if (key == Key::Left || key == Key::Right)
					{
						const int presets = (int)choice.presets.size();
						ChoosePreset(m_presetSelected, (choice.active + (key == Key::Right ? 1 : presets - 1)) % presets);
					}
				}
				UpdatePacks();
			}

			void UpdatePacks()
			{
				const int count = (int)m_packItems.size();
				const int scroll = Scroll(m_packItem, kListRows);
				int packNumber = 0, packCount = 0;
				for (int i = 0; i < count; i++)
					if (m_packItems[i].pack >= 0)
					{
						packCount++;
						if (i <= m_packItem)
							packNumber++;
					}
				for (int row = 0; row < kListRows; row++)
				{
					const int index = scroll + row;
					const std::string id = fmt::format("pack-row-{}", row);
					const bool present = index < count;
					const bool heading = present && m_packItems[index].pack < 0;
					SetClass(m_document, id, "focused", index == m_packItem && !m_presetsFocus);
					SetClass(m_document, id, "chosen", index == m_packItem && m_presetsFocus);
					SetClass(m_document, id, "offscreen", !present);
					SetClass(m_document, id, "pack-header", heading);
					const auto* pack = present && !heading ? &m_packs[m_packItems[index].pack] : nullptr;
					SetText(m_document, fmt::format("pack-name-{}", row), heading ? Upper(m_packItems[index].heading) : pack ? pack->name : "");
					SetText(m_document, fmt::format("pack-state-{}", row), pack ? (pack->enabled ? "ON" : "OFF") : "");
					SetClass(m_document, fmt::format("pack-state-{}", row), "on", pack && pack->enabled);
				}
				SetClass(m_document, "packs-empty", "visible", packCount == 0);
				SetText(m_document, "packs-position", fmt::format("{} OF {}", packNumber, packCount));

				static constexpr const char* kListHints[] = {"On / off", "Back", "Browse packs", "Presets"};
				static constexpr const char* kPresetHints[] = {"Choose", "Back to the packs", "Browse presets", "Change"};
				for (int i = 0; i < 4; i++)
					SetText(m_document, fmt::format("packs-hint-{}", i), m_presetsFocus ? kPresetHints[i] : kListHints[i]);

				const auto* pack = SelectedPack();
				if (!pack)
				{
					for (const char* id : {"pack-detail-title", "pack-detail-description"})
						SetText(m_document, id, "");
					SetClass(m_document, "presets-empty", "visible", false);
					for (int row = 0; row < kPresetRows; row++)
						SetClass(m_document, fmt::format("preset-row-{}", row), "offscreen", true);
					return;
				}
				SetText(m_document, "pack-detail-kicker", pack->folder.empty() ? "GRAPHIC PACK" : "GRAPHIC PACK  /  " + Upper(pack->folder));
				SetText(m_document, "pack-detail-title", pack->name);
				SetLines(m_document, "pack-detail-description", pack->description.empty() ? "This pack has no description." : pack->description);
				const int choices = (int)pack->choices.size();
				SetText(m_document, "presets-kicker", choices == 0 ? "PRESETS" : pack->enabled ? "PRESETS" : "PRESETS  /  CHOOSING ONE TURNS THE PACK ON");
				SetClass(m_document, "presets-empty", "visible", choices == 0);
				m_presetSelected = std::clamp(m_presetSelected, 0, std::max(0, choices - 1));
				const int presetScroll = Scroll(m_presetSelected, kPresetRows);
				for (int row = 0; row < kPresetRows; row++)
				{
					const int index = presetScroll + row;
					const std::string id = fmt::format("preset-row-{}", row);
					SetClass(m_document, id, "offscreen", index >= choices);
					SetClass(m_document, id, "focused", m_presetsFocus && index == m_presetSelected);
					if (index >= choices)
						continue;
					const auto& choice = pack->choices[index];
					SetText(m_document, fmt::format("preset-label-{}", row), choice.category.empty() ? "Preset" : choice.category);
					SetText(m_document, fmt::format("preset-value-{}", row), choice.presets.empty() ? "" : choice.presets[std::clamp(choice.active, 0, (int)choice.presets.size() - 1)]);
				}
				SetText(m_document, "presets-position", choices > kPresetRows ? fmt::format("{} OF {}", m_presetSelected + 1, choices) : "");
			}

			// -- settings ----------------------------------------------------------------------

			void OpenSettings()
			{
				Show("settings-dialog", kSettings);
				UpdateSettingsList();
			}

			void UpdateSettingsList()
			{
				for (int row = 0; row < kSettingsRows; row++)
					SetClass(m_document, fmt::format("settings-row-{}", row), "focused", m_settingsSelected == row);
			}

			void SettingsKey(Key key)
			{
				if (key == Key::Circle)
				{
					GoHome();
					return;
				}
				if (Browse(key, m_settingsSelected, kSettingsRows, kSettingsRows))
				{
					UpdateSettingsList();
					return;
				}
				if (key != Key::Cross)
					return;
				switch (m_settingsSelected)
				{
				case kRowVideo: OpenSettingsPage("video-dialog", kVideo); break;
				case kRowAudio: OpenSettingsPage("audio-dialog", kAudio); break;
				case kRowDiagnostics: OpenSettingsPage("diagnostics-dialog", kDiagnostics); break;
				case kRowControls:
					if (Is3ds())
						OpenConsole();
					else
						OpenControls();
					break;
				case kRowGameFiles: OpenFiles(FilesMode::GamesFolder); break;
				case kRowInstall: OpenFiles(Is3ds() ? FilesMode::InstallCia : FilesMode::Install); break;
				}
			}

			// Video, Audio and Diagnostics: over the settings
			void OpenSettingsPage(const char* id, Screen screen)
			{
				SetClass(m_document, id, "open", true);
				m_screen = screen;
				m_option = 0;
				UpdateSettingsPage();
			}

			static const char* PageOf(Screen screen)
			{
				return screen == kVideo ? "video-dialog" : screen == kAudio ? "audio-dialog" : "diagnostics-dialog";
			}

			void SettingsPageKey(Key key)
			{
				if (key == Key::Circle)
				{
					SetClass(m_document, PageOf(m_screen), "open", false);
					m_screen = kSettings;
					UpdateSettingsList();
					return;
				}
				const bool change = key == Key::Cross || key == Key::Left || key == Key::Right;
				bool changed = false;
				if (m_screen == kVideo && Is3ds())
				{
					auto& n3ds = m_settings.n3ds;
					const int step = key == Key::Left ? -1 : 1;
					if (key == Key::Up || key == Key::Down)
						Browse(key, m_option, 3, 3);
					else if (change && m_option == 0)
						changed = (n3ds.resolution = (n3ds.resolution - 1 + step + 10) % 10 + 1, true);
					else if (change && m_option == 1)
						changed = (n3ds.layout = (n3ds.layout + step + 4) % 4, true);
					else if (change && m_option == 2)
						changed = (n3ds.textureFilter = (n3ds.textureFilter + step + 6) % 6, true);
				}
				else if (m_screen == kVideo)
				{
					if (key == Key::Up || key == Key::Down)
						Browse(key, m_option, 3, 3);
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
					Volume() = std::clamp(Volume() + (key == Key::Right ? 10 : -10), 0, 100);
					changed = true;
				}
				if (changed)
					SaveSettings();
				UpdateSettingsPage();
			}

			void SaveSettings()
			{
				if (!ps5settings::Save(m_settings))
					ps5log::Line("[launcher] could not save {}", ps5paths::kLauncherSettings);
			}

			void UpdateSettingsPage()
			{
				static constexpr const char* kFilters[] = {"Linear", "Bicubic", "Bicubic Hermite", "Nearest neighbour"};
				const auto& n3ds = m_settings.n3ds;
				const std::pair<const char*, std::string> video[] = {
					Is3ds() ? std::pair<const char*, std::string>{"Internal resolution", kResolutions[std::clamp(n3ds.resolution, 1, 10)]} :
						std::pair<const char*, std::string>{"Upscaling to 4K", kFilters[std::clamp(m_settings.upscaleFilter, 0, 3)]},
					Is3ds() ? std::pair<const char*, std::string>{"Screen layout", kLayouts[std::clamp(n3ds.layout, 0, 3)]} :
						std::pair<const char*, std::string>{"120 Hz output", m_settings.highFrameRate ? "On, where the TV has it" : "Off"},
					Is3ds() ? std::pair<const char*, std::string>{"Texture filter", kTextureFilters[std::clamp(n3ds.textureFilter, 0, 5)]} :
						std::pair<const char*, std::string>{"Performance overlay", m_settings.overlay ? "On" : "Off"},
				};
				for (int row = 0; row < 3; row++)
				{
					SetText(m_document, fmt::format("video-label-{}", row), video[row].first);
					SetText(m_document, fmt::format("video-value-{}", row), video[row].second);
					SetClass(m_document, fmt::format("video-row-{}", row), "focused", m_screen == kVideo && m_option == row);
				}
				SetText(m_document, "audio-volume", fmt::format("{}%", Volume()));
				std::string details;
				for (const auto& line : m_status.diagnostics)
					details += (details.empty() ? "" : "<br/>") + Rml::StringUtilities::EncodeRml(line);
				if (Rml::Element* element = m_document->GetElementById("setup-details"))
					element->SetInnerRML(details);
			}

			// -- settings > controls -----------------------------------------------------------

			void OpenControls()
			{
				Show("controls-dialog", kControls);
				UpdateControls();
			}

			void ControlsKey(Key key)
			{
				if (key == Key::Circle)
				{
					OpenSettings();
					return;
				}
				if (key == Key::Cross)
				{
					OpenPlayer(m_controlSelected);
					return;
				}
				Browse(key, m_controlSelected, ps5pad::kMaxPlayers, ps5pad::kMaxPlayers);
				UpdateControls();
			}

			void UpdateControls()
			{
				for (int player = 0; player < ps5pad::kMaxPlayers; player++)
				{
					const auto controls = ps5emu::GetPlayerControls(player);
					SetClass(m_document, fmt::format("control-row-{}", player), "focused", player == m_controlSelected);
					SetText(m_document, fmt::format("control-value-{}", player), TypeName(controls.type));
				}
				const auto controls = ps5emu::GetPlayerControls(m_controlSelected);
				SetText(m_document, "control-kicker", fmt::format("PLAYER {}", m_controlSelected + 1));
				SetText(m_document, "control-title", TypeName(controls.type));
				const std::pair<const char*, std::string> lines[] = {
					{"DUALSENSE", controls.connected ? fmt::format("Player {}'s, connected", m_controlSelected + 1) : "Not connected"},
					{"MOTION", !controls.hasMotion ? "Not on this controller" : controls.motion ? "On" : "Off"},
					{"VIBRATION", controls.rumble ? fmt::format("{}%", controls.rumble) : "Off"},
				};
				for (int i = 0; i < 3; i++)
				{
					SetText(m_document, fmt::format("control-label-{}", i), lines[i].first);
					SetText(m_document, fmt::format("control-detail-{}", i), lines[i].second);
				}
			}

			// -- settings > controls, on Azahar's side: the 3DS ------------------------------------

			void OpenConsole()
			{
				m_playerRow = 0;
				m_resetArmed = false;
				Show("player-dialog", kPlayer);
				SetText(m_document, "player-title", "Controls");
				SetText(m_document, "player-copy", "The DualSense as the 3DS: its touchpad is the touch screen");
				UpdateConsole();
			}

			void ConsoleKey(Key key)
			{
				auto& n3ds = m_settings.n3ds;
				if (key == Key::Circle)
				{
					OpenSettings();
					return;
				}
				if (key == Key::Up || key == Key::Down)
				{
					Browse(key, m_playerRow, kConsoleRows, kConsoleRows);
					m_resetArmed = false;
					UpdateConsole();
					return;
				}
				const bool left = key == Key::Left, cross = key == Key::Cross;
				if (!left && !cross && key != Key::Right)
					return;
				switch (m_playerRow)
				{
				case kRowConsoleMotion: n3ds.motion = !n3ds.motion; break;
				case kRowConsoleDeadzone:
				{
					int value = n3ds.deadzone + (left ? -5 : 5);
					if (cross && value > 50)
						value = 0;
					n3ds.deadzone = std::clamp(value, 0, 50);
					break;
				}
				case kRowConsoleButtons:
					if (cross)
					{
						OpenMapping();
						return;
					}
					break;
				case kRowConsoleReset:
					if (!cross)
						break;
					if (m_resetArmed)
						ps5azahar::ResetControls(n3ds);
					m_resetArmed = !m_resetArmed;
					break;
				}
				SaveSettings();
				UpdateConsole();
			}

			void UpdateConsole()
			{
				const auto& n3ds = m_settings.n3ds;
				const auto mappings = ps5azahar::ListMappings(n3ds);
				const int mapped = (int)std::count_if(mappings.begin(), mappings.end(), [](const ps5emu::ButtonMapping& m) { return !m.input.empty(); });
				struct Row
				{
					const char* name;
					std::string value;
					const char* help;
				};
				const Row rows[kConsoleRows] = {
					{"Motion controls", n3ds.motion ? "On" : "Off",
						"The DualSense's gyroscope and accelerometer as the 3DS's own, for the games that aim or steer by tilting it."},
					{"Stick deadzone", fmt::format("{}%", n3ds.deadzone),
						"How far a stick moves before the game sees it, for the circle pad and the C-stick. Raise it if something "
						"drifts when you let go of the stick; lower it for finer control."},
					{"Buttons", Plural(mapped, "button set", "buttons set"),
						"Which DualSense button is which of the 3DS's.\nA is on Circle and B on Cross by default, where the 3DS has "
						"them; the circle pad is the left stick, the C-stick the right one, and the touchpad the touch screen."},
					{"Reset to defaults", m_resetArmed ? "Press Cross again" : "", "The default buttons, motion and deadzone."},
				};
				for (int row = 0; row < kPlayerRows; row++)
				{
					const std::string id = fmt::format("player-row-{}", row);
					const bool present = row < kConsoleRows;
					SetClass(m_document, id, "offscreen", !present);
					SetClass(m_document, id, "focused", row == m_playerRow);
					SetClass(m_document, id, "dimmed", false);
					SetText(m_document, fmt::format("player-name-{}", row), present ? rows[row].name : "");
					SetText(m_document, fmt::format("player-value-{}", row), present ? rows[row].value : "");
				}
				SetText(m_document, "player-detail-title", rows[m_playerRow].name);
				SetLines(m_document, "player-help", rows[m_playerRow].help);
				static constexpr const char* kCrossHints[kConsoleRows] = {"On / off", "More", "Open", "Reset"};
				SetText(m_document, "player-hint-0", kCrossHints[m_playerRow]);
			}

			// -- settings > controls > a player ------------------------------------------------

			void OpenPlayer(int player)
			{
				m_player = player;
				m_playerRow = 0;
				m_resetArmed = false;
				Show("player-dialog", kPlayer);
				SetText(m_document, "player-title", fmt::format("Player {}", player + 1));
				UpdatePlayer();
			}

			// The emulated controllers a player can have: Cemu has two GamePads at most.
			std::vector<ps5emu::EmulatedType> TypesFor(int player) const
			{
				int otherGamePads = 0;
				for (int other = 0; other < ps5pad::kMaxPlayers; other++)
					if (other != player && ps5emu::GetPlayerControls(other).type == ps5emu::EmulatedType::GamePad)
						otherGamePads++;
				std::vector<ps5emu::EmulatedType> types;
				if (otherGamePads < 2)
					types.push_back(ps5emu::EmulatedType::GamePad);
				for (auto type : {ps5emu::EmulatedType::Pro, ps5emu::EmulatedType::Classic, ps5emu::EmulatedType::Wiimote, ps5emu::EmulatedType::Nunchuk})
					types.push_back(type);
				return types;
			}

			void PlayerKey(Key key)
			{
				if (Is3ds())
				{
					ConsoleKey(key);
					return;
				}
				if (key == Key::Circle)
				{
					OpenControls();
					return;
				}
				if (key == Key::Up || key == Key::Down)
				{
					Browse(key, m_playerRow, kPlayerRows, kPlayerRows);
					m_resetArmed = false;
					UpdatePlayer();
					return;
				}
				const auto controls = ps5emu::GetPlayerControls(m_player);
				const bool left = key == Key::Left, right = key == Key::Right, cross = key == Key::Cross;
				if (!left && !right && !cross)
					return;
				switch (m_playerRow)
				{
				case kRowType:
				{
					const auto types = TypesFor(m_player);
					int active = -1;
					for (int i = 0; i < (int)types.size(); i++)
						if (types[i] == controls.type)
							active = i;
					if (cross)
					{
						std::vector<std::string> names;
						for (auto type : types)
							names.push_back(TypeName(type));
						OpenPicker("Emulated controller", fmt::format("Player {}", m_player + 1), names, active, [this, types](int index) {
							ps5emu::SetEmulatedType(m_player, types[index]);
							UpdatePlayer();
						});
						return;
					}
					const int count = (int)types.size();
					ps5emu::SetEmulatedType(m_player, types[((active < 0 ? 0 : active) + (right ? 1 : count - 1)) % count]);
					break;
				}
				case kRowMotion:
					if (controls.hasMotion)
						ps5emu::SetMotion(m_player, !controls.motion);
					break;
				case kRowRumble:
				{
					int rumble = controls.rumble + (left ? -10 : 10);
					if (cross && rumble > 100)
						rumble = 0;
					rumble = std::clamp(rumble, 0, 100);
					ps5emu::SetRumble(m_player, rumble);
					if (rumble > 0 && !m_settings.rumble)
					{
						// the launcher's own switch would keep the motors still
						m_settings.rumble = true;
						ps5pad::SetVibrationEnabled(true);
						SaveSettings();
					}
					break;
				}
				case kRowLeftDeadzone:
				case kRowRightDeadzone:
				{
					const bool leftStick = m_playerRow == kRowLeftDeadzone;
					int value = (leftStick ? controls.leftDeadzone : controls.rightDeadzone) + (left ? -5 : 5);
					if (cross && value > 50)
						value = 0;
					value = std::clamp(value, 0, 50);
					ps5emu::SetDeadzones(m_player, leftStick ? value : controls.leftDeadzone, leftStick ? controls.rightDeadzone : value);
					break;
				}
				case kRowButtons:
					if (cross)
					{
						OpenMapping();
						return;
					}
					break;
				case kRowReset:
					if (cross)
					{
						if (m_resetArmed)
						{
							ps5emu::ResetControls(m_player);
							m_resetArmed = false;
						}
						else
							m_resetArmed = true;
					}
					break;
				}
				UpdatePlayer();
			}

			void UpdatePlayer()
			{
				const auto controls = ps5emu::GetPlayerControls(m_player);
				const auto mappings = ps5emu::ListMappings(m_player);
				const int mapped = (int)std::count_if(mappings.begin(), mappings.end(), [](const ps5emu::ButtonMapping& m) { return !m.input.empty(); });
				SetText(m_document, "player-copy", fmt::format("{}, on {}", TypeName(controls.type),
					controls.connected ? "this player's DualSense" : "a DualSense that is not connected"));
				struct Row
				{
					const char* name;
					std::string value;
					bool dimmed;
					const char* help;
				};
				const Row rows[kPlayerRows] = {
					{"Emulated controller", TypeName(controls.type), false,
						"What the game sees in this player's hands.\n\nMost games want the Wii U GamePad for player 1: its screen is the "
						"second one PS5CEMU-HAR shows, and the touchpad touches it. Others take Pro Controllers, or Wii Remotes for "
						"games such as New Super Mario Bros. U.\nCemu has two GamePads at most."},
					{"Motion controls", !controls.hasMotion ? "None on this one" : controls.motion ? "On" : "Off", !controls.hasMotion,
						"The DualSense's gyroscope and accelerometer as the controller's own, for the games that aim or steer by "
						"tilting the GamePad or the Wii Remote.\nThe Pro Controller and the Classic Controller have none."},
					{"Vibration", controls.rumble ? fmt::format("{}%", controls.rumble) : "Off", false,
						"How strongly the DualSense rumbles when the game makes the controller vibrate.\nLeft and Right change it by 10%."},
					{"Left stick deadzone", fmt::format("{}%", controls.leftDeadzone), false,
						"How far the left stick moves before the game sees it. Raise it if a character drifts when you let go of "
						"the stick; lower it for finer control."},
					{"Right stick deadzone", fmt::format("{}%", controls.rightDeadzone), false,
						"How far the right stick moves before the game sees it. Raise it if the camera drifts when you let go of "
						"the stick; lower it for finer control."},
					{"Buttons", Plural(mapped, "button set", "buttons set"), false,
						"Which DualSense button is which of the controller's.\nA is on Circle and B on Cross by default, where the "
						"Wii U has them."},
					{"Reset to defaults", m_resetArmed ? "Press Cross again" : "", false,
						"The default buttons, vibration, motion and deadzones for this controller."},
				};
				for (int row = 0; row < kPlayerRows; row++)
				{
					const std::string id = fmt::format("player-row-{}", row);
					SetClass(m_document, id, "focused", row == m_playerRow);
					SetClass(m_document, id, "dimmed", rows[row].dimmed);
					SetText(m_document, fmt::format("player-name-{}", row), rows[row].name);
					SetText(m_document, fmt::format("player-value-{}", row), rows[row].value);
				}
				SetText(m_document, "player-detail-title", rows[m_playerRow].name);
				SetLines(m_document, "player-help", rows[m_playerRow].help);
				static constexpr const char* kCrossHints[kPlayerRows] = {"Choose", "On / off", "More", "More", "More", "Open", "Reset"};
				SetText(m_document, "player-hint-0", kCrossHints[m_playerRow]);
			}

			// -- settings > controls > a player > buttons ----------------------------------------

			struct Capture
			{
				bool active = false;
				bool released = false; // everything let go since it started
				uint64_t until = 0;
			};

			void OpenMapping()
			{
				m_mapSelected = 0;
				m_capture = {};
				m_mapMessage.clear();
				Show("mapping-dialog", kMapping);
				if (Is3ds())
				{
					SetText(m_document, "mapping-copy", "The DualSense's buttons for the 3DS's");
					SetText(m_document, "mapping-kicker", "NINTENDO 3DS");
				}
				else
				{
					const auto controls = ps5emu::GetPlayerControls(m_player);
					SetText(m_document, "mapping-copy", fmt::format("Player {}: the DualSense's buttons for the {}", m_player + 1, TypeName(controls.type)));
					SetText(m_document, "mapping-kicker", Upper(TypeName(controls.type)));
				}
				UpdateMapping();
			}

			std::vector<ps5emu::ButtonMapping> Mappings() const
			{
				return Is3ds() ? ps5azahar::ListMappings(m_settings.n3ds) : ps5emu::ListMappings(m_player);
			}

			void SetMapping(int index, ps5emu::PadInput input)
			{
				if (!Is3ds())
				{
					ps5emu::SetMapping(m_player, index, input);
					return;
				}
				ps5azahar::SetMapping(m_settings.n3ds, index, input);
				SaveSettings();
			}

			void MappingKey(Key key)
			{
				if (m_capture.active)
					return; // PollCapture has the controller
				const int count = (int)Mappings().size();
				if (key == Key::Circle)
				{
					if (Is3ds())
					{
						OpenConsole();
						m_playerRow = kRowConsoleButtons;
						UpdateConsole();
						return;
					}
					OpenPlayer(m_player);
					m_playerRow = kRowButtons;
					UpdatePlayer();
					return;
				}
				if (key == Key::Cross && count)
				{
					m_capture = {true, false, sceKernelGetProcessTime() + kCaptureUs};
					m_mapMessage.clear();
				}
				else if (key == Key::Square && count)
				{
					SetMapping(m_mapSelected, ps5emu::PadInput::None);
					m_mapMessage = "Cleared: no DualSense button is this one now.";
				}
				else if (Browse(key, m_mapSelected, count, kListRows))
					m_mapMessage.clear();
				UpdateMapping();
			}

			// The DualSense input pressed now, or None. Sticks and triggers count past halfway.
			static ps5emu::PadInput Pressed(const ps5pad::Data& data)
			{
				using ps5emu::PadInput;
				static constexpr std::pair<uint32_t, PadInput> kButtons[] = {
					{ps5pad::kCross, PadInput::Cross}, {ps5pad::kCircle, PadInput::Circle}, {ps5pad::kSquare, PadInput::Square},
					{ps5pad::kTriangle, PadInput::Triangle}, {ps5pad::kL1, PadInput::L1}, {ps5pad::kR1, PadInput::R1},
					{ps5pad::kL3, PadInput::L3}, {ps5pad::kR3, PadInput::R3}, {ps5pad::kCreate, PadInput::Create},
					{ps5pad::kOptions, PadInput::Options}, {ps5pad::kUp, PadInput::Up}, {ps5pad::kDown, PadInput::Down},
					{ps5pad::kLeft, PadInput::Left}, {ps5pad::kRight, PadInput::Right},
				};
				for (const auto& [mask, input] : kButtons)
					if (data.buttons & mask)
						return input;
				if (data.l2 > 160 || (data.buttons & ps5pad::kL2))
					return PadInput::L2;
				if (data.r2 > 160 || (data.buttons & ps5pad::kR2))
					return PadInput::R2;
				if (data.leftY < 40) return PadInput::LeftStickUp;
				if (data.leftY > 215) return PadInput::LeftStickDown;
				if (data.leftX < 40) return PadInput::LeftStickLeft;
				if (data.leftX > 215) return PadInput::LeftStickRight;
				if (data.rightY < 40) return PadInput::RightStickUp;
				if (data.rightY > 215) return PadInput::RightStickDown;
				if (data.rightX < 40) return PadInput::RightStickLeft;
				if (data.rightX > 215) return PadInput::RightStickRight;
				return PadInput::None;
			}

			// Waits for everything to be let go (the Cross that started it), then takes the next
			// press. The touchpad cancels.
			void PollCapture()
			{
				ps5pad::Data data{};
				if (!ps5pad::Read(m_player, data) && !ps5pad::Read(0, data))
					return;
				const bool idle = Pressed(data) == ps5emu::PadInput::None && !(data.buttons & ps5pad::kTouchPad) &&
					data.l2 < 60 && data.r2 < 60;
				if (sceKernelGetProcessTime() > m_capture.until)
				{
					m_capture.active = false;
					m_mapMessage = "No button was pressed, so it stays as it was.";
				}
				else if (!m_capture.released)
					m_capture.released = idle;
				else if (data.buttons & ps5pad::kTouchPad)
				{
					m_capture.active = false;
					m_mapMessage = "Cancelled: it stays as it was.";
				}
				else if (const auto input = Pressed(data); input != ps5emu::PadInput::None)
				{
					m_capture.active = false;
					SetMapping(m_mapSelected, input);
					m_mapMessage = "Done.";
				}
				UpdateMapping();
			}

			void UpdateMapping()
			{
				const auto mappings = Mappings();
				const int count = (int)mappings.size();
				m_mapSelected = std::clamp(m_mapSelected, 0, std::max(0, count - 1));
				const int scroll = Scroll(m_mapSelected, kListRows);
				for (int row = 0; row < kListRows; row++)
				{
					const int index = scroll + row;
					const std::string id = fmt::format("map-row-{}", row);
					SetClass(m_document, id, "focused", index == m_mapSelected);
					SetClass(m_document, id, "offscreen", index >= count);
					SetText(m_document, fmt::format("map-name-{}", row), index < count ? mappings[index].button : "");
					const bool unset = index < count && mappings[index].input.empty();
					SetText(m_document, fmt::format("map-value-{}", row), index >= count ? "" : unset ? "Not set" : mappings[index].input);
					SetClass(m_document, fmt::format("map-value-{}", row), "unset", unset);
				}
				SetText(m_document, "mapping-position", fmt::format("{} OF {}", count ? m_mapSelected + 1 : 0, count));
				if (count == 0)
					return;
				const auto& mapping = mappings[m_mapSelected];
				SetText(m_document, "map-title", mapping.button);
				SetText(m_document, "map-input", mapping.input.empty() ? "Not set" : mapping.input);
				std::string message;
				if (m_capture.active)
				{
					const uint64_t now = sceKernelGetProcessTime();
					const int seconds = (int)((m_capture.until > now ? m_capture.until - now : 0) / 1000000) + 1;
					message = fmt::format("Press the DualSense button, trigger or stick direction for {} now.\n\n{} s left. A touchpad click cancels.",
						mapping.button, seconds);
				}
				else
					message = (m_mapMessage.empty() ? "" : m_mapMessage + "\n\n") +
						"Cross, then a DualSense button, trigger or stick direction: it becomes this one.\nSquare clears it.";
				SetLines(m_document, "map-message", message);
			}

			// -- settings > game files, and install updates and dlc -----------------------------

			enum class FilesMode
			{
				GamesFolder,
				Install,	// Cemu's: a folder with code, content and meta
				InstallCia, // Azahar's: a CIA file
			};

			void OpenFiles(FilesMode mode)
			{
				m_filesMode = mode;
				const std::string& games = GamesFolder();
				std::string start = mode != FilesMode::GamesFolder && !m_installFolder.empty() ? m_installFolder :
					games.empty() ? ps5paths::kGames : games;
				while (!BrowseTo(start) && start != "/")
					start = ParentPath(start);
				m_filesMessage.clear();
				Show("files-dialog", kFiles);
				const bool install = mode != FilesMode::GamesFolder;
				const bool cia = mode == FilesMode::InstallCia;
				SetText(m_document, "files-title", cia ? "Install CIA files" : install ? "Install updates and DLC" : "Game files");
				SetText(m_document, "files-copy", cia ? "Choose a CIA file: a game, an update or DLC" :
					install ? "Choose a folder with an update, DLC or game (code, content and meta)" :
					Is3ds() ? "Choose the folder that holds your 3DS games" : "Choose the folder that holds your Wii U games");
				SetText(m_document, "files-kicker", install ? "TO INSTALL" : "THIS FOLDER");
				SetText(m_document, "files-hint-use", install ? "Install it" : "Use this folder");
				SetClass(m_document, "files-dialog", "install", install);
				UpdateFiles();
			}

			// A folder's subfolders, and its CIA files when they are what is being chosen
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
				m_browseFiles = 0;
				if (m_filesMode == FilesMode::InstallCia)
					for (const auto& file : ListEntries(folder, false, ok))
						if (Lower(file).ends_with(".cia"))
						{
							m_browseEntries.push_back(file);
							m_browseFiles++;
						}
				m_browseSelected = 0;
				return true;
			}

			bool IsFileEntry(int index) const
			{
				return index >= (int)m_browseEntries.size() - m_browseFiles && index < (int)m_browseEntries.size();
			}

			// A CIA's kind, from its title ID's high half
			static const char* CiaKind(uint64_t titleId)
			{
				switch (titleId >> 32)
				{
				case 0x00040000: return "Game";
				case 0x0004000E: return "Update";
				case 0x0004008C: return "DLC";
				case 0x00040002: return "Demo";
				default: return (titleId >> 32 & 0x10) ? "System title" : "Title";
				}
			}

			const ps5emu::InstallCandidate& Inspect(const std::string& folder)
			{
				auto it = m_inspected.find(folder);
				if (it == m_inspected.end())
					it = m_inspected.emplace(folder, ps5emu::InspectInstall(folder)).first;
				return it->second;
			}

			void FilesKey(Key key)
			{
				const int count = (int)m_browseEntries.size();
				if (m_installing)
				{
					if (key == Key::Circle)
					{
						if (m_filesMode == FilesMode::InstallCia)
							ps5azahar::CancelInstall();
						else
							ps5emu::CancelInstall();
						m_filesMessage = "Cancelling: what was installed before is put back...";
						UpdateFiles();
					}
					return;
				}
				if (key == Key::Circle)
				{
					OpenSettings();
					return;
				}
				if (key == Key::Cross && count && !IsFileEntry(m_browseSelected))
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
				else if (key == Key::Triangle && m_filesMode == FilesMode::GamesFolder)
				{
					GamesFolder() = m_browseFolder;
					const bool saved = ps5settings::Save(m_settings);
					if (Is3ds())
					{
						ps5azahar::StartScan(GamesFolder());
						m_scanning = true;
						m_games.clear();
					}
					else if (m_status.coreReady)
					{
						ps5emu::ApplyOptions({m_settings.gamesFolder, m_settings.overlay, m_settings.volume, m_settings.upscaleFilter});
						m_scanning = true;
						m_games.clear();
					}
					SetText(m_document, "about-games-path", ShortPath(GamesFolder(), 36));
					m_filesMessage = saved ? "Saved. PS5CEMU-HAR is looking for games there." : "The folder could not be saved. Please try again.";
				}
				else if (key == Key::Triangle && m_filesMode == FilesMode::InstallCia)
				{
					std::string error;
					m_installFolder = m_browseFolder;
					if (!IsFileEntry(m_browseSelected))
						m_filesMessage = "Choose a CIA file first.";
					else if (ps5azahar::StartInstall(JoinPath(m_browseFolder, m_browseEntries[m_browseSelected]), error))
					{
						m_installing = true;
						m_filesMessage.clear();
					}
					else
						m_filesMessage = error;
				}
				else if (key == Key::Triangle && m_filesMode == FilesMode::Install)
				{
					std::string error;
					m_installFolder = m_browseFolder;
					if (!m_status.coreReady)
						m_filesMessage = "Cemu did not start, so nothing can be installed.";
					else if (ps5emu::StartInstall(m_browseFolder, error))
					{
						m_installing = true;
						m_filesMessage.clear();
					}
					else
						m_filesMessage = error;
				}
				else if (!Browse(key, m_browseSelected, count, kFileRows))
					return;
				UpdateFiles();
			}

			void PollInstall()
			{
				const auto status = m_filesMode == FilesMode::InstallCia ? ps5azahar::GetInstallStatus() : ps5emu::GetInstallStatus();
				using State = ps5emu::InstallStatus::State;
				if (status.state == State::Running)
				{
					const int percent = status.total ? (int)(status.copied * 100 / status.total) : 0;
					const std::string progress = status.total ?
						fmt::format("Installing: {}%, {} of {}.\n\nCircle cancels.", percent, Gigabytes(status.copied), Gigabytes(status.total)) :
						"Installing: counting the files...";
					if (m_installProgress != progress)
					{
						m_installProgress = progress;
						if (m_filesMessage.rfind("Cancelling", 0) != 0)
							SetLines(m_document, "files-message", progress);
					}
					return;
				}
				m_installing = false;
				m_installProgress.clear();
				m_inspected.clear();
				if (status.state == State::Done)
				{
					m_filesMessage = "Installed. The library has it with its game.";
					if (Is3ds())
						ps5azahar::StartScan(GamesFolder());
					else
						ps5emu::Rescan();
					m_scanning = true;
				}
				else if (status.state == State::Cancelled)
					m_filesMessage = "Cancelled. What was installed before is as it was.";
				else
					m_filesMessage = "It could not be installed: " + status.message;
				UpdateFiles();
			}

			void UpdateFiles()
			{
				const bool install = m_filesMode == FilesMode::Install;
				const bool cia = m_filesMode == FilesMode::InstallCia;
				const int count = (int)m_browseEntries.size();
				const int scroll = Scroll(m_browseSelected, kFileRows);
				for (int row = 0; row < kFileRows; row++)
				{
					const int index = scroll + row;
					const bool up = index < count && m_browseEntries[index] == "..";
					const std::string id = fmt::format("files-row-{}", row);
					SetClass(m_document, id, "focused", index == m_browseSelected);
					SetClass(m_document, id, "offscreen", index >= count);
					SetClass(m_document, id, "files-up", up);
					SetClass(m_document, id, "files-file", IsFileEntry(index));
					SetText(m_document, fmt::format("files-name-{}", row), index >= count ? "" : up ? "Parent folder" : m_browseEntries[index]);
					std::string meta = IsFileEntry(index) ? "CIA" : "";
					if (install && index < count && !up)
					{
						const auto& candidate = Inspect(JoinPath(m_browseFolder, m_browseEntries[index]));
						if (candidate.kind != ps5emu::InstallCandidate::Kind::None)
							meta = Upper(KindName(candidate.kind));
					}
					SetText(m_document, fmt::format("files-meta-{}", row), meta);
				}
				SetClass(m_document, "files-empty", "visible", count == 0);
				SetText(m_document, "files-path", ShortPath(m_browseFolder, 52));
				SetText(m_document, "files-position", fmt::format("{} OF {}", count ? m_browseSelected + 1 : 0, count));

				// TRIANGLE uses the folder shown: show what Cemu would find in it
				std::pair<std::string, std::string> lines[3];
				bool ready[3]{};
				std::string message = m_filesMessage;
				if (cia)
				{
					const bool file = IsFileEntry(m_browseSelected);
					const auto title = file ? ps5azahar::Inspect(JoinPath(m_browseFolder, m_browseEntries[m_browseSelected])) : ps5azahar::Title{};
					SetText(m_document, "files-current", !file ? ShortPath(m_browseFolder, 40) : title.name.empty() ? m_browseEntries[m_browseSelected] : title.name);
					lines[0] = {"TYPE", !file ? "A folder" : title.titleId ? CiaKind(title.titleId) : "CIA"};
					lines[1] = {"TITLE ID", file && title.titleId ? Hex(title.titleId) : "-"};
					lines[2] = {"VERSION", file && title.titleId ? fmt::format("v{}", title.version) : "-"};
					ready[0] = ready[1] = ready[2] = file;
					if (message.empty())
						message = file ? "Triangle installs it into the 3DS's storage, as Azahar's Install CIA does: an update or DLC "
							"goes with its game, and a game joins the library." :
							"Choose a CIA file to install: a game, an update or DLC.";
				}
				else if (Is3ds() && !install)
				{
					SetText(m_document, "files-current", ShortPath(m_browseFolder, 40));
					const int games = Count3dsGames(m_browseFolder);
					lines[0] = {"GAMES", games < 0 ? "Cannot be read" : Plural(games, "game", "games") + " here"};
					lines[1] = {"IN USE", ShortPath(GamesFolder(), 40)};
					lines[2] = {"", ""};
					ready[0] = games > 0, ready[1] = GamesFolder() == m_browseFolder;
					if (message.empty())
						message = "Games can be .3ds or .cci, .cxi, .cia or .3dsx, decrypted, here or in the folders in it. Triangle uses the folder shown.";
				}
				else if (!install)
				{
					SetText(m_document, "files-current", ShortPath(m_browseFolder, 40));
					const int games = CountGames(m_browseFolder);
					const bool keys = IsFile(std::string(ps5paths::kRoot) + "/keys.txt");
					lines[0] = {"GAMES", games < 0 ? "Cannot be read" : Plural(games, "game", "games")};
					lines[1] = {"KEYS.TXT", keys ? "Found in /data/ps5cemu" : "Missing (only .wud/.wux need it)"};
					lines[2] = {"IN USE", ShortPath(m_settings.gamesFolder, 40)};
					ready[0] = games > 0, ready[1] = keys, ready[2] = m_settings.gamesFolder == m_browseFolder;
					if (message.empty())
						message = "Games can be .wua, .wud, .wux, or folders with code, content and meta. Triangle uses the folder shown.";
				}
				else
				{
					const auto& candidate = Inspect(m_browseFolder);
					const bool valid = candidate.kind != ps5emu::InstallCandidate::Kind::None;
					SetText(m_document, "files-current", valid && !candidate.name.empty() ? candidate.name : ShortPath(m_browseFolder, 40));
					lines[0] = {"TYPE", valid ? KindName(candidate.kind) : "Nothing to install"};
					lines[1] = {"TITLE ID", valid ? Hex(candidate.titleId) : "-"};
					lines[2] = {"VERSION", !valid ? "-" : candidate.installedVersion < 0 ?
						fmt::format("v{}, not installed yet", candidate.version) :
						fmt::format("v{}, v{} installed now", candidate.version, candidate.installedVersion)};
					ready[0] = ready[1] = valid;
					ready[2] = valid && candidate.installedVersion < (int)candidate.version;
					if (message.empty())
						message = valid ? "Triangle installs it into the Wii U's storage (mlc01), as Cemu's Install game title, update or DLC does. "
							"Updates and DLC in the game files folder work as they are, too." :
							candidate.note;
				}
				for (int i = 0; i < 3; i++)
				{
					SetText(m_document, fmt::format("files-label-{}", i), lines[i].first);
					SetText(m_document, fmt::format("files-value-{}", i), lines[i].second);
					SetClass(m_document, fmt::format("files-value-{}", i), "ready", ready[i]);
				}
				if (m_installing && !m_installProgress.empty() && m_filesMessage.empty())
					message = m_installProgress;
				SetLines(m_document, "files-message", message);
			}

			Rml::ElementDocument* m_document;
			System m_system;
			ps5settings::Launcher& m_settings;
			const Status& m_status;
			Screen m_screen = kHome;
			std::optional<ps5emu::Game> m_launch;
			bool m_leaving = false;
			Picker m_picker;

			std::vector<ps5emu::Game> m_games;
			bool m_scanning = false;
			uint32_t m_boxArrivals = 0;	   // ps5boxart::Arrivals() when the library was last drawn
			std::string m_shownBoxArt;	   // the box art the details show, to let go of after
			std::set<std::string> m_rowIcons; // the icons the library's rows have loaded
			std::time_t m_shownMinute = 0;

			int m_selected = kContinue;
			int m_lastIndex = -1;
			bool m_continueReady = false;
			std::vector<int> m_recent; // indices into m_games

			int m_librarySelected = 0;

			int m_packsGame = 0;
			Screen m_packsFrom = kHome;
			std::vector<ps5emu::GraphicPackInfo> m_packs;
			std::vector<PackItem> m_packItems;
			int m_packItem = 0;
			bool m_presetsFocus = false;
			int m_presetSelected = 0;

			int m_settingsSelected = 0;
			int m_option = 0;

			int m_controlSelected = 0;
			int m_player = 0;
			int m_playerRow = 0;
			bool m_resetArmed = false;
			int m_mapSelected = 0;
			Capture m_capture;
			std::string m_mapMessage;

			FilesMode m_filesMode = FilesMode::GamesFolder;
			std::string m_browseFolder;
			std::vector<std::string> m_browseEntries; // ".." first unless at "/", then subfolders, then files
			int m_browseFiles = 0;					  // how many of them, at the end, are files
			int m_browseSelected = 0;
			std::string m_filesMessage;
			std::string m_installFolder;
			std::map<std::string, ps5emu::InstallCandidate> m_inspected;
			bool m_installing = false;
			std::string m_installProgress;
		};
		// The start screen: Cemu on the left half, Azahar on the right; Left and Right choose, Cross starts.
		class StartScreen
		{
		public:
			StartScreen(Rml::ElementDocument* document, const Status& status, System selected)
				: m_document(document), m_status(status), m_selected(selected)
			{
				Update();
			}

			// The clock, and how many games each has once their libraries are read.
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
				std::string wiiu = !m_status.coreReady ? "Setup required" : ps5emu::Scanning() ? "Looking for games" :
					Plural((int)ps5emu::ListGames().size(), "game", "games");
				std::string n3ds = ps5azahar::Scanning() ? "Looking for games" : Plural((int)ps5azahar::ListGames().size(), "game", "games");
				if (!ps5azahar::Available())
					n3ds += "  /  core not in this build";
				if (wiiu != m_wiiu)
					SetText(m_document, "start-status-wiiu", m_wiiu = wiiu);
				if (n3ds != m_n3ds)
					SetText(m_document, "start-status-3ds", m_n3ds = n3ds);
			}

			// The emulator chosen, once Cross is pressed.
			std::optional<System> HandleKey(Key key)
			{
				if (key == Key::Left || key == Key::Right)
				{
					m_selected = m_selected == System::WiiU ? System::N3ds : System::WiiU;
					Update();
				}
				else if (key == Key::Cross)
					return m_selected;
				return std::nullopt;
			}

		private:
			void Update()
			{
				const bool wiiu = m_selected == System::WiiU;
				SetClass(m_document, "start-wiiu", "focused", wiiu);
				SetClass(m_document, "start-wiiu", "dim", !wiiu);
				SetClass(m_document, "start-3ds", "focused", !wiiu);
				SetClass(m_document, "start-3ds", "dim", wiiu);
			}

			Rml::ElementDocument* m_document;
			const Status& m_status;
			System m_selected;
			std::time_t m_shownMinute = 0;
			std::string m_wiiu, m_n3ds;
		};
	}

	std::optional<Choice> Run(ps5settings::Launcher& settings, const Status& status)
	{
		std::string error;
		if (!ps5ui::Start(error))
		{
			ps5log::Line("[launcher] {}", error);
			ps5notify::Send("The launcher cannot show: " + error);
			return std::nullopt;
		}
		ps5azahar::StartScan(settings.n3ds.gamesFolder);
		// After a game, the launcher opens on its emulator's side (and only then: next time, the
		// start screen)
		System system = settings.side == "3ds" ? System::N3ds : System::WiiU;
		bool choosing = settings.side.empty();
		if (!choosing)
		{
			settings.side.clear();
			ps5settings::Save(settings);
		}
		Input input;
		uint64_t frames = 0;
		auto frame = [&] {
			if (++frames % 120 == 0)
				ps5pad::Rescan(); // controllers joining or leaving, about every two seconds
			ps5ui::Frame();
			if (frames == 1)
				sceSystemServiceHideSplashScreen();
		};
		for (;;)
		{
			if (choosing)
			{
				ps5ui::SetScene(ps5ui::Scene::Both);
				Rml::ElementDocument* document = ps5ui::Show("start.rml");
				if (!document)
					break;
				StartScreen start(document, status, system);
				std::optional<System> chosen;
				while (!chosen)
				{
					for (const Key key : input.Poll())
						if (!chosen)
							chosen = start.HandleKey(key);
					start.Poll();
					frame();
				}
				system = *chosen;
				choosing = false;
			}
			const bool n3ds = system == System::N3ds;
			ps5ui::SetScene(n3ds ? ps5ui::Scene::Wave : ps5ui::Scene::Bubbles);
			Rml::ElementDocument* document = ps5ui::Show(n3ds ? "azahar.rml" : "main.rml");
			if (!document)
				break;
			Launcher launcher(document, system, settings, status);
			launcher.Initialize();
			while (!launcher.Done() && !launcher.Leaving())
			{
				for (const Key key : input.Poll())
					launcher.HandleKey(key);
				launcher.Poll();
				frame();
			}
			if (launcher.Leaving())
			{
				choosing = true;
				continue;
			}
			// the loading screen stays on VideoOut while the launcher makes way for the emulator's renderer
			ps5ui::Frame();
			ps5ui::Stop();
			ps5log::Line("[launcher] starting {} ({:016x}) on {}", launcher.Choice()->name, launcher.Choice()->titleId, n3ds ? "Azahar" : "Cemu");
			return Choice{system, *launcher.Choice()};
		}
		ps5ui::Stop();
		ps5notify::Send("The launcher's layout did not load.");
		return std::nullopt;
	}
}
