#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Configures and builds Cemu for the PS5 (build/cemu), with the port's host code from port/.
#
#   tools/build-cemu.sh              apply patches/cemu if needed, configure once, build
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
# RmlUi's Vulkan renderer (compiled into the launcher from the pinned sources) gets its Vulkan
# functions from the driver: patches/rmlui, applied once
rmlui=$PS5CEMU_ROOT/.deps/RmlUi-6.2
for patch in "$PS5CEMU_ROOT"/patches/rmlui/*.patch; do
    if ! git -C "$rmlui" apply --reverse --check "$patch" 2>/dev/null; then
        git -C "$rmlui" apply "$patch"
        echo "==> [rmlui] applied ${patch##*/}"
    fi
done

if [[ ! -f $build/build.ninja ]]; then
    mkdir -p "$build"
    cmake -S "$cemu" -B "$build" -G Ninja -Wno-dev \
        -DCMAKE_TOOLCHAIN_FILE="$PS5CEMU_TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
        -DCEMU_PS5=ON -DCEMU_PS5_PORT_DIR="$PS5CEMU_ROOT/port" \
        -DENABLE_VCPKG=OFF -DENABLE_WXWIDGETS=OFF -DENABLE_OPENGL=OFF -DENABLE_METAL=OFF -DENABLE_VULKAN=ON \
        -DENABLE_DISCORD_RPC=OFF -DENABLE_HIDAPI=OFF -DENABLE_SDL=OFF -DENABLE_LIBUSB=OFF -DENABLE_CUBEB=OFF \
        -DALLOW_PORTABLE=OFF -DBoost_USE_STATIC_LIBS=ON -DBoost_ROOT="$PS5CEMU_SYSROOT" \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE=OFF \
        >"$build.configure.log" 2>&1 || { tail -40 "$build.configure.log"; exit 1; }
fi

ninja -C "$build" -j "$JOBS" "$@"
