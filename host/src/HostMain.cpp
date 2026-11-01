// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The host build's entry point: bring up the window, then run the firmware's
 * own setup() and loop() from src/main.cpp, exactly as the Arduino core does
 * on a board.
 *
 * The loop paces itself with delay() -- see BrainoApp::loop() -- and delay()
 * is where the window is serviced, so the only work here is servicing it once
 * more after each frame in case a frame finished without sleeping at all. */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HostPlatform.h"
#include "HostScreenPower.h"
#include "HostSplash.h"

void setup();
void loop();

namespace {

/* SHA-256("abc"), FIPS 180-4's own example. The device id is only as
 * irreversible as this hash is correct, so it is checked rather than
 * trusted. */
bool sha256SelfTest() {
    static const unsigned char expected[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae,
        0x22, 0x23, 0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61,
        0xf2, 0x00, 0x15, 0xad};
    unsigned char out[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>("abc"), 3, out, 0);
    return std::memcmp(out, expected, sizeof(out)) == 0;
}

}   // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    if (!sha256SelfTest()) {
        std::fprintf(stderr, "[host] SHA-256 self-test failed\n");
        return 1;
    }
    /* braino --splash <file.bmp> [width height [progress [glow]]]: write the
     * boot splash and exit, without opening a window. A handheld's packaging
     * calls this; progress is 0..1, or negative (the default) for no bar, and
     * glow scales the halo (1.0, the static logo's, by default). */
    if (argc >= 3 && std::strcmp(argv[1], "--splash") == 0) {
        const int w = argc >= 5 ? std::atoi(argv[3]) : 640;
        const int h = argc >= 5 ? std::atoi(argv[4]) : 480;
        const float progress = argc >= 6 ? static_cast<float>(std::atof(argv[5])) : -1.0f;
        const float glow = argc >= 7 ? static_cast<float>(std::atof(argv[6])) : 1.0f;
        if (!HostSplash::writeBmp(argv[2], w, h, progress, glow)) {
            std::fprintf(stderr, "[host] could not write %s\n", argv[2]);
            return 1;
        }
        std::printf("[host] splash %dx%d -> %s\n", w, h, argv[2]);
        return 0;
    }
    if (!HostPlatform::begin(argc, argv)) return 1;
    std::printf("[host] data in %s\n", HostPlatform::dataDir().c_str());
    HostScreenPower::begin();

    HostSplash::runBootPhase();
    setup();
    while (HostPlatform::service()) {
        loop();
    }
    /* Takes the beacon off the air and joins the HCI reader. A reader still
     * running when statics are destroyed aborts the program. */
    NimBLEDevice::deinit();
    HostScreenPower::restore();
    HostPlatform::shutdown();
    return 0;
}
