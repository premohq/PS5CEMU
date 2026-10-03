#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Assembles the app folder from build/cemu/ps5cemu.elf, as PS5_Vulkan packages a RADV title
# (tools/build-radv-title.sh) and ProsperoEden its own (tools/package-headless-native.sh):
#
#   build/app/PPSA99360/              copied to /data/homebrew/PPSA99360 on the console
#     eboot.bin                       the ELF, converted and fake-signed by ps5-native-tool
#     sce_sys/param.json, icon0.png   the title's parameters and home screen tile
#     sce_sys/pic0.dds, pic1.dds      its home screen background (selected, starting)
#     sce_module/libc.prx             the boilerplate's clean-room runtime
#     sandbox-elevator.elf            the boilerplate's /data helper, built for PPSA99360
#     assets/ui/                      the launcher: ProsperoEden's artwork (in blue and in gold) and
#                                     fonts, its layouts, port/frontend/ui's stylesheet
#     assets/cemu/                    Cemu's game profiles and the Wii U system fonts
#     assets/graphicPacks/            the community graphic packs (installed on first start)
#
#   tools/package.sh          the app; the ELF must be linked with RADV (tools/link.sh)
#   tools/package.sh --check  the same steps for a link check's ELF, into build/app-check: not an app

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
title=PPSA99360
check=0
[[ ${1:-} == --check ]] && check=1
elf=$PS5CEMU_BUILD/cemu/ps5cemu.elf
deps=$PS5CEMU_ROOT/.deps
vulkan=$deps/PS5_Vulkan
boilerplate=$PS5CEMU_BOILERPLATE
work=$PS5CEMU_BUILD/package
app=$PS5CEMU_BUILD/$( ((check)) && echo app-check || echo app)/$title

[[ -f $elf ]] || { echo "$elf is missing: build it first (make build)" >&2; exit 2; }
if [[ -f $elf.linkcheck ]] && ((!check)); then
    echo "$elf is a link check without RADV, not an app: build RADV (make radv), then make build" >&2
    exit 2
fi
mkdir -p "$work"

# The host tool that converts and signs the ELF: PS5_Vulkan's native tooling, which its RADV
# titles go through, with the boilerplate's zlib.
tool=$PS5CEMU_BUILD/host/ps5-native-tool
native=$vulkan/tooling/native
zlib=$boilerplate/.deps/native/zlib/root
if [[ ! -x $tool ]] || [[ -n $(find "$native" -newer "$tool" -name '*.[ch]*' -print -quit) ]]; then
    mkdir -p "$(dirname "$tool")"
    clang++-18 -std=c++20 -O2 -Wall -Wextra -I "$zlib/usr/include" "$native/native_app_builder.cpp" \
        "$native/self_container.cpp" "$native/elf_object.cpp" "$native/sce_module_writer.cpp" \
        "$zlib/usr/lib/libz.a" -o "$tool"
fi

# The runtime libc.prx: the boilerplate's clean-room build, checked against its recorded digest.
[[ -f $boilerplate/runtime/libc.prx ]] || USE_CCACHE=${USE_CCACHE:-0} bash "$boilerplate/tools/rebuild-libc.sh"
(cd "$boilerplate/runtime" && sha256sum --check --strict --quiet libc.prx.sha256)

# eboot.bin: the AGC link stubs RADV was linked with name its imports from those modules
sdk=$PS5_PAYLOAD_SDK
stubs=()
if ((!check)); then
    sdk=${RADV_SDK:-$vulkan/.deps/native/ps5-payload-sdk}
    stubs=(--stub "$PS5CEMU_BUILD/link/libSceAgc.so" --stub "$PS5CEMU_BUILD/link/libSceAgcDriver.so")
fi
"$tool" link --in "$elf" --out "$work/eboot.elf" --stub-dir "$sdk/target/lib" "${stubs[@]}" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
rm -rf "$app"
mkdir -p "$app/sce_sys" "$app/sce_module" "$app/assets"
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$boilerplate/runtime/libc.prx" "$app/sce_module/libc.prx"
python3 -B "$PS5CEMU_ROOT/tools/render-icons.py" "$work/icons"
cp "$PS5CEMU_ROOT/sce_sys/param.json" "$work/icons/sce_sys/icon0.png" "$app/sce_sys/"
# the home screen's background while PS5Cemu is selected and while it starts: one picture
# (tools/render-presentation.py renders sce_sys/pic0.dds), in the form the boilerplate checks
cp "$PS5CEMU_ROOT/sce_sys/pic0.dds" "$app/sce_sys/pic0.dds"
cp "$PS5CEMU_ROOT/sce_sys/pic0.dds" "$app/sce_sys/pic1.dds"
bash "$boilerplate/tools/validate-assets.sh" "$app/sce_sys" >/dev/null

# The /data helper elfldr runs when the HEN does not jailbreak PS5Cemu (port/ps5/privilege.h).
# It serves one title only: the boilerplate's, made PS5Cemu's.
helper=$work/elevation
rm -rf "$helper"
mkdir -p "$helper/payload"
cp "$boilerplate/examples/sandbox-elevation/protocol.hpp" "$helper/"
cp "$boilerplate/examples/sandbox-elevation/payload/Makefile" "$helper/payload/"
sed "s/PPSA99790/$title/" "$boilerplate/examples/sandbox-elevation/payload/main.cpp" >"$helper/payload/main.cpp"
grep -q "\"$title\"" "$helper/payload/main.cpp" || { echo "the elevation helper's title ID moved" >&2; exit 1; }
make -s -C "$helper/payload" PS5_PAYLOAD_SDK="$PS5_PAYLOAD_SDK" OUTPUT="$app/sandbox-elevator.elf"
python3 -B "$boilerplate/tools/validate-elevation-helper.py" "$app/sandbox-elevator.elf" >/dev/null

# The launcher: ProsperoEden's artwork, fonts and stylesheet in Cemu's blue and Azahar's gold
# (tools/recolour-ui.py), its layouts (tools/render-layout.py: the start screen, Cemu's side and
# Azahar's) and PS5CEMU-HAR's icons. Its backgrounds, the Homebrew Launchers' bubbles and waves, are
# drawn as it runs (port/frontend/bubbles.h, wave.h).
ui=$app/assets/ui
prospero=$deps/ProsperoEden/headless/prosperoeden/ui
python3 -B "$PS5CEMU_ROOT/tools/recolour-ui.py" "$prospero" "$PS5CEMU_ROOT/port/frontend/ui/ps5cemu.rcss" "$ui"
python3 -B "$PS5CEMU_ROOT/tools/render-layout.py" "$ui"
cp "$work/icons/ui/icons/"*.tga "$ui/icons/"
python3 - "$ui" <<'PY'
import os, re, sys
ui = sys.argv[1]
for layout in ("start.rml", "main.rml", "azahar.rml"):
    text = open(os.path.join(ui, layout)).read()
    missing = [src for src in sorted(set(re.findall(r'(?:src|href)="([^"]+)"', text))) if not os.path.isfile(os.path.join(ui, src))]
    if missing:
        sys.exit(f"the launcher's {layout} names missing files: " + ", ".join(missing))
PY

# Cemu's read-only data: game profiles, and the Wii U's system fonts games draw text with.
mkdir -p "$app/assets/cemu/resources"
cp -a "$deps/Cemu/bin/gameProfiles" "$app/assets/cemu/"
cp -a "$deps/Cemu/bin/resources/sharedFonts" "$app/assets/cemu/resources/"

# The community graphic packs, with the version file Cemu's downloader writes.
python3 - "$deps/graphic-packs/graphicPacks987.zip" "$app/assets/graphicPacks" <<'PY'
import os, sys, zipfile
archive, target = sys.argv[1:3]
with zipfile.ZipFile(archive) as packs:
    packs.extractall(target)
with open(os.path.join(target, "version.txt"), "w") as version:
    version.write("Github987")
PY

if ((check)); then
    echo "LINK CHECK ONLY: built without RADV, this folder cannot run on a console." >"$app/NOT-AN-APP.txt"
fi
"$tool" self --inspect --file "$app/eboot.bin" >"$work/eboot-inspection.txt"
echo "==> [package] $app ($(du -sh "$app" | cut -f1), eboot.bin $(stat -c %s "$app/eboot.bin") bytes)"
