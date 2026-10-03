#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The launcher's screens on the PC, as PNGs in build/preview, to work on its layout without a
# console (tools/launcher-preview/preview.cpp says how):
#
#   tools/preview-launcher.sh [SCRIPT]    default: tools/launcher-preview/screens.txt
#
# Needs what `make deps` fetches (RmlUi, ProsperoEden, pacbrew's fmt and the sysroot's RapidJSON),
# clang-18, cmake, ninja, python3 and zlib's headers (zlib1g-dev). What the launcher writes (its
# settings, the 3DS games' icons) goes in build/preview/data, in place of /data/ps5cemu.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
cd "$PS5CEMU_ROOT"
out=$PS5CEMU_BUILD/preview
rmlui=$PS5CEMU_ROOT/.deps/RmlUi-6.2
prospero=$PS5CEMU_ROOT/.deps/ProsperoEden/headless/prosperoeden
script=${1:-tools/launcher-preview/screens.txt}
mkdir -p "$out/include"

# RmlUi's core for the PC, without a font engine: the launcher brings ProsperoEden's
if [[ ! -f $out/rmlui/librmlui.a ]]; then
    cmake -S "$rmlui" -B "$out/rmlui" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-18 \
        -DCMAKE_CXX_COMPILER=clang++-18 -DBUILD_SHARED_LIBS=OFF -DRMLUI_FONT_ENGINE=none -DRMLUI_SAMPLES=OFF \
        -DRMLUI_PRECOMPILED_HEADERS=OFF >"$out/rmlui.configure.log" || { tail -30 "$out/rmlui.configure.log"; exit 1; }
    cmake --build "$out/rmlui" -j "$JOBS" >"$out/rmlui.build.log" || { grep -m10 -A5 error "$out/rmlui.build.log"; exit 1; }
fi
ln -sfn "$PS5CEMU_PACBREW/include/fmt" "$out/include/fmt"

# the launcher's files, as tools/package.sh puts them in the app
ui=$out/ui
rm -rf "$ui"
python3 -B tools/recolour-ui.py "$prospero/ui" port/frontend/ui/ps5cemu.rcss "$ui"
python3 -B tools/render-layout.py "$ui"
[[ -f $out/icons/ui/icons/har-72.tga && -z $(find tools/render-icons.py tools/render-background.py -newer "$out/icons/ui/icons/har-72.tga") ]] ||
    python3 -B tools/render-icons.py "$out/icons" >/dev/null
cp "$out/icons/ui/icons/"*.tga "$ui/icons/"

# folders for the folder browsers to show: Wii U games, and an update and DLC to install; and 3DS
# games for Azahar's side to read
games=$out/games
rm -rf "$games" "$out/data"
mkdir -p "$games/Mario Kart 8" "$games/Splatoon" "$games/Super Mario 3D World [00050000101C9400]" "$out/data"
for title in "BotW Update v208" "BotW DLC"; do
    mkdir -p "$games/Installs/$title/code" "$games/Installs/$title/content" "$games/Installs/$title/meta"
    echo '<menu/>' >"$games/Installs/$title/meta/meta.xml"
done
mkdir -p "$games/Installs/Screenshots"
python3 -B tools/launcher-preview/make-3ds-samples.py "$games/3ds"

version=$(sed -n 's/^set(PS5CEMU_VERSION "\(.*\)")/\1/p' port/CMakeLists.txt)
if [[ ! -x $out/launcher-preview ]] || [[ -n $(find port/frontend port/azahar port/app/emulator.h port/app/paths.h port/app/boxart.h tools/launcher-preview -newer "$out/launcher-preview" -name '*.[ch]*' -print -quit) ]]; then
    clang++-18 -std=c++20 -O1 -w -DFMT_HEADER_ONLY -DRMLUI_STATIC_LIB "-DPS5CEMU_VERSION=\"$version\"" "-DPS5CEMU_DATA=\"$out/data\"" \
        -I "$out/include" -I "$rmlui/Include" -I "$PS5CEMU_SYSROOT/include" -I port -I port/app -I "$prospero" \
        tools/launcher-preview/preview.cpp port/frontend/launcher.cpp port/frontend/settings.cpp port/frontend/bubbles.cpp \
        port/frontend/wave.cpp port/azahar/library.cpp port/azahar/controls.cpp port/azahar/unavailable.cpp \
        "$prospero/bitmap_font_engine.cpp" "$out/rmlui/librmlui.a" -lz -o "$out/launcher-preview"
fi
"$out/launcher-preview" "$ui" "$out" "$script" "$games" "$games/3ds" 2>"$out/preview.log"
