// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

#include "engine/ConsoleText.h"
#include "hal/modules/Modules.h"

/* The serial console's `lights`: every light the firmware can drive, one at a
 * time, each blinking under its own name.
 *
 * It is here because "which light is that?" cannot be answered from a
 * document by somebody holding the console. A board states its status LED's
 * three pins in its profile, and a clip-on module states its own. Whether the
 * pin called red is the red light, whether a port lights its LED when it is
 * driven low or high, and whether a light somebody can see is one the
 * firmware reaches at all, are each a question for a pair of eyes -- the
 * E32R28T-1's red and green were once declared the wrong way round on
 * exactly that kind of reading. So: one step for each thing the firmware can
 * do to a light, held for a few seconds, blinking so that it stands out from
 * a light that is simply on, with the step's name on the serial log as it
 * starts. The last step is every light at rest. That is the one the test was
 * written for: whatever is still lit then is not the firmware's to put out.
 *
 * Nothing here blocks. tickLightTest() runs every frame and reaches the bus
 * only on the frames a light changes, which is a few times a second. */

namespace {

constexpr uint32_t STEP_MS = 4000;        // each step of the whole run
constexpr uint32_t ONE_STEP_MS = 12000;   // a single step asked for by number
constexpr uint32_t BLINK_MS = 300;
constexpr uint8_t STATUS_STEPS = 3;
const char* const STATUS_NAME[STATUS_STEPS] = {"Status light red", "Status light green",
                                               "Status light blue"};

}  // namespace

uint8_t BrainoApp::lightTestSteps() const {
    const uint8_t status = BOARD.hasRgbLed() ? STATUS_STEPS : 0;
    return static_cast<uint8_t>(status + Modules::lightSteps());
}

/* One step done (on) or undone, and what it is. The status LED goes through
 * the door every other colour change uses, so this is also a test of that
 * door. */
void BrainoApp::lightTestDrive(uint8_t step, bool on, char* name, size_t cap) {
    const uint8_t status = BOARD.hasRgbLed() ? STATUS_STEPS : 0;
    if (step < status) {
        snprintf(name, cap, "%s", STATUS_NAME[step]);
        board_.setRgbColor(on && step == 0 ? 255 : 0, on && step == 1 ? 255 : 0,
                           on && step == 2 ? 255 : 0);
        return;
    }
    Modules::lightStep(static_cast<uint8_t>(step - status), on, name, cap);
}

void BrainoApp::lightTestBegin(uint32_t nowMs) {
    LightTest& t = lightTest_;
    char name[48] = "Every light at rest";
    if (t.step < t.steps) {
        lightTestDrive(t.step, true, name, sizeof(name));
    }
    t.lit = true;
    t.stepAtMs = nowMs;
    t.blinkAtMs = nowMs;
    Serial.printf("[lights] %u/%u %s\n", static_cast<unsigned>(t.step + 1),
                  static_cast<unsigned>(t.steps + 1), name);
}

/* Whatever the step in hand had lit goes back to how its driver leaves it. */
void BrainoApp::lightTestEnd() {
    LightTest& t = lightTest_;
    if (t.running && t.step < t.steps) {
        char name[48];
        lightTestDrive(t.step, false, name, sizeof(name));
    }
    t.running = false;
}

void BrainoApp::tickLightTest(uint32_t nowMs) {
    LightTest& t = lightTest_;
    if (!t.running) {
        return;
    }
    if (nowMs - t.stepAtMs >= t.stepMs) {
        const bool last = t.step >= t.stopAfter;
        lightTestEnd();
        if (last) {
            Serial.println("[lights] done");
            return;
        }
        t.running = true;
        ++t.step;
        lightTestBegin(nowMs);
        return;
    }
    if (t.step < t.steps && nowMs - t.blinkAtMs >= BLINK_MS) {
        t.blinkAtMs = nowMs;
        t.lit = !t.lit;
        char name[48];
        lightTestDrive(t.step, t.lit, name, sizeof(name));
    }
}

/* `lights` runs every step in turn; `lights <step>` holds one of them for
 * longer, to look again; `lights off` stops. Steps count from 1, and the last
 * is every light at rest. The reply says how many there are; the name of each
 * comes on the log as it starts. */
void BrainoApp::cmdLights(int argc, char** argv, Print& out) {
    lightTestEnd();
    if (argc == 1 && strcmp(argv[0], "off") == 0) {
        out.println("ok lights=\"off\"");
        return;
    }
    const uint8_t steps = lightTestSteps();
    long only = 0;
    if (argc == 1 && !ConsoleText::parseRange(argv[0], 1, steps + 1, only)) {
        out.printf("err range lights takes a step from 1 to %u, or off\n",
                   static_cast<unsigned>(steps + 1));
        return;
    }
    LightTest& t = lightTest_;
    t = LightTest{};
    t.running = true;
    t.steps = steps;
    t.step = argc == 1 ? static_cast<uint8_t>(only - 1) : 0;
    t.stopAfter = argc == 1 ? t.step : steps;
    t.stepMs = argc == 1 ? ONE_STEP_MS : STEP_MS;
    out.printf("ok lights=\"%u\" first=\"%u\" last=\"%u\" step_ms=\"%lu\"\n",
               static_cast<unsigned>(steps + 1), static_cast<unsigned>(t.step + 1),
               static_cast<unsigned>(t.stopAfter + 1), static_cast<unsigned long>(t.stepMs));
    lightTestBegin(millis());
}
