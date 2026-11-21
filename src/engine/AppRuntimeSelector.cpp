// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

/* The d-pad's selector, on a board with keys.
 *
 * The same selector the Linux host gives the RG40XX V, and built from the
 * same two pieces: ui/FocusMath.h says where it goes next, and
 * hal/ControlJobs.h says what each button is for, given the buttons this
 * console has. Only finding the controls and drawing the ring differ, and
 * those are ui/FocusRecorder.cpp and the end of this file.
 *
 * The firmware has no idea what a selected control is: every screen takes
 * one touch point. So the selector is the runtime's, and pressing it is an
 * ordinary touch at the middle of the control, handed to the routing below
 * as the frame's touch. No screen changed for it -- which is also why the
 * header's Home, padlock, speaker and gear work as stops: a press on one is
 * a tap there, and the runtime already takes those taps.
 *
 * What each job does here, by ControlJobs::Task:
 *
 *   Move       the d-pad, from control to control; past the end of a row
 *              the next page, past the end of a list the rest of it
 *   Press      a finger down at the selected control for as long as the
 *              button is held; the first press only brings the selector up
 *   Home       what BOOT does
 *   PagePrev/  the screen's Prev and Next, wherever it drew them
 *   PageNext
 *   Tab        the next tab, or Settings on a screen with none
 *   Lock/Mute  what the padlock and the speaker do
 *   Restart    both held for three seconds
 *
 * A button is a shortcut, never the only way to do something: every one of
 * those jobs is also a control on the screen the selector reaches. A real
 * finger puts the selector away, as it put the launcher's old focus ring
 * away -- whoever is touching the screen is looking at their finger.
 *
 * Only on a board with GUME_HAS_NAV_KEYS; on every other board this file
 * compiles to nothing. */

#if GUME_HAS_NAV_KEYS

#include "hal/modules/Modules.h"
#include "ui/FocusMath.h"
#include "ui/LauncherLayout.h"

namespace {

using namespace ControlJobs;

constexpr uint32_t MOVE_REPEAT_AFTER_MS = 400;
constexpr uint32_t MOVE_REPEAT_EVERY_MS = 140;
constexpr uint32_t RESTART_HOLD_MS = 3000;
/* How far one press of the d-pad drags a list that has nothing more to
 * select: about one row. */
constexpr int16_t SCROLL_STEP_PX = 24;
/* Half a face-key symbol: 14px across, inside a quiz answer's left end. */
constexpr int32_t FACE_SHAPE_R = 7;

/* Whether the four face keys are on this console now: the gamepad module
 * itself, found by its probe at boot -- not merely a profile that could
 * carry one. Unclipped, nothing below happens and no answer wears a symbol. */
bool facePadAttached() {
    return Modules::allows(ModuleCapability::NavKeys) && BOARD.gamepad.faceLayout != nullptr;
}

/* The face key in corner `i` of the profile's faceLayout (reading order),
 * or ControlCount. */
Control faceKey(int i) {
    if (!facePadAttached()) return ControlCount;
    const char* face = BOARD.gamepad.faceLayout;
    if (face == nullptr || face[0] == '\0' || face[1] == '\0' || face[2] == '\0' ||
        face[3] == '\0') {
        return ControlCount;
    }
    HeldControls one;
    holdLayoutKey(face[i], one);
    for (uint8_t c = 0; c < ControlCount; ++c) {
        if (one.held(static_cast<Control>(c))) return static_cast<Control>(c);
    }
    return ControlCount;
}

FocusMath::Box toBox(const Focus::Target& t) {
    FocusMath::Box b;
    b.x0 = t.box.x;
    b.y0 = t.box.y;
    b.x1 = static_cast<int16_t>(t.box.x + t.box.w - 1);
    b.y1 = static_cast<int16_t>(t.box.y + t.box.h - 1);
    b.header = t.header;
    return b;
}

bool sameBox(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

int16_t centreX(const Rect& r) { return static_cast<int16_t>(r.x + r.w / 2); }
int16_t centreY(const Rect& r) { return static_cast<int16_t>(r.y + r.h / 2); }

/* How far apart two RGB565 colours look: squared distance with the weights
 * the eye gives red, green and blue (2:4:3), on 8-bit channels. */
int32_t colourDistance(uint16_t a, uint16_t b) {
    const int32_t dr = static_cast<int32_t>((a >> 11) & 0x1F) * 255 / 31 -
                       static_cast<int32_t>((b >> 11) & 0x1F) * 255 / 31;
    const int32_t dg = static_cast<int32_t>((a >> 5) & 0x3F) * 255 / 63 -
                       static_cast<int32_t>((b >> 5) & 0x3F) * 255 / 63;
    const int32_t db = static_cast<int32_t>(a & 0x1F) * 255 / 31 -
                       static_cast<int32_t>(b & 0x1F) * 255 / 31;
    return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
}

/* The ring's colour on a control: whichever of the theme's own strong colours
 * stands furthest from BOTH the control's fill, which the inner line sits on,
 * and its outline, which the outer line replaces. A fixed accent vanished on
 * every theme whose buttons are drawn near it; taking the theme's colours
 * keeps the ring in the palette, so it still looks like this theme. */
uint16_t ringColour(const Focus::Target& t) {
    const uint16_t candidates[] = {Ui::accent(), Ui::warning(), Ui::text(), Ui::success(),
                                   Ui::error()};
    uint16_t best = candidates[0];
    int32_t bestScore = -1;
    for (const uint16_t c : candidates) {
        const int32_t toFill = colourDistance(c, t.fill);
        const int32_t toOutline = colourDistance(c, t.outline);
        const int32_t score = toFill < toOutline ? toFill : toOutline;
        if (score > bestScore) {
            best = c;
            bestScore = score;
        }
    }
    return best;
}

/* A header control: a glyph, not a fill and an outline, so its colours are
 * the header's -- what the ring has to stand out against. */
Focus::Target headerTarget(const Rect& r, uint16_t ground, Focus::Ring ring) {
    Focus::Target t;
    t.box = r;
    t.radius = 4;
    t.fill = ground;
    t.outline = ground;
    t.header = true;
    t.ring = ring;
    return t;
}

}   // namespace

/* The controls on the screen now: what the recorder found, and the header's
 * own, which the runtime knows rather than infers -- the same rects its tap
 * routing tests. A stop is offered only where its glyph is drawn: the
 * launcher's header, a screen's own header (Game::headerControls), or the
 * top bar. */
int BrainoApp::selectorTargets(Focus::Target* out, int max) {
    int n = Focus::targets(out, max);
    if (activeGame_ == nullptr) return n;
    const int16_t w = static_cast<int16_t>(renderer_.width());
    const int16_t h = static_cast<int16_t>(renderer_.height());
    uint16_t ground = Ui::surface();
    /* A glyph fills its CONTROL_H rect, so its ring goes round the outside,
     * in the gap the header leaves between controls; Home's rect has room
     * round its house, so its ring goes inside; the name gets a bar under it. */
    auto add = [&](const Rect& r, Focus::Ring ring = Focus::Ring::Around) {
        if (r.w > 0 && r.h > 0 && n < max) out[n++] = headerTarget(r, ground, ring);
    };
    if (activeGame_ == &launcher_) {
        const Board::LayoutMode mode = board_.layoutMode();
        add(LauncherLayout::profileRect(mode, w), Focus::Ring::Under);
        add(LauncherLayout::lockRect(mode, w));
        if (Board::hasSound()) add(LauncherLayout::speakerRect(mode, w));
        if (Ui::settingsShown()) add(LauncherLayout::gearRect(mode, w));   // the admin's only
        return n;
    }
    Rect lock{}, speaker{};
    if (activeGame_->headerControls(w, h, lock, speaker)) {
        add(lock);
        if (Board::hasSound()) add(speaker);
        return n;
    }
    ground = Ui::bar();
    add(LauncherLayout::topBarHomeRect(), Focus::Ring::Inset);
    add(LauncherLayout::topBarLockRect(w));
    if (Board::hasSound()) add(LauncherLayout::topBarSpeakerRect(w));
    if (Ui::settingsShown()) add(LauncherLayout::topBarSettingsRect(w));   // the admin's only
    return n;
}

/* Settings, where the screen shows its gear: the Tab job's fallback. No gear
 * for anyone but the admin, so nothing to fall back to. */
bool BrainoApp::selectorSettingsRect(Rect& out) {
    if (activeGame_ == nullptr || !Ui::settingsShown()) return false;
    const int16_t w = static_cast<int16_t>(renderer_.width());
    if (activeGame_ == &launcher_) {
        out = LauncherLayout::gearRect(board_.layoutMode(), w);
        return true;
    }
    Rect lock{}, speaker{};
    if (activeGame_->headerControls(w, static_cast<int16_t>(renderer_.height()), lock, speaker)) {
        return false;   // a screen with its own header shows no gear
    }
    out = LauncherLayout::topBarSettingsRect(w);
    return true;
}

void BrainoApp::selectorTap(int16_t x, int16_t y) {
    sel_.script = SelectorState::Script::Tap;
    sel_.step = 0;
    sel_.tx = x;
    sel_.ty = y;
}

void BrainoApp::selectorDrag(const Focus::ScrollArea& area, int dy) {
    sel_.script = SelectorState::Script::Drag;
    sel_.step = 0;
    /* Grab the list near its right-hand edge, where the scroll bar is and no
     * action row's chip reaches, and drag the content the way the d-pad
     * points: down shows what is below, which is a finger moving up. */
    sel_.tx = static_cast<int16_t>(area.list.x + area.list.w - 6);
    sel_.ty = centreY(area.list);
    sel_.dragDy = static_cast<int16_t>(dy > 0 ? -SCROLL_STEP_PX : SCROLL_STEP_PX);
}

/* Put the ring away: the outline back in its own colour, the inner line back
 * in the fill. A header control is repainted with the header instead,
 * because its glyph is not a fill and an outline. */
void BrainoApp::selectorErase() {
    if (!sel_.drawn) return;
    sel_.drawn = false;
    const Focus::Target& t = sel_.sel;
    /* A header control's ring is on the header's own ground, whose colour
     * is known, so it comes off by painting that back. Never by asking for a
     * repaint: the launcher's header is a quarter of the panel, and asking
     * for it flashed the screen every time the selector left a badge. */
    if (t.header) {
        selectorPaintRing(t, false);
        return;
    }
    /* Only where that control is still drawn exactly as it was. Anywhere
     * else the screen has painted over the ring already -- a pressed button
     * redrawn a pixel lower, a page turned -- and painting the old colours
     * back would leave a ring-shaped mark of its own. */
    if (!Focus::stillDrawn(t)) return;
    /* A square of a board: the screen redraws it, piece and all. */
    if (activeGame_ != nullptr && activeGame_->repaintUnderRing(*this, t.box)) return;
    selectorPaintRing(t, false);
}

/* A selection belongs to one screen. A new screen starts with none, as it
 * would for anyone arriving at it, and the next press of the d-pad brings the
 * selector up on it afresh: carried over, the ring landed on whatever was
 * nearest the last screen's control, which on the launcher was anywhere. */
void BrainoApp::selectorFollowScreen() {
    if (sel_.screen == activeGame_) return;
    sel_.screen = activeGame_;
    /* launch() draws the new screen's first frame itself, outside the loop's
     * paint step, so no `repainted` ever says so: without this a quiz's first
     * question went without its key symbols. */
    sel_.shapesDue = true;
    sel_.on = false;
    sel_.drawn = false;
    sel_.dirty = false;
    sel_.script = SelectorState::Script::None;
}

void BrainoApp::selectorPaintRing(const Focus::Target& t, bool on) {
    const Rect& b = t.box;
    const uint16_t ring = ringColour(t);
    Focus::setRecording(false);
    const uint16_t ground = t.fill;
    if (t.ring == Focus::Ring::TabBar) {
        /* A tab is open at its foot and rounded only at the top, so its ring
         * is a bar across the inside of the tab, restored in its own fill. */
        renderer_.fillRect(b.x + 6, b.y + b.h - 5, b.w - 12, 2, on ? ring : t.fill);
    } else if (t.ring == Focus::Ring::Around) {
        renderer_.drawRect(b.x - 1, b.y - 1, b.w + 2, b.h + 2, on ? ring : ground);
        renderer_.drawRect(b.x - 2, b.y - 2, b.w + 4, b.h + 4, on ? ring : ground);
    } else if (t.ring == Focus::Ring::Inset) {
        renderer_.drawRect(b.x + 1, b.y + 1, b.w - 2, b.h - 2, on ? ring : ground);
        renderer_.drawRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, on ? ring : ground);
    } else if (t.ring == Focus::Ring::Under) {
        renderer_.fillRect(b.x, b.y + b.h - 2, b.w, 2, on ? ring : ground);
    } else if (b.w > 8 && b.h > 8) {
        const int16_t inner = t.radius > 2 ? static_cast<int16_t>(t.radius - 2) : 0;
        renderer_.drawRoundRect(b.x, b.y, b.w, b.h, t.radius, on ? ring : t.outline);
        renderer_.drawRoundRect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, inner, on ? ring : t.fill);
    }
    Focus::setRecording(true);
}

/* Scratch for one frame's controls. Static rather than on the loop task's
 * stack: two tables of 64 are a couple of kilobytes, and only the loop task
 * ever comes here, one call at a time. */
static Focus::Target s_targets[Focus::MAX_TARGETS];
static FocusMath::Box s_boxes[Focus::MAX_TARGETS];

void BrainoApp::selectorMove(int dx, int dy) {
    Focus::Target* targets = s_targets;
    const int n = selectorTargets(targets, Focus::MAX_TARGETS);
    Focus::ScrollArea area;
    const bool scrolls = Focus::scrollArea(area);
    if (n == 0) {
        /* Nothing to select, but a list like System Info's still scrolls. */
        if (dy != 0 && scrolls && (dy > 0 ? area.moreBelow : area.moreAbove)) {
            selectorDrag(area, dy);
        }
        return;
    }
    FocusMath::Box* boxes = s_boxes;
    for (int i = 0; i < n; ++i) boxes[i] = toBox(targets[i]);

    if (!sel_.on) {
        /* The first press only brings the selector up, on the control nearest
         * the top left of the screen's content, because nobody has seen where
         * it is yet. */
        int first = -1;
        int32_t best = 0;
        for (int i = 0; i < n; ++i) {
            if (targets[i].header) continue;
            const int32_t x = boxes[i].cx();
            const int32_t y = boxes[i].cy() - TOP_BAR_HEIGHT;
            const int32_t score = x * x + y * y;
            if (first < 0 || score < best) {
                first = i;
                best = score;
            }
        }
        if (first < 0) first = 0;
        sel_.on = true;
        sel_.sel = targets[first];
        sel_.dirty = true;
        return;
    }

    int at = -1;
    for (int i = 0; i < n; ++i) {
        if (sameBox(targets[i].box, sel_.sel.box)) at = i;
    }
    if (at < 0) at = FocusMath::nearest(boxes, n, centreX(sel_.sel.box), centreY(sel_.sel.box));
    const FocusMath::Box& from = boxes[at];

    int next = FocusMath::step(boxes, n, from, dx, dy);
    /* Up from the top of a list that has more above is the list, not the
     * header over it: the header is there when the list has run out. */
    if (next >= 0 && dy < 0 && boxes[next].header && !from.header && scrolls && area.moreAbove) {
        next = -1;
    }
    if (next >= 0) {
        selectorErase();
        sel_.sel = targets[next];
        sel_.dirty = true;
        return;
    }
    if (dy != 0 && scrolls && (dy > 0 ? area.moreBelow : area.moreAbove)) {
        selectorDrag(area, dy);
        return;
    }
    /* Nothing in the cone above or below, and no list to scroll: the nearest
     * control that way, wherever it sits along the row. */
    next = FocusMath::stepBeyond(boxes, n, from, dy);
    if (next >= 0) {
        selectorErase();
        sel_.sel = targets[next];
        sel_.dirty = true;
        return;
    }
    /* Nothing further along the row: the page after, arriving at the near end
     * of the same row. Not from the header, whose row is not what the pages
     * are made of. */
    Rect pagerRect;
    if (dx != 0 && !from.header && Focus::pager(dx > 0, pagerRect)) {
        selectorTap(centreX(pagerRect), centreY(pagerRect));
        const int end = FocusMath::rowEnd(boxes, n, from, dx > 0);
        if (end >= 0) {
            selectorErase();
            sel_.sel = targets[end];
            sel_.dirty = true;
        }
    }
}

uint8_t BrainoApp::selectorFrame(TouchPoint& touch, const FrameKeys& keys) {
    if (!sel_.jobsReady) {
        sel_.jobs = assign(board_.controlsPresent());
        sel_.jobsReady = true;
        char line[160];
        size_t used = static_cast<size_t>(snprintf(line, sizeof(line), "[controls] jobs:"));
        for (uint8_t t = 0; t < TaskCount && used < sizeof(line); ++t) {
            if (!sel_.jobs.has(static_cast<Task>(t))) continue;
            used += static_cast<size_t>(snprintf(line + used, sizeof(line) - used, " %s=%s",
                                                 name(static_cast<Task>(t)),
                                                 name(static_cast<Control>(sel_.jobs.control[t]))));
        }
        Serial.println(line);
    }
    selectorFollowScreen();
    const HeldControls& held = keys.held;
    const HeldControls& prev = sel_.prev;
    const uint32_t now = millis();
    auto controlOf = [&](Task t) { return static_cast<Control>(sel_.jobs.control[t]); };
    auto heldNow = [&](Task t) {
        return sel_.jobs.has(t) && controlOf(t) != DPad && held.held(controlOf(t));
    };
    auto pressed = [&](Task t) {
        return heldNow(t) && !prev.held(controlOf(t));
    };
    uint8_t jobs = 0;

    /* A real finger takes over: the selector goes away and anything it was
     * pressing is let go, so the two can never press at once. */
    if (touch.justPressed || touch.down) {
        if (touch.justPressed && sel_.on) {
            selectorErase();
            sel_.on = false;
        }
        sel_.script = SelectorState::Script::None;
        return 0;
    }

    /* Every press, by name: one line, and the way to see which key is which
     * on a console nobody has mapped by hand. */
    const uint32_t fresh = held.buttons & ~prev.buttons;
    for (uint8_t c = 0; c < ControlCount && fresh != 0; ++c) {
        if (fresh & maskOf(static_cast<Control>(c))) {
            Serial.printf("[controls] pressed %s\n", name(static_cast<Control>(c)));
        }
    }

    /* A quiz: four answers in two columns and two rows, and the four face
     * keys answer them, each the one in its own corner. The screen does not
     * change for it -- it is a tap at the answer's middle, as a finger's
     * would be. While the selector is up, its Press key presses the selector
     * as before; the other three still answer. An answer puts the selector
     * away, as a finger does. Only in a game, so that a two-by-two of tiles
     * on the launcher or buttons on a system screen keep their own keys. */
    bool answering = false;
    if (sel_.script == SelectorState::Script::None && activeAppIsPlayable()) {
        Rect grid[4];
        bool gridKnown = false;
        for (int i = 0; i < 4; ++i) {
            const Control c = faceKey(i);
            if (c == ControlCount || !held.held(c) || prev.held(c)) continue;
            if (sel_.on && sel_.jobs.is(Press, c)) continue;
            if (!gridKnown && !(gridKnown = Focus::answerGrid(grid))) break;
            if (sel_.on) {
                selectorErase();
                sel_.on = false;
            }
            selectorTap(centreX(grid[i]), centreY(grid[i]));
            answering = true;
            break;
        }
    }

    if (!answering && pressed(Home)) jobs |= SELECTOR_HOME;
    if (pressed(Lock)) jobs |= SELECTOR_LOCK;
    if (pressed(Mute) && Board::hasSound()) jobs |= SELECTOR_MUTE;

    /* Restart: both held, for three seconds, so that nobody does it by
     * accident. */
    if (heldNow(RestartA) && heldNow(RestartB)) {
        if (sel_.restartSinceMs == 0) sel_.restartSinceMs = now;
        if (now - sel_.restartSinceMs >= RESTART_HOLD_MS) {
            Serial.println("[controls] restart held -- restarting");
            Serial.flush();
            ESP.restart();
        }
    } else {
        sel_.restartSinceMs = 0;
    }

    if (sel_.script == SelectorState::Script::None) {
        Rect r;
        if (pressed(PagePrev) && Focus::pager(false, r)) {
            selectorTap(centreX(r), centreY(r));
        } else if (pressed(PageNext) && Focus::pager(true, r)) {
            selectorTap(centreX(r), centreY(r));
        } else if (pressed(Tab)) {
            if (Focus::nextTab(r) || selectorSettingsRect(r)) selectorTap(centreX(r), centreY(r));
        } else if (pressed(Press)) {
            if (!sel_.on) {
                selectorMove(0, 0);   // brings it up; presses nothing yet
            } else {
                sel_.script = SelectorState::Script::Hold;
                sel_.step = 0;
                sel_.tx = centreX(sel_.sel.box);
                sel_.ty = centreY(sel_.sel.box);
            }
        }
    }

    /* The d-pad: a new direction moves at once, a held one repeats. */
    if (sel_.jobs.has(Move)) {
        const uint8_t fresh = static_cast<uint8_t>(held.dirs & ~prev.dirs);
        uint8_t dir = 0;
        if (fresh != 0) {
            dir = fresh & HELD_UP ? HELD_UP : fresh & HELD_DOWN ? HELD_DOWN
                : fresh & HELD_LEFT ? HELD_LEFT : HELD_RIGHT;
            sel_.moveDir = dir;
            sel_.repeatAtMs = now + MOVE_REPEAT_AFTER_MS;
        } else if (sel_.moveDir != 0 && (held.dirs & sel_.moveDir)) {
            if (static_cast<int32_t>(now - sel_.repeatAtMs) >= 0) {
                dir = sel_.moveDir;
                sel_.repeatAtMs = now + MOVE_REPEAT_EVERY_MS;
            }
        } else {
            sel_.moveDir = 0;
        }
        if (dir != 0 && sel_.script == SelectorState::Script::None) {
            selectorMove(dir == HELD_RIGHT ? 1 : dir == HELD_LEFT ? -1 : 0,
                         dir == HELD_DOWN ? 1 : dir == HELD_UP ? -1 : 0);
        }
    }

    /* The scripted touch, one frame of it, as the frame's touch. */
    TouchPoint t;
    t.x = sel_.tx;
    t.y = sel_.ty;
    t.pressure = 0xFFFF;
    switch (sel_.script) {
        case SelectorState::Script::None:
            return jobs;
        case SelectorState::Script::Hold:
            if (sel_.step == 0) {
                t.down = t.justPressed = true;
                sel_.step = 1;
            } else if (heldNow(Press)) {
                t.down = true;
            } else {
                t.justReleased = true;
                sel_.script = SelectorState::Script::None;
            }
            break;
        case SelectorState::Script::Tap:
            if (sel_.step == 0) {
                t.down = t.justPressed = true;
                sel_.step = 1;
            } else {
                t.justReleased = true;
                sel_.script = SelectorState::Script::None;
            }
            break;
        case SelectorState::Script::Drag:
            /* Down, two moves, up: a list scrolls on the drag, the way a
             * finger on the glass scrolls it. */
            if (sel_.step == 0) {
                t.down = t.justPressed = true;
            } else if (sel_.step <= 2) {
                t.down = true;
                t.y = static_cast<int16_t>(sel_.ty + sel_.dragDy * sel_.step / 2);
            } else {
                t.y = static_cast<int16_t>(sel_.ty + sel_.dragDy);
                t.justReleased = true;
                sel_.script = SelectorState::Script::None;
            }
            ++sel_.step;
            break;
    }
    touch = t;
    return jobs;
}

/* After the screen has drawn: the ring on the selected control, if the
 * selector is up and either it moved or the screen painted over it. If the
 * control it was on is gone -- a page turned, a screen changed -- it follows
 * to the one nearest to where it was. */
/* Each answer of a quiz wears the symbol of the face key that answers it,
 * at its left end, in the ink its label would take. Only with the gamepad
 * module attached, only in a game, and only where answerGrid() finds the
 * four. It is drawn after the screen, whenever the screen has drawn: a
 * button repainted for a new colour or a new question has painted over its
 * symbol, and drawing all four again is four small outlines over pixels that
 * are either the same or gone. Nothing ever erases one -- the answer's own
 * repaint does that. */
void BrainoApp::paintFaceShapes() {
    const char* shapes = BOARD.gamepad.faceShapes;
    if (shapes == nullptr || !facePadAttached() || !activeAppIsPlayable()) return;
    Rect grid[4];
    uint16_t fills[4];
    if (!Focus::answerGrid(grid, fills)) return;
    Focus::setRecording(false);
    for (int i = 0; i < 4 && shapes[i] != '\0'; ++i) {
        const Rect& r = grid[i];
        const uint16_t ink = Ui::onFill(fills[i]);
        const int32_t cx = r.x + 6 + FACE_SHAPE_R;
        const int32_t cy = r.y + r.h / 2;
        const int32_t s = FACE_SHAPE_R;
        switch (shapes[i]) {
            case 'O':
                /* A ring reads smaller than a square or a triangle of the
                 * same width, so it is drawn a pixel larger all round. */
                renderer_.drawCircle(cx, cy, s + 1, ink);
                renderer_.drawCircle(cx, cy, s, ink);
                break;
            case 'U':
                renderer_.drawTriangle(cx, cy - s, cx - s, cy + s - 1, cx + s, cy + s - 1, ink);
                renderer_.drawTriangle(cx, cy - s + 2, cx - s + 2, cy + s - 2, cx + s - 2,
                                       cy + s - 2, ink);
                break;
            case 'D':
                renderer_.drawTriangle(cx, cy + s, cx - s, cy - s + 1, cx + s, cy - s + 1, ink);
                renderer_.drawTriangle(cx, cy + s - 2, cx - s + 2, cy - s + 2, cx + s - 2,
                                       cy - s + 2, ink);
                break;
            case 'S':
                renderer_.drawRect(cx - s + 1, cy - s + 1, 2 * s - 1, 2 * s - 1, ink);
                renderer_.drawRect(cx - s + 2, cy - s + 2, 2 * s - 3, 2 * s - 3, ink);
                break;
            default:
                break;
        }
    }
    Focus::setRecording(true);
}

void BrainoApp::paintSelector(bool repainted) {
    selectorFollowScreen();
    if (repainted || sel_.shapesDue) {
        paintFaceShapes();
        sel_.shapesDue = false;
    }
    if (!sel_.on || !(repainted || sel_.dirty)) return;
    Focus::Target* targets = s_targets;
    const int n = selectorTargets(targets, Focus::MAX_TARGETS);
    if (n == 0) {
        sel_.on = false;
        sel_.drawn = false;
        return;
    }
    int at = -1;
    for (int i = 0; i < n; ++i) {
        if (sameBox(targets[i].box, sel_.sel.box)) at = i;
    }
    if (at < 0) {
        FocusMath::Box* boxes = s_boxes;
        for (int i = 0; i < n; ++i) boxes[i] = toBox(targets[i]);
        at = FocusMath::nearest(boxes, n, centreX(sel_.sel.box), centreY(sel_.sel.box));
    }
    sel_.sel = targets[at];
    selectorPaintRing(sel_.sel, true);
    sel_.drawn = true;
    sel_.dirty = false;
}

#endif  /* GUME_HAS_NAV_KEYS */
