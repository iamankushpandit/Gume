// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "StatesGame.h"
#include "engine/AppRegistry.h"

namespace {
String fitLabel(Ui::Renderer& tft, const String& in, int16_t maxW, uint8_t font) {
    if (tft.textWidth(in, font) <= maxW) return in;
    String s = in;
    while (s.length() > 1 && tft.textWidth(s + ".", font) > maxW) {
        s.remove(s.length() - 1);
    }
    return s + ".";
}

constexpr AppScoreInfo STATES_SCORE = {
    "states", "US States", "stateBest", "pts", false
};

constexpr AppMetadata STATES_METADATA = {
    "states",
    "US States",
    nullptr,
    "states & capitals",
    "US States",
    "US states and their capitals.",
    &STATES_SCORE,
    LauncherIcon::States,
    24,
    true,
};
}

const AppMetadata& statesAppMetadata() {
    return STATES_METADATA;
}

const char* StatesGame::title() const {
    return statesAppMetadata().screenTitle != nullptr
        ? statesAppMetadata().screenTitle
        : statesAppMetadata().title;
}

Rect StatesGame::tierRect() const { return Rect{132, 31, 56, 16}; }

Rect StatesGame::answerRect(uint8_t i) const {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    return Rect{static_cast<int16_t>(6 + col * 158),
                static_cast<int16_t>(150 + row * 40), 150, 36};
}

namespace SC = StatesCourse;

/* The states of the ladder's level; a deck item is a state and a direction. */
void StatesGame::startLevel() {
    states_ = 0;
    for (uint8_t i = 0; i < STATE_COUNT && states_ < sizeof(pool_); ++i) {
        if (SC::asks(ladder_.level(), STATE_FACTS[i].tier)) pool_[states_++] = i;
    }
    deck_.reset(SC::poolSize(ladder_.level(), states_));
}

bool StatesGame::canOffer(const StateFact* cand, bool close) const {
    if (cand == nullptr || cand == current_) return false;
    if (!SC::offers(ladder_.level(), cand->tier)) return false;
    const char* mine = SC::label(mode_, current_->name, current_->capital, current_->code);
    if (strcmp(SC::label(mode_, cand->name, cand->capital, cand->code), mine) == 0) return false;
    if (close && cand->code[0] != current_->code[0]) return false;
    for (uint8_t j = 0; j < OPTION_COUNT; ++j) {
        if (options_[j] == cand) return false;
    }
    return true;
}

void StatesGame::newQuestion() {
    const uint8_t level = ladder_.level();
    if (states_ == 0) { current_ = nullptr; return; }
    item_ = deck_.next(esp_random());
    const uint8_t dirs = SC::dirCount(level);
    current_ = &STATE_FACTS[pool_[(item_ / dirs) % states_]];
    mode_ = SC::dirAt(level, static_cast<uint8_t>(item_ % dirs));

    correctBtn_ = static_cast<uint8_t>(random(OPTION_COUNT));
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) options_[i] = current_;
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (i == correctBtn_) continue;
        /* Close wrong answers first where the level asks for them -- the
         * eight M states for MA -- then anything the level offers. */
        for (uint8_t pass = SC::prefersClose(level) ? 0 : 1; pass < 2 && options_[i] == current_; ++pass) {
            for (uint8_t attempt = 0; attempt < 40; ++attempt) {
                const StateFact* cand = &STATE_FACTS[random(STATE_COUNT)];
                if (!canOffer(cand, pass == 0)) continue;
                options_[i] = cand;
                break;
            }
        }
        /* Every question has three: test/host/states_course_test.cpp. */
        if (options_[i] == current_) {
            for (uint8_t k = 0; k < STATE_COUNT; ++k) {
                if (!canOffer(&STATE_FACTS[k], false)) continue;
                options_[i] = &STATE_FACTS[k];
                break;
            }
        }
    }

    selected_ = -1;
    phase_ = Phase::Asking;
    feedbackUntil_ = 0;
    markFullDirty();
}

void StatesGame::begin(AppContext& host) {
    uint8_t stored = host.courseLevel();
    if (stored == 0) {
        /* A player who climbed the old tiers starts where they were. */
        const uint8_t tier = static_cast<uint8_t>(host.getScore("stateTier", 1));
        stored = tier >= 3 ? 6 : (tier == 2 ? 4 : 1);
    }
    ladder_.start(SC::LEVEL_COUNT, stored);
    announce_ = false;
    score_ = 0;
    rounds_ = 0;
    current_ = nullptr;
    startLevel();
    newQuestion();
}



/* The feedback pause is the only clock: moved on, so the right answer is
 * still on show when the player comes back. */
void StatesGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (phase_ == Phase::Feedback) {
        movedOn(feedbackUntil_, awayMs);
    }
}

void StatesGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Feedback && now >= feedbackUntil_) {
        newQuestion();
        return;
    }
    if (!touch.justPressed) return;

    if (phase_ != Phase::Asking) return;
    /* The level button steps up one and wraps, as the tier button did; after
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

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;

        selected_ = static_cast<int8_t>(i);
        lastCorrect_ = (i == correctBtn_);
        ++rounds_;
        announce_ = false;
        if (lastCorrect_) ++score_;
        if (!lastCorrect_) deck_.missed(item_);   // before the ladder moves
        const Course::Ladder::Step step = ladder_.answer(lastCorrect_);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            startLevel();
        }
        if (step == Course::Ladder::Step::Up) {
            announce_ = true;   // named over the next question
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(Sound::LevelUp);
        } else {
            lastCorrect_ ? host.beepOk() : host.beepError();
        }
        host.saveBestScore(statesAppMetadata().score->bestKey, score_, false);
        feedbackUntil_ = now + 1600UL;
        phase_ = Phase::Feedback;
        markFullDirty();
        return;
    }
}

void StatesGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    char buf[40];
    snprintf(buf, sizeof(buf), "%u/%u", score_, rounds_);
    tft.drawString(buf, 8, 33, 2);

    snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(ladder_.level()));
    Ui::drawButton(tft, tierRect(), buf, Ui::panel(), Ui::outline(), Ui::text(), false, 1);

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No states available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    // Subject first and large, question underneath.
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::rgb(90, 170, 255), Ui::bg());
    tft.drawString(SC::subject(mode_, current_->name, current_->capital, current_->code),
                   GAME_CANVAS_WIDTH / 2, 72, 4);

    const bool done = (phase_ == Phase::Feedback);
    tft.setTextColor(done ? (lastCorrect_ ? Ui::success() : Ui::error()) : Ui::text(), Ui::bg());

    char prompt[48];
    if (done) {
        snprintf(prompt, sizeof(prompt), "%s%s", lastCorrect_ ? "Yes! " : "It is ",
                 SC::label(mode_, current_->name, current_->capital, current_->code));
    } else if (announce_) {
        SC::levelLine(ladder_.level(), prompt, sizeof(prompt));   // a step up
        tft.setTextColor(Ui::rgb(255, 200, 0), Ui::bg());
    } else {
        snprintf(prompt, sizeof(prompt), "%s", SC::question(mode_));
    }
    tft.drawString(fitLabel(tft, prompt, GAME_CANVAS_WIDTH - 16, 2), GAME_CANVAS_WIDTH / 2, 112, 2);

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const StateFact* opt = options_[i];
        if (opt == nullptr) continue;

        uint16_t fill = Ui::panel();
        uint16_t tc   = Ui::text();
        if (done) {
            if (i == correctBtn_) { fill = Ui::success(); tc = TFT_BLACK; }
            else if (i == static_cast<uint8_t>(selected_)) { fill = Ui::error(); tc = TFT_BLACK; }
        }
        const Rect r = answerRect(i);
        const char* label = SC::label(mode_, opt->name, opt->capital, opt->code);
        Ui::drawButton(tft, r, fitLabel(tft, label, r.w - 10, 2), fill, Ui::outline(), tc, false, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
