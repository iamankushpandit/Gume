// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* DevicePose on Android (src/hal/DevicePose.h): which way up the device is
 * held, from its accelerometer. It replaces host/src/HostDevicePose.cpp for
 * this build. On the Portal+ the sensor is a BMA250E, the one Android's own
 * rotation follows.
 *
 * The activity stays in landscape (AndroidManifest.xml), so the window never
 * turns: like a board's panel it is fixed, and the firmware turns what it
 * draws, as it does for the Portrait and 180 settings. This only says which
 * of those the device is in.
 *
 * Gravity, filtered, says which edge is down. The axes are the device's own,
 * in its natural orientation -- landscape, on the Portal+: x to the right,
 * y up the screen. Held to be read in landscape, gravity pulls along -y, so
 * the sensor reads y > 0; turned a quarter, the pull moves to x. A pose is
 * reported only once one edge has been clearly lowest for SETTLE_MS, so a
 * device being carried does not turn at every step; flat on a table no edge
 * is lowest and the last pose stands.
 *
 * The queue is attached to android_main()'s looper, so events arrive in
 * HostAndroid::pump() on the firmware's own thread -- no thread, no lock. */

#include "HostAndroid.h"
#include "hal/DevicePose.h"

#include <android/looper.h>
#include <android/sensor.h>
#include <android_native_app_glue.h>

#include <chrono>
#include <cmath>
#include <cstdio>

namespace {

constexpr float G = 9.80665f;
/* An edge counts as lowest when gravity along it is at least this share of
 * g and clearly more than along the other axis. Below it -- lying flat, or
 * held at a slant -- nothing changes. */
constexpr float LEAN = 0.55f * G;
constexpr float MARGIN = 0.25f * G;
constexpr uint32_t SETTLE_MS = 500;
/* Low-pass weight per sample, at the rate asked for below (~10 Hz). */
constexpr float SMOOTH = 0.3f;

ASensorEventQueue* queue = nullptr;
bool haveSensor = false;
float gx = 0.0f, gy = 0.0f;
bool primed = false;

DevicePose::Pose settled = DevicePose::Pose::Unknown;
DevicePose::Pose candidate = DevicePose::Pose::Unknown;
uint32_t candidateSinceMs = 0;

uint32_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

const char* name(DevicePose::Pose p) {
    switch (p) {
        case DevicePose::Pose::Landscape: return "landscape";
        case DevicePose::Pose::LandscapeFlipped: return "landscape-180";
        case DevicePose::Pose::Portrait: return "portrait";
        case DevicePose::Pose::PortraitFlipped: return "portrait-180";
        default: return "unknown";
    }
}

/* Which edge is lowest now, or Unknown when none clearly is. Which quarter
 * turn is Portrait rather than Portrait 180 is the firmware's: Portrait is
 * the rotation BOARD.panel.portraitRotation draws, and on the Portal+ it
 * reads upright with the device's right edge down -- the sensor reading
 * x < 0. Measured on the device: the first build had the sign the other way
 * and stood the launcher up on its head. */
DevicePose::Pose lowestEdge() {
    const float ax = std::fabs(gx), ay = std::fabs(gy);
    if (ay >= LEAN && ay > ax + MARGIN) {
        return gy > 0 ? DevicePose::Pose::Landscape : DevicePose::Pose::LandscapeFlipped;
    }
    if (ax >= LEAN && ax > ay + MARGIN) {
        return gx < 0 ? DevicePose::Pose::Portrait : DevicePose::Pose::PortraitFlipped;
    }
    return DevicePose::Pose::Unknown;
}

}   // namespace

namespace HostAndroidPose {

void begin() {
    ASensorManager* manager =
        ASensorManager_getInstanceForPackage("io.github.iamankushpandit.braino");
    const ASensor* accel =
        manager ? ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_ACCELEROMETER) : nullptr;
    if (!accel) {
        std::printf("[pose] no accelerometer: the orientation is the Settings choice\n");
        return;
    }
    queue = ASensorManager_createEventQueue(manager, ALooper_forThread(), LOOPER_ID_USER,
                                            nullptr, nullptr);
    if (!queue || ASensorEventQueue_enableSensor(queue, accel) < 0) {
        std::printf("[pose] the accelerometer would not start\n");
        return;
    }
    ASensorEventQueue_setEventRate(queue, accel, 100000);   // 10 Hz is plenty to stand a screen up
    haveSensor = true;
    std::printf("[pose] following %s\n", ASensor_getName(accel));
}

void drain() {
    if (!queue) return;
    ASensorEvent events[16];
    ssize_t n;
    while ((n = ASensorEventQueue_getEvents(queue, events, 16)) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (events[i].type != ASENSOR_TYPE_ACCELEROMETER) continue;
            const float x = events[i].acceleration.x, y = events[i].acceleration.y;
            if (!primed) {
                gx = x;
                gy = y;
                primed = true;
            } else {
                gx += SMOOTH * (x - gx);
                gy += SMOOTH * (y - gy);
            }
        }
    }
    const DevicePose::Pose now = lowestEdge();
    if (now == DevicePose::Pose::Unknown) return;   // flat or slanted: the last pose stands
    const uint32_t t = nowMs();
    if (now != candidate) {
        candidate = now;
        candidateSinceMs = t;
    }
    if (candidate != settled && t - candidateSinceMs >= SETTLE_MS) {
        settled = candidate;
        std::printf("[pose] %s (gravity x=%.1f y=%.1f)\n", name(settled), gx, gy);
    }
}

}   // namespace HostAndroidPose

bool DevicePose::available() { return haveSensor; }
DevicePose::Pose DevicePose::current() { return settled; }
