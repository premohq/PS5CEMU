# SPDX-License-Identifier: GPL-3.0-or-later
# PS5Cemu: the port's host code, added to Cemu's tree by its CEMU_PS5 option (src/CMakeLists.txt):
# the console layer (ps5/), Cemu's start-up and graphic packs (app/), and the launcher (frontend/),
# which draws ProsperoEden's RmlUi layout in software through SDL, as ProsperoEden does. Cemu's own
# platform classes (cemu/) are added to Cemu's targets by the patch.

set(PS5CEMU_VERSION "0.2.0")
set(PS5CEMU_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(PS5CEMU_DEPS "${PS5CEMU_ROOT}/.deps")
# ProsperoEden's bitmap font engine: its launcher fonts are Montserrat atlases, not TrueType
set(PROSPEROEDEN_LAUNCHER "${PS5CEMU_DEPS}/ProsperoEden/headless/prosperoeden")
# the boilerplate's sandbox elevation client (port/ps5/privilege.cpp)
set(SANDBOX_ELEVATION "${PS5CEMU_DEPS}/ps5-native-app-boilerplate/examples/sandbox-elevation")

foreach(input "${PROSPEROEDEN_LAUNCHER}/bitmap_font_engine.cpp" "${SANDBOX_ELEVATION}/src/elevation.cpp")
	if(NOT EXISTS "${input}")
		message(FATAL_ERROR "${input} is missing: run make deps")
	endif()
endforeach()

find_path(RMLUI_INCLUDE_DIR RmlUi/Core.h REQUIRED)
find_library(RMLUI_LIBRARY rmlui REQUIRED)
# SDL2 with its PS5 video driver, which puts the launcher's software-drawn frames on VideoOut, and
# the libiconv pacbrew built it with
find_library(SDL2_LIBRARY SDL2 REQUIRED)
find_library(ICONV_LIBRARY iconv REQUIRED)
find_library(FREETYPE_LIBRARY freetype REQUIRED)
# what pacbrew's FreeType and curl were built with
find_library(PNG16_LIBRARY png16 REQUIRED)
find_library(BZ2_LIBRARY bz2 REQUIRED)
find_library(PSL_LIBRARY psl REQUIRED)

add_library(CemuPS5 STATIC
	app/emulator.cpp
	app/emulator.h
	app/covers.cpp
	app/graphic_packs.cpp
	app/ingame.cpp
	app/ingame.h
	app/paths.h
	frontend/launcher.cpp
	frontend/launcher.h
	frontend/settings.cpp
	frontend/settings.h
	frontend/ui_host.cpp
	frontend/ui_host.h
	ps5/display.cpp
	ps5/display.h
	ps5/kernel.h
	ps5/log.cpp
	ps5/log.h
	ps5/notify.cpp
	ps5/notify.h
	ps5/pad.cpp
	ps5/pad.h
	ps5/privilege.cpp
	ps5/privilege.h
	ps5/vulkan_display.cpp
	ps5/vulkan_display.h
	ps5/window.h
	ps5/window_system.cpp
	"${PROSPEROEDEN_LAUNCHER}/bitmap_font_engine.cpp"
	"${SANDBOX_ELEVATION}/src/elevation.cpp"
)

# Cemu's precompiled header (Common/precompiled.h) for the files that use Cemu's types; the
# launcher's and the console layer's files do not.
cemu_use_precompiled_header(CemuPS5)
set_source_files_properties(
	frontend/launcher.cpp
	frontend/settings.cpp
	frontend/ui_host.cpp
	ps5/display.cpp
	ps5/log.cpp
	ps5/notify.cpp
	ps5/pad.cpp
	ps5/privilege.cpp
	ps5/vulkan_display.cpp
	"${PROSPEROEDEN_LAUNCHER}/bitmap_font_engine.cpp"
	"${SANDBOX_ELEVATION}/src/elevation.cpp"
	PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
# third-party code, compiled as its authors do
set_source_files_properties(
	"${PROSPEROEDEN_LAUNCHER}/bitmap_font_engine.cpp"
	PROPERTIES COMPILE_OPTIONS "-w")

target_include_directories(CemuPS5 PUBLIC
	"${CMAKE_CURRENT_LIST_DIR}"
	"${CMAKE_CURRENT_LIST_DIR}/app"
	"${CMAKE_CURRENT_LIST_DIR}/cemu"
)
target_include_directories(CemuPS5 PRIVATE
	"${PROSPEROEDEN_LAUNCHER}"
	"${SANDBOX_ELEVATION}"
	"${RMLUI_INCLUDE_DIR}"
)
target_compile_definitions(CemuPS5 PRIVATE RMLUI_STATIC_LIB)
# the version the launcher and the boot log show (sce_sys/param.json has it as 01.000.200)
target_compile_definitions(CemuPS5 PUBLIC PS5CEMU_VERSION="${PS5CEMU_VERSION}")

target_link_libraries(CemuPS5 PUBLIC
	CemuAudio
	CemuCafe
	CemuCommon
	CemuComponents
	CemuConfig
	CemuGui
	CemuInput
	CemuResource
	CemuUtil
	imguiImpl
	"${RMLUI_LIBRARY}"
	"${SDL2_LIBRARY}"
	"${ICONV_LIBRARY}"
	"${FREETYPE_LIBRARY}"
	"${PNG16_LIBRARY}"
	"${BZ2_LIBRARY}"
	"${PSL_LIBRARY}"
)
