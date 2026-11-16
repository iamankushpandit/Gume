// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "GreWordsGame.h"
#include "GreWordTable.h"
#include "engine/AppRegistry.h"

#include <cstdio>
#include <cstring>

namespace {
constexpr uint32_t REVEAL_MS = 900;

constexpr AppScoreInfo GRE_WORDS_SCORE = {
    "grewords", "GRE Words", "greBest", "pts", false
};

constexpr AppMetadata GRE_WORDS_METADATA = {
    "grewords",
    "GRE Words",
    nullptr,
    "vocabulary",
    "GRE Words",
    "Learn GRE vocabulary.",
    &GRE_WORDS_SCORE,
    LauncherIcon::GreWords,
    5,
    true,
};
}

const AppMetadata& greWordsAppMetadata() {
    return GRE_WORDS_METADATA;
}

const char* GreWordsGame::title() const {
    return greWordsAppMetadata().screenTitle != nullptr
        ? greWordsAppMetadata().screenTitle
        : greWordsAppMetadata().title;
}

Rect GreWordsGame::flipTabRect(int16_t w) const {
    return Rect{0, 30, static_cast<int16_t>(w / 2), 22};
}
Rect GreWordsGame::quizTabRect(int16_t w) const {
    return Rect{static_cast<int16_t>(w / 2), 30, static_cast<int16_t>(w - w / 2), 22};
}
Rect GreWordsGame::cardRect() const { return Rect{12, 58, 296, 130}; }
Rect GreWordsGame::optionRect(uint8_t i) const {
    // Below the word, which is font 4 centred at y=70 and so reaches ~83.
    return Rect{10, static_cast<int16_t>(88 + i * 34), 300, 30};
}
Rect GreWordsGame::knewRect()   const { return Rect{12,  192, 92, 30}; }
Rect GreWordsGame::missedRect() const { return Rect{114, 192, 92, 30}; }
Rect GreWordsGame::nextRect()   const { return Rect{216, 192, 92, 30}; }

void GreWordsGame::begin(AppContext& host) {
    ladder_.start(GreCourse::LEVEL_COUNT, host.courseLevel());
    deck_.reset(GRE_WORD_COUNT);
    announce_ = false;
    score_  = 0;
    streak_ = 0;
    mode_   = Mode::Flip;
    nextWord();
    markFullDirty();
}

/* Points scored are a quiz under way, and worth the question. Without them
 * the screen holds only a place -- the Quiz tab, or a card turned over to be
 * read -- and the tabs and Next move on from either. An unturned card on
 * Study is nothing begin() would not hand back. */
Game::Keep GreWordsGame::keepsGame() const {
    if (score_ > 0) return Keep::Ask;
    if (mode_ == Mode::Quiz || cardFlipped_) return Keep::Quietly;
    return Keep::No;
}

/* The answer held on screen finishes its moment after the repaint, rather
 * than the next word arriving before the player has seen what they chose. */
void GreWordsGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    movedOn(revealUntilMs_, awayMs);
}

void GreWordsGame::nextWord() {
    /* Every word comes up before any comes back, and a missed one -- a
     * wrong answer or a "Didn't" -- returns after three others. */
    currentWord_ = deck_.next(esp_random());
    kind_ = GreCourse::kindFor(ladder_.level(), currentWord_, esp_random());
    quizLineCount_ = 0;
    if (kind_ != GreCourse::Kind::Meaning) {
        char text[160];
        if (kind_ == GreCourse::Kind::Sentence) {
            GreCourse::blank(GRE_WORDS[currentWord_].example, GRE_WORDS[currentWord_].word, text,
                             sizeof(text));
        } else {
            snprintf(text, sizeof(text), "%s", GRE_WORDS[currentWord_].meaning);
        }
        char lines[WRAP_LINES][WRAP_COLS + 1];
        wrapInto(text, lines, quizLineCount_);
        if (quizLineCount_ > 2) quizLineCount_ = 2;
        for (uint8_t i = 0; i < quizLineCount_; ++i) memcpy(quizLines_[i], lines[i], WRAP_COLS + 1);
    }
    cardFlipped_ = false;
    chosenOption_ = -1;
    revealUntilMs_ = 0;
    wrapStale_ = true;
    buildOptions();
}

void GreWordsGame::buildOptions() {
    /* The wrong options are the level's: another part of speech first, then
     * the same part of speech, then the words beside it in the alphabet. */
    GreCourse::options(ladder_.level(), currentWord_, esp_random(), optionIndices_, correctOption_);
}

void GreWordsGame::wrapInto(const char* src, char dst[WRAP_LINES][WRAP_COLS + 1],
                            uint8_t& lineCount) {
    lineCount = 0;
    for (uint8_t i = 0; i < WRAP_LINES; ++i) dst[i][0] = '\0';
    if (src == nullptr) return;

    uint16_t pos = 0;
    const uint16_t len = static_cast<uint16_t>(strlen(src));
    while (pos < len && lineCount < WRAP_LINES) {
        uint16_t take = WRAP_COLS;
        if (pos + take >= len) {
            take = static_cast<uint16_t>(len - pos);
        } else {
            // Break on the last space that fits, so words stay whole.
            uint16_t brk = take;
            while (brk > 0 && src[pos + brk] != ' ') --brk;
            if (brk > 0) take = brk;
        }
        memcpy(dst[lineCount], src + pos, take);
        dst[lineCount][take] = '\0';
        ++lineCount;
        pos = static_cast<uint16_t>(pos + take);
        while (pos < len && src[pos] == ' ') ++pos;
    }
}

void GreWordsGame::rewrapIfStale() {
    if (!wrapStale_) return;
    const GreWord& w = GRE_WORDS[currentWord_];
    wrapInto(w.meaning, meaningLines_, meaningLineCount_);
    wrapInto(w.example, exampleLines_, exampleLineCount_);
    wrapStale_ = false;
}

void GreWordsGame::answer(AppContext& host, bool correct) {
    announce_ = false;
    if (correct) {
        score_ += 10;
        ++streak_;
    } else {
        streak_ = 0;
        deck_.missed(currentWord_);   // before the ladder moves
    }
    const Course::Ladder::Step step = ladder_.answer(correct);
    if (step != Course::Ladder::Step::Stay) host.setCourseLevel(ladder_.level());
    if (step == Course::Ladder::Step::Up) {
        announce_ = true;
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
    } else {
        correct ? host.beepOk() : host.beepError();
    }
    host.saveBestScore(greWordsAppMetadata().score->bestKey, score_, false);
}

void GreWordsGame::update(AppContext& host, const TouchPoint& touch) {
    const int16_t w = static_cast<int16_t>(host.display().width());

    // A wrong answer holds the correct option highlighted for a moment before
    // moving on, so the round still teaches rather than just scoring.
    if (revealUntilMs_ != 0 && millis() > revealUntilMs_) {
        nextWord();
        markFullDirty();
        return;
    }

    if (!touch.justPressed) return;

    if (flipTabRect(w).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (mode_ != Mode::Flip) { mode_ = Mode::Flip; markFullDirty(); }
        return;
    }
    if (quizTabRect(w).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (mode_ != Mode::Quiz) { mode_ = Mode::Quiz; markFullDirty(); }
        return;
    }

    if (revealUntilMs_ != 0) return;   // mid-reveal: ignore further taps

    if (mode_ == Mode::Flip) {
        if (cardRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cardFlipped_ = !cardFlipped_;
            markFullDirty();
            return;
        }
        // Knew it / Didn't feed the same mastery data the quiz does, so study
        // mode is not wasted effort -- it just does not score.
        /* Study does not move the ladder, but a "Didn't" brings the word
         * back a few words later, as a wrong answer does. */
        if (knewRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            nextWord(); markFullDirty(); return;
        }
        if (missedRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            deck_.missed(currentWord_);
            nextWord(); markFullDirty(); return;
        }
        if (nextRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            nextWord(); markFullDirty(); return;
        }
        return;
    }

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (!optionRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        chosenOption_ = static_cast<int8_t>(i);
        const bool correct = (i == correctOption_);
        answer(host, correct);
        revealUntilMs_ = millis() + REVEAL_MS;
        markFullDirty();
        return;
    }
}

void GreWordsGame::renderFlip(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const GreWord& word = GRE_WORDS[currentWord_];
    const Rect card = cardRect();

    /* A shadow in the background's own colour, which this full repaint has
     * just painted anyway: it makes the card a button to the d-pad's
     * selector, which otherwise reads one card beside three buttons as
     * something to look at, and the card is pressed to turn it over. */
    tft.fillRoundRect(card.x + Ui::BUTTON_SHADOW_DX, card.y + Ui::BUTTON_SHADOW_DY, card.w, card.h, 8,
                      Ui::bg());
    tft.fillRoundRect(card.x, card.y, card.w, card.h, 8, Ui::surface());
    tft.drawRoundRect(card.x, card.y, card.w, card.h, 8, Ui::outline());

    tft.setTextDatum(MC_DATUM);
    if (!cardFlipped_) {
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(word.word, card.x + card.w / 2, card.y + 48, 4);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(word.pos, card.x + card.w / 2, card.y + 78, 2);
        tft.drawString("tap to reveal", card.x + card.w / 2, card.y + 110, 1);
    } else {
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(word.word, card.x + card.w / 2, card.y + 18, 2);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::success(), Ui::surface());
        for (uint8_t i = 0; i < meaningLineCount_; ++i) {
            tft.drawString(meaningLines_[i], card.x + 10,
                           static_cast<int16_t>(card.y + 36 + i * 14), 1);
        }
        tft.setTextColor(Ui::muted(), Ui::surface());
        for (uint8_t i = 0; i < exampleLineCount_; ++i) {
            tft.drawString(exampleLines_[i], card.x + 10,
                           static_cast<int16_t>(card.y + 82 + i * 14), 1);
        }
    }
    tft.setTextDatum(TL_DATUM);

    Ui::drawButton(tft, knewRect(),   "Knew it", Ui::rgb(45, 154, 96),
                   Ui::outline(), TFT_WHITE, false, 2);
    Ui::drawButton(tft, missedRect(), "Didn't",  Ui::rgb(178, 58, 58),
                   Ui::outline(), TFT_WHITE, false, 2);
    Ui::drawButton(tft, nextRect(),   "Next",    Ui::panel(),
                   Ui::outline(), Ui::text(), false, 2);
}

void GreWordsGame::renderQuiz(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const GreWord& word = GRE_WORDS[currentWord_];

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    if (kind_ == GreCourse::Kind::Meaning) {
        tft.drawString(word.word, static_cast<int16_t>(tft.width() / 2), 70, 4);
    } else {
        /* The meaning to recall, or the sentence with its gap: one or two
         * lines of font 1 between the tabs and the first option. */
        const int16_t top = quizLineCount_ > 1 ? 64 : 70;
        for (uint8_t i = 0; i < quizLineCount_; ++i) {
            tft.drawString(quizLines_[i], static_cast<int16_t>(tft.width() / 2),
                           static_cast<int16_t>(top + i * 12), quizLineCount_ > 1 ? 1 : 2);
        }
    }
    tft.setTextDatum(TL_DATUM);
    const bool wordsOnButtons = kind_ != GreCourse::Kind::Meaning;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const Rect r = optionRect(i);
        uint16_t fill = Ui::panel();
        uint16_t ink  = Ui::text();
        if (revealUntilMs_ != 0) {
            if (i == correctOption_) {
                fill = Ui::success(); ink = TFT_BLACK;
            } else if (i == chosenOption_) {
                fill = Ui::error();   ink = TFT_WHITE;
            }
        }
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, Ui::outline());
        tft.setTextColor(ink, fill);
        tft.setTextDatum(ML_DATUM);
        /* Ui::fitted() takes a String and would allocate; the glosses are
         * authored short enough to fit, so draw the flash pointer directly. */
        if (wordsOnButtons) {
            tft.drawString(GRE_WORDS[optionIndices_[i]].word,
                           r.x + 8, static_cast<int16_t>(r.y + r.h / 2), 2);
        } else {
            tft.drawString(GRE_WORDS[optionIndices_[i]].meaning,
                           r.x + 8, static_cast<int16_t>(r.y + r.h / 2), 1);
        }
    }
    tft.setTextDatum(TL_DATUM);
}

void GreWordsGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());

    rewrapIfStale();

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        const Rect fTab = flipTabRect(w);
        const Rect qTab = quizTabRect(w);
        Ui::drawTab(tft, fTab, "Study", mode_ == Mode::Flip);
        Ui::drawTab(tft, qTab, "Quiz",  mode_ == Mode::Quiz);
        Ui::drawTabBaseline(tft, 52, 0, w, mode_ == Mode::Flip ? fTab : qTab);
    }

    if (mode_ == Mode::Flip) {
        renderFlip(host);
    } else {
        renderQuiz(host);
    }

    char buf[24];
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    if (announce_) {
        GreCourse::levelLine(ladder_.level(), buf, sizeof(buf));   // a step up
        tft.setTextColor(Ui::success(), Ui::bg());
    } else {
        snprintf(buf, sizeof(buf), "%u pts  x%u",
                 static_cast<unsigned>(score_), static_cast<unsigned>(streak_));
    }
    tft.fillRect(8, 226, 150, 12, Ui::bg());
    tft.drawString(buf, 8, 226, 1);

    snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(ladder_.level()));
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    tft.fillRect(220, 226, 92, 12, Ui::bg());
    tft.drawString(buf, static_cast<int16_t>(w - 8), 226, 1);
    tft.setTextDatum(TL_DATUM);
}
