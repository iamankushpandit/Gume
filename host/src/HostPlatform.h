// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* What the shims share: the window, the input that drives the virtual pins
 * and the emulated touch controller, and where the host keeps its data.
 *
 * Everything here runs on the main thread except where marked. SDL wants its
 * events pumped from the thread that created the window, and the firmware's
 * loop is that thread, so events are pumped between frames -- from the host
 * main loop and from delay(), which is where the runtime spends its spare
 * frame time. */

#include <cstdint>
#include <string>

class TFT_eSPI;

namespace HostPlatform {

/* Where the pointer is pressing, in the rotation-1 landscape frame
 * (0..319, 0..239), or down=false. Read by the emulated FT6336U. */
struct Contact {
    bool down = false;
    int16_t x = 0;
    int16_t y = 0;
};

bool begin(int argc, char** argv);
void shutdown();

/* Pump events and show the frame. Returns false once the user has asked to
 * quit. Cheap to call often: it presents at most once per display frame. */
bool service();
bool quitRequested();

/* The window's size in real pixels -- the panel's on a handheld, whatever
 * HDMI mode the OS chose on a television. */
void outputSize(int& w, int& h);

/* Pump events and show `rgb` (w x h, 3 bytes a pixel) across the whole
 * window at its own resolution, instead of the 320x240 canvas. The boot
 * splash uses it; nothing the firmware draws does. Returns false once the
 * user has asked to quit. */
bool presentImage(const uint8_t* rgb, int w, int h);

void attachPanel(TFT_eSPI* panel);
Contact contact();
bool homeHeld();

/* Backlight level as the firmware's PWM duty, 0.0 to 1.0. */
void setBacklight(float level);

/* Whether the firmware has put the panel to sleep: nothing to show until it
 * wakes. */
bool screenAsleep();

/* Frames handed to the display since start, for the battery diagnostic:
 * every one is a texture upload and a page flip. Any thread. */
uint32_t presentCount();

/* The directory the host keeps its NVS file in, created on first use. */
const std::string& dataDir();

/* Six random bytes, made once and kept in the data directory: what this
 * device uses where a board would use its factory MAC (esp_read_mac(), and
 * so the beacon's device id; the Bluetooth advertising address). Random, so
 * that no hardware identifier is ever stored, shown or put on the air;
 * stable, so that peers who named this console keep their name for it. */
void radioId(uint8_t out[6]);

/* Re-run this program from the start -- what ESP.restart() means here. */
[[noreturn]] void restart();

/* Main thread only: is the caller the thread that owns the window? delay()
 * pumps events only from there. */
bool onMainThread();

}   // namespace HostPlatform
