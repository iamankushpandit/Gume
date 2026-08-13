#pragma once

#include <Arduino.h>
#include "hal/Board.h"

/*
 * Per-item mastery tracking, used for spaced repetition.
 *
 * Questions used to be drawn uniformly at random, so a child saw Brazil exactly
 * as often as Bhutan and never got extra practice on the ones they missed. This
 * keeps a small score per item and biases selection toward weak items, which is
 * what turns a quiz into actual teaching.
 *
 * Storage is one signed byte per item in a single NVS blob:
 *   > 0  answered correctly more often than not (well known)
 *   = 0  never seen
 *   < 0  recently missed (needs practice)
 *
 * Correct answers add 1 (capped), misses subtract 2 -- misses count double, so
 * an item resurfaces quickly after an error and then fades out again once it is
 * reliably known.
 */
class Progress {
public:
    static constexpr int8_t SCORE_MAX = 6;
    static constexpr int8_t SCORE_MIN = -6;

    /** `key` is the NVS blob name, `count` the number of items tracked. */
    void begin(Board& board, const char* key, uint16_t count);

    /** Record an answer for item `index`. Persisted lazily -- call flush(). */
    void record(uint16_t index, bool correct);

    /** Write pending changes to NVS. Cheap when nothing changed. */
    void flush();

    int8_t score(uint16_t index) const;

    /**
     * Weight for random selection. Weak and unseen items get picked more often
     * without ever starving the well-known ones, which keeps things varied.
     *   missed heavily -> 8, missed weakly -> 6, unseen -> 3,
     *   barely known -> 2, mastered -> 1
     */
    uint8_t weight(uint16_t index) const;

    /**
     * Pick an item by weighted random choice over the members of a pool.
     * `allowed` is called for each index; return false to exclude it.
     * Returns 0xFFFF if the pool is empty.
     */
    uint16_t pickWeighted(bool (*allowed)(uint16_t, void*), void* ctx) const;

    /** Fraction correct over the tracked items, 0..100. Used for auto levels. */
    uint8_t masteryPercent() const;

private:
    static constexpr uint16_t MAX_ITEMS = 256;

    Board* board_ = nullptr;
    const char* key_ = nullptr;
    uint16_t count_ = 0;
    bool dirty_ = false;
    int8_t scores_[MAX_ITEMS] = {};
};
