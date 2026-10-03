# SPDX-License-Identifier: GPL-3.0-or-later
# PS5CEMU-HAR: the port's host code, added to Cemu's tree by its CEMU_PS5 option (src/CMakeLists.txt):
# the console layer (ps5/), Cemu's start-up and graphic packs (app/), Azahar's side (azahar/), and
# the launcher (frontend/), which draws ProsperoEden's RmlUi layout in software through SDL, as
# ProsperoEden does. Cemu's own platform classes (cemu/) are added to Cemu's targets by the patch.

set(PS5CEMU_VERSION "2.0.0")
# the emulators' pinned commits (tools/deps.json), which the diagnostics and the boot log name: they
# are no releases, so their own version numbers would say nothing (Cemu's reads 0.0.0)
file(READ "${CMAKE_CURRENT_LIST_DIR}/../tools/deps.json" PS5CEMU_DEPS_JSON)
string(JSON PS5CEMU_DEPS_COUNT LENGTH "${PS5CEMU_DEPS_JSON}" items)
math(EXPR PS5CEMU_DEPS_LAST "${PS5CEMU_DEPS_COUNT} - 1")
foreach(index RANGE ${PS5CEMU_DEPS_LAST})
	string(JSON name GET "${PS5CEMU_DEPS_JSON}" items ${index} name)
	if(name STREQUAL "cemu" OR name STREQUAL "azahar")
		string(JSON commit GET "${PS5CEMU_DEPS_JSON}" items ${index} commit)
		string(SUBSTRING "${commit}" 0 7 commit)
		string(TOUPPER "${name}" name)
		set(PS5CEMU_${name}_COMMIT "${commit}")
	endif()
endforeach()
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
# the box art GameTDB serves (app/boxart.cpp): JPEG, and PNG through libpng16 above
find_library(TURBOJPEG_LIBRARY turbojpeg REQUIRED)

add_library(CemuPS5 STATIC
	app/emulator.cpp
	app/emulator.h
	app/controllers.cpp
	app/boxart.cpp
	app/boxart.h
	app/covers.cpp
	app/graphic_packs.cpp
	app/ingame.cpp
	app/ingame.h
	app/ingame3ds.cpp
	app/ingame3ds.h
	app/install.cpp
	app/menu_canvas.h
	app/paths.h
	azahar/azahar.h
	app/boxart.cpp
	azahar/controls.cpp
	azahar/controls.h
	azahar/library.cpp
	azahar/library.h
	frontend/bubbles.cpp
	frontend/bubbles.h
	frontend/launcher.cpp
	frontend/launcher.h
	frontend/settings.cpp
	frontend/settings.h
	frontend/ui_host.cpp
	frontend/ui_host.h
	frontend/wave.cpp
	frontend/wave.h
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
	ps5/threads.cpp
	ps5/threads.h
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
	app/boxart.cpp
	azahar/controls.cpp
	azahar/library.cpp
	azahar/unavailable.cpp
	frontend/bubbles.cpp
	frontend/launcher.cpp
	frontend/settings.cpp
	frontend/ui_host.cpp
	frontend/wave.cpp
	ps5/display.cpp
	ps5/log.cpp
	ps5/notify.cpp
	ps5/pad.cpp
	ps5/privilege.cpp
	ps5/threads.cpp
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
	# the platform layer RADV is linked with: its heap's and executable memory's statistics
	"${PS5CEMU_DEPS}/PS5_PayloadSDK/platform/include"
	"${SANDBOX_ELEVATION}"
	"${RMLUI_INCLUDE_DIR}"
)
target_compile_definitions(CemuPS5 PRIVATE RMLUI_STATIC_LIB)
# the version the launcher and the boot log show (sce_sys/param.json has it as 01.001.000)
target_compile_definitions(CemuPS5 PUBLIC PS5CEMU_VERSION="${PS5CEMU_VERSION}"
	PS5CEMU_CEMU_COMMIT="${PS5CEMU_CEMU_COMMIT}" PS5CEMU_AZAHAR_COMMIT="${PS5CEMU_AZAHAR_COMMIT}")

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
	"${TURBOJPEG_LIBRARY}"
)

# Azahar's core and its PS5 frontend (tools/build-azahar.sh builds them in Azahar's tree): the
# archives its build lists. Without that build, 3DS games say why they do not start
# (azahar/unavailable.cpp).
set(PS5CEMU_AZAHAR_LIBRARIES "" CACHE FILEPATH "The list of Azahar's archives (build/azahar/azahar_ps5_libraries.txt)")
if(PS5CEMU_AZAHAR_LIBRARIES AND EXISTS "${PS5CEMU_AZAHAR_LIBRARIES}")
	file(STRINGS "${PS5CEMU_AZAHAR_LIBRARIES}" AZAHAR_ARCHIVES)
	target_link_libraries(CemuPS5 PUBLIC ${AZAHAR_ARCHIVES})
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PS5CEMU_AZAHAR_LIBRARIES}")
	message(STATUS "PS5CEMU-HAR: Azahar's core from ${PS5CEMU_AZAHAR_LIBRARIES}")
else()
	target_sources(CemuPS5 PRIVATE azahar/unavailable.cpp)
	message(STATUS "PS5CEMU-HAR: no Azahar build; 3DS games will not start")
endif()
