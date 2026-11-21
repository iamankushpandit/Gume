# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Braino! on Android -- the Meta Portal+ first: src/ compiled unchanged, as
# every host build compiles it, into libbraino.so for the system's own
# android.app.NativeActivity. No Java is written for it; host/android/build.ps1
# packs this library and AndroidManifest.xml into an APK.
#
#   cmake -S host/android -B build/android -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28
#   cmake --build build/android
#
# What this build replaces from host/src/, and with what:
#   HostPlatform.cpp (SDL window, mouse, gamepad) -> src/HostPlatformAndroid.cpp
#   HostI2s.cpp      (SDL audio queue)            -> src/HostI2sAAudio.cpp
#   HostMain.cpp's main()                         -> src/AndroidMain.cpp's
#                                                    android_main(), which
#                                                    calls the same hostMain()
#   HostDrm.cpp      (kernel display driver)      -> not built: the window is
#                                                    the panel here
# Everything else in host/src/ is the desktop's own file.

cmake_minimum_required(VERSION 3.20)
project(braino_android CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)   # gnu++17, as on the device
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT ANDROID)
  message(FATAL_ERROR "host/android builds with the NDK's toolchain file; see the top of this file")
endif()

include(../cmake/BrainoSources.cmake)

list(FILTER HOST_SOURCES EXCLUDE REGEX "/host/src/(HostPlatform|HostI2s|HostDrm)\\.cpp$")

# libcurl for HostNet.cpp's HTTPClient, built here because Android ships none.
# HTTP only: the time zone lookup (ip-api.com) is plain HTTP, and the update
# check, which is HTTPS, fails cleanly rather than leaving the device by a
# route the desktop build does not take. Giving it TLS is a decision about
# which library to trust with it, not something to slip in here.
include(FetchContent)
set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
set(HTTP_ONLY ON CACHE BOOL "" FORCE)
set(CURL_ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
set(CURL_ZLIB OFF CACHE STRING "" FORCE)
set(CURL_BROTLI OFF CACHE STRING "" FORCE)
set(CURL_ZSTD OFF CACHE STRING "" FORCE)
set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
set(ENABLE_ARES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(curl
  GIT_REPOSITORY https://github.com/curl/curl.git
  GIT_TAG curl-8_11_1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(curl)

# The system's NativeActivity calls into this; the glue turns its callbacks
# into the looper android_main() pumps (src/HostAndroid.h).
add_library(native_app_glue STATIC
  "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
target_include_directories(native_app_glue PUBLIC
  "${ANDROID_NDK}/sources/android/native_app_glue")

file(GLOB ANDROID_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_library(braino SHARED
  ${FIRMWARE_SOURCES} ${HOST_SOURCES} ${MAP_N_FLAG_SOURCES} ${ANDROID_SOURCES})
# This build's own files, then host/src/ for the interfaces they implement
# (HostPlatform.h, HostI2s.h, HostMain.h), then the shared roots.
braino_firmware_target(braino "Portal+"
  "${CMAKE_CURRENT_SOURCE_DIR}/src" "${BRAINO_HOST_DIR}/src")

# The device part of the version (include/FirmwareVersion.h). The sealing
# key for the Wi-Fi network and the admin PIN comes from the app's private
# storage, since an app can read no processor serial (host/src/HostSecret.cpp).
target_compile_definitions(braino PRIVATE
  GUME_VERSION_TAG="android"
  GUME_HOST_SANDBOXED_KEY=1)
target_compile_options(braino PRIVATE -Wall)
# NativeActivity finds the entry point by name; keep it through the link.
target_link_options(braino PRIVATE "-u" "ANativeActivity_onCreate")
target_link_libraries(braino PRIVATE native_app_glue CURL::libcurl android log aaudio m)
