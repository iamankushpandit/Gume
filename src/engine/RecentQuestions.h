#pragma once

#include <Arduino.h>

/*
 * Keeps a question from coming back too soon, without falling into a cycle.
 *
 * A shuffle bag (permute the set, deal without replacement) avoids repeats but
 * becomes predictable: once a child has seen nine of ten, the tenth is certain.
 * This instead draws uniformly at random and simply rejects anything in a short
 * history window, so there is no fixed order to learn while recent items still
 * cannot reappear.
 *
 * Procedural games (Math, Money, ...) do not pick from a list, so they hash the
 * question's parameters into a token and use recentlyUsed()/remember(). Games
 * that pick from a table can call pickIndex() directly.
 */
class RecentQuestions {
public:
    static constexpr uint8_t DEPTH = 10;

    void reset() {
        pos_ = 0;
        filled_ = 0;
    }

    bool recentlyUsed(uint32_t token) const {
        for (uint8_t i = 0; i < filled_; ++i) {
            if (recent_[i] == token) return true;
        }
        return false;
    }

    void remember(uint32_t token) {
        recent_[pos_] = token;
        pos_ = static_cast<uint8_t>((pos_ + 1) % DEPTH);
        if (filled_ < DEPTH) ++filled_;
    }

    /**
     * A fresh index in [0, poolSize). The history window is capped at
     * poolSize - 1 so a small pool cannot exclude every option and spin.
     */
    uint16_t pickIndex(uint16_t poolSize) {
        if (poolSize == 0) return 0;
        if (poolSize == 1) return 0;

        const uint8_t window = static_cast<uint8_t>(
            min<uint16_t>(filled_, static_cast<uint16_t>(poolSize - 1)));

        for (uint8_t attempt = 0; attempt < 40; ++attempt) {
            const uint16_t candidate = static_cast<uint16_t>(random(poolSize));
            bool seen = false;
            for (uint8_t i = 0; i < window; ++i) {
                // Walk backwards from the newest entry.
                const uint8_t idx = static_cast<uint8_t>((pos_ + DEPTH - 1 - i) % DEPTH);
                if (recent_[idx] == candidate) { seen = true; break; }
            }
            if (!seen) {
                remember(candidate);
                return candidate;
            }
        }
        // Exhausted: take anything that is not the immediately previous one.
        const uint16_t fallback = static_cast<uint16_t>(random(poolSize));
        remember(fallback);
        return fallback;
    }

private:
    uint32_t recent_[DEPTH] = {};
    uint8_t pos_ = 0;
    uint8_t filled_ = 0;
};
