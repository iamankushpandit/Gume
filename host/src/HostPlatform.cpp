// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The window and the input. See HostPlatform.h.
 *
 * Input is a pointer, because the firmware is a touch console and every
 * screen takes one TouchPoint. Two things can drive it:
 *
 *   - a mouse: the left button is a finger on the glass, where the mouse is;
 *   - a gamepad or the keyboard: the left stick moves a crosshair, the d-pad or
 *     the arrow keys move a selector from control to control, and A (Enter,
 *     Space) holds a finger down under whichever of the two is showing.
 *
 * B and Start (Escape, Backspace) are the BOOT key, which the runtime treats
 * as Home. Y or L1 (PageUp) and X or R1 (PageDown) press the screen's own
 * Prev and Next buttons -- the launcher's pages, and every other pager drawn
 * the same way, and so does pushing the cursor against the left or right side.
 * Pushing the cursor against the end of a scrolling list scrolls it. M (the M
 * key) is the next tab, or Settings where there are none; R2 (L) is Lock and
 * L2 (K) is mute, by the header's own buttons. While the screen is asleep
 * any button only wakes it.
 * Holding Select + Start for three seconds quits -- long enough that
 * a young player mashing buttons does not leave Braino by accident, which
 * matters on a handheld where Braino is the only thing meant to be running.
 * F10 or closing the window quits at once.
 *
 * The cursor is drawn over the window, never into the panel frame. The
 * firmware repaints only what it knows changed, so anything drawn into its
 * frame from outside would be smeared across the screen by the next partial
 * repaint. Kept out of the frame, the cursor costs the firmware nothing. */

#include "HostFocus.h"
#include "HostLed.h"
#include "HostPlatform.h"

#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <thread>

#include <TFT_eSPI.h>

namespace HostPlatform {
namespace {

constexpr int FRAME_W = TFT_eSPI::FRAME_W;
constexpr int FRAME_H = TFT_eSPI::FRAME_H;
constexpr uint32_t PRESENT_INTERVAL_MS = 16;
constexpr int STICK_DEADZONE = 8000;
/* The stick at full push, in canvas pixels per second. */
constexpr float CURSOR_FAST = 300.0f;
/* How long the cursor stays drawn after the pad or keyboard last moved it,
 * so a mouse user never sees it. */
constexpr uint32_t CURSOR_SHOW_MS = 4000;
/* The d-pad: one control per press, then repeating once it has been held long
 * enough that the hold cannot have been a slow press. */
constexpr uint32_t FOCUS_REPEAT_AFTER_MS = 350;
constexpr uint32_t FOCUS_REPEAT_MS = 130;
/* A screen change clears the panel before it draws the next screen, so an empty
 * list means nothing until it has stayed empty for longer than a repaint. */
constexpr uint32_t FOCUS_LOST_MS = 500;
constexpr uint32_t QUIT_HOLD_MS = 3000;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture = nullptr;
TFT_eSPI* panel = nullptr;
std::thread::id mainThread;
char** savedArgv = nullptr;
bool quit = false;
uint32_t lastPresentMs = 0;
uint32_t lastStepMs = 0;
float backlight = 1.0f;

bool mouseDown = false;
int mouseX = 0, mouseY = 0;

/* Test aids, both off unless asked for. BRAINO_SHOTS="ms,ms,..." saves the
 * panel frame as <data dir>/shot-<ms>.bmp at those times since start, and F12
 * saves one on demand. BRAINO_TAPS="ms:x,y,hold;..." ("ms:next", "ms:prev"
 * for the page keys) presses the panel at
 * frame coordinates (x, y) at that time for `hold` ms (default 120): a
 * screen can be walked to and photographed with no one at the window. */
struct ScriptedTap {
    uint32_t atMs;
    int16_t x;
    int16_t y;
    uint32_t holdMs;
};
constexpr int MAX_SCRIPT = 64;
uint32_t shotTimes[MAX_SCRIPT];
int shotCount = 0;
int shotNext = 0;
ScriptedTap taps[MAX_SCRIPT];
int tapCount = 0;
bool shotRequested = false;

/* The page keys -- X and Y on a pad, PageUp and PageDown on a keyboard --
 * press the screen's own Prev or Next button for PAGE_TAP_MS, exactly as a
 * finger would. The panel shim knows where those buttons are because it drew
 * their labels (TFT_eSPI::pagerTarget()); on a screen with no pager the keys
 * do nothing, rather than pressing whatever happens to sit in that corner. */
constexpr uint32_t PAGE_TAP_MS = 100;
constexpr uint32_t PAGE_TAP_GAP_MS = 60;   // lifted between taps, or the second is not a press
ScriptedTap pageTap{0, 0, 0, 0};

/* BRAINO_TAPS can press keys too: "ms:prev" and "ms:next" are the page keys,
 * "ms:up", "ms:down", "ms:left" and "ms:right" hold the d-pad (for 1000 ms,
 * or ",hold"), "ms:a" holds A (for 120 ms unless given), and "ms:menu",
 * "ms:mute" and "ms:lock" are M, L2 and R2. */
enum class Key : uint8_t { Prev, Next, Up, Down, Left, Right, Menu, Mute, Lock, A };
struct ScriptKey {
    uint32_t atMs;
    uint32_t holdMs;
    Key key;
};
ScriptKey scriptKeys[MAX_SCRIPT];
int scriptKeyCount = 0;
int scriptKeyNext = 0;
bool scriptUp = false, scriptDown = false, scriptLeft = false, scriptRight = false;
bool scriptA = false;

/* Pushing the cursor against the top or bottom of a list that has more to
 * show scrolls it: the cursor stays put and an invisible finger drags the
 * list's scroll track, exactly as a finger on the glass would, so every list
 * scrolls the way it already does and no screen needs to know. The finger
 * starts at the far end of the track so a whole list height passes before it
 * has to lift and grab again. Only RowList lists have a scroll bar, and the
 * panel shim finds it (TFT_eSPI::scrollArea()). */
constexpr float SCROLL_SPEED = 160.0f;        // list pixels per second
constexpr int16_t SCROLL_EDGE = 3;            // how close to the edge counts as against it
constexpr uint32_t SCROLL_REGRAB_MS = 60;     // lifted long enough for a frame to see it
constexpr int16_t SCROLL_INSET = 20;          // the grab's distance from the list's ends
struct Drag {
    bool active = false;
    int16_t x = 0;
    float y = 0.0f;
    uint32_t liftedUntilMs = 0;
};
Drag drag;

/* Pushing the cursor against the left or right side turns the page. */
constexpr uint32_t EDGE_DWELL_MS = 250;
constexpr uint32_t EDGE_REPEAT_MS = 700;
int edgeSide = 0;          // -1 left, +1 right, 0 neither
uint32_t edgeNextMs = 0;

float cursorX = FRAME_W / 2.0f;
float cursorY = FRAME_H / 2.0f;
uint32_t cursorShownAtMs = 0;

/* The d-pad's selector. While it is on, the cursor sits at the middle of the
 * selected control, so A presses it through the same path as the crosshair. The
 * box is kept rather than an index, because the list is read again every frame
 * and a screen may draw the same control in a different slot. */
bool focusOn = false;
bool focusShown = false;
HostFocus::Box focusBox;
int focusDx = 0, focusDy = 0;
uint32_t focusRepeatAtMs = 0;
uint32_t focusEmptySinceMs = 0;

bool padTouch = false;
bool padStickClick = false;
bool keyTouch = false;
bool padHome = false;
bool keyHome = false;
bool padStart = false;
bool padBack = false;
uint32_t quitHeldSinceMs = 0;
bool keyUp = false, keyDown = false, keyLeft = false, keyRight = false;
bool padUp = false, padDown = false, padLeft = false, padRight = false;
int16_t stickX = 0, stickY = 0;
/* Pushing a stick well over asks the light ring for the battery. Well past the
 * cursor's deadzone, and once per push with hysteresis, because a stick drifts
 * and a console in a bag must not keep lighting up. */
constexpr int16_t STICK_ASK_ON = 16000;
constexpr int16_t STICK_ASK_OFF = 8000;
bool stickAsking[4] = {false, false, false, false};
/* L2 mutes, R2 locks. Hysteresis, so a trigger resting half in does not
 * fire twice. */
constexpr int16_t TRIGGER_ON = 16000;
constexpr int16_t TRIGGER_OFF = 8000;
bool leftTriggerHeld = false, rightTriggerHeld = false;
uint32_t unmappedSeen = 0;

/* While the panel sleeps, any button wakes it and does nothing else: the
 * press becomes a short BOOT press, which View::Asleep takes exactly as it
 * takes a touch, and nothing reaches the screen underneath -- a page key
 * pressed in the dark must not turn a page the player cannot see. Longer than
 * the firmware's 100ms sleep poll, so one poll always sees it. The stick does
 * not wake it: a stick drifts, and a console in a bag would never stay dark. */
constexpr uint32_t WAKE_PULSE_MS = 200;
uint32_t wakePulseUntilMs = 0;
bool presentedAsleep = false;

bool panelAsleep() { return panel && panel->asleep(); }

/* While the screen is on the left stick is the cursor, and showing the battery
 * every time it moved would take the ring away from everything else. So the
 * left stick asks only in the dark, and the right, which does nothing else,
 * asks at any time. */
void watchStickForBattery(uint8_t axis, int16_t value) {
    if (axis > SDL_CONTROLLER_AXIS_RIGHTY) return;
    bool& asking = stickAsking[axis];
    if (!asking && std::abs(value) > STICK_ASK_ON) {
        asking = true;
        if (axis >= SDL_CONTROLLER_AXIS_RIGHTX || panelAsleep()) HostLed::showBattery();
    } else if (asking && std::abs(value) < STICK_ASK_OFF) {
        asking = false;
    }
}

/* True, and the wake press started, if this press is for waking. */
bool wakeInstead() {
    if (!panelAsleep()) return false;
    wakePulseUntilMs = SDL_GetTicks() + WAKE_PULSE_MS;
    return true;
}

void openController(int index) {
    if (!SDL_IsGameController(index)) return;
    SDL_GameController* pad = SDL_GameControllerOpen(index);
    if (pad) std::printf("[host] gamepad: %s\n", SDL_GameControllerName(pad));
}

/* Press the panel at (x, y) for PAGE_TAP_MS, after any press still running. */
void tapAt(int16_t x, int16_t y) {
    const uint32_t now = SDL_GetTicks();
    const uint32_t freeAt = pageTap.atMs + pageTap.holdMs + PAGE_TAP_GAP_MS;
    pageTap = ScriptedTap{now > freeAt ? now : freeAt, x, y, PAGE_TAP_MS};
}

/* M, R2 and L2: the header's gear, padlock and speaker, wherever the screen
 * has drawn them -- top bar or launcher header -- and nothing where it has
 * not (TFT_eSPI::iconTarget()). */
void pressIcon(TFT_eSPI::Icon icon) {
    int16_t x = 0, y = 0;
    if (panel && panel->iconTarget(icon, x, y)) tapAt(x, y);
}

/* M: the next tab on a screen that has tabs, wrapping round; Settings on one
 * that does not. */
void pressMenu() {
    int16_t x = 0, y = 0;
    if (panel && panel->nextTabTarget(x, y)) {
        tapAt(x, y);
        return;
    }
    pressIcon(TFT_eSPI::Icon::Settings);
}

void pressPager(bool next) {
    int16_t x = 0, y = 0;
    if (panel && panel->pagerTarget(next, x, y)) tapAt(x, y);
}

void onButton(uint8_t button, bool down) {
    if (down && wakeInstead()) return;
    switch (button) {
        /* M, the small button: the next tab, or Settings. */
        case SDL_CONTROLLER_BUTTON_GUIDE: if (down) pressMenu(); return;
        /* X or R1 on to the next page, Y or L1 back a page -- the owner's
         * choice on the RG40XXV, where X is the upper face button and Y the
         * left one. */
        case SDL_CONTROLLER_BUTTON_X:
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: if (down) pressPager(true); return;
        case SDL_CONTROLLER_BUTTON_Y:
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: if (down) pressPager(false); return;
        /* A, or pressing the stick in -- the click a mouse-shaped control
         * invites. Both hold the finger down for as long as they are held. */
        case SDL_CONTROLLER_BUTTON_A: padTouch = down; break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK: padStickClick = down; break;
        case SDL_CONTROLLER_BUTTON_B: padHome = down; break;
        case SDL_CONTROLLER_BUTTON_START: padStart = down; break;
        case SDL_CONTROLLER_BUTTON_BACK: padBack = down; break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: padUp = down; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: padDown = down; break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: padLeft = down; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: padRight = down; break;
        default:
            /* Said once per button, so a handheld whose buttons SDL names
             * differently can be mapped from its log. */
            if (down && button < 32 && !(unmappedSeen & (1u << button))) {
                unmappedSeen |= 1u << button;
                std::printf("[host] button %u is not mapped\n", static_cast<unsigned>(button));
            }
            return;
    }
    cursorShownAtMs = SDL_GetTicks();
    if (padStart && padBack) {
        if (quitHeldSinceMs == 0) quitHeldSinceMs = SDL_GetTicks();
    } else {
        quitHeldSinceMs = 0;
    }
}

void onKey(SDL_Keycode key, bool down) {
    if (down && key != SDLK_F10 && key != SDLK_F12 && wakeInstead()) return;
    switch (key) {
        case SDLK_RETURN:
        case SDLK_SPACE:
        case SDLK_z: keyTouch = down; break;
        case SDLK_ESCAPE:
        case SDLK_BACKSPACE:
        case SDLK_x: keyHome = down; break;
        case SDLK_UP: keyUp = down; break;
        case SDLK_DOWN: keyDown = down; break;
        case SDLK_LEFT: keyLeft = down; break;
        case SDLK_RIGHT: keyRight = down; break;
        case SDLK_PAGEUP: if (down) pressPager(false); return;
        case SDLK_PAGEDOWN: if (down) pressPager(true); return;
        case SDLK_m: if (down) pressMenu(); return;
        case SDLK_l: if (down) pressIcon(TFT_eSPI::Icon::Lock); return;
        case SDLK_k: if (down) pressIcon(TFT_eSPI::Icon::Mute); return;
        case SDLK_F10: if (down) quit = true; return;
        case SDLK_F12: if (down) shotRequested = true; return;
        default: return;
    }
    cursorShownAtMs = SDL_GetTicks();
}

void pumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
            case SDL_QUIT:
                std::printf("[host] window closed\n");
                quit = true;
                break;
            case SDL_KEYDOWN:
                if (!e.key.repeat) onKey(e.key.keysym.sym, true);
                break;
            case SDL_KEYUP: onKey(e.key.keysym.sym, false); break;
            /* With a logical size set, SDL has already mapped mouse
             * coordinates into the 320x240 frame, letterbox and all. */
            case SDL_MOUSEBUTTONDOWN:
                if (e.button.button == SDL_BUTTON_LEFT) {
                    mouseDown = true;
                    mouseX = e.button.x;
                    mouseY = e.button.y;
                    cursorShownAtMs = 0;
                    focusOn = false;
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (e.button.button == SDL_BUTTON_LEFT) mouseDown = false;
                break;
            case SDL_MOUSEMOTION:
                mouseX = e.motion.x;
                mouseY = e.motion.y;
                break;
            case SDL_CONTROLLERDEVICEADDED: openController(e.cdevice.which); break;
            case SDL_CONTROLLERBUTTONDOWN: onButton(e.cbutton.button, true); break;
            case SDL_CONTROLLERBUTTONUP: onButton(e.cbutton.button, false); break;
            case SDL_CONTROLLERAXISMOTION:
                watchStickForBattery(e.caxis.axis, e.caxis.value);
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) stickX = e.caxis.value;
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY) stickY = e.caxis.value;
                /* L2 and R2 are triggers, which SDL reports as axes: act on
                 * the press, once, and not again until released. */
                if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT ||
                    e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
                    const bool left = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT;
                    bool& held = left ? leftTriggerHeld : rightTriggerHeld;
                    if (!held && e.caxis.value > TRIGGER_ON) {
                        held = true;
                        if (!wakeInstead()) {
                            pressIcon(left ? TFT_eSPI::Icon::Mute : TFT_eSPI::Icon::Lock);
                        }
                    } else if (held && e.caxis.value < TRIGGER_OFF) {
                        held = false;
                    }
                    break;
                }
                if (std::abs(e.caxis.value) > STICK_DEADZONE) cursorShownAtMs = SDL_GetTicks();
                break;
            default: break;
        }
    }
}

/* Drag the list one step in `dir` (+1 shows more of what is below), or let
 * go when dir is 0. */
void stepDrag(int dir, const TFT_eSPI::ScrollArea& area, float dt, uint32_t now) {
    if (dir == 0) {
        drag.active = false;
        return;
    }
    if (now < drag.liftedUntilMs) return;
    /* Kept well inside the list: a touch lands within TOUCH_HIT_SLOP of what
     * is next to it, and a grab just under a tab strip pressed the tab. */
    const float lo = static_cast<float>(area.y0 + SCROLL_INSET);
    const float hi = static_cast<float>(area.y1 - SCROLL_INSET);
    if (hi - lo < 8.0f) return;
    if (!drag.active) {
        /* The finger moves against the direction of travel, as on glass:
         * to see further down, it pushes the list up. */
        drag.active = true;
        drag.x = area.grabX;
        drag.y = dir > 0 ? hi : lo;
        return;
    }
    drag.y -= static_cast<float>(dir) * SCROLL_SPEED * dt;
    if (drag.y < lo || drag.y > hi) {
        drag.active = false;
        drag.liftedUntilMs = now + SCROLL_REGRAB_MS;
    }
}

/* The selector for this frame: follow the control it was on, take a d-pad press
 * or repeat, and put the cursor under it. Answers how a list should scroll, for
 * a d-pad held towards where there is no control left to go to. */
int stepFocus(uint32_t now) {
    int dx = 0, dy = 0;
    if (padUp || keyUp || scriptUp) dy -= 1;
    if (padDown || keyDown || scriptDown) dy += 1;
    if (dy == 0) {
        if (padLeft || keyLeft || scriptLeft) dx -= 1;
        if (padRight || keyRight || scriptRight) dx += 1;
    }
    const bool newPress = (dx != 0 || dy != 0) && (dx != focusDx || dy != focusDy);
    const bool repeat = !newPress && (dx != 0 || dy != 0) && now >= focusRepeatAtMs;
    focusDx = dx;
    focusDy = dy;
    if (!focusOn && dx == 0 && dy == 0) return 0;

    TFT_eSPI::FocusBox raw[TFT_eSPI::MAX_FOCUS];
    HostFocus::Box boxes[TFT_eSPI::MAX_FOCUS];
    const int count = panel ? panel->focusTargets(raw, TFT_eSPI::MAX_FOCUS) : 0;
    for (int i = 0; i < count; ++i) {
        boxes[i].x0 = raw[i].x0;
        boxes[i].y0 = raw[i].y0;
        boxes[i].x1 = raw[i].x1;
        boxes[i].y1 = raw[i].y1;
        boxes[i].header = raw[i].header;
    }
    if (count == 0) {
        focusShown = false;
        if (focusEmptySinceMs == 0) focusEmptySinceMs = now;
        if (now - focusEmptySinceMs >= FOCUS_LOST_MS) focusOn = false;
        return dy;   // nothing to select, but a list like System Info's still scrolls
    }
    focusEmptySinceMs = 0;
    if (!focusOn && !newPress) return 0;

    /* The first press only brings the selector up, on what the crosshair was
     * nearest: nobody has seen where it is yet, so it must not move or turn a
     * page. Otherwise, the same control if it is still there, or the nearest
     * to where it was, which is how a scrolled list or a new page is followed. */
    int at = -1;
    const bool arriving = !focusOn;
    if (arriving) {
        at = HostFocus::nearest(boxes, count, static_cast<int>(cursorX), static_cast<int>(cursorY));
    } else {
        for (int i = 0; i < count && at < 0; ++i) {
            if (boxes[i].same(focusBox)) at = i;
        }
        if (at < 0) at = HostFocus::nearest(boxes, count, focusBox.cx(), focusBox.cy());
    }
    focusOn = true;
    focusShown = true;
    focusBox = boxes[at];

    int scroll = 0;
    if (!arriving && (dx != 0 || dy != 0)) {
        int next = HostFocus::step(boxes, count, focusBox, dx, dy);
        /* Up from the top of a list that has more above is the list, not the
         * header over it: the header is there when the list has run out. */
        TFT_eSPI::ScrollArea area;
        if (next >= 0 && dy < 0 && boxes[next].header && !focusBox.header && panel &&
            panel->scrollArea(area) && area.moreAbove) {
            next = -1;
        }
        if (next < 0) scroll = dy;
        if (newPress || repeat) {
            if (next >= 0) {
                focusBox = boxes[next];
            } else if (dx != 0 && !focusBox.header) {
                /* Nothing further along the row: the page after, arriving at
                 * the near end of the same row. Not from the header, whose row
                 * is not what the pages are made of. */
                int16_t px = 0, py = 0;
                if (panel && panel->pagerTarget(dx > 0, px, py)) {
                    pressPager(dx > 0);
                    const int end = HostFocus::rowEnd(boxes, count, focusBox, dx > 0);
                    if (end >= 0) focusBox = boxes[end];
                }
            }
        }
    }
    if (newPress) focusRepeatAtMs = now + FOCUS_REPEAT_AFTER_MS;
    else if (repeat) focusRepeatAtMs = now + FOCUS_REPEAT_MS;

    cursorX = static_cast<float>(focusBox.cx());
    cursorY = static_cast<float>(focusBox.cy());
    return scroll;
}

/* Move the cursor for the time since the last step. */
void stepCursor(uint32_t now) {
    const float dt = lastStepMs ? (now - lastStepMs) / 1000.0f : 0.0f;
    lastStepMs = now;
    if (dt <= 0.0f || dt > 0.25f) return;
    /* Nothing the cursor does in the dark may press anything: a stick
     * resting against an edge would turn pages and wake the panel. */
    if (panelAsleep()) {
        drag.active = false;
        edgeSide = 0;
        return;
    }
    const float startY = cursorY;

    /* The stick takes over from the selector the moment it is pushed. */
    if (std::abs(stickX) > STICK_DEADZONE || std::abs(stickY) > STICK_DEADZONE) focusOn = false;
    const int padScroll = stepFocus(now);

    auto axis = [](int16_t v) {
        if (std::abs(v) <= STICK_DEADZONE) return 0.0f;
        const float sign = v < 0 ? -1.0f : 1.0f;
        return sign * (std::abs(v) - STICK_DEADZONE) / (32767.0f - STICK_DEADZONE);
    };
    cursorX += axis(stickX) * CURSOR_FAST * dt;
    cursorY += axis(stickY) * CURSOR_FAST * dt;

    if (cursorX < 0) cursorX = 0;
    if (cursorY < 0) cursorY = 0;
    if (cursorX > FRAME_W - 1) cursorX = FRAME_W - 1;
    if (cursorY > FRAME_H - 1) cursorY = FRAME_H - 1;

    /* Against the edge of a list with more beyond it: scroll instead. Not
     * while a finger is already down, which would make the drag a second
     * finger. */
    const float push = axis(stickY);
    TFT_eSPI::ScrollArea area;
    int dir = 0;
    const bool pressing = mouseDown || padTouch || padStickClick || keyTouch;
    if (!pressing && padScroll != 0 && panel && panel->scrollArea(area)) {
        /* The d-pad, with no control left that way: wherever the selector is. */
        if (padScroll > 0 && area.moreBelow) dir = 1;
        if (padScroll < 0 && area.moreAbove) dir = -1;
    } else if (!pressing && push != 0.0f && panel && panel->scrollArea(area) &&
        cursorX >= area.x0 && cursorX <= area.x1 && startY >= area.y0 - 1 &&
        startY <= area.y1) {
        if (push > 0.0f && cursorY >= area.y1 - SCROLL_EDGE && area.moreBelow) {
            dir = 1;
            cursorY = static_cast<float>(area.y1 - SCROLL_EDGE);
        } else if (push < 0.0f && cursorY <= area.y0 + SCROLL_EDGE && area.moreAbove) {
            dir = -1;
            cursorY = static_cast<float>(area.y0 + SCROLL_EDGE);
        }
    }
    stepDrag(dir, area, dt, now);

    /* Against the left or right side of the screen: the page before or the
     * page after, through the same Prev and Next buttons as Y/L1 and X/R1.
     * A short dwell first, so a cursor swept across to a tile by the edge
     * does not turn the page under it; then again while it is held there. */
    const float pushX = axis(stickX);
    int side = 0;
    if (!pressing && pushX < 0.0f && cursorX <= 0.0f) side = -1;
    if (!pressing && pushX > 0.0f && cursorX >= FRAME_W - 1) side = 1;
    if (side != edgeSide) {
        edgeSide = side;
        edgeNextMs = now + EDGE_DWELL_MS;
    } else if (side != 0 && now >= edgeNextMs) {
        pressPager(side > 0);
        edgeNextMs = now + EDGE_REPEAT_MS;
    }
}

void drawCursor() {
    const int x = static_cast<int>(cursorX);
    const int y = static_cast<int>(cursorY);
    const bool pressing = padTouch || padStickClick || keyTouch;
    /* A plain crosshair in a contrasting pair, so it reads on every theme. */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawLine(renderer, x - 6, y + 1, x + 6, y + 1);
    SDL_RenderDrawLine(renderer, x + 1, y - 6, x + 1, y + 6);
    if (pressing) {
        SDL_SetRenderDrawColor(renderer, 255, 210, 0, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    }
    SDL_RenderDrawLine(renderer, x - 6, y, x + 6, y);
    SDL_RenderDrawLine(renderer, x, y - 6, x, y + 6);
}

/* A ring two pixels thick, just outside the control and in a contrasting pair,
 * so it reads on every theme and over every fill. It turns the colour the
 * crosshair turns while A is held. */
/* Kept on the screen: Home sits in the corner, and a ring just outside it would
 * lose two whole sides. */
SDL_Rect selectorRect() {
    const int x0 = focusBox.x0 - 2 < 0 ? 0 : focusBox.x0 - 2;
    const int y0 = focusBox.y0 - 2 < 0 ? 0 : focusBox.y0 - 2;
    const int x1 = focusBox.x1 + 2 > FRAME_W - 1 ? FRAME_W - 1 : focusBox.x1 + 2;
    const int y1 = focusBox.y1 + 2 > FRAME_H - 1 ? FRAME_H - 1 : focusBox.y1 + 2;
    return SDL_Rect{x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

void drawSelector() {
    const bool pressing = padTouch || padStickClick || keyTouch;
    const SDL_Rect outer = selectorRect();
    const SDL_Rect middle{outer.x + 1, outer.y + 1, outer.w - 2, outer.h - 2};
    const SDL_Rect inner{outer.x + 2, outer.y + 2, outer.w - 4, outer.h - 4};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderDrawRect(renderer, &inner);
    if (pressing) {
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 210, 0, 255);
    }
    SDL_RenderDrawRect(renderer, &outer);
    SDL_RenderDrawRect(renderer, &middle);
}

void saveShot(uint32_t now) {
    if (!panel) return;
    SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint16_t*>(panel->frame()), FRAME_W, FRAME_H, 16, FRAME_W * 2,
        SDL_PIXELFORMAT_RGB565);
    if (!frame) return;
    /* A copy, so that the selector can be in the picture and stay out of the
     * panel's frame. */
    SDL_Surface* s = SDL_ConvertSurface(frame, frame->format, 0);
    SDL_FreeSurface(frame);
    if (!s) return;
    if (focusOn && focusShown) {
        const Uint32 ring = SDL_MapRGB(s->format, 255, 210, 0);
        const SDL_Rect r = selectorRect();
        const int x = r.x, y = r.y, w = r.w, h = r.h;
        const SDL_Rect edges[4] = {{x, y, w, 2}, {x, y + h - 2, w, 2}, {x, y, 2, h}, {x + w - 2, y, 2, h}};
        SDL_FillRects(s, edges, 4, ring);
    }
    char path[512];
    std::snprintf(path, sizeof(path), "%s/shot-%06u.bmp", dataDir().c_str(),
                  static_cast<unsigned>(now));
    if (SDL_SaveBMP(s, path) == 0) {
        TFT_eSPI::FocusBox boxes[TFT_eSPI::MAX_FOCUS];
        std::printf("[host] saved %s, %d controls\n", path,
                    panel->focusTargets(boxes, TFT_eSPI::MAX_FOCUS));
    }
    SDL_FreeSurface(s);
}

void parseScripts() {
    if (const char* shots = std::getenv("BRAINO_SHOTS")) {
        const char* p = shots;
        while (*p && shotCount < MAX_SCRIPT) {
            char* end = nullptr;
            const unsigned long ms = std::strtoul(p, &end, 10);
            if (end == p) break;
            shotTimes[shotCount++] = static_cast<uint32_t>(ms);
            p = (*end == ',') ? end + 1 : end;
        }
    }
    if (const char* script = std::getenv("BRAINO_TAPS")) {
        const char* p = script;
        while (*p && tapCount < MAX_SCRIPT) {
            /* "ms:prev", "ms:next", "ms:up[,hold]", "ms:down[,hold]": a key
             * instead of a point. */
            unsigned long keyMs = 0;
            char word[6] = {0};
            int keyUsed = 0;
            if (std::sscanf(p, "%lu:%5[a-z]%n", &keyMs, word, &keyUsed) == 2) {
                Key key;
                if (std::strcmp(word, "prev") == 0) key = Key::Prev;
                else if (std::strcmp(word, "next") == 0) key = Key::Next;
                else if (std::strcmp(word, "up") == 0) key = Key::Up;
                else if (std::strcmp(word, "down") == 0) key = Key::Down;
                else if (std::strcmp(word, "left") == 0) key = Key::Left;
                else if (std::strcmp(word, "right") == 0) key = Key::Right;
                else if (std::strcmp(word, "menu") == 0) key = Key::Menu;
                else if (std::strcmp(word, "mute") == 0) key = Key::Mute;
                else if (std::strcmp(word, "lock") == 0) key = Key::Lock;
                else if (std::strcmp(word, "a") == 0) key = Key::A;
                else break;
                p += keyUsed;
                unsigned long keyHold = key == Key::A ? 120 : 1000;
                if (*p == ',') {
                    ++p;
                    keyHold = std::strtoul(p, const_cast<char**>(&p), 10);
                }
                if (scriptKeyCount < MAX_SCRIPT) {
                    scriptKeys[scriptKeyCount++] =
                        ScriptKey{static_cast<uint32_t>(keyMs), static_cast<uint32_t>(keyHold), key};
                }
                if (*p == ';') ++p;
                continue;
            }
            ScriptedTap t{0, 0, 0, 120};
            unsigned long ms = 0, hold = 120;
            int x = 0, y = 0, used = 0;
            const int n = std::sscanf(p, "%lu:%d,%d%n", &ms, &x, &y, &used);
            if (n < 3) break;
            p += used;
            if (*p == ',') {
                ++p;
                hold = std::strtoul(p, const_cast<char**>(&p), 10);
            }
            t.atMs = static_cast<uint32_t>(ms);
            t.x = static_cast<int16_t>(x);
            t.y = static_cast<int16_t>(y);
            t.holdMs = static_cast<uint32_t>(hold);
            taps[tapCount++] = t;
            if (*p == ';') ++p;
        }
    }
}

/* The scripted tap in progress at `now`, if any. */
const ScriptedTap* scriptedTap(uint32_t now) {
    for (int i = 0; i < tapCount; ++i) {
        if (now >= taps[i].atMs && now < taps[i].atMs + taps[i].holdMs) return &taps[i];
    }
    return nullptr;
}

void present(uint32_t now) {
    if (!panel) return;
    if (shotRequested) {
        shotRequested = false;
        saveShot(now);
    }
    while (shotNext < shotCount && now >= shotTimes[shotNext]) {
        saveShot(now);
        ++shotNext;
    }
    /* Asleep: one black frame, then nothing until it wakes. The lamp is off
     * (HostScreenPower), and drawing black sixty times a second would only
     * spend the battery the lamp just saved. */
    const bool asleep = panel->asleep();
    if (asleep && presentedAsleep) {
        lastPresentMs = now;
        return;
    }
    presentedAsleep = asleep;
    SDL_UpdateTexture(texture, nullptr, panel->frame(), FRAME_W * sizeof(uint16_t));
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    if (panel->displayOn()) {
        float level = backlight;
        if (level < 0.0f) level = 0.0f;
        if (level > 1.0f) level = 1.0f;
        const uint8_t mod = static_cast<uint8_t>(level * 255.0f + 0.5f);
        SDL_SetTextureColorMod(texture, mod, mod, mod);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    }
    if (focusOn) {
        if (focusShown) drawSelector();
    } else if (cursorShownAtMs != 0 && now - cursorShownAtMs < CURSOR_SHOW_MS) {
        drawCursor();
    }
    SDL_RenderPresent(renderer);
    lastPresentMs = now;
}

bool makeDir(const std::string& path) {
    for (size_t at = 1; at <= path.size(); ++at) {
        if (at == path.size() || path[at] == '/') {
            const std::string part = path.substr(0, at);
            if (::mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

}   // namespace

bool begin(int argc, char** argv) {
    (void)argc;
    savedArgv = argv;
    mainThread = std::this_thread::get_id();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "[host] SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    /* A handheld runs without a desktop and wants the whole panel; a PC wants
     * a window. BRAINO_FULLSCREEN=1 forces the first. */
    const char* fs = std::getenv("BRAINO_FULLSCREEN");
    const bool fullscreen = fs && fs[0] == '1';
    const uint32_t flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_RESIZABLE;
    window = SDL_CreateWindow("Braino!", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              FRAME_W * 2, FRAME_H * 2, flags);
    if (!window) {
        std::fprintf(stderr, "[host] window: %s\n", SDL_GetError());
        return false;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        std::fprintf(stderr, "[host] renderer: %s\n", SDL_GetError());
        return false;
    }
    /* Whole multiples only: the firmware draws 1px lines and 5x7 glyphs, and
     * a fractional scale smears them. The window letterboxes instead. */
    SDL_RenderSetLogicalSize(renderer, FRAME_W, FRAME_H);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING,
                                FRAME_W, FRAME_H);
    if (!texture) {
        std::fprintf(stderr, "[host] texture: %s\n", SDL_GetError());
        return false;
    }
    if (fullscreen) SDL_ShowCursor(SDL_DISABLE);

    for (int i = 0; i < SDL_NumJoysticks(); ++i) openController(i);
    parseScripts();
    return true;
}

void outputSize(int& w, int& h) {
    w = FRAME_W * 2;
    h = FRAME_H * 2;
    if (renderer) SDL_GetRendererOutputSize(renderer, &w, &h);
}

bool presentImage(const uint8_t* rgb, int w, int h) {
    static SDL_Texture* image = nullptr;
    static int imageW = 0, imageH = 0;
    pumpEvents();
    if (!renderer) return !quit;
    if (!image || imageW != w || imageH != h) {
        if (image) SDL_DestroyTexture(image);
        image = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, w, h);
        imageW = w;
        imageH = h;
        if (!image) return !quit;
    }
    SDL_UpdateTexture(image, nullptr, rgb, w * 3);
    /* Step out of the 320x240 logical size for this one picture, and back. */
    SDL_RenderSetLogicalSize(renderer, 0, 0);
    SDL_RenderSetIntegerScale(renderer, SDL_FALSE);
    SDL_RenderSetViewport(renderer, nullptr);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, image, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    SDL_RenderSetLogicalSize(renderer, FRAME_W, FRAME_H);
    SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
    return !quit;
}

void shutdown() {
    if (texture) SDL_DestroyTexture(texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    texture = nullptr;
    renderer = nullptr;
    window = nullptr;
    SDL_Quit();
}

bool service() {
    pumpEvents();
    const uint32_t now = SDL_GetTicks();
    stepCursor(now);
    scriptUp = scriptDown = scriptLeft = scriptRight = scriptA = false;
    for (int i = 0; i < scriptKeyCount; ++i) {
        const ScriptKey& k = scriptKeys[i];
        const bool held = now >= k.atMs && now < k.atMs + k.holdMs;
        if (k.key == Key::Up && held) scriptUp = true;
        if (k.key == Key::Down && held) scriptDown = true;
        if (k.key == Key::Left && held) scriptLeft = true;
        if (k.key == Key::Right && held) scriptRight = true;
        if (k.key == Key::A && held) scriptA = true;
    }
    while (scriptKeyNext < scriptKeyCount && now >= scriptKeys[scriptKeyNext].atMs) {
        const Key key = scriptKeys[scriptKeyNext++].key;
        if (wakeInstead()) continue;
        if (key == Key::Prev || key == Key::Next) pressPager(key == Key::Next);
        if (key == Key::Menu) pressMenu();
        if (key == Key::Mute) pressIcon(TFT_eSPI::Icon::Mute);
        if (key == Key::Lock) pressIcon(TFT_eSPI::Icon::Lock);
    }
    if (quitHeldSinceMs != 0 && now - quitHeldSinceMs >= QUIT_HOLD_MS && !quit) {
        std::printf("[host] Select+Start held: leaving Braino\n");
        quit = true;
    }
    if (now - lastPresentMs >= PRESENT_INTERVAL_MS) present(now);
    return !quit;
}

bool quitRequested() { return quit; }

void attachPanel(TFT_eSPI* p) { panel = p; }

Contact contact() {
    Contact c;
    const uint32_t now = SDL_GetTicks();
    if (const ScriptedTap* t = scriptedTap(now)) {
        c.down = true;
        c.x = t->x;
        c.y = t->y;
    } else if (now >= pageTap.atMs && now < pageTap.atMs + pageTap.holdMs) {
        c.down = true;
        c.x = pageTap.x;
        c.y = pageTap.y;
    } else if (drag.active) {
        c.down = true;
        c.x = drag.x;
        c.y = static_cast<int16_t>(drag.y);
    } else if (mouseDown) {
        c.down = true;
        c.x = static_cast<int16_t>(mouseX < 0 ? 0 : (mouseX >= FRAME_W ? FRAME_W - 1 : mouseX));
        c.y = static_cast<int16_t>(mouseY < 0 ? 0 : (mouseY >= FRAME_H ? FRAME_H - 1 : mouseY));
    } else if (padTouch || padStickClick || keyTouch || scriptA) {
        c.down = true;
        c.x = static_cast<int16_t>(cursorX);
        c.y = static_cast<int16_t>(cursorY);
    }
    return c;
}

/* Start is Home on its own; with Select it is the start of the quit hold, and
 * must not also send the console Home underneath it. A button pressed while
 * the panel sleeps is a BOOT press too (wakeInstead()). */
bool homeHeld() {
    return padHome || keyHome || (padStart && !padBack) || SDL_GetTicks() < wakePulseUntilMs;
}

void setBacklight(float level) { backlight = level; }

const std::string& dataDir() {
    static std::string dir;
    if (dir.empty()) {
        if (const char* env = std::getenv("BRAINO_DATA_DIR")) {
            dir = env;
        } else if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
            dir = std::string(xdg) + "/braino";
        } else if (const char* home = std::getenv("HOME")) {
            dir = std::string(home) + "/.local/share/braino";
        } else {
            dir = "./braino-data";
        }
        if (!makeDir(dir)) std::fprintf(stderr, "[host] cannot create %s\n", dir.c_str());
    }
    return dir;
}

void radioId(uint8_t out[6]) {
    static uint8_t id[6];
    static bool have = false;
    if (!have) {
        const std::string path = dataDir() + "/radio-id";
        unsigned v[6];
        FILE* f = std::fopen(path.c_str(), "r");
        if (f && std::fscanf(f, "%2x%2x%2x%2x%2x%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
            for (int i = 0; i < 6; ++i) id[i] = static_cast<uint8_t>(v[i]);
        } else {
            std::random_device rd;
            for (int i = 0; i < 6; ++i) id[i] = static_cast<uint8_t>(rd());
            if (FILE* w = std::fopen(path.c_str(), "w")) {
                std::fprintf(w, "%02x%02x%02x%02x%02x%02x\n", id[0], id[1], id[2], id[3], id[4], id[5]);
                std::fflush(w);
                ::fsync(::fileno(w));
                std::fclose(w);
            }
        }
        if (f) std::fclose(f);
        have = true;
    }
    std::memcpy(out, id, 6);
}

void restart() {
    std::fflush(stdout);
    shutdown();
    ::execv("/proc/self/exe", savedArgv);
    std::_Exit(0);
}

bool onMainThread() { return std::this_thread::get_id() == mainThread; }

}   // namespace HostPlatform
