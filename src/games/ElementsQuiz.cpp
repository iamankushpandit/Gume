// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ElementsGame.h"
#include "ElementData.h"
#include "engine/AppRegistry.h"

#include <cstdio>
#include <cstring>

/*
 * The quiz and the level tab. The other half of ElementsGame lives in
 * ElementsGame.cpp; split because one file would run past the ~600 line
 * ceiling CLAUDE.md sets.
 *
 * Every question is generated from ElementData, never authored. Six shapes
 * over 118 rows is upwards of seven hundred distinct questions, and -- more to
 * the point -- a question bank that cannot drift from the table it describes.
 */

namespace {

constexpr uint32_t REVEAL_MS = 1200;

/* Level tab, in the order the buttons appear. */
constexpr const char* LEVEL_LABELS[4] = { "Auto", "Easy", "Common", "All" };

}  // namespace

// ---- question construction -------------------------------------------------

namespace EC = ElementsCourse;

void ElementsGame::nextQuestion() {
    const bool wasTable = questionUsesTable();
    const uint8_t level = quizLevel();
    const uint8_t tier = activeTier();
    if (deckTier_ != tier) {
        deck_.reset(elementPoolSize(tier));
        deckTier_ = tier;
    }

    if (forcedElement_ >= 0 && forcedElement_ < static_cast<int16_t>(ELEMENT_COUNT)) {
        qElement_ = static_cast<uint8_t>(forcedElement_);
        forcedElement_ = -1;
    } else {
        /* Every element of the pool before any comes back; a missed one
         * returns after three others. */
        const uint8_t picked = elementFromPool(deck_.next(esp_random()), tier);
        qElement_ = picked == 0xFF ? 0 : picked;
    }

    /* Each element gets each of its level's kinds in turn, so knowing an
     * element's name is not taken as knowing where it sits. */
    EC::make(level, qElement_, kindTurn_[qElement_]++, esp_random(), q_);

    chosenOption_  = -1;
    pendingCell_   = 0xFF;
    revealUntilMs_ = 0;
    buildOptions();
    writePrompt();

    if (wasTable != questionUsesTable()) {
        markFullDirty();      // the chart appeared or went away
    } else {
        markDirty();
    }
}

void ElementsGame::buildOptions() {
    optionCount_ = q_.count;
    correctOption_ = 0;
    if (optionCount_ == 0) return;
    /* Option 0 of the course's question is the answer; the buttons take
     * them in a shuffled order. The state buttons used to be Solid, Liquid,
     * Gas every time, with Solid the answer nineteen times in twenty-six. */
    uint8_t order[MAX_OPTIONS] = {0, 1, 2, 3};
    for (int8_t i = static_cast<int8_t>(optionCount_ - 1); i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const uint8_t t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    for (uint8_t i = 0; i < optionCount_; ++i) {
        optionValue_[i] = q_.values[order[i]];
        if (order[i] == 0) correctOption_ = i;
    }
}

void ElementsGame::writePrompt() {
    EC::prompt(q_, prompt_, sizeof(prompt_));
}

void ElementsGame::optionLabel(uint8_t index, char* out, size_t len) const {
    if (index >= optionCount_) { out[0] = '\0'; return; }
    EC::label(q_, optionValue_[index], out, len);
}

// ---- answering -------------------------------------------------------------

void ElementsGame::answer(AppContext& host, bool correct) {
    announce_ = false;
    if (correct) {
        score_ += 10;
        ++streak_;
    } else {
        streak_ = 0;
    }
    /* The ladder moves on Auto only: a level pinned on the Level tab is the
     * player choosing the pool, not being measured. */
    Course::Ladder::Step step = Course::Ladder::Step::Stay;
    if (tierPin_ == 0) {
        if (!correct && deckTier_ == activeTier()) {
            /* The deck deals pool positions; find this element's. */
            const uint16_t n = elementPoolSize(deckTier_);
            for (uint16_t i = 0; i < n; ++i) {
                if (elementFromPool(i, deckTier_) == qElement_) { deck_.missed(i); break; }
            }
        }
        step = ladder_.answer(correct);
        if (step != Course::Ladder::Step::Stay) host.setCourseLevel(ladder_.level());
    }
    if (step == Course::Ladder::Step::Up) {
        announce_ = true;
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
    } else {
        correct ? host.beepOk() : host.beepError();
    }
    host.saveBestScore(elementsAppMetadata().score->bestKey, score_, false);
    /* Hold the right answer on screen either way. A round that only says
     * "wrong" and moves on has scored the player without teaching them. */
    revealUntilMs_ = millis() + REVEAL_MS;
    markDirty();
}

void ElementsGame::updateQuiz(AppContext& host, const TouchPoint& touch) {
    if (revealUntilMs_ != 0) return;   // mid-reveal: ignore further taps

    if (questionUsesTable()) {
        const uint8_t hit = cellHit(touch.x, touch.y);
        if (hit == 0xFF) return;
        if (hit != pendingCell_) {
            // Same two-step as Explore: a 16x14 square is smaller than a
            // fingertip, so the first tap aims and the second commits.
            pendingCell_ = hit;
            markDirty();
            return;
        }
        answer(host, hit == qElement_);
        return;
    }

    const int16_t w = host.display().width();
    for (uint8_t i = 0; i < optionCount_; ++i) {
        if (!optionRect(w, i, optionCount_).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        chosenOption_ = static_cast<int8_t>(i);
        answer(host, i == correctOption_);
        return;
    }
}

// ---- quiz rendering --------------------------------------------------------

void ElementsGame::renderQuiz(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const int16_t w = tft.width();

    uint16_t ink = Ui::text();
    if (revealUntilMs_ != 0) {
        ink = (chosenOption_ >= 0 && static_cast<uint8_t>(chosenOption_) == correctOption_)
            ? Ui::success() : Ui::text();
    }
    drawStrip(tft, prompt_, ink);

    if (questionUsesTable()) {
        if (needsFullRender()) {
            drawTable(tft, true);
            restoreA_ = 0xFF;
            restoreB_ = 0xFF;
        } else {
            if (restoreA_ != 0xFF) { drawCellResting(tft, restoreA_); restoreA_ = 0xFF; }
            if (restoreB_ != 0xFF) { drawCellResting(tft, restoreB_); restoreB_ = 0xFF; }
        }

        if (revealUntilMs_ != 0) {
            drawCell(tft, qElement_, CellStyle::Correct);
            restoreA_ = qElement_;
            if (pendingCell_ != 0xFF && pendingCell_ != qElement_) {
                drawCell(tft, pendingCell_, CellStyle::Wrong);
                restoreB_ = pendingCell_;
            }
        } else if (pendingCell_ != 0xFF) {
            drawCell(tft, pendingCell_, CellStyle::Pending);
            restoreA_ = pendingCell_;
        }

        // The confirm hint lives under the chart, where the score line sits on
        // the other question shapes.
        tft.fillRect(8, 228, static_cast<int16_t>(w - 16), 12, Ui::bg());
        if (pendingCell_ != 0xFF && revealUntilMs_ == 0) {
            tft.setTextColor(Ui::warning(), Ui::bg());
            tft.setTextDatum(TC_DATUM);
            tft.drawString("tap it again to answer", w / 2, 228, 1);
            tft.setTextDatum(TL_DATUM);
        }
        return;
    }

    char label[24];
    for (uint8_t i = 0; i < optionCount_; ++i) {
        const Rect r = optionRect(w, i, optionCount_);
        uint16_t fill = Ui::panel();
        uint16_t text = Ui::text();
        if (revealUntilMs_ != 0) {
            if (i == correctOption_) {
                fill = Ui::success(); text = TFT_BLACK;
            } else if (i == static_cast<uint8_t>(chosenOption_)) {
                fill = Ui::error();   text = TFT_WHITE;
            }
        }
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
        optionLabel(i, label, sizeof(label));
        tft.setTextColor(text, fill);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
        tft.setTextDatum(TL_DATUM);
    }
    /* Three options leave the fourth slot behind on a partial repaint. */
    for (uint8_t i = optionCount_; i < MAX_OPTIONS; ++i) {
        const Rect r = optionRect(w, i, optionCount_);
        tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    }
}

// ---- level tab -------------------------------------------------------------

void ElementsGame::updateLevel(AppContext& host, const TouchPoint& touch) {
    const int16_t w = host.display().width();
    for (uint8_t i = 0; i < 4; ++i) {
        if (!levelRect(w, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        if (tierPin_ == i) return;
        tierPin_ = i;
        host.setScore("elemTier", i);
        host.beepOk();
        // The pool just changed underneath the pending question.
        forcedElement_ = -1;
        nextQuestion();
        markFullDirty();
        return;
    }
}

void ElementsGame::renderLevel(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const int16_t w = tft.width();
    const uint8_t tier = activeTier();

    char buf[64];
    snprintf(buf, sizeof(buf), "Quiz asks about %u of 118",
             static_cast<unsigned>(elementPoolSize(tier)));
    drawStrip(tft, buf, Ui::text());

    for (uint8_t i = 0; i < 4; ++i) {
        const Rect r = levelRect(w, i);
        const bool on = (tierPin_ == i);
        const uint16_t fill = on ? Ui::rgb(45, 154, 96) : Ui::panel();
        const uint16_t ink  = on ? TFT_WHITE : Ui::text();
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
        tft.setTextColor(ink, fill);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(LEVEL_LABELS[i], r.x + 12, r.y + r.h / 2, 2);

        /* Say what each one means in elements, not in words like "tier". */
        if (i == 0) {
            snprintf(buf, sizeof(buf), "your level - %u of them",
                     static_cast<unsigned>(elementPoolSize(EC::tierFor(ladder_.level()))));
        } else {
            snprintf(buf, sizeof(buf), "%u elements",
                     static_cast<unsigned>(elementPoolSize(i)));
        }
        tft.setTextDatum(MR_DATUM);
        tft.drawString(buf, r.x + r.w - 12, r.y + r.h / 2, 1);
        tft.setTextDatum(TL_DATUM);
    }

    EC::levelLine(ladder_.level(), buf, sizeof(buf));
    tft.fillRect(8, 230, static_cast<int16_t>(w - 16), 12, Ui::bg());
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString(buf, w / 2, 230, 1);
    tft.setTextDatum(TL_DATUM);
}
