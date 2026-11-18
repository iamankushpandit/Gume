// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "CinnamonGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t DARK_RED    = 0x6000;
constexpr uint16_t DARK_BLUE   = 0x0008;
constexpr uint16_t DARK_GREEN  = 0x0200;
constexpr uint16_t DARK_YELLOW = 0x8400;
constexpr uint16_t LIT_RED     = 0xF800;
constexpr uint16_t LIT_BLUE    = 0x041F;
constexpr uint16_t LIT_GREEN   = 0x07E0;
constexpr uint16_t LIT_YELLOW  = 0xFFE0;

constexpr AppScoreInfo CINNAMON_SCORE = {
    "cinnamon", "Cinnamon", "cinnamonBest", "steps", false
};

constexpr AppMetadata CINNAMON_METADATA = {
    "cinnamon",
    "Cinnamon",
    "Cinnamon Says",
    "repeat pattern",
    "Cinnamon",
    "Watch the pattern, then repeat it.",
    &CINNAMON_SCORE,
    LauncherIcon::Cinnamon,
    13,
    true,
};

/* Each pad's note, in the same order as the colours: red, blue, green,
 * yellow, rising in pitch left-to-right and top-to-bottom. The mapping is
 * fixed for the life of the game and must stay that way -- the whole reason
 * this game has sound is that the sequence becomes a tune a player can hear
 * back, and a pad whose note moved between rounds would be worse than
 * silence. The four pitches themselves live in hal/Sound.h with the rest of
 * the vocabulary, so they stay in key with the cues around them. */
Sound padSound(uint8_t pad) {
    static constexpr Sound PAD_NOTE[4] = {
        Sound::Pad1, Sound::Pad2, Sound::Pad3, Sound::Pad4
    };
    return PAD_NOTE[pad % 4];
}
}

const AppMetadata& cinnamonAppMetadata() {
    return CINNAMON_METADATA;
}

const char* CinnamonGame::title() const {
    return cinnamonAppMetadata().screenTitle != nullptr
        ? cinnamonAppMetadata().screenTitle
        : cinnamonAppMetadata().title;
}

namespace CC = CinnamonCourse;

void CinnamonGame::begin(AppContext& host) {
    bestScore_ = static_cast<uint16_t>(host.getScore(cinnamonAppMetadata().score->bestKey, 0));
    ladder_.start(CC::LEVEL_COUNT, host.courseLevel());
    climbed_ = false;
    newRun();
}

/* A run at the ladder's level, from that level's first length. */
void CinnamonGame::newRun() {
    markFullDirty();
    statusDrawn_[0] = '\0';
    runLevel_ = ladder_.level();
    won_ = false;
    length_ = 0;
    score_ = 0;
    while (length_ < CC::levelOf(runLevel_).startLength) {
        appendStep();
    }
    startShowing();
    markDirty();
}

/* One step longer; on a "new every time" level, a whole new sequence. */
void CinnamonGame::nextRound() {
    if (CC::levelOf(runLevel_).fresh) {
        const uint8_t want = static_cast<uint8_t>(length_ + 1);
        length_ = 0;
        while (length_ < want) appendStep();
    } else {
        appendStep();
    }
    startShowing();
}

uint8_t CinnamonGame::expected() const {
    return CC::expectedPad(runLevel_, sequence_, length_, inputIndex_);
}

/* A run with a sequence repeated back is worth coming back to. A failed one
 * is not: its best is saved, and the next tap starts again anyway. */
Game::Keep CinnamonGame::keepsGame() const {
    return (phase_ != Phase::Failed && score_ > 0) ? Keep::Ask : Keep::No;
}

/* The sequence is the whole game, and the player was not watching it. Half
 * a tune shown before they left, or half of it tapped back, is not something
 * they can be asked to pick up: it is shown again from its first note, at
 * the same length, and they repeat it from the start. Only the pause after
 * a good round is carried on, since what follows it is a fresh showing. */
void CinnamonGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (phase_ == Phase::Showing || phase_ == Phase::Waiting) {
        startShowing();
    } else if (phase_ == Phase::Good) {
        movedOn(nextAt_, awayMs);
    }
}

void CinnamonGame::appendStep() {
    if (length_ < CC::MAX_STEPS) {
        sequence_[length_++] = random(4);
    }
}

void CinnamonGame::startShowing() {
    phase_ = Phase::Showing;
    showIndex_ = 0;
    inputIndex_ = 0;
    litPad_ = -1;
    nextAt_ = millis() + CC::levelOf(runLevel_).offMs;
}

Rect CinnamonGame::padRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(30 + col * 150), static_cast<int16_t>(74 + row * 66), 112, 54};
}

int8_t CinnamonGame::touchedPad(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 4; ++i) {
        if (padRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

uint16_t CinnamonGame::padColor(uint8_t index, bool lit) const {
    static constexpr uint16_t dark[4] = {DARK_RED, DARK_BLUE, DARK_GREEN, DARK_YELLOW};
    static constexpr uint16_t bright[4] = {LIT_RED, LIT_BLUE, LIT_GREEN, LIT_YELLOW};
    return lit ? bright[index] : dark[index];
}

void CinnamonGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Showing && now >= nextAt_) {
        if (litPad_ >= 0) {
            litPad_ = -1;
            ++showIndex_;
            nextAt_ = now + CC::levelOf(runLevel_).offMs;  // gap between colours
            if (showIndex_ >= length_) {
                phase_ = Phase::Waiting;
                climbed_ = false;
            }
        } else if (showIndex_ < length_) {
            litPad_ = sequence_[showIndex_];
            nextAt_ = now + CC::levelOf(runLevel_).onMs;
            // Echo the pad on the case LED so the cue does not rely on the
            // screen changing brightness.
            static const uint8_t PAD_RGB[4][3] = {
                {255, 40, 40}, {40, 90, 255}, {40, 255, 90}, {255, 220, 40}
            };
            const uint8_t* c = PAD_RGB[litPad_ % 4];
            host.pulseRgb(c[0], c[1], c[2], CC::levelOf(runLevel_).onMs);
            host.playSound(padSound(litPad_));
        }
        markDirty();
        return;
    }

    if (phase_ == Phase::Good && now >= nextAt_) {
        if (ladder_.level() != runLevel_) {
            newRun();          // a step up: the new level starts its own run
        } else {
            nextRound();
            markDirty();
        }
        return;
    }

    if (phase_ == Phase::Waiting && litPad_ >= 0 && now >= nextAt_) {
        litPad_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (phase_ == Phase::Failed) {
        climbed_ = false;
        newRun();
        return;
    }

    if (phase_ != Phase::Waiting) {
        return;
    }

    const int8_t pad = touchedPad(touch.x, touch.y);
    if (pad < 0) {
        return;
    }
    /* A quick player's next tap used to be dropped while the last pad was
     * still lit, and the tap after it then failed. It is taken now, and the
     * old pad goes dark under it. */

    litPad_ = pad;
    nextAt_ = now + 120UL;
    /* The pad's own note, on the way in as well as on the way out. This is
     * the point of the game: the sequence is a tune, and a player who can
     * hear it back is not relying on remembering four colours in order. It
     * sounds before the right/wrong test so that pressing a pad always makes
     * that pad's sound -- a wrong press that stayed silent would give the
     * answer away before the screen did. */
    host.playSound(padSound(static_cast<uint8_t>(pad)));
    if (pad == expected()) {
        ++inputIndex_;
        if (inputIndex_ >= length_) {
            score_ = length_;
            const bool newBest = CC::keepsBest(runLevel_) &&
                host.saveBestScore(cinnamonAppMetadata().score->bestKey, score_, false);
            if (newBest) {
                bestScore_ = score_;
            }
            /* A round of four steps or more says the player can do this
             * level; six of them, in one run or several, is a step up. */
            Course::Ladder::Step step = Course::Ladder::Step::Stay;
            if (CC::judgeRepeated(length_) > 0) {
                step = ladder_.answer(true);
                if (step != Course::Ladder::Step::Stay) host.setCourseLevel(ladder_.level());
            }
            phase_ = Phase::Good;
            litPad_ = pad;
            nextAt_ = now + 520UL;
            host.pulseRgb(0, 255, 40, 450);
            if (length_ >= CC::MAX_STEPS && step != Course::Ladder::Step::Up) {
                /* Thirty-two steps is the whole store: the run is won, not
                 * shown again for ever. */
                won_ = true;
                phase_ = Phase::Failed;
                litPad_ = -1;
                host.playSound(Sound::Victory);
                markFullDirty();
            } else if (step == Course::Ladder::Step::Up) {
                climbed_ = true;
                host.playSound(Sound::LevelUp);
            } else {
                /* HighScore when that is a new personal length, LevelUp when
                 * it is not; both land after the pad's own note. */
                host.playSound(newBest ? Sound::HighScore : Sound::LevelUp);
            }
        }
    } else {
        const int8_t verdict = CC::judgeFailed(runLevel_, length_);
        if (verdict < 0 && ladder_.answer(false) != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
        }
        phase_ = Phase::Failed;
        /* The pad pressed goes dark; only the one that was owed is bright. */
        litPad_ = -1;
        host.pulseRgb(255, 0, 0, 600);
        host.playSound(Sound::GameOver);
        markFullDirty();   // the failure line needs a clean background
    }
    markDirty();
}

void CinnamonGame::drawPad(Ui::Renderer& tft, uint8_t index, bool lit) const {
    const Rect r = padRect(index);
    tft.fillRoundRect(r.x + 2, r.y + 3, r.w, r.h, 8, Ui::surface());
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, padColor(index, lit));
    if (lit) {
        /* A bold ring so the active pad is obvious without the rest of the
         * screen changing. Ui::text() is the ink of the ground it is drawn
         * on, so it is black on a light theme and white on a dark one; it
         * was TFT_BLACK, which was invisible on every dark palette and only
         * ever looked right because this screen forced itself light. */
        const uint16_t ring = Ui::text();
        tft.drawRoundRect(static_cast<int16_t>(r.x - 3), static_cast<int16_t>(r.y - 3),
                          static_cast<int16_t>(r.w + 6), static_cast<int16_t>(r.h + 6), 11, ring);
        tft.drawRoundRect(static_cast<int16_t>(r.x - 4), static_cast<int16_t>(r.y - 4),
                          static_cast<int16_t>(r.w + 8), static_cast<int16_t>(r.h + 8), 12, ring);
    } else {
        tft.drawRoundRect(static_cast<int16_t>(r.x - 4), static_cast<int16_t>(r.y - 4),
                          static_cast<int16_t>(r.w + 8), static_cast<int16_t>(r.h + 8), 12, Ui::bg());
        tft.drawRoundRect(static_cast<int16_t>(r.x - 3), static_cast<int16_t>(r.y - 3),
                          static_cast<int16_t>(r.w + 6), static_cast<int16_t>(r.h + 6), 11, Ui::bg());
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, Ui::outline());
    }
}

/* CINNAMON HONOURS THE THEME, like every other screen.
 *
 * It used to force Ui::Theme::Light for the duration of each render half and
 * put the palette back afterwards -- a decision taken when there were two
 * themes and the four pad colours had been picked against white. With nine
 * palettes it was indefensible: the one screen in the console that ignored
 * the owner's choice, and it announced itself by flashing white on the way
 * in from a dark launcher.
 *
 * What made it look necessary was two hard-coded colours inside drawPad --
 * a black ring and a grey outline -- rather than anything about the pads.
 * Those are Ui::text() and Ui::outline() now and the forcing is gone. The
 * four hues stay fixed: they are the game, the way a traffic light is not
 * themeable, and they are pad fills rather than text so nothing has to be
 * read off them.
 *
 * Do not reintroduce the save/force/restore. The theme is global state in
 * Ui, and on a partial repaint renderStatic() does not run at all -- so a
 * force in one half and a restore in the other leaves the whole firmware
 * drawing in this screen's palette. */
void CinnamonGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    Ui::clear(tft);
    host.drawTopBar(title());
    for (uint8_t i = 0; i < 4; ++i) {
        const bool lit = (litPad_ == i) ||
                         (phase_ == Phase::Failed && !won_ && i == expected());
        drawPad(tft, i, lit);
        litDrawn_[i] = lit;
    }
    statusDrawn_[0] = '\0';
    scoreDrawn_ = 0xFFFF;
    bestDrawn_ = 0xFFFF;
    tft.setTextDatum(TL_DATUM);
}

void CinnamonGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* Only the pads that actually changed state get touched -- and on a full
     * paint renderStatic() has just set litDrawn_[] to what it drew, so this
     * loop correctly finds nothing to do. */
    for (uint8_t i = 0; i < 4; ++i) {
        const bool lit = (litPad_ == i) ||
                         (phase_ == Phase::Failed && !won_ && i == expected());
        if (lit != litDrawn_[i]) {
            drawPad(tft, i, lit);
            litDrawn_[i] = lit;
        }
    }

    /* The two counters, each cleared to its own half of the row before it
     * is written: they used to be drawn once at the start of a run and were
     * wrong for the rest of it. */
    if (score_ != scoreDrawn_ || bestScore_ != bestDrawn_) {
        char buf[20];
        tft.fillRect(10, 36, 140, 16, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 150, 36, 140, 16, Ui::bg());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Score %u", static_cast<unsigned>(score_));
        tft.drawString(buf, 10, 36, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(bestScore_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 36, 2);
        tft.setTextDatum(TL_DATUM);
        scoreDrawn_ = score_;
        bestDrawn_ = bestScore_;
    }

    char status[CC::STATUS_MAX];
    if (phase_ == Phase::Showing) {
        /* The first showing of a new level says which level it is. */
        if (climbed_) CC::levelLine(runLevel_, status, sizeof(status));
        else snprintf(status, sizeof(status), "Watch");
    } else if (phase_ == Phase::Waiting) {
        CC::repeatLine(runLevel_, inputIndex_, length_, status, sizeof(status));
    } else if (phase_ == Phase::Good) {
        snprintf(status, sizeof(status), "Nice");
    } else if (won_) {
        snprintf(status, sizeof(status), "All %u steps! Tap to play", static_cast<unsigned>(length_));
    } else {
        snprintf(status, sizeof(status), "Oops - tap to retry");
    }

    if (strcmp(status, statusDrawn_) != 0) {
        // Repaint just the status strip, never the whole screen.
        tft.fillRect(20, 52, 280, 18, Ui::bg());
        /* Drawn from the buffer: drawLabel() takes a String. */
        tft.setTextColor((phase_ == Phase::Failed && !won_) ? Ui::error() : Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(status, 20 + 280 / 2, 52 + 18 / 2, 2);
        tft.setTextDatum(TL_DATUM);
        snprintf(statusDrawn_, sizeof(statusDrawn_), "%s", status);
    }

    if (phase_ == Phase::Failed && !won_) {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Bright pad was next", GAME_CANVAS_WIDTH / 2, 214, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
