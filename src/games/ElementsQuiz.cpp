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

void ElementsGame::nextQuestion() {
    const bool wasTable = questionUsesTable();

    if (forcedElement_ >= 0 && forcedElement_ < static_cast<int16_t>(ELEMENT_COUNT)) {
        qElement_ = static_cast<uint8_t>(forcedElement_);
        forcedElement_ = -1;
    } else {
        const uint16_t picked = progress_.pickWeighted(inPool, this);
        qElement_ = (picked == 0xFFFF) ? 0 : static_cast<uint8_t>(picked);
    }

    /* "What state is it at room temperature?" is only honest for elements a
     * player could meet. Beyond tier 2 the answer is a laboratory prediction,
     * and a quiz that scores a prediction as a fact is teaching the wrong
     * thing. Everything else works at any tier. */
    const bool allowState = ELEMENTS[qElement_].tier <= 2;
    for (uint8_t attempt = 0; attempt < 8; ++attempt) {
        qType_ = static_cast<QType>(random(static_cast<long>(QType::COUNT)));
        if (allowState || qType_ != QType::StateOf) break;
    }
    if (!allowState && qType_ == QType::StateOf) qType_ = QType::SymbolToName;

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
    if (qType_ == QType::FindIt) {
        optionCount_ = 0;
        correctOption_ = 0;
        return;
    }
    if (qType_ == QType::StateOf) {
        optionCount_ = ELEMENT_STATE_COUNT;
        for (uint8_t i = 0; i < ELEMENT_STATE_COUNT; ++i) optionValue_[i] = i;
        correctOption_ = ELEMENTS[qElement_].state;
        return;
    }

    /* Distractors come from the level's own pool, widened if the player pressed
     * "Quiz me" on something above their level -- otherwise Oganesson would be
     * the only unfamiliar name among three they know, and the round would be
     * answerable without reading it. */
    uint8_t pool = activeTier();
    if (ELEMENTS[qElement_].tier > pool) pool = ELEMENTS[qElement_].tier;
    const uint16_t poolSize = elementPoolSize(pool);

    optionCount_ = MAX_OPTIONS;
    correctOption_ = static_cast<uint8_t>(random(MAX_OPTIONS));
    for (uint8_t i = 0; i < MAX_OPTIONS; ++i) {
        if (i == correctOption_) {
            optionValue_[i] = qElement_;
            continue;
        }
        optionValue_[i] = qElement_;   // overwritten below; never left unset
        /* Bounded retries. An unbounded while() in a path a frame can reach is
         * never acceptable, however unlikely the collision run. */
        for (uint8_t attempt = 0; attempt < 64; ++attempt) {
            const uint8_t cand = elementFromPool(
                static_cast<uint16_t>(random(static_cast<long>(poolSize))), pool);
            if (cand == 0xFF || cand == qElement_) continue;
            /* For "how many protons", a near miss is the instructive one: four
             * numbers scattered across the whole table can be answered by
             * shape alone. Relax the nearness rule late so the loop always
             * terminates with a real distractor. */
            if (qType_ == QType::ProtonCount && attempt < 40) {
                const int16_t gap = static_cast<int16_t>(ELEMENTS[cand].z) -
                                    static_cast<int16_t>(ELEMENTS[qElement_].z);
                if (gap > 20 || gap < -20) continue;
            }
            bool clash = false;
            for (uint8_t j = 0; j < i; ++j) {
                if (optionValue_[j] == cand) { clash = true; break; }
            }
            if (!clash) { optionValue_[i] = cand; break; }
        }
    }
}

void ElementsGame::writePrompt() {
    const ElementFact& e = ELEMENTS[qElement_];
    switch (qType_) {
        case QType::SymbolToName:
            snprintf(prompt_, sizeof(prompt_), "Which element is %s?", e.symbol);
            break;
        case QType::NameToSymbol:
            snprintf(prompt_, sizeof(prompt_), "What is the symbol for %s?", e.name);
            break;
        case QType::FindIt:
            snprintf(prompt_, sizeof(prompt_), "Find %s in the table", e.name);
            break;
        case QType::ProtonCount:
            snprintf(prompt_, sizeof(prompt_), "How many protons does %s have?", e.name);
            break;
        case QType::FactToElement:
            snprintf(prompt_, sizeof(prompt_), "%s", e.fact);
            break;
        case QType::StateOf:
            snprintf(prompt_, sizeof(prompt_), "%s in a warm room is a...", e.name);
            break;
        default:
            prompt_[0] = '\0';
            break;
    }
}

void ElementsGame::optionLabel(uint8_t index, char* out, size_t len) const {
    if (index >= optionCount_) { out[0] = '\0'; return; }
    const uint8_t value = optionValue_[index];
    switch (qType_) {
        case QType::NameToSymbol:
            snprintf(out, len, "%s", ELEMENTS[value].symbol);
            break;
        case QType::ProtonCount:
            snprintf(out, len, "%u", static_cast<unsigned>(ELEMENTS[value].z));
            break;
        case QType::StateOf:
            snprintf(out, len, "%s", elementStateName(value));
            break;
        default:
            snprintf(out, len, "%s", ELEMENTS[value].name);
            break;
    }
}

// ---- answering -------------------------------------------------------------

void ElementsGame::answer(AppContext& host, bool correct) {
    if (correct) {
        host.beepOk();
        score_ += 10;
        ++streak_;
    } else {
        host.beepError();
        streak_ = 0;
    }
    progress_.record(qElement_, correct);
    progress_.maybeFlush();
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
            snprintf(buf, sizeof(buf), "follows you - now %u",
                     static_cast<unsigned>(elementPoolSize(tier)));
        } else {
            snprintf(buf, sizeof(buf), "%u elements",
                     static_cast<unsigned>(elementPoolSize(i)));
        }
        tft.setTextDatum(MR_DATUM);
        tft.drawString(buf, r.x + r.w - 12, r.y + r.h / 2, 1);
        tft.setTextDatum(TL_DATUM);
    }

    snprintf(buf, sizeof(buf), "You know %u%% of the easy ones, %u%% of the common",
             static_cast<unsigned>(tierMastery(1)), static_cast<unsigned>(tierMastery(2)));
    tft.fillRect(8, 230, static_cast<int16_t>(w - 16), 12, Ui::bg());
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString(buf, w / 2, 230, 1);
    tft.setTextDatum(TL_DATUM);
}
