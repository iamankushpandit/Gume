// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

/* Which control does which job, worked out from the controls a console has.
 *
 * A console's profile (HostConsole.h) says what is on its case. This file
 * decides what each of those is for, and is pure arithmetic so that the
 * decision is tested on a PC (test/host/host_controls_test.cpp) for consoles
 * nobody here owns.
 *
 * THE RULE THE ORDER RESTS ON: a button is a shortcut, never the only way to
 * do something. Home, the padlock, the speaker, the gear, the tabs and both
 * page buttons are all stops for the d-pad's selector (HostTftFocus.cpp), so a
 * console with a d-pad and one button reaches every one of them. That is what
 * makes it safe to hand out jobs in order of how much they are missed and
 * stop when the buttons run out -- and it is the same rule the firmware has
 * for the BOOT key ("nothing may become reachable only through it").
 *
 * The one thing no button stands in for is the stick's crosshair, which is
 * the only way onto a board drawn as one picture (host/CLAUDE.md, "The d-pad's
 * selector"). A console without a stick does not get those games' boards, and
 * nothing here pretends otherwise.
 *
 * A control that is left over once every job has one gets no job. It is not
 * given a second copy of somebody else's: two buttons that do the same thing
 * teach a player nothing, and on the next console one of them is missing. */
namespace HostControls {

enum Control : uint8_t {
    DPad, A, B, X, Y, L1, R1, L2, R2, Select, Start, Menu,
    LStick, L3, RStick, R3,
    ControlCount
};

constexpr uint32_t bit(Control c) { return 1u << c; }
constexpr uint32_t ALL = (1u << ControlCount) - 1u;

/* In the order they are missed. Restart is one job held by two buttons: both
 * held for three seconds, so that nobody does it by accident. */
enum Task : uint8_t {
    Move,           // the selector, from control to control
    Press,          // a finger down under the selector or the crosshair
    Home,           // the BOOT key
    PagePrev, PageNext,
    Tab,            // the next tab, or Settings on a screen with none
    Lock, Mute,
    Cursor,         // the crosshair
    CursorPress,    // clicking that stick in: the same finger down
    AskBattery,     // push it over and the light ring shows the charge
    RestartA, RestartB,
    TaskCount
};

struct Assignment {
    uint8_t control[TaskCount];     // a Control, or ControlCount for "nothing does this"
    uint32_t unassigned = 0;        // controls the console has that got no job

    bool has(Task t) const { return control[t] != ControlCount; }
    bool is(Task t, Control c) const { return control[t] == c; }
    /* The job `c` does, or TaskCount. */
    Task taskOf(Control c) const {
        for (uint8_t t = 0; t < TaskCount; ++t) {
            if (control[t] == c) return static_cast<Task>(t);
        }
        return TaskCount;
    }
};

namespace detail {

/* One way of giving out a job, or a pair of jobs that only make sense
 * together (a page-back button with no page-forward is half a control). */
struct Want {
    Task task;
    Control control;
    Task task2 = TaskCount;
    Control control2 = ControlCount;
};

/* Read top to bottom: a job takes the first line whose controls the console
 * has and nothing above has taken. So the order of the JOBS is the priority,
 * and the lines under one job are where it goes when its usual button is
 * missing or busy. */
constexpr Want WANTS[] = {
    {Move, DPad},
    /* Without A the next face button presses, and Home moves along below. */
    {Press, A}, {Press, B}, {Press, X}, {Press, Y},
    {Home, B}, {Home, Start}, {Home, Select}, {Home, Menu},
    {PagePrev, L1, PageNext, R1},
    {PagePrev, L2, PageNext, R2},
    {PagePrev, Y, PageNext, X},
    {Tab, Menu}, {Tab, Start}, {Tab, Select},
    {Lock, R2}, {Lock, X},
    {Mute, L2}, {Mute, Y},
    {Cursor, LStick}, {Cursor, RStick},
    /* A second stick does nothing else, so it may ask for the battery at any
     * time; the crosshair's own stick asks only in the dark, and needs no
     * line here to do it (HostPlatform.cpp). */
    {AskBattery, RStick},
    {RestartA, Select, RestartB, Start},
};

}   // namespace detail

inline Assignment assign(uint32_t present) {
    Assignment a;
    for (uint8_t t = 0; t < TaskCount; ++t) a.control[t] = ControlCount;
    uint32_t free = present & ALL;
    for (const detail::Want& w : detail::WANTS) {
        if (a.has(w.task)) continue;
        uint32_t need = bit(w.control);
        if (w.task2 != TaskCount) need |= bit(w.control2);
        if ((free & need) != need) continue;
        a.control[w.task] = w.control;
        if (w.task2 != TaskCount) a.control[w.task2] = w.control2;
        free &= ~need;
    }
    /* The click belongs to whichever stick is the crosshair. */
    const Control click = a.is(Cursor, LStick) ? L3 : a.is(Cursor, RStick) ? R3 : ControlCount;
    if (click != ControlCount && (free & bit(click))) {
        a.control[CursorPress] = click;
        free &= ~bit(click);
    }
    a.unassigned = free;
    return a;
}

inline const char* name(Control c) {
    static const char* const NAMES[ControlCount] = {
        "dpad", "a", "b", "x", "y", "l1", "r1", "l2", "r2", "select", "start", "menu",
        "lstick", "l3", "rstick", "r3"};
    return c < ControlCount ? NAMES[c] : "none";
}

inline const char* name(Task t) {
    static const char* const NAMES[TaskCount] = {
        "move", "press", "home", "page-back", "page-on", "tab", "lock", "mute",
        "cursor", "cursor-press", "ask-battery", "restart", "restart"};
    return t < TaskCount ? NAMES[t] : "none";
}

/* The control a profile calls `word` (not NUL-terminated), if there is one. */
inline bool parseControl(const char* word, size_t len, Control& out) {
    for (uint8_t c = 0; c < ControlCount; ++c) {
        const char* n = name(static_cast<Control>(c));
        if (std::strlen(n) == len && std::strncmp(n, word, len) == 0) {
            out = static_cast<Control>(c);
            return true;
        }
    }
    return false;
}

}   // namespace HostControls
