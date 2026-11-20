// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "Stops.h"

namespace Ui {
namespace {

Rect board_{0, 0, 0, 0};
Rect also_{0, 0, 0, 0};
bool boardLive_ = false;
Rect overlay_{0, 0, 0, 0};

bool empty(const Rect& r) { return r.w <= 0 || r.h <= 0; }

}   // namespace

void setBoardStops(const Rect& board, bool live, const Rect& also) {
    board_ = board;
    also_ = also;
    boardLive_ = live;
}

void clearBoardStops() {
    board_ = Rect{0, 0, 0, 0};
    also_ = Rect{0, 0, 0, 0};
}

void setOverlayStops(const Rect& overlay) { overlay_ = overlay; }

void clearOverlayStops() { overlay_ = Rect{0, 0, 0, 0}; }

StopRule stopRuleAt(int16_t cx, int16_t cy) {
    if (!empty(overlay_)) {
        return overlay_.contains(cx, cy) ? StopRule::Unstated : StopRule::Quiet;
    }
    if ((!empty(board_) && board_.contains(cx, cy)) || (!empty(also_) && also_.contains(cx, cy))) {
        return boardLive_ ? StopRule::Live : StopRule::Quiet;
    }
    return StopRule::Unstated;
}

}   // namespace Ui
