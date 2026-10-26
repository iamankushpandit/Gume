// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Modules.h"

/* A typing keyboard: Attaky's two-half matrix keyboard, on the boards that
 * can carry one. See KeyboardProfile in include/BoardProfile.h for the
 * wiring and the layout codes.
 *
 * Its probe asks both halves at boot and sets each up -- columns
 * (P0_0..P0_4) inputs, rows (P1_0..P1_4) outputs driven LOW, the half's LED
 * (P1_5..P1_7) off. Both halves or neither: half a keyboard types half the
 * alphabet. `[keyboard] attached|absent` says which.
 *
 * Read once per frame from the loop, beside touch and the navigation keys,
 * and cheap when nobody is typing. The probe leaves every row driven LOW, so
 * a single read of a half's five columns says whether any of its keys is
 * down; only then are the rows walked one at a time -- five writes and five
 * reads -- and put back low. Idle, that is one register read per half per
 * frame, about the cost of the touch read beside it. While a key is held it
 * is about a millisecond, which a frame can carry.
 *
 * What it reports, one character per frame:
 *
 *  - A letter, lower case, or upper case while Shift is held. Each text field
 *    decides what it accepts: the name fields take capitals, as their
 *    on-screen keypad does, and the Wi-Fi password takes what was typed.
 *  - A digit, a space, or the punctuation printed on the keys, as itself.
 *  - '\b' for Delete, repeating while it is held, because clearing a
 *    mistyped password one press at a time is the case that needs it.
 *  - '\n' for Enter.
 *
 * Nothing else repeats, and two new keys in one frame report the first; at
 * typing speed against a 20ms frame the second simply arrives next frame,
 * since it is still down. A matrix with no diodes can ghost a fourth key
 * from three held in a rectangle -- nobody types like that, and Shift plus
 * one key is two.
 */

#if GUME_HAS_KEYBOARD
#include <Arduino.h>
#include <Wire.h>

namespace {

constexpr uint8_t AW9523_INPUT_P0 = 0x00;
constexpr uint8_t AW9523_OUTPUT_P0 = 0x02;
constexpr uint8_t AW9523_OUTPUT_P1 = 0x03;
constexpr uint8_t AW9523_CONFIG_P0 = 0x04;   // 1 = input, 0 = output
constexpr uint8_t AW9523_MODE_P0 = 0x12;     // 1 = GPIO, 0 = LED current
constexpr uint8_t AW9523_MODE_P1 = 0x13;
constexpr uint8_t ROWS_LOW = 0xE0;   // rows P1_0..P1_4 low, LED P1_5..P1_7 off
constexpr uint8_t COLS = 0x1F;

constexpr uint32_t DELETE_REPEAT_DELAY_MS = 450;
constexpr uint32_t DELETE_REPEAT_EVERY_MS = 90;

/* Which positions were down last frame (25 bits a half), and Delete's
 * auto-repeat. Loop task only. */
uint32_t s_down[2] = {0, 0};
uint32_t s_repeatAtMs = 0;

void aw9523Write(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

bool setupHalf(uint8_t address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() != 0) {
        return false;
    }
    /* Rows held LOW between scans, so one read of the columns says whether
     * any key on this half is down -- the scan only walks the rows when one
     * is. Level before direction, so a row is never driven to a value nobody
     * chose; the LED bits high, which is off on this common-anode part. */
    aw9523Write(address, AW9523_MODE_P0, 0xFF);
    aw9523Write(address, AW9523_MODE_P1, 0xFF);
    aw9523Write(address, AW9523_OUTPUT_P0 + 1, ROWS_LOW);
    aw9523Write(address, AW9523_CONFIG_P0, 0xFF);
    aw9523Write(address, AW9523_CONFIG_P0 + 1, 0x00);
    return true;
}

bool writeRows(uint8_t address, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(AW9523_OUTPUT_P1);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

/* The five column bits of one half, 1 = a key down in the selected rows. */
bool readColumns(uint8_t address, uint8_t& down) {
    Wire.beginTransmission(address);
    Wire.write(AW9523_INPUT_P0);
    if (Wire.endTransmission(false) != 0 ||
        Wire.requestFrom(static_cast<int>(address), 1) != 1) {
        return false;
    }
    down = static_cast<uint8_t>(~Wire.read() & COLS);   // pulled up: low = down
    return true;
}

/* Every position of one half that is down, as 25 bits: ROW r COL c is bit
 * r*5+c, matching the layout string. False on a bus error. */
bool scanHalf(uint8_t address, uint32_t& positions) {
    uint8_t any = 0;
    if (!readColumns(address, any)) {
        return false;
    }
    positions = 0;
    if (any == 0) {
        return true;
    }
    bool ok = true;
    for (uint8_t row = 0; row < 5 && ok; ++row) {
        uint8_t cols = 0;
        ok = writeRows(address, static_cast<uint8_t>(ROWS_LOW | (COLS & ~(1u << row)))) &&
             readColumns(address, cols);
        positions |= static_cast<uint32_t>(cols) << (row * 5);
    }
    /* Back to all rows low, whatever happened, or the next frame's idle test
     * would be reading a single row. */
    ok = writeRows(address, ROWS_LOW) && ok;
    return ok;
}

Modules::Probe probe() {
    const bool attached = setupHalf(BOARD.keyboard.keyboardLeft) &&
                          setupHalf(BOARD.keyboard.keyboardRight);
    Serial.printf("[keyboard] %s\n", attached ? "attached" : "absent");
    return attached ? Modules::Probe::Present : Modules::Probe::Absent;
}

char typedChar() {
    const uint8_t address[2] = {BOARD.keyboard.keyboardLeft, BOARD.keyboard.keyboardRight};
    const char* const layout[2] = {BOARD.keyboard.leftLayout, BOARD.keyboard.rightLayout};

    /* A failed read is not a key released: keep the last state, so a bus
     * glitch cannot type a character when it clears. */
    uint32_t down[2];
    for (uint8_t half = 0; half < 2; ++half) {
        if (!scanHalf(address[half], down[half])) {
            return 0;
        }
    }

    bool shift = false;
    for (uint8_t half = 0; half < 2; ++half) {
        for (uint8_t pos = 0; pos < 25; ++pos) {
            if ((down[half] >> pos & 1) && layout[half][pos] == '^') shift = true;
        }
    }

    char typed = 0;
    for (uint8_t half = 0; half < 2 && typed == 0; ++half) {
        const uint32_t fresh = down[half] & ~s_down[half];
        for (uint8_t pos = 0; pos < 25 && typed == 0; ++pos) {
            if (!(fresh >> pos & 1)) continue;
            const char key = layout[half][pos];
            if (key == '^' || key == '~') continue;
            if (key == '<') {
                typed = '\b';
                s_repeatAtMs = millis() + DELETE_REPEAT_DELAY_MS;
            } else if (key == '>') {
                typed = '\n';
            } else if (key >= 'A' && key <= 'Z') {
                typed = shift ? key : static_cast<char>(key - 'A' + 'a');
            } else {
                typed = key;
            }
        }
    }

    /* Delete, held: repeat. Found by looking for a held '<' rather than
     * remembering which position it was, so the layout stays the only
     * statement of where it is. */
    if (typed == 0) {
        bool deleteHeld = false;
        for (uint8_t half = 0; half < 2; ++half) {
            for (uint8_t pos = 0; pos < 25; ++pos) {
                if ((down[half] & s_down[half]) >> pos & 1 && layout[half][pos] == '<') {
                    deleteHeld = true;
                }
            }
        }
        if (deleteHeld && static_cast<int32_t>(millis() - s_repeatAtMs) >= 0) {
            s_repeatAtMs = millis() + DELETE_REPEAT_EVERY_MS;
            typed = '\b';
        }
    }

    s_down[0] = down[0];
    s_down[1] = down[1];
    return typed;
}

constexpr Modules::Driver DRIVER = {
    /* name           */ "Keyboard",
    /* tag            */ "keyboard",
    /* order          */ 10,
    /* provides       */ static_cast<uint8_t>(ModuleCapability::Typing),
    /* slot           */ nullptr,
    /* byElimination  */ false,
    /* live           */ false,
    /* probe          */ probe,
    /* heldNavKeys    */ nullptr,
    /* typedChar      */ typedChar,
    /* readBattery    */ nullptr,
};
const Modules::Registration REGISTRATION(DRIVER);

}  // namespace

#endif  /* GUME_HAS_KEYBOARD */
