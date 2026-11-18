// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of which television mode the RG40XX V's image uses over
// HDMI, and whether it uses the television at all (host/src/HostTvMode.h).
// HostDrm feeds it the modes the kernel read from the TV; nothing here needs
// a display.
//
// Run by CI (tools/run_host_tests.py), not by pio test. From the repository root:
//   g++ -std=c++17 -Wall -Wextra -Ihost/src test/host/tv_mode_test.cpp -o /tmp/tv_mode && /tmp/tv_mode

#include <cstdio>
#include <cstdlib>

#include "HostTvMode.h"

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAILED line %d: %s\n", __LINE__, #cond);       \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

using HostTvMode::Mode;

static Mode mode(uint16_t w, uint16_t h, uint32_t hz, bool preferred = false, bool interlaced = false) {
    Mode m;
    m.w = w;
    m.h = h;
    m.refresh = hz;
    m.preferred = preferred;
    m.interlaced = interlaced;
    return m;
}

int main() {
    int checks = 0;

    // A 1080p television: its preferred mode, whatever else it lists first.
    {
        const Mode m[] = {mode(1280, 720, 60), mode(1920, 1080, 60, true), mode(720, 480, 60)};
        CHECK(HostTvMode::pick(m, 3) == 1);
        ++checks;
    }
    // A 4K television prefers 2160p: too big for the buffers, so the
    // largest mode that fits, at 60 Hz rather than 50 or 120.
    {
        const Mode m[] = {mode(3840, 2160, 60, true), mode(1920, 1080, 50), mode(1920, 1080, 120),
                          mode(1920, 1080, 60), mode(1280, 720, 60)};
        CHECK(HostTvMode::pick(m, 5) == 3);
        ++checks;
    }
    // Interlaced 1080i is never chosen, even preferred.
    {
        const Mode m[] = {mode(1920, 1080, 60, true, true), mode(1280, 720, 60)};
        CHECK(HostTvMode::pick(m, 2) == 1);
        ++checks;
    }
    // Above 60 Hz only: the nearest to 60.
    {
        const Mode m[] = {mode(1920, 1080, 144), mode(1920, 1080, 75)};
        CHECK(HostTvMode::pick(m, 2) == 1);
        ++checks;
    }
    // Nothing usable: no television.
    {
        const Mode m[] = {mode(3840, 2160, 60, true), mode(1920, 1080, 60, false, true)};
        CHECK(HostTvMode::pick(m, 2) == -1);
        CHECK(HostTvMode::pick(nullptr, 0) == -1);
        checks += 2;
    }
    // The panel's 640x480 frame: held by 1080p, 720p and VGA, not by 720x400.
    CHECK(HostTvMode::holdsPanel(mode(1920, 1080, 60), 640, 480));
    CHECK(HostTvMode::holdsPanel(mode(1280, 720, 60), 640, 480));
    CHECK(HostTvMode::holdsPanel(mode(640, 480, 60), 640, 480));
    CHECK(!HostTvMode::holdsPanel(mode(720, 400, 70), 640, 480));
    CHECK(HostTvMode::holdsPanel(mode(720, 400, 70), 0, 0));
    checks += 5;

    std::printf("tv_mode: %d checks, 0 failures\n", checks);
    return 0;
}
