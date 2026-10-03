#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Configures and builds Cemu for the PS5 (build/cemu), with the port's host code from port/.
#
#   tools/build-cemu.sh              apply patches/cemu if needed, configure once, build and link
#                                    build/cemu/ps5cemu.elf (tools/link.sh; PS5CEMU_LINK_CHECK=1 links
#                                    a check without RADV)
#   tools/build-cemu.sh --reconfigure  start the CMake build directory over
#   tools/build-cemu.sh TARGET...    build only those targets
#
# Cemu itself stays at its pinned commit in .deps/Cemu; patches/cemu/*.patch are applied on a
# branch named ps5 (tools/cemu-patches.sh).

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cemu=$PS5CEMU_ROOT/.deps/Cemu
build=$PS5CEMU_BUILD/cemu

if [[ ${1:-} == --reconfigure ]]; then
    rm -rf "$build"
    shift
fi

bash "$PS5CEMU_ROOT/tools/cemu-patches.sh" apply

options=(
    -DCMAKE_TOOLCHAIN_FILE="$PS5CEMU_TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release
    -DCEMU_PS5=ON -DCEMU_PS5_PORT_DIR="$PS5CEMU_ROOT/port"
    -DENABLE_VCPKG=OFF -DENABLE_WXWIDGETS=OFF -DENABLE_OPENGL=OFF -DENABLE_METAL=OFF -DENABLE_VULKAN=ON
    -DENABLE_DISCORD_RPC=OFF -DENABLE_HIDAPI=OFF -DENABLE_SDL=OFF -DENABLE_LIBUSB=OFF -DENABLE_CUBEB=OFF
    -DALLOW_PORTABLE=OFF -DBoost_USE_STATIC_LIBS=ON -DBoost_ROOT="$PS5CEMU_SYSROOT"
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF
    # Azahar's core, when tools/build-azahar.sh has built it (port/CMakeLists.txt)
    -DPS5CEMU_AZAHAR_LIBRARIES="$PS5CEMU_BUILD/azahar/azahar_ps5_libraries.txt"
)
# configured again when the options change (this script's), as CMake would not know
if [[ ! -f $build/build.ninja || $(cat "$build/ps5-options" 2>/dev/null) != "${options[*]}" ]]; then
    mkdir -p "$build"
    cmake -S "$cemu" -B "$build" -G Ninja -Wno-dev "${options[@]}" \
        >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
    echo "${options[*]}" >"$build/ps5-options"
fi

# ps5cemu.elf is relinked when a link check gives way to RADV (tools/link.sh), or the other way round:
# ninja does not see the difference
elf=$build/ps5cemu.elf
if [[ -f $elf.linkcheck && ${PS5CEMU_LINK_CHECK:-0} != 1 ]] || [[ -f $elf && ! -f $elf.linkcheck && ${PS5CEMU_LINK_CHECK:-0} == 1 ]]; then
    rm -f "$elf" "$elf.linkcheck"
fi

ninja -C "$build" -j "$JOBS" "$@"
