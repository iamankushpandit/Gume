// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "NumberNestGame.h"
#include "NumberNestLayout.h"
#include "engine/AppRegistry.h"
#include "engine/Entropy.h"

/* The screen's life and its input. How each region is painted is in
 * NumberNestDraw.cpp; where everything sits is in NumberNestLayout.h. */

using namespace NumberNestLayout;

namespace {

constexpr AppScoreInfo NUMBERNEST_SCORE = {
    "numbernest", "NumberNest", "nnBest", "streak", false
};

constexpr AppMetadata NUMBERNEST_METADATA = {
    "numbernest",
    "NumberNest",
    nullptr,
    "add & multiply",
    "NumberNest",
    "Spot the number made from two others.",
    &NUMBERNEST_SCORE,
    LauncherIcon::NumberNest,
    40,
    true,
};

/* Per-player storage. Plain keys; the profile prefix is Board's business.
 * Eleven characters or fewer each, which is what the best-score key needs. */
constexpr const char* KEY_LEVEL[Nest::OP_COUNT] = {"nnAddLvl", "nnMulLvl"};
constexpr const char* KEY_RULES = "nnSeen";

/* What KEY_RULES holds. Zero is "nothing stored", and it is also all a guest
 * can ever read back, because a guest's writes are dropped. */
constexpr uint32_t RULES_NOT_YET = 2;   // has been here; has not closed the page
constexpr uint32_t RULES_READ = 1;

}  // namespace

const AppMetadata& numberNestAppMetadata() {
    return NUMBERNEST_METADATA;
}

const char* NumberNestGame::title() const {
    return numberNestAppMetadata().title;
}

uint32_t NumberNestGame::freshSeed() const {
    /* Hardware entropy, as Backgammon and Go seed their rules. The seed
     * fixes the whole nest; see NumberNestRules.h. */
    return Entropy::below(0xFFFFFFFFU);
}

void NumberNestGame::begin(AppContext& host) {
    Nest::resetSession(session_);
    /* The level each operation has reached is the one thing kept between
     * visits, read here once and mirrored in the session. A player with
     * nothing stored -- and a guest, whose writes are dropped -- reads the
     * fallback and starts both operations at the easiest level. */
    for (uint8_t i = 0; i < Nest::OP_COUNT; ++i) {
        session_.progress.level[i] = Nest::clampLevel(host.getScore(KEY_LEVEL[i], 1));
    }
    const uint32_t storedBest = host.getScore(numberNestAppMetadata().score->bestKey, 0);
    best_ = static_cast<uint16_t>(storedBest > 0xFFFF ? 0xFFFF : storedBest);
    run_ = 0;

    /* The rules page is shown to each player once, and remembered per
     * player. A screen is not told who is playing, and a guest keeps
     * nothing -- so a first visit writes "been here, not read yet" and reads
     * it back. If nothing came back the writes are going nowhere: this is a
     * guest, and the page a guest has already closed since boot is not shown
     * again. One write per player, ever, on the way into the screen. */
    uint32_t rules = host.getScore(KEY_RULES, 0);
    if (rules == 0) {
        host.setScore(KEY_RULES, RULES_NOT_YET);
        rules = host.getScore(KEY_RULES, 0);
    }
    keepsNothing_ = rules == 0;
    showRules_ = keepsNothing_ ? !guestSawRules_ : rules != RULES_READ;
    rulesUnread_ = showRules_;

    Nest::deal(session_, freshSeed(), millis());
    paintedValid_ = false;
    markDirty();
}

void NumberNestGame::saveLevel(AppContext& host, Nest::Op op) {
    const uint8_t i = Nest::opIndex(op);
    host.setScore(KEY_LEVEL[i], session_.progress.level[i]);
}

void NumberNestGame::switchTo(AppContext& host, Nest::Op op, uint32_t now) {
    /* chooseOp() deals a fresh nest and leaves the session unarmed, so the
     * press that switched cannot also answer what it brought up. Tapping the
     * operation already in play changes nothing and draws nothing. */
    if (!Nest::chooseOp(session_, op, freshSeed(), now)) {
        return;
    }
    host.playSound(Sound::Select);
    markDirty();
}

void NumberNestGame::closeRules(AppContext& host, uint32_t now) {
    showRules_ = false;
    if (rulesUnread_) {
        rulesUnread_ = false;
        if (keepsNothing_) {
            guestSawRules_ = true;
        } else {
            host.setScore(KEY_RULES, RULES_READ);
        }
    }
    /* The press that closed the page is still down. */
    Nest::holdInput(session_, now);
    host.playSound(Sound::Select);
    markFullDirty();
}

void NumberNestGame::answer(AppContext& host, uint8_t index, uint32_t now) {
    const bool firstTry = session_.slips == 0;
    const uint8_t op = Nest::opIndex(session_.op);
    const uint8_t levelBefore = session_.progress.level[op];
    const bool earnedBefore = session_.progress.earned[op];

    const Nest::Tap result = Nest::tapNumber(session_, index, now);
    if (result == Nest::Tap::Ignored) {
        return;
    }
    if (result == Nest::Tap::Wrong) {
        run_ = 0;
        host.beepError();
        markDirty();
        return;
    }

    if (firstTry) {
        if (run_ < 0xFFFF) {
            ++run_;
        }
        if (run_ > best_) {
            best_ = run_;
            host.saveBestScore(numberNestAppMetadata().score->bestKey, best_, false);
        }
    } else {
        run_ = 0;
    }

    /* Six in a row, reached part way through a nest. The level on the screen
     * waits for the nest to be finished; the stored one must not, or going
     * Home now would lose what was just earned. */
    if (!earnedBefore && session_.progress.earned[op]) {
        host.setScore(KEY_LEVEL[op], static_cast<uint32_t>(levelBefore) + 1);
    }

    /* One cue per event: the synthesiser plays one at a time, and a second
     * would cut the first short. */
    if (session_.progress.level[op] != levelBefore) {
        saveLevel(host, session_.op);
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
    } else if (result == Nest::Tap::Finished) {
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::Victory);
    } else {
        host.beepOk();
    }
    markDirty();
}

void NumberNestGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (showRules_) {
        /* The nest waits while the page is up: its timers are not ticked,
         * only the finger is watched, so the press that opened the page
         * cannot be the press that closes it. */
        Nest::watchFinger(session_, now, touch.down);
        if (touch.justPressed && Nest::controlsArmed(session_, now)) {
            closeRules(host, now);
        }
        return;
    }

    /* Timers first, and before the early return below, or a wrong mark would
     * never clear and the next group would never arrive on an idle screen.
     * Dirty only when something a player can see changed: an idle frame must
     * not reach render() at all. */
    if (Nest::tick(session_, now, touch.down)) {
        markDirty();
    }
    if (!touch.justPressed) {
        return;
    }
    /* ONE GATE FOR EVERY CONTROL. A press counts only from a finger that has
     * been off the glass for RELEASE_MS and not within SETTLE_MS of the
     * picture changing. Without it the level control was the weak one: a
     * held press that flickered stepped the level twice. tapNumber() asks
     * the same question again for the numbers, and adds the phase. */
    if (!Nest::controlsArmed(session_, now)) {
        return;
    }

    if (ADD_RECT.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        switchTo(host, Nest::Op::Add, now);
        return;
    }
    if (MUL_RECT.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        switchTo(host, Nest::Op::Mul, now);
        return;
    }
    if (LEVEL_RECT.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        /* The way down as well as up: nothing demotes a level by itself, as
         * nothing does in Flags or States, so this control is how a player
         * who has outrun themselves gets back. */
        Nest::chooseNextLevel(session_, freshSeed(), now);
        saveLevel(host, session_.op);
        host.playSound(Sound::Select);
        markDirty();
        return;
    }
    if (RULES_RECT.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        showRules_ = true;
        Nest::holdInput(session_, now);
        host.playSound(Sound::Tap);
        markFullDirty();
        return;
    }

    if (session_.phase == Nest::Phase::Done) {
        if (AGAIN_RECT.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            Nest::deal(session_, freshSeed(), now);
            host.playSound(Sound::Select);
            markDirty();
        } else if (HOME_RECT.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.goHome();   // this screen is no longer the one that is up
        }
        return;
    }

    for (uint8_t i = 0; i < Nest::GROUP_SIZE; ++i) {
        if (insideCircle(numberCx(i), NUMBER_CY, NUMBER_R + NUMBER_SLOP, touch)) {
            answer(host, i, now);
            return;
        }
    }
}
