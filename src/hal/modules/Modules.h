// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <stdint.h>
#include "hal/HeldControls.h"

#include "BoardConfig.h"

/* The module table: what is clipped on to this console, found at boot.
 *
 * THE PROFILE SAYS WHERE IT COULD BE; BOOT DECIDES WHETHER IT IS THERE. A
 * board that takes plug-in modules -- the Attaky Core, whose keyboard,
 * gamepad, battery and Expand modules all come off -- states in its profile
 * the facts about each one it can carry: addresses, ports, layouts. Whether
 * one is attached today is not a fact about the board, and nothing may assume
 * it: each module has a driver with a probe, the probes run once in
 * Board::begin(), and every module the firmware uses is used only if its probe
 * found it.
 *
 * One driver file per module, in this directory, holding a Driver and a
 * Registration for it. Supporting a new module is that file plus its facts in
 * the board profile: the table, the boot sequence, the input stream, the
 * battery path and both screens that list modules read it from here, so none
 * of them changes. The Registration is a static object, so it is the driver's
 * own file that puts it in the table -- there is no list to add it to.
 *
 * What a module provides is a set of Capability bits, and those decide what
 * the console does with it:
 *
 *   NavKeys  heldControls() adds what the module holds to the board's own keys
 *            every frame, before they are turned for the rotation
 *   Typing   typedChar() is the frame's typed character
 *   Battery  readBattery() is what the battery task samples
 *   Storage  the SD slot is brought up only if a module that provides it is
 *   Sound    and so is the speaker
 *
 * Modules that share lines name a slot. In a slot, the first driver (by
 * `order`) whose probe answers is the module there and the rest are not
 * asked; a probe that could not run decides the slot as "unknown", and nothing
 * in it is used, because a guess about who owns a line is how a line gets
 * driven into somebody else's chip. A driver that is `byElimination` has no
 * identity a host can read and is present only when nothing else in its slot
 * was found -- Media I/O.
 *
 * Built only where GUME_HAS_ATTAKY_MODULES is set. On every other board the
 * table does not exist, `allows()` is a constant true, and the image carries
 * none of it. Everything here runs on the loop task -- at boot, per frame, or
 * as the panel sleeps and wakes -- except
 * readBattery(), which the battery task calls (see Board::sampleModuleBattery).
 * No allocation anywhere: the table is a fixed array of pointers to the
 * drivers' own constants. */

enum class ModuleCapability : uint8_t {
    None    = 0,
    NavKeys = 1u << 0,
    Typing  = 1u << 1,
    Battery = 1u << 2,
    Storage = 1u << 3,
    Sound   = 1u << 4,
};

namespace Modules {

#if GUME_HAS_ATTAKY_MODULES

enum class Probe : uint8_t {
    Absent,
    Present,
    Unknown,     // the probe could not ask; its slot is then decided "unknown"
};

struct Driver {
    /* What System Info and About call it. */
    const char* name;
    /* One token, no spaces, for the serial log. */
    const char* tag;
    /* Probe and listing order, low first. */
    uint8_t order;
    /* ModuleCapability bits. */
    uint8_t provides;
    /* Modules that share lines name the same slot; nullptr for none. Also
     * the serial log's prefix for that slot's one-line verdict. */
    const char* slot;
    /* Present when nothing else in its slot is. Such a driver has no probe. */
    bool byElimination;
    /* Detachable while running, and asked again by its own hook rather than
     * trusted from boot: the battery module. Its presence follows
     * readBattery()'s answer. */
    bool live;

    /* Once, in Board::begin(), with the touch bus up; also sets the module up
     * when it is found. nullptr for a byElimination or a live driver. */
    Probe (*probe)();
    /* Per frame, only if present: add the controls held now to `into`, with
     * directions PHYSICAL (see hal/HeldControls.h). */
    void (*heldControls)(HeldControls& into);
    /* Per frame, only if present: a typed character, or 0. */
    char (*typedChar)();
    /* Every BATTERY_SAMPLE_MS, from the battery task, whatever boot found:
     * false when the module did not answer. `percent` is the part's own
     * state of charge, or PERCENT_FROM_VOLTS for a part that measures only
     * the voltage, which the board then reads through its LiPo curve. */
    bool (*readBattery)(float& volts, uint8_t& percent);
    /* When the panel goes to sleep (true) and when it wakes (false), only if
     * present: a module with a light of its own puts it out for as long as
     * the console is dark, and back as it was. From Board::displaySleep() and
     * displayWake(), on the loop task. A module whose light the host cannot
     * reach -- the Power module's charge light -- has nothing to do here. */
    void (*panelSleep)(bool asleep);
    /* The bench's light test, the serial console's `lights`. lightSteps is
     * how many separate things this module can do to its own lights, and
     * lightStep does number `step` of them (on), or leaves that light as the
     * driver normally has it (off), and writes what the step is into `name`
     * either way. A light on a port the driver never drives is a step for
     * each level the port can be driven to, so that which level lights it is
     * seen on the module rather than read off a document. 0 and nullptr for a
     * module with no light the host can reach. Loop task, only if present. */
    uint8_t lightSteps;
    void (*lightStep)(uint8_t step, bool on, char* name, size_t cap);
};

/* A driver's file puts it in the table with one of these at namespace
 * scope. Runs before setup(), during static initialisation; the table it
 * writes is zero-initialised storage, so the order files are initialised in
 * does not matter. */
struct Registration {
    explicit Registration(const Driver& driver);
};

/* Probe everything, once, in `order`. Before the SD slot and the speaker, and
 * after the touch bus is up. */
void probeAll();

/* The table, in `order`. */
uint8_t count();
const Driver& driver(uint8_t index);
bool present(uint8_t index);

/* Whether some module that is present provides `capability`. */
bool provides(ModuleCapability capability);

/* One module's state and what it gives the console, as the screens show it:
 * "Found (keys)", "Assumed (SD, sound)", "Not found". Into the caller's
 * buffer; allocates nothing. System Info and About both read this, so the
 * two cannot describe a module differently. */
void describe(uint8_t index, char* out, size_t cap);

/* The per-frame hooks of every present module, combined. */
void heldControls(HeldControls& into);
char typedChar();

/* Tell every present module the panel has gone to sleep, or woken. */
void panelSleep(bool asleep);

/* The light test's steps over every module that is present, in table order:
 * how many there are, and one of them done (on) or undone, with its name. */
uint8_t lightSteps();
void lightStep(uint8_t step, bool on, char* name, size_t cap);

/* What a Battery driver puts in `percent` when its part knows only the
 * voltage (see Driver::readBattery). */
constexpr uint8_t PERCENT_FROM_VOLTS = 0xFF;

/* The voltage at which the battery module fitted now reads full, for a
   PERCENT_FROM_VOLTS reading; 0 means the LiPo curve's own 4.20 V. */
float gaugeFullVolts();

/* The first Battery driver's reading; sets that module's presence from the
 * answer. False when none answered, or there is no such driver. */
bool readBattery(float& volts, uint8_t& percent);

/* Whether the console may use what `capability` needs. */
inline bool allows(ModuleCapability capability) { return provides(capability); }

#if GUME_HAS_KEYBOARD
/* Whether the keyboard boot found is still answering: false once its scan
 * has failed for several frames in a row, as it does when the module comes
 * off, and true again when a scan succeeds. Costs nothing -- it counts the
 * per-frame scan's own failures. ui/TypingPanel.h brings a text field's
 * on-screen keypad back on it. Defined in KeyboardModule.cpp. */
bool keyboardAnswering();
#endif

#else

/* No modules: nothing to look for, and nothing is gated on one. */
inline void probeAll() {}
inline void panelSleep(bool) {}
inline uint8_t lightSteps() { return 0; }
inline void lightStep(uint8_t, bool, char*, size_t) {}
constexpr bool allows(ModuleCapability) { return true; }

#endif

}  // namespace Modules
