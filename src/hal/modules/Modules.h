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
 *   NavKeys  heldNavKeys() is ORed into the navigation-key stream every frame,
 *            beside the board's own D-pad, and turned for the rotation there
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
 * none of it. Everything here runs on the loop task at boot, except
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
    /* Per frame, only if present: the NavKey bits (1 << NavKey) of the keys
     * held now, in PHYSICAL directions. */
    uint8_t (*heldNavKeys)();
    /* Per frame, only if present: a typed character, or 0. */
    char (*typedChar)();
    /* Every BATTERY_SAMPLE_MS, from the battery task, whatever boot found:
     * false when the module did not answer. */
    bool (*readBattery)(float& volts, uint8_t& percent);
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
uint8_t heldNavKeys();
char typedChar();

/* The first Battery driver's reading; sets that module's presence from the
 * answer. False when none answered, or there is no such driver. */
bool readBattery(float& volts, uint8_t& percent);

/* Whether the console may use what `capability` needs. */
inline bool allows(ModuleCapability capability) { return provides(capability); }

#else

/* No modules: nothing to look for, and nothing is gated on one. */
inline void probeAll() {}
constexpr bool allows(ModuleCapability) { return true; }

#endif

}  // namespace Modules
