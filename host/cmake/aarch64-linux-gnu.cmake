# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

# Cross-compiling the host build for an aarch64 Linux handheld (the Anbernic
# RG40XXV under muOS is the first).
#
#   cmake -S host -B build/host-arm64 \
#         -DCMAKE_TOOLCHAIN_FILE=host/cmake/aarch64-linux-gnu.cmake \
#         -DBRAINO_SYSROOT=/opt/sysroot-bullseye-arm64
#
# The sysroot is an older Debian on purpose -- bullseye, glibc 2.31 -- because
# a program built against an older C library runs on a newer one and not the
# other way round, and a handheld's image is whatever its maintainers last
# built. It needs libc6-dev, libstdc++-10-dev, libsdl2-dev and
# libcurl4-openssl-dev for arm64 unpacked into it.
#
# The compiler is clang, not a distribution's aarch64-linux-gnu-gcc. That gcc
# carries its own, newer, glibc and searches it before the sysroot, so a
# build links half against one C library and half against the other -- and
# with glibc 2.38's headers, strtol() quietly becomes __isoc23_strtol, which
# no older device has. clang with a target and a sysroot uses the sysroot's
# headers, libraries and GCC 10 runtime and nothing else. libstdc++ and
# libgcc are linked statically, so the C++ runtime is never something the
# device has to provide.

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET aarch64-linux-gnu)
set(CMAKE_CXX_COMPILER_TARGET aarch64-linux-gnu)

if(NOT BRAINO_SYSROOT)
  set(BRAINO_SYSROOT /opt/sysroot-bullseye-arm64)
endif()
set(CMAKE_SYSROOT "${BRAINO_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH "${BRAINO_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config must read the target's .pc files and prefix their paths with the
# sysroot, or it hands the cross compiler the build machine's own libraries.
set(ENV{PKG_CONFIG_DIR} "")
set(ENV{PKG_CONFIG_LIBDIR} "${BRAINO_SYSROOT}/usr/lib/aarch64-linux-gnu/pkgconfig:${BRAINO_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_SYSROOT_DIR} "${BRAINO_SYSROOT}")

set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld -static-libstdc++ -static-libgcc")
