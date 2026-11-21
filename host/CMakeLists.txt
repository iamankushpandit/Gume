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

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(Threads REQUIRED)

# The pinned libraries, the firmware's sources, the generated headers and the
# include roots -- shared with host/android/ (cmake/BrainoSources.cmake).
include(cmake/BrainoSources.cmake)

add_executable(braino ${FIRMWARE_SOURCES} ${HOST_SOURCES} ${MAP_N_FLAG_SOURCES})
braino_firmware_target(braino "Host-SDL")

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

# The device part of the version (include/FirmwareVersion.h): "host" on a
# desktop, the chip family on a handheld image (braino.mk passes h700).
set(BRAINO_VERSION_TAG "host" CACHE STRING "Device part of the version, after the +")
if(NOT BRAINO_VERSION_TAG MATCHES "^[0-9a-z-]+$")
  message(FATAL_ERROR "BRAINO_VERSION_TAG must be lower case [0-9a-z-]: ${BRAINO_VERSION_TAG}")
endif()
target_compile_definitions(braino PRIVATE GUME_VERSION_TAG="${BRAINO_VERSION_TAG}")

target_compile_options(braino PRIVATE -Wall)
target_link_libraries(braino PRIVATE PkgConfig::SDL2 PkgConfig::CURL Threads::Threads m)

# The synthesiser on its own: every cue written to a WAV file, through the
# firmware's own src/hal/AudioSynth.cpp, BoardAudio.cpp and BoardAudioCues.cpp
# and nothing else from src/. braino-sounds generates as the codec path does (16-bit,
# 16 kHz: this program, the Freenove and a bare I2S amplifier);
# braino-sounds-dac as the E32R28T-1's built-in DAC does (24 kHz, of which
# the speaker hears the high byte). host/CLAUDE.md, "Hearing the sounds".
set(SOUND_SOURCES
  "${REPO_ROOT}/src/hal/AudioSynth.cpp"
  "${REPO_ROOT}/src/hal/BoardAudio.cpp"
  "${REPO_ROOT}/src/hal/BoardAudioCues.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundRender.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/sound/SoundStubs.cpp")
foreach(flavour IN ITEMS codec dac)
  if(flavour STREQUAL "codec")
    set(sound_target braino-sounds)
    set(sound_board "boards/host.h")
    set(sound_name "Host-SDL")
  else()
    set(sound_target braino-sounds-dac)
    set(sound_board "boards/e32r28t1.h")
    set(sound_name "E32R28T-1")
  endif()
  add_executable(${sound_target} ${SOUND_SOURCES})
  target_include_directories(${sound_target} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/shim"
    "${REPO_ROOT}/include"
    "${REPO_ROOT}/src"
    "${REPO_ROOT}/src/hal")
  target_compile_definitions(${sound_target} PRIVATE
    GUME_BOARD_HEADER="${sound_board}"
    BOARD_NAME="${sound_name}")
  target_compile_options(${sound_target} PRIVATE -Wall)
  target_link_libraries(${sound_target} PRIVATE m)
endforeach()
