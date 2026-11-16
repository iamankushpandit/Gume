// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The memory panel behind host/shim/TFT_eSPI.h. See that header for what it
 * promises; this file is how. */

#include <Arduino.h>
#include <TFT_eSPI.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "HostLed.h"
#include "HostPlatform.h"
#include "HostScreenPower.h"
#include "ui/LauncherLayout.h"


void TFT_eSPI::begin(uint8_t) {
    rotation_ = 1;
    resetViewport();
    clipOn_ = false;
    forgetMarks();
    /* The panel HostPlatform found before the firmware started. The frame is
     * laid out at its size over the UI scale, and stored at its real
     * resolution: every logical pixel is scale_ x scale_ real ones, and text
     * is rasterised at the real resolution (src/hal/PanelSize.h). */
    frameW_ = static_cast<int16_t>(HostPlatform::panelWidth());
    frameH_ = static_cast<int16_t>(HostPlatform::panelHeight());
    scale_ = static_cast<int16_t>(HostPlatform::panelScale());
    frame_.assign(static_cast<size_t>(frameW_) * scale_ * frameH_ * scale_, 0);
    edgeUnder_.assign(frame_.size(), 0);
    edgeColour_.assign(frame_.size(), 0);
    edgeAlpha_.assign(frame_.size(), 0);
    edgeShape_.assign(frame_.size(), 0);
    resetViewport();
    ++frameGen_;
    HostPlatform::attachPanel(this);
}

void TFT_eSPI::setRotation(uint8_t r) {
    rotation_ = r & 3;
    resetViewport();
    clipOn_ = false;   // it was in the old rotation's coordinates
    forgetMarks();
}

void TFT_eSPI::writecommand(uint8_t c) {
    switch (c) {
        case 0x10:   // Sleep In
            sleeping_ = true;
            HostLed::setDisplayAwake(false);
            HostScreenPower::setAwake(false);
            break;
        case 0x11:   // Sleep Out
            sleeping_ = false;
            HostLed::setDisplayAwake(true);
            HostScreenPower::setAwake(true);
            break;
        case 0x28: displayOn_ = false; break;    // Display Off
        case 0x29: displayOn_ = true; break;     // Display On
        default: break;
    }
}

int16_t TFT_eSPI::width() const {
    if (vpDatum_) return static_cast<int16_t>(vpW_);
    return (rotation_ & 1) ? frameW_ : frameH_;
}

int16_t TFT_eSPI::height() const {
    if (vpDatum_) return static_cast<int16_t>(vpH_);
    return (rotation_ & 1) ? frameH_ : frameW_;
}

void TFT_eSPI::setViewport(int32_t x, int32_t y, int32_t w, int32_t h, bool vpDatum) {
    const int32_t lw = (rotation_ & 1) ? frameW_ : frameH_;
    const int32_t lh = (rotation_ & 1) ? frameH_ : frameW_;
    int32_t x1 = std::min(x + w, lw);
    int32_t y1 = std::min(y + h, lh);
    x = std::max<int32_t>(x, 0);
    y = std::max<int32_t>(y, 0);
    vpX_ = x;
    vpY_ = y;
    vpW_ = std::max<int32_t>(x1 - x, 0);
    vpH_ = std::max<int32_t>(y1 - y, 0);
    vpDatum_ = vpDatum;
    vpActive_ = true;
}

void TFT_eSPI::resetViewport() {
    vpX_ = 0;
    vpY_ = 0;
    vpW_ = (rotation_ & 1) ? frameW_ : frameH_;
    vpH_ = (rotation_ & 1) ? frameH_ : frameW_;
    vpDatum_ = false;
    vpActive_ = false;
}

bool TFT_eSPI::toFrame(int32_t x, int32_t y, int32_t& fx, int32_t& fy) const {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (x < vpX_ || y < vpY_ || x >= vpX_ + vpW_ || y >= vpY_ + vpH_) return false;
    /* The inverse of Board::pollTouch()'s rotation switch: logical
     * coordinates at this rotation into the rotation-1 frame. */
    switch (rotation_) {
        case 1: fx = x; fy = y; break;
        case 3: fx = frameW_ - 1 - x; fy = frameH_ - 1 - y; break;
        case 2: fx = frameW_ - 1 - y; fy = x; break;
        default: fx = y; fy = frameH_ - 1 - x; break;   // 0
    }
    return fx >= 0 && fy >= 0 && fx < frameW_ && fy < frameH_;
}

void TFT_eSPI::setPaintClip(int32_t x, int32_t y, int32_t w, int32_t h) {
    clipX_ = x;
    clipY_ = y;
    clipW_ = w;
    clipH_ = h;
    clipOn_ = true;
}

void TFT_eSPI::clearPaintClip() {
    clipOn_ = false;
}

bool TFT_eSPI::paintClipFrame(int16_t& x0, int16_t& y0, int16_t& x1, int16_t& y1) const {
    if (!clipOn_ || clipW_ <= 0 || clipH_ <= 0) return false;
    int16_t ax = 0, ay = 0, bx = 0, by = 0;
    logicalToFrame(clipX_, clipY_, ax, ay);
    logicalToFrame(clipX_ + clipW_ - 1, clipY_ + clipH_ - 1, bx, by);
    x0 = std::min(ax, bx);
    y0 = std::min(ay, by);
    x1 = std::max(ax, bx);
    y1 = std::max(ay, by);
    return true;
}

void TFT_eSPI::plot(int32_t x, int32_t y, uint16_t color) {
    if (clipOn_) {
        const int32_t ax = vpDatum_ ? x + vpX_ : x;
        const int32_t ay = vpDatum_ ? y + vpY_ : y;
        if (ax < clipX_ || ay < clipY_ || ax >= clipX_ + clipW_ || ay >= clipY_ + clipH_) return;
    }
    int32_t fx = 0, fy = 0;
    if (toFrame(x, y, fx, fy)) {
        const int32_t d = scale_;
        const int32_t pitch = static_cast<int32_t>(frameW_) * d;
        const size_t first = static_cast<size_t>(fy * d) * pitch + fx * d;
        uint16_t* at = frame_.data() + first;
        uint8_t* edge = edgeAlpha_.data() + first;
        for (int32_t j = 0; j < d; ++j, at += pitch, edge += pitch) {
            for (int32_t i = 0; i < d; ++i) {
                at[i] = color;
                edge[i] = 0;   // solid: no longer anybody's edge
            }
        }
        ++frameGen_;
    }
}

namespace {

uint16_t blend565(uint16_t under, uint16_t over, uint8_t a) {
    if (a == 255) return over;
    if (a == 0) return under;
    const uint32_t ia = 255u - a;
    const uint32_t r = (((over >> 11) & 31) * a + ((under >> 11) & 31) * ia + 127) / 255;
    const uint32_t g = (((over >> 5) & 63) * a + ((under >> 5) & 63) * ia + 127) / 255;
    const uint32_t b = ((over & 31) * a + (under & 31) * ia + 127) / 255;
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

}   // namespace

/* One real pixel at (px, py) -- the rotation's own coordinates times the UI
 * scale -- covered `a`/255 by `color`, with the viewport and the paint clip
 * applied to the logical pixel it falls in, as plot() does for a whole one.
 *
 * An anti-aliased edge is a blend, and a blend drawn twice is a different
 * colour: the screens redraw the same shape over itself all the time (a
 * repaint "is visually a no-op" is a rule they rely on), and every redraw
 * would darken every edge until it was as jagged as the blocks it replaced.
 * So each pixel left partly covered remembers what it was blended over, in
 * what colour, how much and by which shape (edgeUnder_, edgeColour_,
 * edgeAlpha_, edgeShape_), and a solid write forgets it:
 *  - the same shape again takes the larger coverage, over the same ground,
 *    so a redraw is a no-op;
 *  - another shape of the same colour adds its coverage to the first, over
 *    the same ground: two edges that meet -- a thick ring drawn as rings one
 *    pixel apart, two wedges of one pie -- close up with no seam between
 *    them, as the one shape they make would have;
 *  - another colour covering at least as much is drawn over that ground too,
 *    so an outline's edge laid on its fill's edge leaves no halo of the fill;
 *  - anything less is blended over what is there now. */
void TFT_eSPI::blendReal(int32_t px, int32_t py, uint16_t color, uint8_t a) {
    if (a == 0 || px < 0 || py < 0) return;
    const int32_t d = scale_;
    const int32_t lx = px / d, ly = py / d;   // absolute logical
    if (clipOn_ && (lx < clipX_ || ly < clipY_ || lx >= clipX_ + clipW_ || ly >= clipY_ + clipH_)) return;
    if (lx < vpX_ || ly < vpY_ || lx >= vpX_ + vpW_ || ly >= vpY_ + vpH_) return;
    const int32_t pw = static_cast<int32_t>(frameW_) * d;
    const int32_t ph = static_cast<int32_t>(frameH_) * d;
    int32_t fx = 0, fy = 0;
    switch (rotation_) {   // toFrame()'s transforms, at the real resolution
        case 1: fx = px; fy = py; break;
        case 3: fx = pw - 1 - px; fy = ph - 1 - py; break;
        case 2: fx = pw - 1 - py; fy = px; break;
        default: fx = py; fy = ph - 1 - px; break;
    }
    if (fx < 0 || fy < 0 || fx >= pw || fy >= ph) return;
    const size_t i = static_cast<size_t>(fy) * pw + fx;
    uint16_t& at = frame_[i];
    ++frameGen_;
    if (a == 255) {
        at = color;
        edgeAlpha_[i] = 0;
        return;
    }
    uint16_t ground = at;
    uint8_t cover = a;
    if (edgeAlpha_[i] != 0) {
        if (edgeColour_[i] == color) {
            ground = edgeUnder_[i];
            cover = edgeShape_[i] == shape_
                        ? std::max(cover, edgeAlpha_[i])
                        : static_cast<uint8_t>(std::min(255, cover + edgeAlpha_[i]));
        } else if (a >= edgeAlpha_[i]) {
            ground = edgeUnder_[i];
        }
    }
    at = blend565(ground, color, cover);
    if (cover == 255) {
        edgeAlpha_[i] = 0;
        return;
    }
    edgeUnder_[i] = ground;
    edgeColour_[i] = color;
    edgeAlpha_[i] = cover;
    edgeShape_[i] = shape_;
}

void TFT_eSPI::beginShape(uint32_t a, int32_t b, int32_t c, int32_t d, int32_t e, int32_t f,
                          int32_t g) {
    uint32_t h = 2166136261u;   // FNV-1a over the shape's words
    for (const uint32_t v : {a, static_cast<uint32_t>(b), static_cast<uint32_t>(c),
                             static_cast<uint32_t>(d), static_cast<uint32_t>(e),
                             static_cast<uint32_t>(f), static_cast<uint32_t>(g)}) {
        h = (h ^ v) * 16777619u;
    }
    shape_ = static_cast<uint16_t>(h ^ (h >> 16));
}

/* ---- the page keys' target (see pagerTarget() in TFT_eSPI.h) ---------- */

void TFT_eSPI::notePagerLabel(const char* text, int32_t x, int32_t y, int32_t w, int32_t h) {
    int slot = -1;
    if (std::strcmp(text, "Prev") == 0) slot = 0;
    else if (std::strcmp(text, "Next") == 0) slot = 1;
    if (slot < 0) return;
    const int32_t cx = x + w / 2;
    const int32_t cy = y + h / 2;
    int32_t fx = 0, fy = 0;
    PagerMark& m = pager_[slot];
    m.valid = toFrame(cx, cy, fx, fy);   // a clipped label is not a target
    m.lx = vpDatum_ ? cx + vpX_ : cx;
    m.ly = vpDatum_ ? cy + vpY_ : cy;
    m.fx = static_cast<int16_t>(fx);
    m.fy = static_cast<int16_t>(fy);
}

void TFT_eSPI::forgetMarks() {
    pager_[0].valid = pager_[1].valid = false;
    scroll_.valid = scroll_.pending = false;
    iconsRot_ = 0xFF;   // re-placed, with every count zeroed, on the next draw
    for (TabMark& t : tabs_) t.valid = false;
    pendingTab_.valid = false;
    forgetFocus();
}

void TFT_eSPI::forgetMarksUnder(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    auto under = [&](int32_t px, int32_t py) {
        return px >= x && px < x + w && py >= y && py < y + h;
    };
    for (PagerMark& m : pager_) {
        if (m.valid && under(m.lx, m.ly)) m.valid = false;
    }
    /* The track's left edge, which its own thumb never covers: a fill there is
     * the list being repainted or taken away. */
    if (scroll_.valid && under(scroll_.x, scroll_.y + scroll_.h / 2)) scroll_.valid = false;
    forgetIconsUnder(x, y, w, h);
    forgetFocusUnder(x, y, w, h);
    /* A tab's left edge near its foot: its own label never reaches it, and a
     * strip being repainted covers it. */
    for (TabMark& t : tabs_) {
        if (t.valid && under(t.x, t.bottom - 2)) t.valid = false;
    }
}

/* ---- tabs (see nextTabTarget() in TFT_eSPI.h) ------------------------- */

void TFT_eSPI::noteTabFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    pendingTab_.valid = r == 6 && h > 7;
    pendingTab_.rot = rotation_;
    pendingTab_.x = x;
    pendingTab_.top = y;
    pendingTab_.w = w;
    pendingTab_.bottom = y + h;
}

/* Called from fillRect() after it has forgotten whatever it covered, so the
 * tab it completes is recorded after, not wiped by, its own square-off. */
void TFT_eSPI::noteTabSquare(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!pendingTab_.valid) return;
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (h != 7 || x != pendingTab_.x || w != pendingTab_.w || y + h != pendingTab_.bottom) return;
    pendingTab_.valid = false;
    TabMark* slot = nullptr;
    for (TabMark& t : tabs_) {
        if (t.valid && t.x == pendingTab_.x && t.bottom == pendingTab_.bottom) {
            slot = &t;   // the same tab, redrawn -- selected or not
            break;
        }
        if (!t.valid && slot == nullptr) slot = &t;
    }
    if (slot == nullptr) return;
    *slot = pendingTab_;
    slot->valid = true;
}

bool TFT_eSPI::nextTabTarget(int16_t& fx, int16_t& fy) const {
    /* The selected tab is the one standing taller than another in its strip. */
    const TabMark* active = nullptr;
    for (const TabMark& a : tabs_) {
        if (!a.valid || a.rot != rotation_) continue;
        for (const TabMark& b : tabs_) {
            if (b.valid && b.rot == rotation_ && &b != &a && b.bottom == a.bottom &&
                a.top < b.top) {
                active = &a;
                break;
            }
        }
        if (active) break;
    }
    if (active == nullptr) return false;
    /* Next to the right in the same strip, or the leftmost if it was last. */
    const TabMark* next = nullptr;
    const TabMark* first = nullptr;
    for (const TabMark& t : tabs_) {
        if (!t.valid || t.rot != rotation_ || t.bottom != active->bottom || &t == active) continue;
        if (t.x > active->x && (next == nullptr || t.x < next->x)) next = &t;
        if (first == nullptr || t.x < first->x) first = &t;
    }
    if (next == nullptr) next = first;
    if (next == nullptr) return false;
    logicalToFrame(next->x + next->w / 2, (next->top + next->bottom) / 2, fx, fy);
    return true;
}

void TFT_eSPI::logicalToFrame(int32_t x, int32_t y, int16_t& fx, int16_t& fy) const {
    switch (rotation_) {   // as toFrame(), without the viewport
        case 1: fx = static_cast<int16_t>(x); fy = static_cast<int16_t>(y); break;
        case 3: fx = static_cast<int16_t>(frameW_ - 1 - x); fy = static_cast<int16_t>(frameH_ - 1 - y); break;
        case 2: fx = static_cast<int16_t>(frameW_ - 1 - y); fy = static_cast<int16_t>(x); break;
        default: fx = static_cast<int16_t>(y); fy = static_cast<int16_t>(frameH_ - 1 - x); break;
    }
}

/* ---- the header icons (see iconTarget() in TFT_eSPI.h) ---------------- */

void TFT_eSPI::placeIcons() {
    using Mode = Board::LayoutMode;
    const int16_t w = width();
    const Rect places[ICON_KINDS][ICON_SLOTS] = {
        {LauncherLayout::topBarSettingsRect(w), LauncherLayout::gearRect(Mode::Horizontal, w),
         LauncherLayout::gearRect(Mode::Vertical, w), Rect{}},
        {LauncherLayout::topBarLockRect(w), LauncherLayout::lockRect(Mode::Horizontal, w),
         LauncherLayout::lockRect(Mode::Vertical, w), LauncherLayout::pickerLockRect(w)},
        {LauncherLayout::topBarSpeakerRect(w), LauncherLayout::speakerRect(Mode::Horizontal, w),
         LauncherLayout::speakerRect(Mode::Vertical, w), LauncherLayout::pickerSpeakerRect(w)},
        /* Only the top bar: the launcher has no Home, being it. The empty rects
         * count nothing, and iconTrack() passes over them. */
        {LauncherLayout::topBarHomeRect(), Rect{}, Rect{}, Rect{}},
        /* Only the launcher: the top bar names no player. */
        {Rect{}, LauncherLayout::profileRect(Mode::Horizontal, w),
         LauncherLayout::profileRect(Mode::Vertical, w), Rect{}},
    };
    for (int i = 0; i < ICON_KINDS; ++i) {
        for (int j = 0; j < ICON_SLOTS; ++j) {
            IconTrack& t = icons_[i][j];
            t = IconTrack{};
            t.x = places[i][j].x;
            t.y = places[i][j].y;
            t.w = places[i][j].w;
            t.h = places[i][j].h;
        }
    }
    iconsRot_ = rotation_;
}

void TFT_eSPI::noteIconPrim(Prim prim, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (iconsRot_ != rotation_) placeIcons();
    if (vpDatum_) {
        x0 += vpX_; x1 += vpX_;
        y0 += vpY_; y1 += vpY_;
    }
    if (x1 < x0) std::swap(x0, x1);
    if (y1 < y0) std::swap(y0, y1);
    for (auto& kind : icons_) {
        for (IconTrack& t : kind) {
            /* A pixel of slack: the gear's teeth reach one past its radius. A
             * name only has to start inside: portrait draws it 2px in from the
             * rect it was fitted to, so a long one ends just past it. */
            const int32_t right = prim == Prim::Text ? x0 : x1;
            if (x0 >= t.x - 1 && right <= t.x + t.w && y0 >= t.y - 1 && y1 <= t.y + t.h) {
                uint8_t& n = t.count[static_cast<uint8_t>(prim)];
                if (prim == Prim::Text) {
                    const bool first = n == 0;
                    t.tx0 = first ? x0 : std::min(t.tx0, x0);
                    t.ty0 = first ? y0 : std::min(t.ty0, y0);
                    t.tx1 = first ? x1 : std::max(t.tx1, x1);
                    t.ty1 = first ? y1 : std::max(t.ty1, y1);
                }
                if (n < 255) ++n;
            }
        }
    }
}

void TFT_eSPI::forgetIconsUnder(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (iconsRot_ != rotation_) return;   // nothing counted at this rotation yet
    for (auto& kind : icons_) {
        for (IconTrack& t : kind) {
            if (x <= t.x && y <= t.y && x + w >= t.x + t.w && y + h >= t.y + t.h) {
                std::fill(std::begin(t.count), std::end(t.count), 0);
            }
        }
    }
}

bool TFT_eSPI::iconDrawn(Icon icon, int slot) const {
    const IconTrack& t = icons_[static_cast<int>(icon)][slot];
    if (t.w <= 0 || t.h <= 0) return false;
    auto n = [&](Prim p) { return t.count[static_cast<uint8_t>(p)]; };
    switch (icon) {
        case Icon::Settings:   // eight teeth of four lines, three hub circles
            return n(Prim::Line) >= 24 && n(Prim::CircleOutline) >= 2;
        case Icon::Lock:       // shackle, body, keyhole
            return n(Prim::RoundOutline) >= 1 && n(Prim::RoundFill) >= 1 && n(Prim::CircleFill) >= 1;
        case Icon::Mute:       // the cone, as two triangles
            return n(Prim::Triangle) >= 2;
        case Icon::Home:       // the roof and the body
            return n(Prim::Triangle) >= 1 && n(Prim::RoundFill) >= 1;
        case Icon::Profile:    // any text there, beside this layout's launcher gear
            return n(Prim::Text) >= 1 && iconDrawn(Icon::Settings, slot);
    }
    return false;
}

const TFT_eSPI::IconTrack* TFT_eSPI::iconTrack(Icon icon) const {
    if (iconsRot_ != rotation_) return nullptr;
    for (int slot = 0; slot < ICON_SLOTS; ++slot) {
        if (iconDrawn(icon, slot)) return &icons_[static_cast<int>(icon)][slot];
    }
    return nullptr;
}

bool TFT_eSPI::iconTarget(Icon icon, int16_t& fx, int16_t& fy) const {
    const IconTrack* t = iconTrack(icon);
    if (!t) return false;
    logicalToFrame(t->x + t->w / 2, t->y + t->h / 2, fx, fy);
    return true;
}

/* ---- the list's scroll bar (see scrollArea() in TFT_eSPI.h) ----------- *
 *
 * RowList::drawScrollBar() draws a SCROLLBAR_W (6) track with corner radius 3,
 * outlines exactly that rectangle, then fills a thumb one pixel inside it with
 * radius 2. The pair of the first two is what identifies it. */

void TFT_eSPI::noteScrollFill(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (w == 6 && r == 3 && h >= 24) {
        scroll_.pending = true;
        scroll_.rot = rotation_;
        scroll_.x = x;
        scroll_.y = y;
        scroll_.w = w;
        scroll_.h = h;
        return;
    }
    scroll_.pending = false;
    if (scroll_.valid && r == 2 && x == scroll_.x + 1 && w == scroll_.w - 2) {
        scroll_.thumbTop = y - 1;
        scroll_.thumbBottom = y + h + 1;
    }
}

void TFT_eSPI::noteScrollOutline(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r) {
    if (vpDatum_) {
        x += vpX_;
        y += vpY_;
    }
    if (scroll_.pending && r == 3 && x == scroll_.x && y == scroll_.y && w == scroll_.w &&
        h == scroll_.h) {
        scroll_.valid = true;
        /* Until the thumb arrives, which is the next thing drawn: nowhere to go. */
        scroll_.thumbTop = y;
        scroll_.thumbBottom = y + h;
    }
    scroll_.pending = false;
}

bool TFT_eSPI::scrollArea(ScrollArea& out) const {
    if (!scroll_.valid || scroll_.rot != rotation_ || (rotation_ & 1) == 0) return false;
    /* The list RowList clipped to: the track sits 3px inside its top and
     * bottom and 2px inside its right edge. Its left edge is not drawn
     * anywhere this can see; every list in the firmware starts at x = 0. */
    const int32_t left = 0;
    const int32_t right = scroll_.x + scroll_.w + 1;
    const int32_t top = scroll_.y - 3;
    const int32_t bottom = scroll_.y + scroll_.h + 2;
    const bool moreUp = scroll_.thumbTop > scroll_.y;
    const bool moreDown = scroll_.thumbBottom < scroll_.y + scroll_.h;
    const int32_t grab = scroll_.x + scroll_.w / 2;
    if (rotation_ == 1) {
        out.x0 = static_cast<int16_t>(left);
        out.x1 = static_cast<int16_t>(right);
        out.y0 = static_cast<int16_t>(top);
        out.y1 = static_cast<int16_t>(bottom);
        out.grabX = static_cast<int16_t>(grab);
        out.moreAbove = moreUp;
        out.moreBelow = moreDown;
    } else {   // 3: the same picture turned half a turn
        out.x0 = static_cast<int16_t>(frameW_ - 1 - right);
        out.x1 = static_cast<int16_t>(frameW_ - 1 - left);
        out.y0 = static_cast<int16_t>(frameH_ - 1 - bottom);
        out.y1 = static_cast<int16_t>(frameH_ - 1 - top);
        out.grabX = static_cast<int16_t>(frameW_ - 1 - grab);
        out.moreAbove = moreDown;
        out.moreBelow = moreUp;
    }
    if (out.y0 < 0) out.y0 = 0;
    if (out.y1 > frameH_ - 1) out.y1 = frameH_ - 1;
    return true;
}

bool TFT_eSPI::pagerTarget(bool next, int16_t& fx, int16_t& fy) const {
    const PagerMark& m = pager_[next ? 1 : 0];
    if (!m.valid) return false;
    fx = m.fx;
    fy = m.fy;
    return true;
}

void TFT_eSPI::setAddrWindow(int32_t x, int32_t y, int32_t w, int32_t h) {
    winX_ = x;
    winY_ = y;
    winW_ = w;
    winH_ = h;
    winAt_ = 0;
}

void TFT_eSPI::pushPixels(const void* data, uint32_t len) {
    const uint16_t* px = static_cast<const uint16_t*>(data);
    for (uint32_t i = 0; i < len; ++i) pushColor(px[i]);
}

void TFT_eSPI::pushColor(uint16_t color) {
    if (winW_ <= 0 || winH_ <= 0 || winAt_ >= winW_ * winH_) return;
    /* The driver sends words high byte first only when swap is ON, so a
     * caller with swap off has handed over words already in bus order. The
     * frame holds values, so it is that case that needs turning round. */
    if (!swapBytes_) color = static_cast<uint16_t>((color >> 8) | (color << 8));
    plot(winX_ + winAt_ % winW_, winY_ + winAt_ / winW_, color);
    ++winAt_;
}
