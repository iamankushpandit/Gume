// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <cmath>
#include <cstdint>

/* The battery as a picture on one light ring: pure arithmetic, no device.
 *
 * Each LED has a fixed place on a red -> orange -> green scale, and the
 * percentage decides how far round the ring is lit, so 100% is the whole
 * gradient and 50% stops at orange. The last LED lit takes a part brightness
 * for the part of its eighth that is filled.
 *
 * With the charger in, the whole ring first breathes twice in the one colour
 * of the percentage; pulling it out opens with a mood instead. Then, either
 * way, the gauge fills up to where the battery is and holds still. With the
 * charger in, what is charged holds still and what is yet to charge flashes,
 * each LED in its own place on the scale, which is what tells charging apart
 * from a battery that is only being looked at. */
namespace HostRingGauge {

constexpr int LEDS = 8;
constexpr uint32_t GLOW_MS = 2500;
constexpr uint32_t FILL_MS = 800;
/* Well under three flashes a second, and softer than what holds still, so the
 * charged part stays the thing the eye reads. */
constexpr uint32_t CHARGE_FLASH_MS = 1200;
constexpr float CHARGE_FLASH_PEAK = 0.7f;
constexpr uint32_t FADE_MS = 1000;
constexpr float PI_F = 3.14159265f;
/* A nearly empty battery must still light something: dark means "not showing". */
constexpr float FIRST_LED_FLOOR = 0.25f;
constexpr int ANGRY_BELOW_PERCENT = 50;
constexpr uint32_t ANGRY_PULSE_MS = 500;
constexpr float HAPPY_TAIL = 4.0f;       // LEDs
constexpr float HAPPY_END_MS = 400.0f;

struct Colour {
    uint8_t r, g, b;
};

inline float mix(float a, float b, float t) { return a + (b - a) * t; }

/* 0 is red, 0.5 orange, 1 green. The orange is low in green because these
 * LEDs' green is much the stronger: an even mix reads as yellow. */
inline Colour scale(float t) {
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    if (t < 0.5f) {
        const float u = t * 2.0f;
        return {255, static_cast<uint8_t>(mix(0.0f, 90.0f, u)), 0};
    }
    const float u = (t - 0.5f) * 2.0f;
    return {static_cast<uint8_t>(mix(255.0f, 0.0f, u)), static_cast<uint8_t>(mix(90.0f, 255.0f, u)), 0};
}

inline Colour dimmed(Colour c, float level) {
    return {static_cast<uint8_t>(c.r * level), static_cast<uint8_t>(c.g * level),
            static_cast<uint8_t>(c.b * level)};
}

/* How the gauge opens, before it fills. The charger going in glows. Taking it
 * away is taken personally, by how much the console still wanted it. */
enum class Opening : uint8_t { None, Glow, Angry, Warm, Happy };

inline Opening openingForUnplug(int percent) {
    if (percent >= 100) return Opening::Happy;
    return percent >= ANGRY_BELOW_PERCENT ? Opening::Warm : Opening::Angry;
}

/* One frame of an opening, all of which end dark so that the fill starts from
 * nothing. `t` is below GLOW_MS. */
inline void renderOpening(Opening opening, int percent, uint32_t t, float fade, Colour out[LEDS]) {
    const float at = static_cast<float>(t);
    if (opening == Opening::Happy) {
        /* A green light running round the ring, twice, with a tail. */
        const float head = at * 2.0f * LEDS / GLOW_MS;
        const float ending = at + HAPPY_END_MS >= GLOW_MS ? (GLOW_MS - at) / HAPPY_END_MS : 1.0f;
        for (int i = 0; i < LEDS; ++i) {
            const float behind = std::fmod(head - static_cast<float>(i) + LEDS, static_cast<float>(LEDS));
            const float level = behind < HAPPY_TAIL ? 1.0f - behind / HAPPY_TAIL : 0.0f;
            out[i] = dimmed(scale(1.0f), level * ending * fade);
        }
        return;
    }
    Colour c{0, 0, 0};
    if (opening == Opening::Angry) {
        /* Snaps on and dies away. Two a second, and it must stay under three:
         * this is saturated red in a young player's hands. */
        const float into = std::fmod(at, static_cast<float>(ANGRY_PULSE_MS)) / ANGRY_PULSE_MS;
        c = dimmed(scale(0.0f), (1.0f - into) * (1.0f - into) * fade);
    } else if (opening == Opening::Warm) {
        /* One slow breath of orange. */
        const float s = std::sin(PI_F * at / GLOW_MS);
        c = dimmed(scale(0.5f), s * s * fade);
    } else {
        /* Two breaths in the colour of the percentage. */
        const float s = std::sin(PI_F * 2.0f * at / GLOW_MS);
        c = dimmed(scale(percent / 100.0f), s * s * fade);
    }
    for (int i = 0; i < LEDS; ++i) out[i] = c;
}

/* One ring's LEDs. `sinceMs` is how long the gauge has been up and
 * `remainingMs` how long it has left, which is the fade at the end. */
inline void render(int percent, Opening opening, bool plugged, uint32_t sinceMs, uint32_t remainingMs,
                   Colour out[LEDS]) {
    percent = percent < 0 ? 0 : (percent > 100 ? 100 : percent);
    const float fade = remainingMs >= FADE_MS ? 1.0f : static_cast<float>(remainingMs) / FADE_MS;

    uint32_t t = sinceMs;
    if (opening != Opening::None) {
        if (t < GLOW_MS) {
            renderOpening(opening, percent, t, fade, out);
            return;
        }
        t -= GLOW_MS;
    }

    const float filled = t >= FILL_MS ? 1.0f : static_cast<float>(t) / FILL_MS;
    const float lit = percent * LEDS / 100.0f * filled;
    const bool flashing = t >= FILL_MS && plugged && percent < 100;
    const float wave = std::sin(PI_F * static_cast<float>(t % CHARGE_FLASH_MS) / CHARGE_FLASH_MS);
    const float flash = CHARGE_FLASH_PEAK * wave * wave;
    for (int i = 0; i < LEDS; ++i) {
        const float share = lit - static_cast<float>(i);
        float level = share <= 0.0f ? 0.0f : (share >= 1.0f ? 1.0f : share);
        if (i == 0 && level < FIRST_LED_FLOOR) level = FIRST_LED_FLOOR;
        /* The LED part-way through keeps its filled share as a floor. */
        if (flashing && level < 1.0f && flash > level) level = flash;
        out[i] = dimmed(scale(static_cast<float>(i) / (LEDS - 1)), level * fade);
    }
}

}   // namespace HostRingGauge
