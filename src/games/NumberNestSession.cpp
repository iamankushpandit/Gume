// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "NumberNestRules.h"

/* The flow of one sitting -- levels, taps, the gate in front of them -- and
 * what the screen should show for it. Pure, like NumberNestRules.cpp, and
 * declared in the same header; kept apart so that file stays about building
 * puzzles and this one about playing them. */
namespace Nest {

namespace {

/* Unsigned subtraction, so a millisecond counter that wraps still measures
 * the interval correctly. */
bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
    return static_cast<uint32_t>(now - since) >= duration;
}

void bumpSaturating(uint8_t& counter) {
    if (counter < 0xFF) {
        ++counter;
    }
}

}  // namespace

/* ---- levels ------------------------------------------------------------
 *
 * How a level is earned, applied and stepped by hand. Stated in
 * NumberNestRules.h beside Progress; here because it is part of how a sitting
 * goes rather than of how a nest is built. */

void resetProgress(Progress& p) {
    for (uint8_t i = 0; i < OP_COUNT; ++i) {
        p.level[i] = 1;
        p.run[i] = 0;
        p.earned[i] = false;
    }
}

bool recordGroup(Progress& p, Op op, bool firstTry) {
    const uint8_t i = opIndex(op);
    if (!firstTry) {
        /* The run is lost. A promotion already earned is not: six in a row
         * happened, and that is the whole of the criterion. */
        p.run[i] = 0;
        return false;
    }
    if (p.run[i] < 0xFF) {
        ++p.run[i];
    }
    p.level[i] = clampLevel(p.level[i]);
    if (p.run[i] >= PROMOTE_RUN && p.level[i] < LEVEL_COUNT && !p.earned[i]) {
        p.earned[i] = true;
        p.run[i] = 0;
        return true;
    }
    return false;
}

bool finishNest(Progress& p, Op op) {
    const uint8_t i = opIndex(op);
    if (!p.earned[i]) {
        return false;
    }
    p.earned[i] = false;
    p.level[i] = clampLevel(p.level[i]);
    if (p.level[i] >= LEVEL_COUNT) {
        return false;
    }
    ++p.level[i];
    /* Whatever was answered since the promotion was earned was answered at
     * the old level. It is not a head start on the next one. */
    p.run[i] = 0;
    return true;
}

uint8_t cycleLevel(Progress& p, Op op) {
    const uint8_t i = opIndex(op);
    p.level[i] = static_cast<uint8_t>(clampLevel(p.level[i]) % LEVEL_COUNT + 1);
    p.run[i] = 0;
    p.earned[i] = false;
    return p.level[i];
}

void resetSession(Session& s) {
    resetProgress(s.progress);
    s.puzzle = fallbackPuzzle(Op::Add, 1);
    s.op = Op::Add;
    s.phase = Phase::Play;
    s.stage = 0;
    s.collected = 0;
    s.slips = 0;
    s.mistakes = 0;
    s.wrongIndex = -1;
    s.armed = false;
    s.promoted = false;
    s.fingerUp = false;
    s.upSince = 0;
    s.wrongSince = 0;
    s.phaseSince = 0;
}

void deal(Session& s, uint32_t seed, uint32_t now) {
    const uint8_t i = opIndex(s.op);
    s.progress.level[i] = clampLevel(s.progress.level[i]);
    generate(s.puzzle, s.op, s.progress.level[i], seed);
    s.phase = Phase::Play;
    s.stage = 0;
    s.collected = 0;
    s.slips = 0;
    s.mistakes = 0;
    s.wrongIndex = -1;
    /* Whatever finger asked for this nest is still on the glass. */
    s.armed = false;
    s.promoted = false;
    s.wrongSince = now;
    s.phaseSince = now;
}

bool chooseOp(Session& s, Op op, uint32_t seed, uint32_t now) {
    if (op == s.op) {
        return false;
    }
    /* Only the operation changes hands. Both levels and both runs stay as
     * they are: what was earned in one is not spent by visiting the other. */
    s.op = op;
    deal(s, seed, now);
    return true;
}

void chooseNextLevel(Session& s, uint32_t seed, uint32_t now) {
    cycleLevel(s.progress, s.op);
    deal(s, seed, now);
}

const Group& currentGroup(const Session& s) {
    return s.stage < GROUP_SIZE ? s.puzzle.prelim[s.stage] : s.puzzle.final;
}

bool controlsArmed(const Session& s, uint32_t now) {
    return s.armed && elapsed(now, s.phaseSince, SETTLE_MS);
}

Tap tapNumber(Session& s, uint8_t index, uint32_t now) {
    if (s.phase != Phase::Play || index >= GROUP_SIZE || !controlsArmed(s, now)) {
        return Tap::Ignored;
    }

    /* EVERY ACCEPTED TAP CLOSES THE GATE BEHIND IT. The press that made it
     * is still down, and a resistive contact that flickers reads as a second
     * press at the same place: without this, one held press on a wrong
     * number was two slips, two error cues and "2 slips on the way". The
     * next tap needs the finger off the glass for RELEASE_MS first. */
    s.armed = false;

    const Group& group = currentGroup(s);
    if (index != group.rel.answer) {
        bumpSaturating(s.slips);
        bumpSaturating(s.mistakes);
        /* The run ends here, not when the group is finally solved: a player
         * who slipped and then walked away from the group -- switched mode,
         * stepped the level -- would otherwise keep a run with a wrong tap
         * in it. A promotion already earned is not touched. */
        s.progress.run[opIndex(s.op)] = 0;
        s.wrongIndex = static_cast<int8_t>(index);
        s.wrongSince = now;
        return Tap::Wrong;
    }

    /* Right at the first tap is what counts toward the level; right after a
     * slip is still right, and still moves the nest on. */
    recordGroup(s.progress, s.op, s.slips == 0);
    s.wrongIndex = -1;
    s.phaseSince = now;

    if (s.stage >= FINAL_STAGE) {
        /* The one place a level moves by itself: when a nest is finished.
         * Until then the nest on the screen and the level shown beside it
         * are the level it was dealt at. */
        s.promoted = finishNest(s.progress, s.op);
        s.phase = Phase::Done;
        return Tap::Finished;
    }
    s.collected = static_cast<uint8_t>(s.stage + 1);
    s.phase = Phase::Reveal;
    return Tap::Right;
}

bool tick(Session& s, uint32_t now, bool fingerDown) {
    bool changed = false;

    if (s.wrongIndex >= 0 && elapsed(now, s.wrongSince, WRONG_MS)) {
        s.wrongIndex = -1;
        changed = true;
    }

    if (s.phase == Phase::Reveal && elapsed(now, s.phaseSince, REVEAL_MS)) {
        ++s.stage;
        s.slips = 0;
        s.phase = Phase::Play;
        s.phaseSince = now;
        s.armed = false;
        changed = true;
    }

    /* After the change above, so a finger resting on the glass as the next
     * group appears leaves it unarmed until it lifts. */
    watchFinger(s, now, fingerDown);
    return changed;
}

void watchFinger(Session& s, uint32_t now, bool fingerDown) {
    if (fingerDown) {
        s.fingerUp = false;
        return;
    }
    if (!s.fingerUp) {
        s.fingerUp = true;
        s.upSince = now;
    }
    /* Continuously up for RELEASE_MS. A finger that was already well clear
     * when the group appeared arms it on the very next look; one that was
     * resting on the glass has to lift and stay lifted. */
    if (!s.armed && elapsed(now, s.upSince, RELEASE_MS)) {
        s.armed = true;
    }
}

void holdInput(Session& s, uint32_t now) {
    s.armed = false;
    s.phaseSince = now;
    /* A wrong mark belongs to the picture it was made on. The nest's clock
     * is stopped while the rules page is up, so a mark left standing would
     * still be there when the page closed -- feedback for a tap made however
     * long ago the page was opened. */
    s.wrongIndex = -1;
}

View viewOf(const Session& s) {
    View v{};
    v.op = opIndex(s.op);
    v.level = clampLevel(s.progress.level[v.op]);
    v.stage = s.stage;
    v.mistakes = s.mistakes;
    v.done = s.phase == Phase::Done;

    const Group& group = currentGroup(s);
    const bool showing = s.phase != Phase::Play;
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        v.number[i] = group.value[i];
        NumberLook look = NumberLook::Idle;
        if (showing && i == group.rel.answer) {
            look = NumberLook::Answer;
        } else if (showing && (i == group.rel.first || i == group.rel.second)) {
            look = NumberLook::Operand;
        } else if (s.wrongIndex == static_cast<int8_t>(i)) {
            look = NumberLook::Wrong;
        }
        v.numberLook[i] = static_cast<uint8_t>(look);

        SlotLook slot = SlotLook::Empty;
        if (i < s.collected) {
            v.slot[i] = s.puzzle.final.value[i];
            const bool justIn = s.phase == Phase::Reveal && i + 1 == s.collected;
            slot = justIn ? SlotLook::Fresh : SlotLook::Filled;
        }
        v.slotLook[i] = static_cast<uint8_t>(slot);
    }

    const uint16_t a = group.value[group.rel.first];
    const uint16_t b = group.value[group.rel.second];
    Status status = Status::Prompt;
    if (s.phase == Phase::Done) {
        status = s.promoted ? Status::LevelUp
                            : (s.mistakes == 0 ? Status::CompleteClean : Status::Complete);
    } else if (s.phase == Phase::Reveal) {
        status = Status::Equation;
    } else if (s.wrongIndex >= 0) {
        status = Status::TryAgain;
    }
    v.status = static_cast<uint8_t>(status);
    if (showing) {
        v.equation[0] = a < b ? a : b;
        v.equation[1] = a < b ? b : a;
        v.equation[2] = group.value[group.rel.answer];
    }
    return v;
}

uint16_t changedRegions(const View& was, const View& now) {
    uint16_t mask = 0;
    if (was.op != now.op) {
        /* The prompt names the operation, so it moves with the mode. */
        mask |= REGION_MODE | REGION_STATUS;
    }
    if (was.level != now.level) {
        mask |= REGION_LEVEL;
    }
    if (was.stage != now.stage || was.done != now.done) {
        mask |= REGION_CAPTION;
    }
    if (now.done && (was.equation[0] != now.equation[0] || was.equation[1] != now.equation[1] ||
                     was.equation[2] != now.equation[2])) {
        mask |= REGION_CAPTION;   // a finished nest's caption states its equation
    }
    if (was.done != now.done) {
        mask |= REGION_FOOTER;
    }
    if (was.status != now.status) {
        mask |= REGION_STATUS;
    } else {
        const Status status = static_cast<Status>(now.status);
        const bool final = (was.stage >= FINAL_STAGE) != (now.stage >= FINAL_STAGE);
        if (status == Status::Prompt && final) {
            mask |= REGION_STATUS;   // "Final group: ..." reads differently
        }
        if (status == Status::Equation &&
            (was.equation[0] != now.equation[0] || was.equation[1] != now.equation[1] ||
             was.equation[2] != now.equation[2])) {
            mask |= REGION_STATUS;
        }
        if ((status == Status::Complete || status == Status::LevelUp) &&
            (was.mistakes != now.mistakes || was.level != now.level)) {
            mask |= REGION_STATUS;
        }
    }
    for (uint8_t i = 0; i < GROUP_SIZE; ++i) {
        if (was.number[i] != now.number[i] || was.numberLook[i] != now.numberLook[i]) {
            mask |= numberRegion(i);
        }
        if (was.slot[i] != now.slot[i] || was.slotLook[i] != now.slotLook[i]) {
            mask |= slotRegion(i);
        }
    }
    return mask;
}

}  // namespace Nest
