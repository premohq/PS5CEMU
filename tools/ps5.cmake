# SPDX-License-Identifier: GPL-3.0-or-later
# CMake toolchain for PS5 native titles: clang-18 for x86_64-sie-ps5 against the public
# payload SDK (PS5_PAYLOAD_SDK), with pacbrew's prebuilt libraries and the libraries
# tools/build-deps.sh installs (PS5CEMU_SYSROOT) on the search path.
#
# The console's userland is FreeBSD-derived, so CMake is told FreeBSD; projects that need
# to know it is the PS5 test __PROSPERO__ (the compiler defines it) or CEMU_PS5.

set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_VERSION 9)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER clang-18)
set(CMAKE_CXX_COMPILER clang++-18)
set(CMAKE_ASM_COMPILER clang-18)
set(CMAKE_C_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_CXX_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_ASM_COMPILER_TARGET x86_64-sie-ps5)
set(CMAKE_AR llvm-ar-18 CACHE FILEPATH "")
set(CMAKE_RANLIB llvm-ranlib-18 CACHE FILEPATH "")
set(CMAKE_NM llvm-nm-18 CACHE FILEPATH "")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT DEFINED ENV{PS5_PAYLOAD_SDK})
	message(FATAL_ERROR "PS5_PAYLOAD_SDK is not set (tools/env.sh sets it; make deps installs the SDK)")
endif()
set(PS5_SDK "$ENV{PS5_PAYLOAD_SDK}")
set(PS5_PACBREW "$ENV{PS5CEMU_PACBREW}")
set(PS5_SYSROOT "$ENV{PS5CEMU_SYSROOT}")

# Zen 2 is the PS5's CPU; the SDK's own flags for a title: no stack protector (no
# __stack_chk_guard is exported to titles), no PLT, emulated TLS.
set(PS5_COMMON_FLAGS "-isysroot ${PS5_SDK} -isystem ${PS5_SDK}/target/include -march=znver2 -fno-stack-protector -fno-plt -femulated-tls -fvisibility-nodllstorageclass=default -ffunction-sections -fdata-sections")
if(PS5_PACBREW)
	string(APPEND PS5_COMMON_FLAGS " -isystem ${PS5_PACBREW}/include")
endif()
set(CMAKE_C_FLAGS_INIT "${PS5_COMMON_FLAGS}")
# The PS5 target defaults to -fno-rtti; Cemu, Boost and glslang need RTTI and exceptions.
# libc++ 18 keeps std::jthread and std::stop_token behind _LIBCPP_ENABLE_EXPERIMENTAL.
set(CMAKE_CXX_FLAGS_INIT "-isystem ${PS5_SDK}/target/include/c++/v1 ${PS5_COMMON_FLAGS} -frtti -fexceptions -fcxx-exceptions -D_LIBCPP_ENABLE_EXPERIMENTAL=1")
set(CMAKE_ASM_FLAGS_INIT "${PS5_COMMON_FLAGS}")

# Everything is static, and an executable's link is tools/link.sh's (PS5_Vulkan's recipe for a
# title that links RADV), not the compiler driver's.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
set(CMAKE_CXX_LINK_EXECUTABLE "bash \"${CMAKE_CURRENT_LIST_DIR}/link.sh\" <TARGET> <LINK_FLAGS> <OBJECTS> <LINK_LIBRARIES>")
set(CMAKE_C_LINK_EXECUTABLE "${CMAKE_CXX_LINK_EXECUTABLE}")
set(CMAKE_FIND_ROOT_PATH ${PS5_SYSROOT} ${PS5_PACBREW} ${PS5_SDK}/target)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_PREFIX_PATH ${PS5_SYSROOT} ${PS5_PACBREW})
set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
set(PKG_CONFIG_EXECUTABLE pkg-config CACHE FILEPATH "")
set(ENV{PKG_CONFIG_LIBDIR} "${PS5_SYSROOT}/lib/pkgconfig:${PS5_PACBREW}/lib/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "")
