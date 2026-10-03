#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Configures and builds Azahar's core for the PS5 (build/azahar): its emulation libraries, from
# Mihawk's PS5_Azahar (.deps/PS5_Azahar, with PS5_Dynarmic, whose code cache is the console's
# executable memory), with the toolchain Cemu's build uses (tools/ps5.cmake), and PS5CEMU-HAR's
# frontend for it (port/azahar: the window, the DualSense, AudioOut). Azahar's own frontends (Qt,
# libretro) are not built. build/azahar/azahar_ps5_libraries.txt then lists the archives the app
# links (tools/build-cemu.sh).
#
#   tools/build-azahar.sh               apply patches/azahar if needed, configure, build
#   tools/build-azahar.sh --reconfigure start the build directory over
#   tools/build-azahar.sh TARGET...     build only those targets
#
# Azahar shares Cemu's copies of fmt, glslang, zstd and OpenSSL (USE_SYSTEM_*), so the app links
# each once. No crypto keys are built in (ENABLE_BUILTIN_KEYBLOB=OFF): games are decrypted dumps,
# or the player's own aes_keys.txt decrypts them. Dynarmic's code cache comes from the platform
# layer RADV is linked with (ps5platform/exec.h, in libps5platform.a), whose headers it is given.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
azahar=$PS5CEMU_ROOT/.deps/PS5_Azahar
build=$PS5CEMU_BUILD/azahar

if [[ ${1:-} == --reconfigure ]]; then
    rm -rf "$build"
    shift
fi

bash "$PS5CEMU_ROOT/tools/azahar-patches.sh" apply

options=(
    -DCMAKE_TOOLCHAIN_FILE="$PS5CEMU_TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release
    -DENABLE_QT=OFF -DENABLE_SDL2=OFF -DENABLE_LIBRETRO=OFF -DENABLE_TESTS=OFF -DENABLE_ROOM=OFF
    -DENABLE_ROOM_STANDALONE=OFF -DENABLE_WEB_SERVICE=OFF -DENABLE_SCRIPTING=OFF -DENABLE_GDBSTUB=OFF
    -DENABLE_CUBEB=OFF -DENABLE_OPENAL=OFF -DENABLE_LIBUSB=OFF -DENABLE_DISCORD_RPC=OFF
    -DENABLE_OPENGL=OFF -DENABLE_VULKAN=ON -DENABLE_SOFTWARE_RENDERER=OFF -DENABLE_BUILTIN_KEYBLOB=OFF
    -DENABLE_LTO=OFF -DCITRA_USE_PRECOMPILED_HEADERS=OFF -DCITRA_WARNINGS_AS_ERRORS=OFF
    -DUSE_SYSTEM_FMT=ON -DUSE_SYSTEM_GLSLANG=ON -DUSE_SYSTEM_ZSTD=ON -DUSE_SYSTEM_OPENSSL=ON
    -DPS5_PLATFORM_INCLUDE="$PS5CEMU_ROOT/.deps/PS5_PayloadSDK/platform/include"
    -DAZAHAR_EXTERNAL_FRONTEND="$PS5CEMU_ROOT/port/azahar"
)
# configured again when the options change (this script's), as CMake would not know
if [[ ! -f $build/build.ninja || $(cat "$build/ps5-options" 2>/dev/null) != "${options[*]}" ]]; then
    mkdir -p "$build"
    cmake -S "$azahar" -B "$build" -G Ninja -Wno-dev "${options[@]}" \
        >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
    echo "${options[*]}" >"$build/ps5-options"
fi

if (($# == 0)); then
    set -- azahar_ps5_libraries
fi
ninja -C "$build" -j "$JOBS" "$@"
