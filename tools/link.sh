#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The PS5 link of Cemu's executable: tools/ps5.cmake makes this CMake's link rule, which calls
#
#   tools/link.sh OUTPUT [link flags] OBJECTS... LIBRARIES...
#
# It links as PS5_Vulkan links a RADV title (its tools/build-radv-title.sh): PS5_Vulkan's native
# CRT and C++ runtime, Cemu's and the port's objects and archives, RADV linked whole with its
# platform layer from the payload SDK fork (PS5_Vulkan's tools/radv-link.sh, sourced as is), the
# AGC link stubs, and the SDK's system module stubs, with prospero-lld into OUTPUT, a PIE ELF that
# tools/package.sh converts into eboot.bin.
#
# RADV and the SDK fork it was built with (tools/build-radv.sh): RADV_ARCHIVE and RADV_SDK, by
# default PS5_Vulkan's release build in .deps/PS5_Vulkan/.deps/native. Without them, the link
# fails, unless PS5CEMU_LINK_CHECK=1: then a stand-in that has no GPU takes RADV's place, which
# checks that everything else links, and OUTPUT.linkcheck marks the result as not an app.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
vulkan=$PS5CEMU_ROOT/.deps/PS5_Vulkan
radv_archive=${RADV_ARCHIVE:-$vulkan/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a}
radv_sdk=${RADV_SDK:-$vulkan/.deps/native/ps5-payload-sdk}
work=$PS5CEMU_BUILD/link
output=$1
shift

# CMake's link line, as prospero-lld takes it: the compiler driver's options go, -Wl, ones are
# unwrapped, and the system libraries are the SDK's module stubs, linked below.
inputs=()
for argument in "$@"; do
    case $argument in
        -Xlinker | -pthread | -lpthread | -ldl | -lrt | -lm | -rdynamic | -fPIC | -fPIE | -pie) ;;
        -O* | -g* | -m* | -f* | --target=* | -isysroot* | -isystem* | -D*) ;;
        -Wl,*) IFS=, read -r -a flags <<<"${argument#-Wl,}" && inputs+=("${flags[@]}") ;;
        *) inputs+=("$argument") ;;
    esac
done

mkdir -p "$work"
cc() { clang-18 -target x86_64-sie-ps5 -isysroot "$PS5_PAYLOAD_SDK" -isystem "$PS5_PAYLOAD_SDK/target/include/c++/v1" \
    -isystem "$PS5_PAYLOAD_SDK/target/include" -fno-stack-protector -fno-plt -femulated-tls \
    -fvisibility-nodllstorageclass=default "$@"; }
for name in app_crt app_cpp_runtime; do
    object=$work/$name.o
    if [[ ! -f $object || $vulkan/tooling/native/$name.cpp -nt $object ]]; then
        cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
            -c "$vulkan/tooling/native/$name.cpp" -o "$object"
    fi
done

rm -f "$output.linkcheck"
if [[ -f $radv_archive && -f $radv_sdk/target/lib/libps5platform.a ]]; then
    sdk=$radv_sdk
    # AGC comes from system modules; these link stubs only name the imports (as build-radv-title.sh)
    for stub in libSceAgc:agc_canary_link_stub libSceAgcDriver:agc_driver_canary_link_stub; do
        library=${stub%%:*} source=$vulkan/vendor/ps5/sdk/stubs/${stub#*:}.c
        if [[ ! -f $work/$library.so || $source -nt $work/$library.so || ${BASH_SOURCE[0]} -nt $work/$library.so ]]; then
            # exported as the PS5 target exports only with its dllstorageclass default
            clang-18 -target x86_64-sie-ps5 -isysroot "$sdk" -isystem "$sdk/target/include" -std=c11 -O2 -fPIC \
                -fvisibility-nodllstorageclass=default -c "$source" -o "$work/$library.o"
            "$sdk/bin/prospero-lld" --shared -soname "$library.prx" -o "$work/$library.so" "$work/$library.o"
        fi
    done
    # The recipe finds Clang's builtins through the compiler's resource directory; here they are
    # compiler-rt's emulated TLS (tools/build-deps.sh).
    printf '#!/bin/sh\necho %q\n' "$PS5CEMU_SYSROOT/clang-rt" >"$work/clang"
    chmod +x "$work/clang"
    # shellcheck source=/dev/null
    source "$vulkan/tools/radv-link.sh"
    PS5_CLANG=$work/clang radv_link_recipe "$vulkan" "$sdk" "$radv_archive"
    # everything is linked in one group below, and lld takes no group inside another
    flattened=()
    for input in "${radv_link_inputs[@]}"; do
        [[ $input == --start-group || $input == --end-group ]] || flattened+=("$input")
    done
    radv_link_inputs=("${flattened[@]}")
    stubs=("$work/libSceAgc.so" "$work/libSceAgcDriver.so")
    check=0
    echo "==> [link] RADV: $radv_archive"
elif [[ ${PS5CEMU_LINK_CHECK:-0} == 1 ]]; then
    sdk=$PS5_PAYLOAD_SDK
    clang-18 -target x86_64-sie-ps5 -isysroot "$sdk" -isystem "$sdk/target/include" -std=c11 -O2 -fPIC \
        -c "$PS5CEMU_ROOT/tools/radv-stand-in.c" -o "$work/radv-stand-in.o"
    radv_linker_script=(-T "$vulkan/tooling/psbc/ps5-pie-unwind.ld" -L "$vulkan/tooling/native")
    radv_link_inputs=(-L "$sdk/target/lib" "$work/radv-stand-in.o" "$sdk/target/lib/libc++.a" "$sdk/target/lib/libc++abi.a"
        "$sdk/target/lib/libunwind.a" "$PS5CEMU_SYSROOT/clang-rt/lib/linux/libclang_rt.builtins-x86_64.a")
    radv_link_flags=()
    stubs=()
    check=1
    echo "==> [link] no RADV: checking the link with a stand-in driver (not an app)"
else
    echo "RADV is missing: $radv_archive, with the payload SDK fork $radv_sdk." >&2
    echo "Build it with tools/build-radv.sh (PS5_PAYLOAD_SDK_FORK=<the fork>), or name a build with" >&2
    echo "RADV_ARCHIVE and RADV_SDK. PS5CEMU_LINK_CHECK=1 checks the rest of the link without it." >&2
    exit 2
fi

# Optional hooks, weak and checked before use, that nothing on the console provides: as null they
# stay out of the imports, which must all name a system module. The SDK libc's dlfcn wrappers report
# no dynamic loader without theirs (as ProsperoEden links); zstd's are its tracing.
absent=()
for name in __dlopen __dlsym __dladdr __dlclose __dlerror ZSTD_trace_compress_begin ZSTD_trace_compress_end \
        ZSTD_trace_decompress_begin ZSTD_trace_decompress_end; do
    absent+=("--defsym=$name=0")
done
# RADV's platform layer has the real thread-local destructor registration; a link check has none
((check)) && absent+=(--defsym=__cxa_thread_atexit_impl=0)

# The system modules a title imports from: all the SDK names, but the web process's (WebKit's POSIX
# layer and its libkernel, which a title does not load; the SDK's libc.a below has what they would
# have supplied, fnmatch) and the Mono runtime's.
modules=()
for module in "$sdk"/target/lib/*.so; do
    case ${module##*/} in libScePosixForWebKit.so | libkernel_web.so | libmonosgen-2.0.so) ;; *) modules+=("$module") ;; esac
done

# Mesa's dispatch tables name every entry point weakly, and those nothing defines must read as null
# rather than become imports, which the title converter would look for in the system modules: as
# PS5_Vulkan links its titles (its tools/check-vulkan-runtime.sh), with no dynamic linker (LLD 18 then
# leaves undefined weak symbols out of the dynamic symbol table) and, for later LLDs, explicitly.
# RADV goes first: its archive, linked whole, has zlib built in (Mesa's subproject), which then
# stands for the zlib pacbrew's libz.a would otherwise add a second time.
"$sdk/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr --no-dynamic-linker -z nodynamic-undefined-weak \
    "${radv_link_flags[@]}" "${absent[@]}" \
    --version-script "$vulkan/tooling/native/app-symbols.map" --exclude-libs=ALL --gc-sections \
    --error-limit=0 -Map="$output.map" -e _start -o "$output" \
    "$work/app_crt.o" "$work/app_cpp_runtime.o" \
    --start-group "${radv_link_inputs[@]}" "${inputs[@]}" --end-group \
    "${stubs[@]}" --as-needed "${modules[@]}" \
    "$sdk/target/lib/libc.a" # what no module exports to a title and the platform layer does not bind (as ProsperoEden links)
if ((check)); then
    touch "$output.linkcheck"
fi
echo "==> [link] $output ($(stat -c %s "$output") bytes)"
