# SPDX-License-Identifier: GPL-3.0-or-later
# Sourced by the build scripts: where the pinned inputs and the build outputs live.

ps5cemu_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PS5CEMU_ROOT=$ps5cemu_root
export PS5_PAYLOAD_SDK=${PS5_PAYLOAD_SDK:-$ps5cemu_root/.deps/ps5-native-app-boilerplate/.deps/native/ps5-payload-sdk}
export PS5CEMU_BOILERPLATE=$ps5cemu_root/.deps/ps5-native-app-boilerplate
export PS5CEMU_PACBREW=$ps5cemu_root/.deps/pacbrew-0.40.2/opt/ps5-payload-sdk/target/user/homebrew
export PS5CEMU_SYSROOT=$ps5cemu_root/build/sysroot
export PS5CEMU_BUILD=$ps5cemu_root/build
export PS5CEMU_TOOLCHAIN=$ps5cemu_root/tools/ps5.cmake
export JOBS=${JOBS:-$(nproc)}

ps5cemu_cmake() {
    # ps5cemu_cmake SOURCE BUILD [cmake arguments...]: configure, build and install into the sysroot.
    local source=$1 build=$2
    shift 2
    cmake -S "$source" -B "$build" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$PS5CEMU_TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PS5CEMU_SYSROOT" "$@" >"$build.configure.log" 2>&1 ||
        { tail -40 "$build.configure.log"; return 1; }
    cmake --build "$build" -j "$JOBS" >"$build.build.log" 2>&1 || { grep -m20 -B2 -A8 "error" "$build.build.log"; return 1; }
    cmake --install "$build" >"$build.install.log" 2>&1 || { tail -20 "$build.install.log"; return 1; }
}
