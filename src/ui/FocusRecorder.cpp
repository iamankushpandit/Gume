// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ui/FocusRecorder.h"

/* See FocusRecorder.h. The rules for what is a control are the Linux host's
 * (host/src/HostTftFocus.cpp), measured there across every screen; they are
 * restated here rather than shared because the host's live in its TFT_eSPI
 * shim and these in the firmware's renderer -- two places that see the same
 * drawing -- and a change to one is a change to make to the other. */

#if GUME_HAS_NAV_KEYS

#include <string.h>

namespace Focus {
namespace {

/* Ui::BUTTON_SHADOW_DX and DY, the offset of the fill drawn before a button.
 * Not read from Ui.h, as on the host: a little slack costs nothing. */
constexpr int32_t SHADOW_REACH = 4;
constexpr int32_t SMALLEST_CONTROL = 12;
constexpr int MIN_CARD_CONTROLS = 4;
constexpr int MAX_TABS = 8;

struct Mark {
    bool valid = false;
    bool button = false;          // drawn over a shadow
    int16_t x = 0, y = 0, w = 0, h = 0;   // as drawn, absolute
    int16_t vx0 = 0, vy0 = 0, vx1 = 0, vy1 = 0;   // the part inside the clip, inclusive
    int16_t radius = 0;
    uint16_t fill = 0;
    uint16_t outline = 0;
};

struct Fill {
    int32_t x = 0, y = 0, w = 0, h = 0, radius = 0;
    uint16_t color = 0;
    bool shadowed = false;
    bool valid = false;
};

struct TabMark {
    bool valid = false;
    bool active = false;
    Rect r;
    uint16_t fill = 0;
};

struct PagerMark {
    bool valid = false;
    Rect r;
};

struct ScrollMark {
    bool valid = false;
    ScrollArea area;
};

/* Fixed and static, as every other table in this firmware: nothing here
 * allocates. About 1.6 KB, in the Attaky image only. */
Mark s_marks[MAX_TARGETS];
TabMark s_tabs[MAX_TABS];
PagerMark s_pager[2];
ScrollMark s_scroll;
Fill s_lastFill;
bool s_recording = true;

int32_t s_vpX = 0, s_vpY = 0, s_vpW = 0x7FFF, s_vpH = 0x7FFF;
bool s_vpDatum = false;

void toAbsolute(int32_t& x, int32_t& y) {
    if (s_vpDatum) {
        x += s_vpX;
        y += s_vpY;
    }
}

bool centreUnder(const Rect& r, int32_t x, int32_t y, int32_t w, int32_t h) {
    const int32_t cx = r.x + r.w / 2;
    const int32_t cy = r.y + r.h / 2;
    return cx >= x && cx < x + w && cy >= y && cy < y + h;
}

bool inside(int32_t x, int32_t y, int32_t w, int32_t h, const Rect& r) {
    return x >= r.x && y >= r.y && x + w <= r.x + r.w && y + h <= r.y + r.h;
}

/* A fill over a control's middle has taken it away, unless the fill sits
 * inside the control: that is the control repainting its own contents, which
 * a launcher tile does on every page turn and a label's background does all
 * the time. */
void forgetUnder(int32_t x, int32_t y, int32_t w, int32_t h) {
    for (Mark& m : s_marks) {
        if (!m.valid) continue;
        const Rect r{m.x, m.y, m.w, m.h};
        if (centreUnder(r, x, y, w, h) && !inside(x, y, w, h, r)) m.valid = false;
    }
    for (TabMark& t : s_tabs) {
        /* A tab's left edge near its foot: its own label never reaches it,
         * and a strip being repainted covers it. */
        if (t.valid && t.r.x >= x && t.r.x < x + w && t.r.y + t.r.h - 2 >= y &&
            t.r.y + t.r.h - 2 < y + h) {
            t.valid = false;
        }
    }
    for (PagerMark& p : s_pager) {
        if (p.valid && centreUnder(p.r, x, y, w, h) && !inside(x, y, w, h, p.r)) p.valid = false;
    }
    if (s_scroll.valid && centreUnder(s_scroll.area.list, x, y, w, h) &&
        !inside(x, y, w, h, s_scroll.area.list)) {
        s_scroll.valid = false;
    }
}

}   // namespace

void setRecording(bool on) { s_recording = on; }

void noteScreenClear() {
    if (!s_recording) return;
    for (Mark& m : s_marks) m.valid = false;
    for (TabMark& t : s_tabs) t.valid = false;
    for (PagerMark& p : s_pager) p.valid = false;
    s_scroll.valid = false;
    s_lastFill = Fill{};
}

void noteViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool datum) {
    s_vpX = x;
    s_vpY = y;
    s_vpW = w;
    s_vpH = h;
    s_vpDatum = datum;
}

void noteViewportReset() {
    s_vpX = 0;
    s_vpY = 0;
    s_vpW = 0x7FFF;
    s_vpH = 0x7FFF;
    s_vpDatum = false;
}

void noteFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint16_t color) {
    if (!s_recording || w <= 0 || h <= 0) return;
    toAbsolute(x, y);
    forgetUnder(x, y, w, h);
    /* A button's shadow is the same size, a few pixels down and to the right,
     * and drawn just before it. */
    const int32_t dx = s_lastFill.x - x;
    const int32_t dy = s_lastFill.y - y;
    const bool shadowed = s_lastFill.valid && s_lastFill.w == w && s_lastFill.h == h &&
                          dx > 0 && dx <= SHADOW_REACH && dy >= 0 && dy <= SHADOW_REACH;
    s_lastFill = Fill{x, y, w, h, radius, color, shadowed, true};
}

void noteOutline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t radius, uint16_t color) {
    if (!s_recording) return;
    toAbsolute(x, y);
    const Fill& f = s_lastFill;
    if (!f.valid || f.x != x || f.y != y || f.w != w || f.h != h) return;
    if (w < SMALLEST_CONTROL || h < SMALLEST_CONTROL) return;

    /* The part inside the clip: a row half scrolled out of its list can still
     * be chosen, and is pressed where it can be seen. */
    const int32_t vx0 = x > s_vpX ? x : s_vpX;
    const int32_t vy0 = y > s_vpY ? y : s_vpY;
    const int32_t vx1 = (x + w - 1) < (s_vpX + s_vpW - 1) ? (x + w - 1) : (s_vpX + s_vpW - 1);
    const int32_t vy1 = (y + h - 1) < (s_vpY + s_vpH - 1) ? (y + h - 1) : (s_vpY + s_vpH - 1);
    if (vx0 > vx1 || vy0 > vy1) return;

    Mark* slot = nullptr;
    for (Mark& m : s_marks) {
        if (m.valid && m.x == x && m.y == y && m.w == w && m.h == h) {
            slot = &m;   // the same control, redrawn
            break;
        }
        if (!slot && !m.valid) slot = &m;
    }
    if (!slot) return;
    Mark m;
    m.valid = true;
    m.button = f.shadowed;
    m.x = static_cast<int16_t>(x);
    m.y = static_cast<int16_t>(y);
    m.w = static_cast<int16_t>(w);
    m.h = static_cast<int16_t>(h);
    m.vx0 = static_cast<int16_t>(vx0);
    m.vy0 = static_cast<int16_t>(vy0);
    m.vx1 = static_cast<int16_t>(vx1);
    m.vy1 = static_cast<int16_t>(vy1);
    m.radius = static_cast<int16_t>(radius);
    m.fill = f.color;
    m.outline = color;
    *slot = m;
}

void noteTab(const Rect& r, bool active, uint16_t fill) {
    if (!s_recording) return;
    TabMark* slot = nullptr;
    for (TabMark& t : s_tabs) {
        if (t.valid && t.r.x == r.x && t.r.y + t.r.h == r.y + r.h) {
            slot = &t;   // the same tab, redrawn -- selected or not
            break;
        }
        if (!slot && !t.valid) slot = &t;
    }
    if (!slot) return;
    *slot = TabMark{true, active, r, fill};
}

void notePager(const Rect& r, bool next, bool enabled) {
    if (!s_recording) return;
    s_pager[next ? 1 : 0] = PagerMark{enabled, r};
}

void noteScroll(const Rect& list, bool moreAbove, bool moreBelow) {
    if (!s_recording) return;
    s_scroll.valid = true;
    s_scroll.area = ScrollArea{list, moreAbove, moreBelow};
}

int targets(Target* out, int max) {
    int count = 0;
    int buttons = 0, cards = 0;
    for (const Mark& m : s_marks) {
        if (m.valid) ++(m.button ? buttons : cards);
    }
    /* A card is a fill and an outline with no button's shadow. It might be a
     * control -- Memory's cards, Cinnamon's pads, Ludo's seats -- or something
     * to look at, like a quiz's question box or Color Mix's two colours.
     * Beside buttons, one to three of them have always been the second kind;
     * from the fourth they are a grid of controls. Measured on the host across
     * every screen. */
    const bool cardsAreControls = buttons == 0 || cards >= MIN_CARD_CONTROLS;
    const int32_t quarter = static_cast<int32_t>(SCREEN_WIDTH) * SCREEN_HEIGHT / 4;
    for (const Mark& m : s_marks) {
        if (!m.valid || count >= max) continue;
        if (!m.button) {
            if (!cardsAreControls) continue;
            /* Too big, or with a control inside it, it is the panel under them. */
            if (static_cast<int32_t>(m.w) * m.h > quarter) continue;
            bool holdsAnother = false;
            for (const Mark& o : s_marks) {
                if (!o.valid || &o == &m) continue;
                const int32_t cx = o.x + o.w / 2;
                const int32_t cy = o.y + o.h / 2;
                if (cx >= m.x && cx < m.x + m.w && cy >= m.y && cy < m.y + m.h) holdsAnother = true;
            }
            if (holdsAnother) continue;
        }
        Target t;
        t.box = Rect{m.vx0, m.vy0, static_cast<int16_t>(m.vx1 - m.vx0 + 1),
                     static_cast<int16_t>(m.vy1 - m.vy0 + 1)};
        t.radius = m.radius;
        t.outline = m.outline;
        t.fill = m.fill;
        out[count++] = t;
    }
    /* Tabs are not drawn as a fill and an outline of one rectangle (a rounded
     * fill, a square foot, three edges), so Ui::drawTab says where they are.
     * Without this the d-pad could reach every control on Settings, Wi-Fi,
     * System Info and About but never the strip that moves between them. */
    for (const TabMark& tm : s_tabs) {
        if (!tm.valid || count >= max) continue;
        Target t;
        t.box = tm.r;
        t.radius = 6;
        t.outline = Ui::outline();
        t.fill = tm.fill;
        t.tab = true;
        t.ring = Ring::TabBar;
        out[count++] = t;
    }
    return count;
}

bool nextTab(Rect& out) {
    const TabMark* active = nullptr;
    for (const TabMark& t : s_tabs) {
        if (t.valid && t.active) active = &t;
    }
    if (active == nullptr) return false;
    const TabMark* next = nullptr;
    const TabMark* first = nullptr;
    for (const TabMark& t : s_tabs) {
        if (!t.valid || &t == active || t.r.y + t.r.h != active->r.y + active->r.h) continue;
        if (t.r.x > active->r.x && (next == nullptr || t.r.x < next->r.x)) next = &t;
        if (first == nullptr || t.r.x < first->r.x) first = &t;
    }
    if (next == nullptr) next = first;
    if (next == nullptr) return false;
    out = next->r;
    return true;
}

bool pager(bool next, Rect& out) {
    const PagerMark& p = s_pager[next ? 1 : 0];
    if (!p.valid) return false;
    out = p.r;
    return true;
}

bool stillDrawn(const Target& t) {
    if (t.tab) {
        for (const TabMark& m : s_tabs) {
            if (m.valid && m.r.x == t.box.x && m.r.y == t.box.y && m.r.w == t.box.w &&
                m.r.h == t.box.h && m.fill == t.fill) {
                return true;
            }
        }
        return false;
    }
    for (const Mark& m : s_marks) {
        if (m.valid && m.vx0 == t.box.x && m.vy0 == t.box.y &&
            m.vx1 - m.vx0 + 1 == t.box.w && m.vy1 - m.vy0 + 1 == t.box.h &&
            m.fill == t.fill && m.outline == t.outline && m.radius == t.radius) {
            return true;
        }
    }
    return false;
}

bool scrollArea(ScrollArea& out) {
    if (!s_scroll.valid) return false;
    out = s_scroll.area;
    return true;
}

}   // namespace Focus

#endif  /* GUME_HAS_NAV_KEYS */
