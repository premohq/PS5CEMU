# SPDX-License-Identifier: GPL-3.0-or-later
# PS5CEMU-HAR: Azahar's PS5 frontend (core.cpp, input.cpp, sink.cpp), built in Azahar's tree
# (tools/build-azahar.sh names this directory AZAHAR_EXTERNAL_FRONTEND) against its libraries.
# The rest of port/azahar (the launcher's 3DS library and controls, azahar.h) is built with the app
# (port/CMakeLists.txt), which links what this builds: azahar_ps5_libraries.txt lists the archives.

add_library(azahar_ps5 STATIC
    core.cpp
    input.cpp
    input.h
    libc.cpp
    sink.cpp
)
target_include_directories(azahar_ps5 PRIVATE "${CMAKE_CURRENT_LIST_DIR}/..")
target_link_libraries(azahar_ps5 PUBLIC citra_core video_core audio_core input_common citra_common network)
# the window: a VK_KHR_display surface (ps5/vulkan_display.h)
target_link_libraries(azahar_ps5 PRIVATE vulkan-headers)

# The archives the app links for Azahar, in link order: an executable whose link only writes them
# down (record-libraries.sh), into azahar_ps5_libraries.txt in the build directory.
add_executable(azahar_ps5_libraries libraries.cpp)
target_link_libraries(azahar_ps5_libraries PRIVATE azahar_ps5)
set(CMAKE_CXX_LINK_EXECUTABLE
    "bash \"${CMAKE_CURRENT_LIST_DIR}/record-libraries.sh\" \"${CMAKE_BINARY_DIR}/azahar_ps5_libraries.txt\" <TARGET> <LINK_LIBRARIES>")
