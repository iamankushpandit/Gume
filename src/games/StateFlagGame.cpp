// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "StateFlagGame.h"
#include "map_n_flag.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t FLAG_BG = 0xFFFF;

constexpr AppScoreInfo STATE_FLAG_SCORE = {
    "stateflags", "State Flags", "sflagBest", "pts", false
};

constexpr AppMetadata STATE_FLAG_METADATA = {
    "stateflags",
    "State Flags",
    nullptr,
    "name the flag",
    "State Flags",
    "Name the state flag, then capital.",
    &STATE_FLAG_SCORE,
    LauncherIcon::StateFlag,
    25,
    true,
};
}

const AppMetadata& stateFlagAppMetadata() {
    return STATE_FLAG_METADATA;
}

const char* StateFlagGame::title() const {
    return stateFlagAppMetadata().screenTitle != nullptr
        ? stateFlagAppMetadata().screenTitle
        : stateFlagAppMetadata().title;
}

Rect StateFlagGame::imageRect() const { return Rect{80, 48, 160, 120}; }
Rect StateFlagGame::tierRect() const  { return Rect{132, 31, 56, 16}; }

Rect StateFlagGame::answerRect(uint8_t i) const {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    return Rect{static_cast<int16_t>(6 + col * 158),
                static_cast<int16_t>(186 + row * 27), 150, 25};
}

namespace SIC = StateImageCourse;

/* The states of the ladder's level, in table order; the deck deals
 * positions in this list. */
void StateFlagGame::startLevel() {
    poolSize_ = 0;
    for (uint8_t i = 0; i < STATE_COUNT && poolSize_ < sizeof(pool_); ++i) {
        if (SIC::asks(ladder_.level(), STATE_FACTS[i].tier)) pool_[poolSize_++] = i;
    }
    deck_.reset(poolSize_);
}

bool StateFlagGame::canOffer(const StateFact* cand) const {
    if (cand == nullptr || cand == current_) return false;
    if (!SIC::offers(ladder_.level(), cand->tier)) return false;
    if (false) return false;
    // Capitals must be distinct too, since the bonus round reuses these.
    if (strcmp(cand->capital, current_->capital) == 0) return false;
    for (uint8_t j = 0; j < OPTION_COUNT; ++j) {
        if (options_[j] == cand) return false;
    }
    return true;
}

void StateFlagGame::makeOptions() {
    correctBtn_ = static_cast<uint8_t>(random(OPTION_COUNT));
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) options_[i] = current_;
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (i == correctBtn_) continue;
        for (uint8_t attempt = 0; attempt < 60; ++attempt) {
            const StateFact* cand = &STATE_FACTS[random(STATE_COUNT)];
            if (!canOffer(cand)) continue;
            options_[i] = cand;
            break;
        }
        /* Every question of every level has three to find:
         * test/host/state_image_course_test.cpp. */
        if (options_[i] == current_) {
            const uint8_t start = static_cast<uint8_t>(random(STATE_COUNT));
            for (uint8_t k = 0; k < STATE_COUNT; ++k) {
                const StateFact* cand = &STATE_FACTS[(start + k) % STATE_COUNT];
                if (!canOffer(cand)) continue;
                options_[i] = cand;
                break;
            }
        }
    }
}

/* A plain shuffle. State Flags used to force the capital off the button
 * that had just been right, which gave one of the four away; State Maps did
 * not shuffle at all, so the capital was always on that button. */


void StateFlagGame::shuffleOptionsForBonus() {
    const uint8_t oldCorrect = correctBtn_;
    for (int8_t i = static_cast<int8_t>(OPTION_COUNT - 1); i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const StateFact* tmp = options_[i];
        options_[i] = options_[j];
        options_[j] = tmp;
    }

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (options_[i] == current_) {
            correctBtn_ = i;
            break;
        }
    }

    if (correctBtn_ == oldCorrect && OPTION_COUNT > 1) {
        const uint8_t swapWith = static_cast<uint8_t>(
            (correctBtn_ + 1 + random(OPTION_COUNT - 1)) % OPTION_COUNT);
        const StateFact* tmp = options_[correctBtn_];
        options_[correctBtn_] = options_[swapWith];
        options_[swapWith] = tmp;
        correctBtn_ = swapWith;
    }
}

void StateFlagGame::newQuestion() {
    if (poolSize_ == 0) { current_ = nullptr; return; }
    item_ = deck_.next(esp_random());
    current_ = &STATE_FACTS[pool_[item_ % poolSize_]];
    selected_ = -1;
    phase_ = Phase::State;
    feedbackUntil_ = 0;
    makeOptions();
    markFullDirty();
}

void StateFlagGame::begin(AppContext& host) {
    uint8_t stored = host.courseLevel();
    if (stored == 0) {
        /* A player who climbed the old tiers starts where they were. */
        const uint8_t tier = static_cast<uint8_t>(host.getScore("sflagTier", 1));
        stored = tier >= 3 ? 3 : (tier == 2 ? 2 : 1);
    }
    ladder_.start(SIC::LEVEL_COUNT, stored);
    announce_ = false;
    score_ = 0; rounds_ = 0; capBonus_ = 0;
    startLevel();
    newQuestion();
}

/* One answer to the ladder. True on a step up, which is announced in place
 * of the beep. */
bool StateFlagGame::climb(AppContext& host, bool right) {
    if (!right) deck_.missed(item_);   // before the ladder moves
    const Course::Ladder::Step step = ladder_.answer(right);
    if (step == Course::Ladder::Step::Stay) return false;
    host.setCourseLevel(ladder_.level());
    startLevel();
    if (step != Course::Ladder::Step::Up) return false;
    announce_ = true;
    host.pulseRgb(0, 255, 40, 450);
    host.playSound(Sound::LevelUp);
    return true;
}

/* The feedback pause is the only clock: moved on, so the right answer is
 * still on show when the player comes back. */
void StateFlagGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (phase_ == Phase::FeedbackState || phase_ == Phase::FeedbackCapital) {
        movedOn(feedbackUntil_, awayMs);
    }
}

void StateFlagGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if ((phase_ == Phase::FeedbackState || phase_ == Phase::FeedbackCapital) &&
        now >= feedbackUntil_) {
        if (phase_ == Phase::FeedbackState && lastCorrect_) {
            shuffleOptionsForBonus();
            selected_ = -1;
            phase_ = Phase::CapitalBonus;
        } else {
            newQuestion();
            return;
        }
        markFullDirty();
        return;
    }

    if (!touch.justPressed) return;
    if (phase_ != Phase::State && phase_ != Phase::CapitalBonus) return;

    /* The level button steps up one and wraps, as the tier button did. After
     * the phase test, so a tap during feedback does not throw it away. */
    if (tierRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        ladder_.setLevel(ladder_.atTop() ? 1 : static_cast<uint8_t>(ladder_.level() + 1));
        host.setCourseLevel(ladder_.level());
        host.playSound(Sound::Select);
        startLevel();
        announce_ = true;
        newQuestion();
        return;
    }

    if (current_ == nullptr) return;

    /* The rows are 2px apart and the slop is 8: a button the press is really
     * inside wins before the slop is tried. */
    int8_t hit = -1;
    for (uint8_t i = 0; i < OPTION_COUNT && hit < 0; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, 0)) hit = static_cast<int8_t>(i);
    }
    for (uint8_t i = 0; i < OPTION_COUNT && hit < 0; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) hit = static_cast<int8_t>(i);
    }
    if (hit < 0) return;
    {
        const uint8_t i = static_cast<uint8_t>(hit);

        selected_ = static_cast<int8_t>(i);
        lastCorrect_ = (i == correctBtn_);

        if (phase_ == Phase::State) {
            announce_ = false;   // the level's name has stood over its first picture
            ++rounds_;
            if (lastCorrect_) ++score_;
            /* On the last level the picture alone does not move the ladder
             * up: the capital that follows does. A wrong picture counts. */
            const bool counts = !SIC::capitalCounts(ladder_.level()) || !lastCorrect_;
            const bool up = counts && climb(host, lastCorrect_);
            if (!up) lastCorrect_ ? host.beepOk() : host.beepError();
            host.saveBestScore(stateFlagAppMetadata().score->bestKey, score_, false);
            feedbackUntil_ = now + 1500UL;
            phase_ = Phase::FeedbackState;
        } else {
            if (lastCorrect_) ++capBonus_;
            const bool up = SIC::capitalCounts(ladder_.level()) && climb(host, lastCorrect_);
            if (!up) lastCorrect_ ? host.beepOk() : host.beepError();
            feedbackUntil_ = now + 1600UL;
            phase_ = Phase::FeedbackCapital;
        }
        markFullDirty();
        return;
    }
}

void StateFlagGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    char scoreBuf[16];
    snprintf(scoreBuf, sizeof(scoreBuf), "%u/%u", score_, rounds_);
    tft.drawString(scoreBuf, 8, 33, 2);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(Ui::rgb(255, 200, 0), Ui::bg());
    char bonusBuf[8];
    snprintf(bonusBuf, sizeof(bonusBuf), "+%u", capBonus_);
    tft.drawString(bonusBuf, GAME_CANVAS_WIDTH - 8, 33, 2);

    char lv[12];
    snprintf(lv, sizeof(lv), "Level %u", static_cast<unsigned>(ladder_.level()));
    Ui::drawButton(tft, tierRect(), lv, Ui::panel(), Ui::outline(), Ui::text(), false, 1);

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No states available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    const Rect fr = imageRect();
    tft.fillRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, FLAG_BG);
    tft.drawRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, Ui::outline());
    Ui::drawCountryImageScaled(tft, mnf_state_flag(current_->code), fr, FLAG_BG, 2);

    const bool capitalRound = (phase_ == Phase::CapitalBonus || phase_ == Phase::FeedbackCapital);
    tft.setTextDatum(MC_DATUM);
    char prompt[48];
    if (capitalRound) {
        snprintf(prompt, sizeof(prompt), "%s%s?",
                 SIC::capitalCounts(ladder_.level()) ? "Capital of " : "Bonus! Capital of ",
                 current_->name);
    } else if (announce_) {
        SIC::levelLine(ladder_.level(), prompt, sizeof(prompt));   // a step up
    } else {
        snprintf(prompt, sizeof(prompt), "Which state?");
    }
    tft.setTextColor((capitalRound || announce_) ? Ui::rgb(255, 200, 0) : Ui::text(), Ui::bg());
    tft.drawString(prompt, GAME_CANVAS_WIDTH / 2, 176, 2);

    const bool showingFeedback =
        (phase_ == Phase::FeedbackState || phase_ == Phase::FeedbackCapital);

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const StateFact* opt = options_[i];
        if (opt == nullptr) continue;

        uint16_t fill = Ui::panel();
        uint16_t tc   = Ui::text();
        if (showingFeedback) {
            if (i == correctBtn_) { fill = Ui::success(); tc = TFT_BLACK; }
            else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error(); tc = TFT_BLACK; }
        }

        const char* label = capitalRound ? opt->capital : opt->name;
        const Rect r = answerRect(i);
        Ui::drawButton(tft, r, label, fill, Ui::outline(), tc, false, 2);
    }

    tft.setTextDatum(TL_DATUM);
}
