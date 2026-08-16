#pragma once

#include <Arduino.h>
#include <esp_system.h>

/* Randomness for the games that *are* randomness.
 *
 * Everything else here calls Arduino random(), and on this core that is backed
 * by the hardware RNG -- but only for as long as nobody calls randomSeed().
 * That call does the opposite of what it reads like: arduino-esp32's WMath.cpp
 * defaults s_useRandomHW to true and uses esp_random(), and randomSeed() sets
 * the flag false and switches the whole firmware to newlib rand() for the rest
 * of the boot. AppRuntime::begin() used to call it, seeded from esp_random(),
 * which looked careful and in fact downgraded every draw in all thirty games to
 * a single-seeded software PRNG.
 *
 * That call is gone, so random() is hardware-backed again. Dice and Coin Flip
 * go through here instead of relying on it, because for those two the draw is
 * the entire product: a rigged-looking die is not a cosmetic bug, and this makes
 * the guarantee local and impossible to undo from somewhere else in the tree.
 *
 * Caveat worth knowing rather than glossing: esp_random() is documented as a
 * true random source when the RF subsystem is running. This device brings Wi-Fi
 * up for NTP and can advertise a BLE beacon, so the radio is often but not
 * always on; with it off the value comes from a weaker internal entropy source.
 * For a die and a coin that is comfortably good enough, and it is strictly
 * better than the seeded LCG this replaced. */
namespace Entropy {

/** Uniform in [0, bound), or 0 if bound is 0. Never biased by the modulo. */
inline uint32_t below(uint32_t bound) {
    if (bound == 0) {
        return 0;
    }
    /* Reject the short tail at the top of the 32-bit range rather than taking
     * esp_random() % bound. For a six-sided die the bias would have been about
     * four parts in 2^32 and no child would ever see it, but a helper that is
     * only correct for small bounds is the kind that gets reused for a large
     * one. The loop retries with probability under 2^-29 for any bound a game
     * here would ask for. */
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);
    uint32_t draw;
    do {
        draw = esp_random();
    } while (draw >= limit);
    return draw % bound;
}

/** Uniform in [low, high], inclusive at both ends. */
inline uint32_t inRange(uint32_t low, uint32_t high) {
    return high <= low ? low : low + below(high - low + 1);
}

/** A fair coin. */
inline bool coin() {
    return below(2) == 0;
}

}   // namespace Entropy
