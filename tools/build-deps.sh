#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Builds, for the PS5, the libraries Cemu needs that pacbrew does not provide, and installs
# them into build/sysroot: Boost (filesystem, program_options, nowide and the headers),
# pugixml, RapidJSON, libzip, glslang, CMake package files for pacbrew's header-only glm, and
# Clang's builtins from compiler-rt (emulated TLS, __cpu_model).
# Each library is built once; delete build/sysroot/.stamps/<name> to rebuild one.

set -euo pipefail
source "$(dirname -- "${BASH_SOURCE[0]}")/env.sh"
deps=$PS5CEMU_ROOT/.deps
work=$PS5CEMU_BUILD/deps
stamps=$PS5CEMU_SYSROOT/.stamps
mkdir -p "$work" "$stamps" "$PS5CEMU_SYSROOT/include" "$PS5CEMU_SYSROOT/lib/cmake"

done_already() { [[ -f $stamps/$1 ]] && echo "==> [deps] $1 is built"; }
mark_done() { touch "$stamps/$1"; echo "==> [deps] built $1"; }

# Boost: the three compiled libraries Cemu links, built from their sources with a small CMake
# project (the b2 tarball has no top-level CMake build). Everything else is header-only.
if ! done_already boost; then
    boost=$deps/boost-1.90.0
    mkdir -p "$work/boost-src"
    cat >"$work/boost-src/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.21)
project(boost_ps5 CXX)
set(CMAKE_CXX_STANDARD 20)
add_compile_definitions(BOOST_ALL_NO_LIB BOOST_ALL_STATIC_LINK)
include_directories(SYSTEM "$boost")
file(GLOB fs_sources "$boost/libs/filesystem/src/*.cpp")
list(FILTER fs_sources EXCLUDE REGEX "windows_file_codecvt")
add_library(boost_filesystem STATIC \${fs_sources})
target_include_directories(boost_filesystem PRIVATE "$boost/libs/filesystem/src")
# The SDK's libc++ 18 has no std::atomic_ref; Boost.Atomic's is header-only for pointers.
target_compile_definitions(boost_filesystem PRIVATE BOOST_FILESYSTEM_SOURCE BOOST_FILESYSTEM_STATIC_LINK=1 BOOST_FILESYSTEM_NO_CXX20_ATOMIC_REF)
file(GLOB po_sources "$boost/libs/program_options/src/*.cpp")
list(FILTER po_sources EXCLUDE REGEX "winmain")
add_library(boost_program_options STATIC \${po_sources})
target_compile_definitions(boost_program_options PRIVATE BOOST_PROGRAM_OPTIONS_SOURCE)
file(GLOB nowide_sources "$boost/libs/nowide/src/*.cpp")
add_library(boost_nowide STATIC \${nowide_sources})
target_compile_definitions(boost_nowide PRIVATE BOOST_NOWIDE_SOURCE)
install(TARGETS boost_filesystem boost_program_options boost_nowide ARCHIVE DESTINATION lib)
EOF
    ps5cemu_cmake "$work/boost-src" "$work/boost"
    rm -rf "$PS5CEMU_SYSROOT/include/boost"
    cp -r "$boost/boost" "$PS5CEMU_SYSROOT/include/boost"
    mark_done boost
fi

if ! done_already pugixml; then
    ps5cemu_cmake "$deps/pugixml-1.15" "$work/pugixml" -DPUGIXML_BUILD_TESTS=OFF
    mark_done pugixml
fi

if ! done_already rapidjson; then
    ps5cemu_cmake "$deps/rapidjson" "$work/rapidjson" -DRAPIDJSON_BUILD_DOC=OFF \
        -DRAPIDJSON_BUILD_EXAMPLES=OFF -DRAPIDJSON_BUILD_TESTS=OFF -DRAPIDJSON_BUILD_THIRDPARTY_GTEST=OFF
    mark_done rapidjson
fi

if ! done_already libzip; then
    # The toolchain's try-compiles build static libraries and never link, so every
    # check_function_exists passes. Answer the ones the console does not have: the Windows and
    # Annex K functions, and those its system modules do not export (arc4random, clonefile...).
    no_probe=()
    for name in _CLOSE _DUP _FDOPEN _FILENO _FSEEKI64 _FSTAT64 _SETMODE _STAT64 _SNPRINTF _SNPRINTF_S \
            _SNWPRINTF_S _STRDUP _STRICMP _STRTOI64 _STRTOUI64 _UNLINK ARC4RANDOM CLONEFILE EXPLICIT_BZERO \
            EXPLICIT_MEMSET GETPROGNAME GETSECURITYINFO LOCALTIME_S MEMCPY_S SETMODE SNPRINTF_S STRERROR_S \
            STRERRORLEN_S STRICMP STRNCPY_S; do
        no_probe+=("-DHAVE_$name=0")
    done
    # As Cemu's vcpkg manifest has it: no default features (zlib only, no encryption).
    ps5cemu_cmake "$deps/libzip-1.11.4" "$work/libzip" -DENABLE_COMMONCRYPTO=OFF -DENABLE_GNUTLS=OFF \
        -DENABLE_MBEDTLS=OFF -DENABLE_OPENSSL=OFF -DENABLE_WINDOWS_CRYPTO=OFF -DENABLE_BZIP2=OFF \
        -DENABLE_LZMA=OFF -DENABLE_ZSTD=OFF -DBUILD_TOOLS=OFF -DBUILD_REGRESS=OFF -DBUILD_OSSFUZZ=OFF \
        -DBUILD_EXAMPLES=OFF -DBUILD_DOC=OFF -DLIBZIP_DO_INSTALL=ON "${no_probe[@]}"
    mark_done libzip
fi

if ! done_already glslang; then
    # GLSL to SPIR-V for Cemu's Vulkan shaders; no optimizer (SPIRV-Tools) or HLSL front end.
    ps5cemu_cmake "$deps/glslang-15.1.0" "$work/glslang" -DENABLE_OPT=OFF -DENABLE_HLSL=OFF \
        -DGLSLANG_TESTS=OFF -DENABLE_GLSLANG_BINARIES=OFF -DGLSLANG_ENABLE_INSTALL=ON \
        -DENABLE_SPVREMAPPER=OFF -DBUILD_EXTERNAL=OFF
    mark_done glslang
fi

if ! done_already glm; then
    # pacbrew ships glm's headers without its CMake package; Cemu asks for glm::glm.
    mkdir -p "$PS5CEMU_SYSROOT/lib/cmake/glm"
    cat >"$PS5CEMU_SYSROOT/lib/cmake/glm/glmConfig.cmake" <<EOF
if(NOT TARGET glm::glm)
    add_library(glm::glm INTERFACE IMPORTED)
    set_target_properties(glm::glm PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "$PS5CEMU_PACBREW/include")
endif()
set(glm_FOUND TRUE)
EOF
    mark_done glm
fi

if ! done_already compiler-rt-builtins; then
    # Clang's builtins, which the host's Clang does not have for the PS5, from the same LLVM
    # release, where PS5_Vulkan's link recipe (tools/radv-link.sh) looks for them:
    # __emutls_get_address for -femulated-tls code (emutls.c; the destructor rounds as ProsperoEden
    # has them, one, for libc++abi's pthread-key fallback) and __cpu_model for
    # __builtin_cpu_supports, which RADV's address library calls (x86.c).
    rt=$deps/compiler-rt-18.1.8
    out=$PS5CEMU_SYSROOT/clang-rt/lib/linux
    mkdir -p "$out" "$work/compiler-rt"
    sed 's/#define EMUTLS_SKIP_DESTRUCTOR_ROUNDS 0/#define EMUTLS_SKIP_DESTRUCTOR_ROUNDS 1/' "$rt/emutls.c" \
        >"$work/compiler-rt/emutls.c"
    for source in "$work/compiler-rt/emutls.c" "$rt/x86.c"; do
        object=$work/compiler-rt/$(basename "$source" .c).o
        clang-18 -target x86_64-sie-ps5 -isysroot "$PS5_PAYLOAD_SDK" -isystem "$PS5_PAYLOAD_SDK/target/include" \
            -I "$rt" -O2 -fPIC -march=znver2 -fno-stack-protector -fno-plt -femulated-tls \
            -c "$source" -o "$object"
    done
    rm -f "$out/libclang_rt.builtins-x86_64.a"
    llvm-ar-18 rcs "$out/libclang_rt.builtins-x86_64.a" "$work/compiler-rt/emutls.o" "$work/compiler-rt/x86.o"
    mark_done compiler-rt-builtins
fi

echo "==> [deps] all libraries are in $PS5CEMU_SYSROOT"
