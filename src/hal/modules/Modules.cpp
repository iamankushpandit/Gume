// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

#if GUME_HAS_ATTAKY_MODULES
#include <Arduino.h>
#include <string.h>

namespace Modules {

namespace {

/* Room for every module the one board with a table could wear at once, and
 * some to spare. A driver past the end is logged and left out rather than
 * written over anything. */
constexpr uint8_t MAX_DRIVERS = 12;

/* Zero-initialised, so it is valid before any Registration runs. */
const Driver* s_drivers[MAX_DRIVERS];
uint8_t s_count;
/* Written by probeAll() on the loop task, and for a live driver by the
 * battery task; a byte, read whole. */
volatile bool s_present[MAX_DRIVERS];

const char* probeText(Probe p) {
    switch (p) {
        case Probe::Present: return "present";
        case Probe::Unknown: return "unknown";
        case Probe::Absent:
        default:             return "absent";
    }
}

/* Each slot's verdict, while probeAll() walks the table. */
struct SlotState {
    const char* slot;
    bool decided;           // found, or could not ask
    bool unknown;
    const Driver* found;
};

SlotState* slotFor(SlotState* states, uint8_t& used, const char* slot) {
    for (uint8_t i = 0; i < used; ++i) {
        if (strcmp(states[i].slot, slot) == 0) return &states[i];
    }
    states[used] = SlotState{slot, false, false, nullptr};
    return &states[used++];
}

}  // namespace

Registration::Registration(const Driver& driver) {
    if (s_count >= MAX_DRIVERS) {
        /* Before Serial is up; this reaches the log only if something has
         * opened it, which is the most that can be done this early. */
        return;
    }
    /* Kept in `order` as they arrive, so nothing downstream sorts. */
    uint8_t at = s_count;
    while (at > 0 && s_drivers[at - 1]->order > driver.order) {
        s_drivers[at] = s_drivers[at - 1];
        --at;
    }
    s_drivers[at] = &driver;
    ++s_count;
}

void probeAll() {
    SlotState slots[MAX_DRIVERS];
    uint8_t slotsUsed = 0;

    for (uint8_t i = 0; i < s_count; ++i) {
        const Driver& d = *s_drivers[i];
        SlotState* slot = d.slot != nullptr ? slotFor(slots, slotsUsed, d.slot) : nullptr;
        if (d.byElimination || d.live || d.probe == nullptr) {
            continue;   // decided below, or by its own hook
        }
        if (slot != nullptr && slot->decided) {
            s_present[i] = false;   // another module has the slot; not asked
            continue;
        }
        const Probe p = d.probe();
        s_present[i] = p == Probe::Present;
        Serial.printf("[module] %s %s\n", d.tag, probeText(p));
        if (slot != nullptr && p != Probe::Absent) {
            slot->decided = true;
            slot->unknown = p == Probe::Unknown;
            slot->found = p == Probe::Present ? &d : nullptr;
        }
    }

    for (uint8_t i = 0; i < s_count; ++i) {
        const Driver& d = *s_drivers[i];
        if (!d.byElimination) continue;
        SlotState* slot = d.slot != nullptr ? slotFor(slots, slotsUsed, d.slot) : nullptr;
        s_present[i] = slot == nullptr || !slot->decided;
        if (slot != nullptr && !slot->decided) {
            slot->found = &d;
        }
    }

    /* One line a slot, saying what it was decided to be and what that let up. */
    for (uint8_t s = 0; s < slotsUsed; ++s) {
        const SlotState& slot = slots[s];
        const char* who = slot.unknown ? "probe-error"
                        : slot.found != nullptr ? slot.found->tag : "none";
        Serial.printf("[%s] module=%s sd=%s audio=%s\n", slot.slot, who,
                      provides(ModuleCapability::Storage) ? "allowed" : "skipped",
                      provides(ModuleCapability::Sound) ? "allowed" : "skipped");
    }
}

uint8_t count() {
    return s_count;
}

const Driver& driver(uint8_t index) {
    return *s_drivers[index];
}

bool present(uint8_t index) {
    return index < s_count && s_present[index];
}

bool provides(ModuleCapability capability) {
    const uint8_t bit = static_cast<uint8_t>(capability);
    for (uint8_t i = 0; i < s_count; ++i) {
        if (s_present[i] && (s_drivers[i]->provides & bit)) return true;
    }
    return false;
}

void describe(uint8_t index, char* out, size_t cap) {
    if (cap == 0) return;
    out[0] = '\0';
    if (index >= s_count) return;
    const Driver& d = *s_drivers[index];
    if (!s_present[index]) {
        snprintf(out, cap, "Not found");
        return;
    }
    static const struct {
        ModuleCapability capability;
        const char* text;
    } WORDS[] = {
        {ModuleCapability::NavKeys, "keys"},  {ModuleCapability::Typing, "typing"},
        {ModuleCapability::Battery, "battery"}, {ModuleCapability::Storage, "SD"},
        {ModuleCapability::Sound, "sound"},
    };
    char gives[32] = "not used";
    size_t used = 0;
    for (const auto& w : WORDS) {
        if (!(d.provides & static_cast<uint8_t>(w.capability))) continue;
        used += static_cast<size_t>(snprintf(gives + used, sizeof(gives) - used, "%s%s",
                                             used == 0 ? "" : ", ", w.text));
        if (used >= sizeof(gives)) break;
    }
    /* Found by elimination is not found: say so. */
    snprintf(out, cap, "%s (%s)", d.byElimination ? "Assumed" : "Found", gives);
}

uint8_t heldNavKeys() {
    uint8_t held = 0;
    for (uint8_t i = 0; i < s_count; ++i) {
        if (s_present[i] && s_drivers[i]->heldNavKeys != nullptr) {
            held |= s_drivers[i]->heldNavKeys();
        }
    }
    return held;
}

char typedChar() {
    for (uint8_t i = 0; i < s_count; ++i) {
        if (s_present[i] && s_drivers[i]->typedChar != nullptr) {
            const char c = s_drivers[i]->typedChar();
            if (c != 0) return c;
        }
    }
    return 0;
}

bool readBattery(float& volts, uint8_t& percent) {
    for (uint8_t i = 0; i < s_count; ++i) {
        if (s_drivers[i]->readBattery == nullptr) continue;
        const bool answered = s_drivers[i]->readBattery(volts, percent);
        s_present[i] = answered;
        return answered;
    }
    return false;
}

}  // namespace Modules

#endif  /* GUME_HAS_ATTAKY_MODULES */
