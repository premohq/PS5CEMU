// SPDX-License-Identifier: GPL-3.0-or-later
#include "ingame3ds.h"
#include "menu_canvas.h"
#include "../ps5/kernel.h"
#include "../ps5/log.h"
#include "../ps5/pad.h"

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "imgui/imgui_impl_vulkan.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <atomic>
#include <mutex>

// Cemu's overlay font, compressed into Cemu (resource/CafeDefaultFont.cpp)
uint8* extractCafeDefaultFont(sint32* size);

namespace ps5ingame3ds
{
	namespace
	{
		using ps5menu::Canvas;
		const ps5menu::Palette& kColours = ps5menu::kGold;

		// What the game's loop and the menu share
		std::mutex s_mutex;
		Settings s_settings;  // under s_mutex
		bool s_changed = false;
		std::string s_name;
		uint64_t s_titleId = 0;
		double s_fps = 0, s_speed = 0;
		std::atomic<bool> s_open{false};
		std::atomic<uint64_t> s_openedAt{0}; // sceKernelGetProcessTime
		std::atomic<bool> s_libraryRequested{false};

		// The rest belongs to Azahar's renderer thread, which draws the menu.
		struct Gpu
		{
			bool ready = false, failed = false;
			VkDevice device = VK_NULL_HANDLE;
			VkRenderPass renderPass = VK_NULL_HANDLE;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			ImGuiContext* context = nullptr;
			ImFontAtlas* atlas = nullptr;
			ImFont* title = nullptr;
			ImFont* head = nullptr;
			ImFont* row = nullptr;
			ImFont* small = nullptr;
			bool fontsUploaded = false;
			int uploadAge = -1;	   // frames since the font upload, until its buffer goes
			bool pending = false;  // a frame's draw data waits for the render pass
			uint64_t lastFrame = 0;
		};
		Gpu g;
		uint32_t s_buttons = 0, s_pressed = 0;
		bool s_confirmLibrary = false;
		enum class Page
		{
			Main,
			Controls,
		};
		Page s_page = Page::Main;
		bool s_pageChanged = false;

		constexpr const char* kLayouts[] = {"Top above bottom", "Top screen only", "Large top screen", "Side by side"};
		constexpr const char* kFilters[] = {"None", "Anime4K", "Bicubic", "ScaleForce", "xBRZ", "MMPX"};

		void Change(const Settings& settings)
		{
			std::lock_guard lock(s_mutex);
			s_settings = settings;
			s_changed = true;
		}

		void CloseMenu()
		{
			s_open = false;
		}

		void ShowPage(Page page)
		{
			s_page = page;
			s_pageChanged = true;
			s_confirmLibrary = false;
		}

		// Cemu's Vulkan entry points from Azahar's instance and device (Cemu's renderer never ran),
		// a descriptor pool for the font, an ImGui context of its own with Cemu's font at the
		// menu's four sizes, and ImGui's Vulkan backend on Azahar's render pass.
		bool Initialize(const Target& target, float scale)
		{
			if (!InitializeGlobalVulkan() || !InitializeInstanceVulkan(target.instance) || !InitializeDeviceVulkan(target.device))
			{
				ps5log::Line("[ingame3ds] Cemu's Vulkan commands did not load from Azahar's device: no menu");
				return false;
			}
			const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8};
			VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
			poolInfo.maxSets = 8;
			poolInfo.poolSizeCount = 1;
			poolInfo.pPoolSizes = &size;
			if (vkCreateDescriptorPool(target.device, &poolInfo, nullptr, &g.pool) != VK_SUCCESS)
			{
				ps5log::Line("[ingame3ds] no descriptor pool: no menu");
				return false;
			}

			sint32 fontSize = 0;
			uint8* font = extractCafeDefaultFont(&fontSize); // kept: the atlas reads it
			g.atlas = new ImFontAtlas();
			ImFontConfig config{};
			config.FontDataOwnedByAtlas = false;
			g.title = g.atlas->AddFontFromMemoryTTF(font, fontSize, 48.0f * scale, &config);
			g.head = g.atlas->AddFontFromMemoryTTF(font, fontSize, 32.0f * scale, &config);
			g.row = g.atlas->AddFontFromMemoryTTF(font, fontSize, 24.0f * scale, &config);
			g.small = g.atlas->AddFontFromMemoryTTF(font, fontSize, 20.0f * scale, &config);
			g.context = ImGui::CreateContext(g.atlas);
			ImGui::SetCurrentContext(g.context);
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
			io.BackendFlags |= ImGuiBackendFlags_HasGamepad;

			ImGui_ImplVulkan_InitInfo info{};
			info.Instance = target.instance;
			info.PhysicalDevice = target.physicalDevice;
			info.Device = target.device;
			info.QueueFamily = target.queueFamily;
			info.Queue = target.queue;
			info.DescriptorPool = g.pool;
			info.MinImageCount = std::max(2u, target.imageCount);
			info.ImageCount = info.MinImageCount;
			ImGui_ImplVulkan_Init(&info, target.renderPass);
			g.device = target.device;
			g.renderPass = target.renderPass;
			ps5log::Line("[ingame3ds] the menu is ready ({} images in flight)", info.ImageCount);
			return true;
		}

		// ImGui's input from player 1's DualSense: the D-pad and the left stick move between items,
		// Cross chooses; Circle and Options are read here (ImGui gives Circle another use).
		void Input()
		{
			ImGuiIO& io = ImGui::GetIO();
			ps5pad::Data data{};
			const bool connected = ps5pad::Read(0, data) && !(data.buttons & ps5pad::kIntercepted);
			const uint32_t buttons = connected ? data.buttons : 0;
			s_pressed = buttons & ~s_buttons;
			s_buttons = buttons;
			const bool touchpadHeld = buttons & ps5pad::kTouchPad; // a shortcut's
			auto key = [&](ImGuiKey imguiKey, uint32_t mask) { io.AddKeyEvent(imguiKey, !touchpadHeld && (buttons & mask)); };
			key(ImGuiKey_GamepadDpadUp, ps5pad::kUp);
			key(ImGuiKey_GamepadDpadDown, ps5pad::kDown);
			key(ImGuiKey_GamepadDpadLeft, ps5pad::kLeft);
			key(ImGuiKey_GamepadDpadRight, ps5pad::kRight);
			key(ImGuiKey_GamepadFaceDown, ps5pad::kCross);
			auto stick = [&](ImGuiKey negative, ImGuiKey positive, uint8_t raw) {
				const float value = connected ? std::clamp((raw - 128) / 127.0f, -1.0f, 1.0f) : 0.0f;
				io.AddKeyAnalogEvent(negative, value < -0.5f, std::max(-value, 0.0f));
				io.AddKeyAnalogEvent(positive, value > 0.5f, std::max(value, 0.0f));
			};
			stick(ImGuiKey_GamepadLStickLeft, ImGuiKey_GamepadLStickRight, data.leftX);
			stick(ImGuiKey_GamepadLStickUp, ImGuiKey_GamepadLStickDown, data.leftY);
			io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);
			io.MouseDown[0] = false;
		}

		void DrawMenu(float scale)
		{
			ImGuiIO& io = ImGui::GetIO();
			Settings settings;
			std::string name;
			uint64_t titleId;
			{
				std::lock_guard lock(s_mutex);
				settings = s_settings;
				name = s_name;
				titleId = s_titleId;
			}

			const ImVec2 origin{(io.DisplaySize.x - 1920.0f * scale) * 0.5f, (io.DisplaySize.y - 1080.0f * scale) * 0.5f};
			ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
			ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);
			ImGui::SetNextWindowFocus();
			ImGui::PushStyleColor(ImGuiCol_NavHighlight, IM_COL32(0, 0, 0, 0)); // the rows show the focus
			constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
				ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;
			if (ImGui::Begin("PS5CEMU-HAR##InGameMenu3ds", nullptr, kFlags))
			{
				const bool appearing = ImGui::IsWindowAppearing();
				if (appearing)
				{
					ImGui::GetCurrentContext()->NavDisableHighlight = false;
					s_confirmLibrary = false;
					s_page = Page::Main;
				}
				const Canvas canvas{ImGui::GetWindowDrawList(), scale, origin, kColours};
				canvas.draw->AddRectFilled({0, 0}, io.DisplaySize, kColours.dim); // the game, dimmed

				canvas.Text(g.title, 48, 108, 62, kColours.title, "PS5 AZAHAR");
				canvas.Text(g.small, 20, 110, 132, kColours.copy, name);
				canvas.Panel(108, 188, 820, 720);
				canvas.Text(g.small, 20, 138, 208, kColours.kicker, s_page == Page::Controls ? "CONTROLS" : "IN THE GAME");
				canvas.Panel(980, 188, 820, 720);

				struct Item
				{
					const char* id;
					std::string label, value;
					bool setting; // Left and Right change it
					const char* help;
				};
				std::vector<Item> items;
				const int resolution = std::clamp(settings.resolution, 1, 10);
				if (s_page == Page::Main)
					items = {
						{"resume", "Back to the game", "", false, "Closes this menu: the game carries on where it is."},
						{"layout", "Screens", kLayouts[std::clamp(settings.layout, 0, 3)], true,
							"How the two screens share the TV: one above the other, the top one alone, the top one large with the "
							"bottom one beside it, or the two side by side.\nIn the game, touchpad click + R1 goes to the next."},
						{"swap", "Main screen", settings.swapScreens ? "Bottom" : "Top", true,
							"Which screen takes the top screen's place.\nIn the game, touchpad click + L1 swaps them."},
						{"resolution", "Internal resolution", fmt::format("{}x  ({}x{})", resolution, 400 * resolution, 240 * resolution), true,
							"How large the 3DS's 3D scenes are drawn before they are scaled to the TV. Higher is sharper and asks "
							"more of the GPU."},
						{"filter", "Texture filter", kFilters[std::clamp(settings.textureFilter, 0, 5)], true,
							"Smooths the game's textures as they are scaled up. None keeps them as the 3DS draws them."},
						{"performance", "Performance overlay", settings.performance ? "On" : "Off", true,
							"The frame rate and the emulation's speed, in the top left corner."},
						{"volume", "Volume", fmt::format("{}%", settings.volume), true, "The game's sound. Left and Right change it by 10%."},
						{"controls", "Controls", "", false, "Motion controls, the sticks' deadzone and where A and B are. They are "
							"kept for the next games too; every button can be set in the launcher's Settings > Controls."},
						{"library", "Back to the library", s_confirmLibrary ? "Press Cross again" : "", false,
							"Leaves the game for the library. What you have not saved in the game is lost."},
					};
				else
					items = {
						{"motion", "Motion controls", settings.motion ? "On" : "Off", true,
							"The DualSense's gyroscope and accelerometer as the 3DS's, for the games that aim or steer by tilting."},
						{"deadzone", "Stick deadzone", fmt::format("{}%", settings.deadzone), true,
							"How far a stick moves before the game sees it. Raise it if something drifts when you let go."},
						{"ab", "A and B", settings.aOnCircle ? "A on Circle" : "A on Cross", true,
							"A on Circle and B on Cross, where the 3DS has them, or A on Cross and B on Circle, with X and Y swapped "
							"to match."},
						{"back", "Back", "", false, "To the menu's first page."},
					};

				int focused = 0;
				const float spacing = 72, height = 64;
				for (int i = 0; i < (int)items.size(); i++)
				{
					const Item& item = items[i];
					const float y = 250 + i * spacing;
					ImGui::SetCursorScreenPos(canvas.At(138, y));
					const bool chosen = ImGui::InvisibleButton(item.id, {760 * scale, height * scale});
					if (i == 0 && (appearing || s_pageChanged))
					{
						ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
						ImGui::GetCurrentContext()->NavDisableHighlight = false;
						s_pageChanged = false;
					}
					const bool isFocused = ImGui::IsItemFocused();
					if (isFocused)
						focused = i;
					int change = chosen ? 1 : 0; // Cross moves a setting on, Left and Right either way
					if (isFocused && item.setting)
					{
						if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft))
							change = -1;
						else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) || ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight))
							change = 1;
					}
					canvas.Row(138, y, 760, height, isFocused);
					canvas.Text(g.row, 24, 164, y + 17, kColours.text, item.label);
					canvas.TextRight(g.row, 22, 872, y + 19, kColours.accent, item.value);
					if (change == 0)
						continue;
					const std::string id = item.id;
					Settings next = settings;
					bool changed = true; // a setting's item; the others set it false
					if (id == "resume")
					{
						CloseMenu();
						changed = false;
					}
					else if (id == "layout")
						next.layout = (settings.layout + change + 4) % 4;
					else if (id == "swap")
						next.swapScreens = !settings.swapScreens;
					else if (id == "resolution")
						next.resolution = chosen && resolution >= 10 ? 1 : std::clamp(resolution + change, 1, 10);
					else if (id == "filter")
						next.textureFilter = (settings.textureFilter + change + 6) % 6;
					else if (id == "performance")
						next.performance = !settings.performance;
					else if (id == "volume")
						next.volume = chosen && settings.volume >= 100 ? 0 : std::clamp(settings.volume + change * 10, 0, 100);
					else if (id == "controls")
					{
						ShowPage(Page::Controls);
						changed = false;
					}
					else if (id == "library")
					{
						changed = false;
						if (s_confirmLibrary)
							s_libraryRequested = true;
						s_confirmLibrary = true;
					}
					else if (id == "motion")
						next.motion = !settings.motion;
					else if (id == "deadzone")
						next.deadzone = chosen && settings.deadzone >= 50 ? 0 : std::clamp(settings.deadzone + change * 5, 0, 50);
					else if (id == "ab")
						next.aOnCircle = !settings.aOnCircle;
					else if (id == "back")
					{
						ShowPage(Page::Main);
						changed = false;
					}
					if (changed)
						Change(next);
				}
				if (items[focused].id != std::string("library"))
					s_confirmLibrary = false;

				// the right-hand panel: the game, and what the focused item does
				canvas.Text(g.small, 20, 1016, 208, kColours.kicker, "THIS GAME");
				canvas.Text(g.head, 32, 1016, 248, kColours.title, name, 748);
				canvas.Text(g.small, 20, 1016, 378, kColours.kicker, "TITLE ID");
				canvas.TextRight(g.small, 20, 1764, 378, kColours.accent, fmt::format("{:016X}", titleId));
				canvas.draw->AddLine(canvas.At(1016, 434), canvas.At(1764, 434), kColours.line, scale);
				canvas.Text(g.head, 32, 1016, 456, kColours.title, items[focused].label);
				canvas.Text(g.row, 22, 1016, 508, kColours.copy, items[focused].help, 748);

				// the controller hints, along the bottom
				canvas.draw->AddLine(canvas.At(108, 955), canvas.At(1812, 955), kColours.line, scale);
				float x = 108;
				x = canvas.Hint(g.small, x, 973, "cross", "Choose");
				x = canvas.Hint(g.small, x, 973, "leftright", "Change");
				x = canvas.Hint(g.small, x, 973, "circle", s_page == Page::Controls ? "Back" : "Back to the game");
				canvas.Hint(g.small, x, 973, "touchpad", "Touchpad click: touch the bottom screen, in the game");
			}
			ImGui::End();
			ImGui::PopStyleColor();

			// Circle or Options closes it, but not the press of Options that opened it; on the
			// controls' page, Circle goes back to the first
			const bool settled = sceKernelGetProcessTime() - s_openedAt > 300000;
			if (settled && (s_pressed & (ps5pad::kCircle | ps5pad::kOptions)) && !(s_buttons & ps5pad::kTouchPad))
			{
				if (s_page == Page::Controls && (s_pressed & ps5pad::kCircle))
					ShowPage(Page::Main);
				else
					CloseMenu();
			}
		}

		void DrawPerformance(float scale)
		{
			double fps, speed;
			{
				std::lock_guard lock(s_mutex);
				fps = s_fps;
				speed = s_speed;
			}
			const std::string text = fmt::format("{:.0f} FPS   {:.0f}%", fps, speed);
			ImDrawList* draw = ImGui::GetForegroundDrawList();
			const ImVec2 at{24 * scale, 20 * scale};
			const ImVec2 size = g.small->CalcTextSizeA(20 * scale, FLT_MAX, 0.0f, text.c_str());
			draw->AddRectFilled({at.x - 12 * scale, at.y - 8 * scale}, {at.x + size.x + 12 * scale, at.y + size.y + 8 * scale},
				kColours.panel, 10 * scale);
			draw->AddText(g.small, 20 * scale, at, kColours.title, text.c_str());
		}
	}

	void Start(const std::string& name, uint64_t titleId, const Settings& settings)
	{
		std::lock_guard lock(s_mutex);
		s_name = name;
		s_titleId = titleId;
		s_settings = settings;
		s_changed = false;
	}

	void ToggleMenu()
	{
		if (s_open)
		{
			CloseMenu();
			return;
		}
		s_openedAt = sceKernelGetProcessTime();
		s_open = true;
	}

	bool MenuOpen()
	{
		return s_open;
	}

	bool TakeChanges(Settings& settings)
	{
		std::lock_guard lock(s_mutex);
		if (!s_changed)
			return false;
		s_changed = false;
		settings = s_settings;
		return true;
	}

	bool TakeLibraryRequest()
	{
		return s_libraryRequested.exchange(false);
	}

	void SetPerformance(double fps, double speed)
	{
		std::lock_guard lock(s_mutex);
		s_fps = fps;
		s_speed = speed;
	}

	void Record(const Target& target)
	{
		if (g.failed)
			return;
		const float scale = std::max(1.0f, target.height / 1080.0f);
		if (!target.insideRenderPass)
		{
			g.pending = false;
			bool performance;
			{
				std::lock_guard lock(s_mutex);
				performance = s_settings.performance;
			}
			const bool open = s_open;
			if (!open && !performance)
			{
				s_buttons = 0; // the menu sees a fresh controller when it next opens
				return;
			}
			if (!g.ready)
			{
				if (!Initialize(target, scale))
				{
					g.failed = true;
					return;
				}
				g.ready = true;
			}
			if (target.renderPass != g.renderPass)
				return; // another pass than the screen's (a screenshot's)
			ImGui::SetCurrentContext(g.context);
			// the font, once; its staging buffer goes once the copy has long run
			if (!g.fontsUploaded)
			{
				ImGui_ImplVulkan_CreateFontsTexture(target.commandBuffer);
				g.fontsUploaded = true;
				g.uploadAge = 0;
			}
			else if (g.uploadAge >= 0 && ++g.uploadAge > 16)
			{
				ImGui_ImplVulkan_DestroyFontUploadObjects();
				g.uploadAge = -1;
			}
			ImGuiIO& io = ImGui::GetIO();
			io.DisplaySize = {(float)target.width, (float)target.height};
			const uint64_t now = sceKernelGetProcessTime();
			io.DeltaTime = g.lastFrame && now > g.lastFrame ? std::min((now - g.lastFrame) / 1e6f, 0.25f) : 1.0f / 60.0f;
			g.lastFrame = now;
			Input();
			ImGui::NewFrame();
			if (open)
				DrawMenu(scale);
			if (performance)
				DrawPerformance(scale);
			ImGui::Render();
			g.pending = true;
			return;
		}
		if (!g.pending || target.renderPass != g.renderPass)
			return;
		g.pending = false;
		ImGui::SetCurrentContext(g.context);
		ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), target.commandBuffer);
	}
}
