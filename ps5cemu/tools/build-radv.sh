#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds RADV, the PS5's Vulkan driver, with PS5_Vulkan's own recipe (its tools/build-radv.sh
# release): the Mesa fork pinned in deps.json (.deps/PS5_Mesa, the revision PS5_Vulkan pins) into
# .deps/PS5_Vulkan/.deps/native/radv-release, where tools/link.sh finds it.
#
# RADV's PS5 winsys is built on the payload SDK fork's platform layer (ps5platform/ and
# libps5platform.a: direct memory, AGC, VideoOut, the heap and libc's gaps), which the public SDK
# does not have. PS5_PAYLOAD_SDK_FORK names a checkout of that fork (Mihawk-99's PS5_PayloadSDK) that
# has the revision PS5_Vulkan's tools/setup-native-dependencies.sh pins.
#
# Host tools, as PS5_Vulkan's recipe needs them: meson, ninja, rsync, and for Mesa's OpenCL
# kernels LLVM, Clang, libclc and the SPIR-V LLVM translator.
#
# A RADV built elsewhere can be used instead: RADV_ARCHIVE (libvulkan_radeon.ps5.a) and RADV_SDK
# (the fork's SDK it was built with) name it to tools/link.sh.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
vulkan=$PS5CEMU_ROOT/.deps/PS5_Vulkan

if [[ -z ${PS5_PAYLOAD_SDK_FORK:-} ]]; then
    echo "RADV needs the payload SDK fork with the PS5 platform layer: set PS5_PAYLOAD_SDK_FORK to its checkout" >&2
    echo "(or name a RADV build with RADV_ARCHIVE and RADV_SDK instead of building one)." >&2
    exit 2
fi
export PS5_PAYLOAD_SDK_FORK
export PS5_MESA_FORK=$PS5CEMU_ROOT/.deps/PS5_Mesa
bash "$vulkan/tools/setup-native-dependencies.sh"
bash "$vulkan/tools/build-radv.sh" release
echo "==> [radv] $vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
