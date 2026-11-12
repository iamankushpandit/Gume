# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Braino! as a desktop program: the firmware under src/, compiled unchanged
# against the shims in host/shim/ and the SDL platform in host/src/.
#
#   cmake -S host -B build/host -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/host -j
#   ./build/host/braino
#
# The libraries are fetched at the exact versions platformio.ini pins, so the
# fonts and the flag artwork are the ones the device draws. TFT_eSPI's driver
# code is never compiled -- only its font tables are included, by
# host/src/HostTft.cpp. NimBLE is not fetched: host/shim/NimBLEDevice.h answers
# the few calls the beacon and scanner make, over Linux's HCI socket.

cmake_minimum_required(VERSION 3.20)
project(braino_host CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)   # gnu++17, as on the device
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(Threads REQUIRED)

include(FetchContent)
# SOURCE_SUBDIR points at nothing on purpose: these are Arduino libraries,
# not CMake projects, and only their sources are wanted.
FetchContent_Declare(tft_espi
  GIT_REPOSITORY https://github.com/Bodmer/TFT_eSPI.git
  GIT_TAG V2.5.43
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR none)
FetchContent_Declare(arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR none)
FetchContent_Declare(map_n_flag
  GIT_REPOSITORY https://github.com/iamankushpandit/map-n-flag.git
  GIT_TAG 6f516cbfcb87ccd2cacacae5329ffe185ace00ab
  SOURCE_SUBDIR none)
FetchContent_MakeAvailable(tft_espi arduinojson map_n_flag)

# The firmware, minus what the host replaces: the hardware
# watchdog and the codec/DAC backend (see host/src/Host*.cpp), and every
# standalone bench probe, which has its own setup() and loop().
file(GLOB FIRMWARE_SOURCES CONFIGURE_DEPENDS
  "${REPO_ROOT}/src/engine/*.cpp"
  "${REPO_ROOT}/src/games/*.cpp"
  "${REPO_ROOT}/src/ui/*.cpp"
  "${REPO_ROOT}/src/hal/*.cpp")
list(FILTER FIRMWARE_SOURCES EXCLUDE REGEX
  "/src/hal/(Watchdog|BoardAudioBackend|PowerProbeEsp32|SystemSuspendEsp32)\\.cpp$")
list(APPEND FIRMWARE_SOURCES
  "${REPO_ROOT}/src/main.cpp"
  "${REPO_ROOT}/src/BuildStamp.cpp")

file(GLOB HOST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE MAP_N_FLAG_SOURCES CONFIGURE_DEPENDS
  "${map_n_flag_SOURCE_DIR}/src/*.cpp" "${map_n_flag_SOURCE_DIR}/src/*.c")
list(FILTER MAP_N_FLAG_SOURCES EXCLUDE REGEX "[Ll]vgl")

set(STAMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${STAMP_DIR}")
add_custom_target(build_stamp ALL
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${REPO_ROOT}
          -DOUTPUT=${STAMP_DIR}/GumeBuildStamp.h
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/BuildStamp.cmake
  BYPRODUCTS ${STAMP_DIR}/GumeBuildStamp.h
  COMMENT "Build stamp")

add_custom_command(
  OUTPUT ${STAMP_DIR}/BrainoMarkSvg.h
  COMMAND ${CMAKE_COMMAND} -DSVG=${REPO_ROOT}/tools/braino-badge.svg
          -DOUTPUT=${STAMP_DIR}/BrainoMarkSvg.h
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedMark.cmake
  DEPENDS ${REPO_ROOT}/tools/braino-badge.svg ${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedMark.cmake
  COMMENT "Embedding the Braino mark")
add_custom_target(embed_mark DEPENDS ${STAMP_DIR}/BrainoMarkSvg.h)

add_executable(braino ${FIRMWARE_SOURCES} ${HOST_SOURCES} ${MAP_N_FLAG_SOURCES})
add_dependencies(braino build_stamp embed_mark)

# host/ first, so boards/host.h and the shims win; then the firmware's own
# include roots, as PlatformIO lays them out.
target_include_directories(braino PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/shim"
  "${STAMP_DIR}"
  "${REPO_ROOT}/include"
  "${REPO_ROOT}/src"
  "${REPO_ROOT}/src/hal"
  "${tft_espi_SOURCE_DIR}"
  "${arduinojson_SOURCE_DIR}/src"
  "${map_n_flag_SOURCE_DIR}/src")

target_compile_definitions(braino PRIVATE
  GUME_BOARD_HEADER="boards/host.h"
  BOARD_NAME="Host-SDL"
  LOAD_GLCD=1
  LOAD_FONT2=1
  LOAD_FONT4=1)

# A handheld reads its battery through Linux's power-supply driver; a desktop
# build reports none (see boards/host.h). Cross-compiled builds are handhelds.
option(BRAINO_HOST_BATTERY "Read the battery from /sys/class/power_supply" ${CMAKE_CROSSCOMPILING})
if(BRAINO_HOST_BATTERY)
  target_compile_definitions(braino PRIVATE GUME_HOST_BATTERY=1)
endif()

# The Wi-Fi network and the admin PIN are sealed before they reach nvs.txt
# (host/src/HostSecret.h). A handheld keeps that file on a card any laptop
# reads, so its key must come from the processor and nothing else; a desktop
# build may fall back to the OS's machine id.
option(BRAINO_HOST_STRICT_SECRETS "Seal settings only with a processor-bound key" ${CMAKE_CROSSCOMPILING})
if(BRAINO_HOST_STRICT_SECRETS)
  target_compile_definitions(braino PRIVATE GUME_HOST_STRICT_SECRETS=1)
endif()

# A handheld's backlight is switched off while the firmware's panel sleeps
# (host/src/HostScreenPower.h). A desktop build leaves the computer's own
# screen alone.
option(BRAINO_HOST_SCREEN_POWER "Switch the real backlight off in sleep" ${CMAKE_CROSSCOMPILING})
if(BRAINO_HOST_SCREEN_POWER)
  target_compile_definitions(braino PRIVATE GUME_HOST_SCREEN_POWER=1)
endif()

# A whole-device image with no window system draws straight to the kernel's
# display driver (host/src/HostDrm.h) and uses SDL only for the gamepad and
# sound. Off for desktops, where SDL's window is the screen.
option(BRAINO_HOST_DRM "Draw to the panel through DRM instead of an SDL window" OFF)
if(BRAINO_HOST_DRM)
  target_compile_definitions(braino PRIVATE GUME_HOST_DRM=1)
endif()

target_compile_options(braino PRIVATE -Wall)
target_link_libraries(braino PRIVATE PkgConfig::SDL2 PkgConfig::CURL Threads::Threads m)
