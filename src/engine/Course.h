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
#include <string.h>

/* What turns a quiz into a course: a place on a ladder that is kept, and a
 * way of dealing questions that covers the whole of a level.
 *
 * Two complaints came back from players, and they are one complaint. "The
 * same questions again" and "it starts easy every time" are both what a game
 * does when it knows nothing about the player once the screen is left: most
 * games worked a level out of the score of the visit (level = 1 + score / 5),
 * so every visit began with 3 + 4, and drew each question independently, so
 * of 81 single-digit sums a third had not come up after ninety questions
 * while others had come up four times.
 *
 * Pure C++, no Arduino: test/host/course_test.cpp compiles this file as it
 * is, and that is what proves it. Randomness is handed in as a number, so a
 * game passes esp_random() and a test passes its own.
 */
namespace Course {

/* THE PLAYER'S LEVEL IN ONE GAME.
 *
 * A meter that a right answer pushes up by one and a wrong answer pulls down
 * by two, the same weighting engine/Progress uses for a single item. Six
 * above moves the player up a level and six below moves them down one, so
 * two right in three is where a level holds a player. Nine in ten climbs a
 * level about every nine questions -- somebody who already knows their sums
 * is not kept on 3 + 4 -- and one in two sinks a level about every twelve,
 * without the player being told they failed. Down takes three misses, not
 * two: a level just reached is new, and a player needs to be allowed to get
 * it wrong while the deck brings the missed questions back.
 *
 * The level is all that is kept between visits (AppContext::courseLevel()).
 * The meter starts over each visit, so a player always has a few questions
 * at their level before it moves. */
class Ladder {
public:
    static constexpr int8_t UP_AT = 6;
    static constexpr int8_t DOWN_AT = -6;

    enum class Step : int8_t { Down = -1, Stay = 0, Up = 1 };

    /** `stored` is what AppContext::courseLevel() gave: 0 means never set. */
    void start(uint8_t levelCount, uint8_t stored) {
        count_ = levelCount == 0 ? 1 : levelCount;
        level_ = stored == 0 ? 1 : (stored > count_ ? count_ : stored);
        meter_ = 0;
    }

    uint8_t level() const { return level_; }
    uint8_t levelCount() const { return count_; }
    bool atTop() const { return level_ >= count_; }
    int8_t meter() const { return meter_; }

    /** How far to the next level, 0..100, for a strip on the screen. */
    uint8_t percentToNext() const {
        if (meter_ <= 0) return 0;
        return static_cast<uint8_t>((static_cast<int16_t>(meter_) * 100) / UP_AT);
    }

    /* One answer. The caller stores level() when the result is not Stay, and
     * deals the next question from the new level. */
    Step answer(bool correct) {
        int16_t m = static_cast<int16_t>(meter_) + (correct ? 1 : -2);
        if (m >= UP_AT) {
            if (level_ < count_) {
                ++level_;
                meter_ = 0;
                return Step::Up;
            }
            m = UP_AT;      // top of the ladder: stay full
        } else if (m <= DOWN_AT) {
            if (level_ > 1) {
                --level_;
                meter_ = 0;
                return Step::Down;
            }
            m = DOWN_AT;    // bottom: nowhere lower to go
        }
        meter_ = static_cast<int8_t>(m);
        return Step::Stay;
    }

    /** A level chosen by hand, where a game has a button for it. */
    void setLevel(uint8_t level) {
        level_ = level == 0 ? 1 : (level > count_ ? count_ : level);
        meter_ = 0;
    }

private:
    uint8_t count_ = 1;
    uint8_t level_ = 1;
    int8_t meter_ = 0;
};

/* DEALS EVERY QUESTION OF A LEVEL BEFORE ANY COMES BACK.
 *
 * engine/RecentQuestions keeps a question from returning too SOON. It does
 * nothing to make sure every question is seen, and that is the half a course
 * needs: a level is a set of things to know, and a player has not been taught
 * the six times table by being asked 6 x 7 three times and 6 x 8 never. So:
 * one bit per item, and each deal is drawn at random from the items not yet
 * dealt in this pass.
 *
 * RecentQuestions refuses to be a shuffle bag because a bag is predictable --
 * once nine of ten have gone, the tenth is certain. Three things answer that
 * here:
 *   - nothing is stored but the bits, so every pass comes in a new order;
 *   - a pass stops before its last eighth and the next one starts, so the
 *     end of a pass cannot be counted down to. The items held back are owed:
 *     they are the first the next pass deals, so nothing waits longer than
 *     two passes and every item is covered;
 *   - nothing dealt in the last few questions is dealt again across the join.
 * And one thing makes it teach: an item answered wrong is put back, and comes
 * round again after three others, until it is answered right.
 *
 * Up to 256 items, 64 bytes of bits and a few more of bookkeeping, no heap.
 * A question space too large to number (two-digit sums) keeps using
 * RecentQuestions -- there, a repeat is unlikely rather than certain. */
class QuestionDeck {
public:
    static constexpr uint16_t MAX_ITEMS = 256;
    static constexpr uint16_t NONE = 0xFFFF;
    static constexpr uint8_t RETRY_AFTER = 3;

    /** A new level, or a new game: `poolSize` items, numbered from 0. */
    void reset(uint16_t poolSize) {
        size_ = poolSize > MAX_ITEMS ? MAX_ITEMS : poolSize;
        memset(dealt_, 0, sizeof(dealt_));
        memset(owed_, 0, sizeof(owed_));
        left_ = size_;
        recentCount_ = 0;
        recentPos_ = 0;
        for (uint8_t i = 0; i < RETRIES; ++i) {
            retry_[i].item = NONE;
            retry_[i].wait = 0;
        }
    }

    uint16_t poolSize() const { return size_; }
    /** Items of this pass still to come. */
    uint16_t leftThisPass() const { return left_; }

    /* The next item. `rnd` is any 32-bit random number. */
    uint16_t next(uint32_t rnd) {
        if (size_ == 0) return 0;

        /* A missed item whose turn has come round goes first. */
        uint8_t due = RETRIES;
        for (uint8_t i = 0; i < RETRIES; ++i) {
            if (retry_[i].item == NONE) continue;
            if (retry_[i].wait > 0) {
                --retry_[i].wait;
            } else if (due == RETRIES) {
                due = i;
            }
        }
        if (due != RETRIES) {
            const uint16_t item = retry_[due].item;
            retry_[due].item = NONE;
            remember(item);
            return item;
        }

        if (left_ <= heldBack()) {
            newPass();
        }

        /* Uniform over what is not dealt, not waiting to be retried and not
         * just seen -- and among those, the items the last pass held back go
         * first. If "not just seen" leaves nothing it is the rule that gives
         * way: on a pool of three it has to. */
        Among among = Among::OwedFresh;
        uint16_t choices = countChoices(among);
        if (choices == 0) {
            among = Among::Fresh;
            choices = countChoices(among);
        }
        if (choices == 0) {
            among = Among::Any;
            choices = countChoices(among);
        }
        if (choices == 0) {
            /* Everything undealt is waiting in the retry queue. */
            newPass();
            choices = countChoices(among);
            if (choices == 0) {
                /* A pool no bigger than the queue, all of it missed: hand
                 * back the one that has waited longest. */
                return takeEarliestRetry();
            }
        }
        uint16_t pick = static_cast<uint16_t>(rnd % choices);
        for (uint16_t i = 0; i < size_; ++i) {
            if (!isChoice(i, among)) continue;
            if (pick == 0) {
                setBit(dealt_, i);
                clearBit(owed_, i);
                --left_;
                remember(i);
                return i;
            }
            --pick;
        }
        return 0;   // unreachable: `pick` is below the number of choices
    }

    /* The item just dealt was answered wrong: it comes back after
     * RETRY_AFTER others. With the queue full the one nearest its turn gives
     * up its place, which is right -- more misses than the queue holds is a
     * player on the wrong level, and the ladder is about to say so. */
    void missed(uint16_t item) {
        if (item >= size_) return;
        uint8_t slot = RETRIES;
        for (uint8_t i = 0; i < RETRIES; ++i) {
            if (retry_[i].item == item) {
                retry_[i].wait = RETRY_AFTER;
                return;
            }
            if (retry_[i].item == NONE && slot == RETRIES) slot = i;
        }
        if (slot == RETRIES) {
            slot = 0;
            for (uint8_t i = 1; i < RETRIES; ++i) {
                if (retry_[i].wait < retry_[slot].wait) slot = i;
            }
        }
        retry_[slot].item = item;
        retry_[slot].wait = RETRY_AFTER;
    }

private:
    static constexpr uint8_t RETRIES = 4;
    static constexpr uint8_t RECENT = 4;

    struct Retry {
        uint16_t item;
        uint8_t wait;
    };

    /* How many items a pass leaves undealt. An eighth, so the end of a pass
     * cannot be counted down to; none on a pool too small to spare one. */
    uint16_t heldBack() const { return static_cast<uint16_t>(size_ / 8); }

    enum class Among : uint8_t { OwedFresh, Fresh, Any };

    static void setBit(uint8_t* bits, uint16_t i) {
        bits[i >> 3] |= static_cast<uint8_t>(1U << (i & 7));
    }
    static void clearBit(uint8_t* bits, uint16_t i) {
        bits[i >> 3] &= static_cast<uint8_t>(~(1U << (i & 7)));
    }
    static bool bit(const uint8_t* bits, uint16_t i) {
        return ((bits[i >> 3] >> (i & 7)) & 1U) != 0;
    }

    /* What this pass did not deal is owed to the next one. */
    void newPass() {
        for (uint16_t i = 0; i < size_; ++i) {
            if (!bit(dealt_, i)) setBit(owed_, i);
        }
        memset(dealt_, 0, sizeof(dealt_));
        left_ = size_;
    }

    uint16_t takeEarliestRetry() {
        uint8_t best = RETRIES;
        for (uint8_t i = 0; i < RETRIES; ++i) {
            if (retry_[i].item == NONE) continue;
            if (best == RETRIES || retry_[i].wait < retry_[best].wait) best = i;
        }
        if (best == RETRIES) return 0;
        const uint16_t item = retry_[best].item;
        retry_[best].item = NONE;
        remember(item);
        return item;
    }

    bool isRecent(uint16_t i) const {
        /* A third of the pool at most. On a pool of three, "not one of the
         * last two" would leave exactly one choice every time -- a fixed
         * cycle, which is the thing this class exists to prevent. */
        uint8_t window = recentCount_;
        if (window > size_ / 3) window = static_cast<uint8_t>(size_ / 3);
        if (window == 0 && size_ > 1 && recentCount_ > 0) window = 1;
        for (uint8_t k = 0; k < window; ++k) {
            const uint8_t idx = static_cast<uint8_t>((recentPos_ + RECENT - 1 - k) % RECENT);
            if (recent_[idx] == i) return true;
        }
        return false;
    }

    bool isQueued(uint16_t i) const {
        for (uint8_t k = 0; k < RETRIES; ++k) {
            if (retry_[k].item == i) return true;
        }
        return false;
    }

    bool isChoice(uint16_t i, Among among) const {
        if (bit(dealt_, i) || isQueued(i)) return false;
        if (among == Among::Any) return true;
        if (isRecent(i)) return false;
        return among == Among::Fresh || bit(owed_, i);
    }

    uint16_t countChoices(Among among) const {
        uint16_t n = 0;
        for (uint16_t i = 0; i < size_; ++i) {
            if (isChoice(i, among)) ++n;
        }
        return n;
    }

    void remember(uint16_t item) {
        recent_[recentPos_] = item;
        recentPos_ = static_cast<uint8_t>((recentPos_ + 1) % RECENT);
        if (recentCount_ < RECENT) ++recentCount_;
    }

    uint8_t dealt_[MAX_ITEMS / 8] = {};
    uint8_t owed_[MAX_ITEMS / 8] = {};
    uint16_t size_ = 0;
    uint16_t left_ = 0;
    uint16_t recent_[RECENT] = {};
    uint8_t recentCount_ = 0;
    uint8_t recentPos_ = 0;
    Retry retry_[RETRIES] = {{NONE, 0}, {NONE, 0}, {NONE, 0}, {NONE, 0}};
};

}   // namespace Course
