// SPDX-License-Identifier: GPL-3.0-or-later
// PS5CEMU-HAR: the in-game menu over a 3DS game, laid out as the Wii U's (ingame.h) in Azahar's
// gold (menu_canvas.h). It is drawn with Cemu's ImGui and its Vulkan backend into Azahar's frames,
// through the hook patches/azahar adds to Azahar's renderer; the game's loop (port/azahar/core.cpp)
// applies what it changes. Both sides include this header, so it names no Cemu or Azahar type.

#pragma once

#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>

namespace ps5ingame3ds
{
	// What the menu shows and changes
	struct Settings
	{
		int layout = 2;			  // ps5azahar::Layout
		bool swapScreens = false; // the bottom screen where the top one is
		int resolution = 6;		  // times the 3DS's 400x240
		int textureFilter = 0;	  // None, Anime4K, Bicubic, ScaleForce, xBRZ, MMPX
		int volume = 100;		  // percent
		bool performance = false; // the frame rate and speed in a corner
		bool motion = true;
		int deadzone = 15;		  // percent
		bool aOnCircle = true;	  // A on Circle and B on Cross, where the 3DS has them
	};

	// Azahar's frame being recorded (its renderer's FrontendOverlayTarget): before its render pass
	// begins, then in it
	struct Target
	{
		VkInstance instance;
		VkPhysicalDevice physicalDevice;
		VkDevice device;
		uint32_t queueFamily;
		VkQueue queue;
		VkRenderPass renderPass;
		uint32_t imageCount;
		VkCommandBuffer commandBuffer;
		uint32_t width, height;
		bool insideRenderPass;
	};

	// When the game starts: its name and title ID, and the settings it starts with.
	void Start(const std::string& name, uint64_t titleId, const Settings& settings);
	// Touchpad click + Options.
	void ToggleMenu();
	bool MenuOpen();

	// For the game's loop: a change made in the menu since the last call (the settings as they are
	// now), and whether the library was chosen.
	bool TakeChanges(Settings& settings);
	bool TakeLibraryRequest();
	// The performance overlay's numbers, about once a second: frames per second, speed in percent.
	void SetPerformance(double fps, double speed);

	// For Azahar's renderer, twice a frame, where it records its commands.
	void Record(const Target& target);
}
