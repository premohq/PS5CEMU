#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds RADV, the PS5's Vulkan driver, with PS5_Vulkan's own recipe (its tools/build-radv.sh
# release): the Mesa fork pinned in deps.json (.deps/PS5_Mesa, the revision PS5_Vulkan pins) into
# .deps/PS5_Vulkan/.deps/native/radv-release, where tools/link.sh finds it.
#
# RADV's PS5 winsys is built on the payload SDK fork's platform layer (ps5platform/ and
# libps5platform.a: direct memory, AGC, VideoOut, the heap and libc's gaps), which the public SDK
# does not have: Mihawk-99's PS5_PayloadSDK, pinned in deps.json (.deps/PS5_PayloadSDK).
# PS5_PAYLOAD_SDK_FORK names another checkout that has the revision PS5_Vulkan's
# tools/setup-native-dependencies.sh pins.
#
# Host tools, as PS5_Vulkan's recipe needs them: meson, mako, ninja, rsync, and for Mesa's OpenCL
# kernels LLVM, Clang, libclc and the SPIR-V LLVM translator (on Ubuntu: llvm-18-dev,
# libclang-18-dev, libclc-18-dev, libllvmspirvlib-18-dev, llvm-spirv-18).
#
# A RADV built elsewhere can be used instead: RADV_ARCHIVE (libvulkan_radeon.ps5.a) and RADV_SDK
# (the fork's SDK it was built with) name it to tools/link.sh.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
vulkan=$PS5CEMU_ROOT/.deps/PS5_Vulkan

export PS5_PAYLOAD_SDK_FORK=${PS5_PAYLOAD_SDK_FORK:-$PS5CEMU_ROOT/.deps/PS5_PayloadSDK}
[[ -d $PS5_PAYLOAD_SDK_FORK/platform ]] || { echo "$PS5_PAYLOAD_SDK_FORK is not the payload SDK fork: run make deps" >&2; exit 2; }
export PS5_MESA_FORK=$PS5CEMU_ROOT/.deps/PS5_Mesa
bash "$vulkan/tools/setup-native-dependencies.sh"
bash "$vulkan/tools/build-radv.sh" release
echo "==> [radv] $vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
