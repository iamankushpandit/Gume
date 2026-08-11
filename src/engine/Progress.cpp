#include "Progress.h"

void Progress::begin(Board& board, const char* key, uint16_t count) {
    board_ = &board;
    key_ = key;
    count_ = count > MAX_ITEMS ? MAX_ITEMS : count;
    dirty_ = false;
    memset(scores_, 0, sizeof(scores_));
    board.loadBlob(key_, scores_, count_);
}

void Progress::record(uint16_t index, bool correct) {
    if (index >= count_) return;
    int16_t v = scores_[index];
    // A miss costs twice what a correct answer earns, so a wrong item comes
    // back around soon and only leaves the rotation after repeated successes.
    v += correct ? 1 : -2;
    if (v > SCORE_MAX) v = SCORE_MAX;
    if (v < SCORE_MIN) v = SCORE_MIN;
    scores_[index] = static_cast<int8_t>(v);
    dirty_ = true;
}

void Progress::flush() {
    if (!dirty_ || board_ == nullptr || key_ == nullptr) return;
    board_->saveBlob(key_, scores_, count_);
    dirty_ = false;
}

int8_t Progress::score(uint16_t index) const {
    return index < count_ ? scores_[index] : 0;
}

uint8_t Progress::weight(uint16_t index) const {
    const int8_t s = score(index);
    if (s <= -4) return 8;
    if (s <= -1) return 6;
    if (s == 0)  return 3;      // unseen: show it, but do not swamp the mix
    if (s <= 2)  return 2;
    return 1;                   // mastered: still appears, just rarely
}

uint16_t Progress::pickWeighted(bool (*allowed)(uint16_t, void*), void* ctx) const {
    uint32_t total = 0;
    for (uint16_t i = 0; i < count_; ++i) {
        if (allowed && !allowed(i, ctx)) continue;
        total += weight(i);
    }
    if (total == 0) return 0xFFFF;

    uint32_t roll = static_cast<uint32_t>(random(static_cast<long>(total)));
    for (uint16_t i = 0; i < count_; ++i) {
        if (allowed && !allowed(i, ctx)) continue;
        const uint32_t w = weight(i);
        if (roll < w) return i;
        roll -= w;
    }
    return 0xFFFF;
}

uint8_t Progress::masteryPercent() const {
    if (count_ == 0) return 0;
    uint16_t seen = 0;
    uint16_t known = 0;
    for (uint16_t i = 0; i < count_; ++i) {
        if (scores_[i] == 0) continue;
        ++seen;
        if (scores_[i] > 0) ++known;
    }
    if (seen == 0) return 0;
    return static_cast<uint8_t>((known * 100UL) / seen);
}
