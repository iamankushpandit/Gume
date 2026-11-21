// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The host's answer to DevicePose (src/hal/DevicePose.h): a PC and the
 * RG40XX V have no motion sensor, so there is none -- unless BRAINO_POSE
 * names one, which lets the desktop build show what a device with a sensor
 * does: Settings offers Auto, and with it chosen the layout follows the pose.
 *
 *   BRAINO_POSE=landscape | landscape-180 | portrait | portrait-180
 *
 * The Portal's build replaces this file with one that reads its
 * accelerometer (host/android/src/HostDevicePoseAndroid.cpp). */

#include "hal/DevicePose.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

DevicePose::Pose fromEnvironment() {
    const char* env = std::getenv("BRAINO_POSE");
    if (env == nullptr || *env == 0) return DevicePose::Pose::Unknown;
    if (std::strcmp(env, "landscape") == 0) return DevicePose::Pose::Landscape;
    if (std::strcmp(env, "landscape-180") == 0) return DevicePose::Pose::LandscapeFlipped;
    if (std::strcmp(env, "portrait") == 0) return DevicePose::Pose::Portrait;
    if (std::strcmp(env, "portrait-180") == 0) return DevicePose::Pose::PortraitFlipped;
    std::fprintf(stderr, "[host] BRAINO_POSE=%s is not a pose; ignored\n", env);
    return DevicePose::Pose::Unknown;
}

DevicePose::Pose pose() {
    static const DevicePose::Pose p = fromEnvironment();
    return p;
}

}   // namespace

bool DevicePose::available() { return pose() != Pose::Unknown; }
DevicePose::Pose DevicePose::current() { return pose(); }
