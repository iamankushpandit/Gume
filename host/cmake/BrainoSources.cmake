# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# What every build of src/ off the ESP32 shares: the libraries at the versions
# platformio.ini pins, the firmware's sources minus what a host replaces, the
# generated headers, and the include roots and definitions the firmware is
# compiled with. host/CMakeLists.txt (the desktop and the RG40XX V) and
# host/android/CMakeLists.txt (the Portal) both include this, so a pinned
# version or an excluded file is stated once and cannot differ between them.
#
# Sets FIRMWARE_SOURCES, HOST_SOURCES (every host/src/*.cpp; a build filters
# out what it replaces), MAP_N_FLAG_SOURCES and STAMP_DIR, and defines
# braino_firmware_target(<target> <board name>).

get_filename_component(BRAINO_HOST_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
get_filename_component(REPO_ROOT "${BRAINO_HOST_DIR}/.." ABSOLUTE)

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
  GIT_TAG 3c412cb5cea4dcc5c696abd7a4db78470a97fbd1
  SOURCE_SUBDIR none)
FetchContent_MakeAvailable(tft_espi arduinojson map_n_flag)

# The firmware, minus what the host replaces: the hardware
# watchdog, the codec/DAC backend and each ESP32 implementation of a platform
# interface -- the power probe, suspend and the panel's size (see
# host/src/Host*.cpp) -- and every
# standalone bench probe, which has its own setup() and loop().
file(GLOB FIRMWARE_SOURCES CONFIGURE_DEPENDS
  "${REPO_ROOT}/src/engine/*.cpp"
  "${REPO_ROOT}/src/games/*.cpp"
  "${REPO_ROOT}/src/ui/*.cpp"
  "${REPO_ROOT}/src/hal/*.cpp")
list(FILTER FIRMWARE_SOURCES EXCLUDE REGEX
  "/src/hal/(Watchdog|BoardAudioBackend|PowerProbeEsp32|SystemSuspendEsp32|PanelSizeEsp32|SleepPowerEsp32|DevicePoseEsp32)\.cpp$")
list(FILTER FIRMWARE_SOURCES EXCLUDE REGEX "/src/ui/PanelArtEsp32\\.cpp$")
list(APPEND FIRMWARE_SOURCES
  "${REPO_ROOT}/src/main.cpp"
  "${REPO_ROOT}/src/BuildStamp.cpp")

file(GLOB HOST_SOURCES CONFIGURE_DEPENDS "${BRAINO_HOST_DIR}/src/*.cpp")
file(GLOB_RECURSE MAP_N_FLAG_SOURCES CONFIGURE_DEPENDS
  "${map_n_flag_SOURCE_DIR}/src/*.cpp" "${map_n_flag_SOURCE_DIR}/src/*.c")
list(FILTER MAP_N_FLAG_SOURCES EXCLUDE REGEX "[Ll]vgl")

set(STAMP_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${STAMP_DIR}")
add_custom_target(build_stamp ALL
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${REPO_ROOT}
          -DOUTPUT=${STAMP_DIR}/GumeBuildStamp.h
          -P ${BRAINO_HOST_DIR}/cmake/BuildStamp.cmake
  BYPRODUCTS ${STAMP_DIR}/GumeBuildStamp.h
  COMMENT "Build stamp")

add_custom_command(
  OUTPUT ${STAMP_DIR}/BrainoMarkSvg.h
  COMMAND ${CMAKE_COMMAND} -DSVG=${REPO_ROOT}/tools/braino-badge.svg
          -DOUTPUT=${STAMP_DIR}/BrainoMarkSvg.h
          -P ${BRAINO_HOST_DIR}/cmake/EmbedMark.cmake
  DEPENDS ${REPO_ROOT}/tools/braino-badge.svg ${BRAINO_HOST_DIR}/cmake/EmbedMark.cmake
  COMMENT "Embedding the Braino mark")
add_custom_target(embed_mark DEPENDS ${STAMP_DIR}/BrainoMarkSvg.h)

# The typeface the host draws text in (host/fonts/README.md), compiled in so
# no device needs a font directory. Rasterised by host/third_party/stb.
add_custom_command(
  OUTPUT ${STAMP_DIR}/BrainoFontTtf.h
  COMMAND ${CMAKE_COMMAND} -DFONT=${BRAINO_HOST_DIR}/fonts/FreeSans-Latin.ttf
          -DOUTPUT=${STAMP_DIR}/BrainoFontTtf.h
          -P ${BRAINO_HOST_DIR}/cmake/EmbedFont.cmake
  DEPENDS ${BRAINO_HOST_DIR}/fonts/FreeSans-Latin.ttf
          ${BRAINO_HOST_DIR}/cmake/EmbedFont.cmake
  COMMENT "Embedding the font")
add_custom_target(embed_font DEPENDS ${STAMP_DIR}/BrainoFontTtf.h)

# The firmware's include roots and definitions on a target. `extra_include` is
# searched first, so a build can put its own stand-ins ahead of host/'s.
function(braino_firmware_target target board_name)
  add_dependencies(${target} build_stamp embed_mark embed_font)
  # host/ first, so boards/host.h and the shims win; then the firmware's own
  # include roots, as PlatformIO lays them out.
  target_include_directories(${target} PRIVATE
    ${ARGN}
    "${BRAINO_HOST_DIR}"
    "${BRAINO_HOST_DIR}/shim"
    "${STAMP_DIR}"
    "${REPO_ROOT}/include"
    "${REPO_ROOT}/src"
    "${REPO_ROOT}/src/hal"
    "${tft_espi_SOURCE_DIR}"
    "${arduinojson_SOURCE_DIR}/src"
    "${map_n_flag_SOURCE_DIR}/src")
  target_compile_definitions(${target} PRIVATE
    GUME_BOARD_HEADER="boards/host.h"
    BOARD_NAME="${board_name}"
    LOAD_GLCD=1
    LOAD_FONT2=1
    LOAD_FONT4=1)
endfunction()
