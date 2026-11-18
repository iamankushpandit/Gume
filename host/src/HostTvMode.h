// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* Which of a television's modes Braino uses, and whether it uses the
 * television at all. Pure arithmetic, so test/host/tv_mode_test.cpp checks it
 * without a display; HostDrm feeds it the modes the kernel read from the
 * television's EDID.
 *
 * The frame is the built-in panel's (640x480 on the RG40XX V), shown at a
 * whole scale, centred. So a mode is worth using if it holds that frame at
 * least once, and nothing above 1920x1080 is: two buffers at 1080p are 16 MB
 * of the kernel's contiguous memory, a 4K mode's would be 66 MB, and 4K only
 * adds a wider black border around the same 2x picture. Interlaced modes are
 * left out: the display engine is not set up for them here, and a TV offers
 * a progressive mode of the same size anyway. */

#include <cstdint>

namespace HostTvMode {

constexpr uint16_t MAX_W = 1920;
constexpr uint16_t MAX_H = 1080;

struct Mode {
    uint16_t w = 0;
    uint16_t h = 0;
    uint32_t refresh = 0;   // Hz
    bool preferred = false;
    bool interlaced = false;
};

inline bool usable(const Mode& m) { return !m.interlaced && m.w <= MAX_W && m.h <= MAX_H && m.w && m.h; }

/* Nearest 60 Hz without going over ranks highest; above 60 ranks below all
 * of those, nearer first. */
inline int refreshRank(const Mode& m) {
    return m.refresh <= 60 ? static_cast<int>(m.refresh) : 60 - static_cast<int>(m.refresh);
}

/* The index of the mode to use: the television's preferred mode if it is
 * usable, otherwise the largest usable one, a tie going to the better
 * refresh. -1 if none is usable. The preferred mode wins over a larger one
 * because it is the panel's own resolution, which a TV shows without
 * rescaling. */
inline int pick(const Mode* modes, int count) {
    for (int i = 0; i < count; ++i) {
        if (modes[i].preferred && usable(modes[i])) return i;
    }
    int best = -1;
    for (int i = 0; i < count; ++i) {
        const Mode& m = modes[i];
        if (!usable(m)) continue;
        if (best < 0) {
            best = i;
            continue;
        }
        const Mode& b = modes[best];
        const uint32_t area = uint32_t(m.w) * m.h, bestArea = uint32_t(b.w) * b.h;
        if (area > bestArea || (area == bestArea && refreshRank(m) > refreshRank(b))) best = i;
    }
    return best;
}

/* Whether a television in `tv` can show a frame of panelW x panelH at all.
 * One that cannot would stay dark, so the picture stays on the panel. A
 * panel not yet known (0) is held by anything. */
inline bool holdsPanel(const Mode& tv, int panelW, int panelH) {
    return tv.w >= panelW && tv.h >= panelH;
}

}   // namespace HostTvMode
