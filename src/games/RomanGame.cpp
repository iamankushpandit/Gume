// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "RomanGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

constexpr AppScoreInfo ROMAN_SCORE = {
    "roman", "Roman", "romanBest", "pts", false
};

constexpr AppMetadata ROMAN_METADATA = {
    "roman",
    "Roman",
    "Roman Numerals",
    "read them and write them",
    "Roman",
    "Read and write Roman numerals.",
    &ROMAN_SCORE,
    LauncherIcon::Roman,
    39,
    true,
};

/* The notation lives in RomanCourse.h, once: the course builds the
 * questions from it and this screen takes numerals apart with the same
 * table, so a numeral and its explanation cannot disagree. */
namespace RC = RomanCourse;
using RomanCourse::toRoman;
constexpr const RC::Piece* PIECES = RC::PIECES;
constexpr uint8_t PIECE_COUNT = 13;

/* "XIV = X + IV = 10 + 4". Returns false when the number has too many pieces
 * for the line to be worth reading -- MMMDCCCLXXXVIII taken apart is fourteen
 * terms, which teaches nobody anything. */
bool explain(uint16_t value, char* out, size_t outLen) {
    char roman[24];
    const char* symbols[8];
    uint16_t values[8];
    uint8_t used = 0;

    uint16_t left = value;
    for (uint8_t i = 0; i < PIECE_COUNT; ++i) {
        while (left >= PIECES[i].value) {
            if (used >= 8) return false;
            symbols[used] = PIECES[i].symbol;
            values[used] = PIECES[i].value;
            ++used;
            left = static_cast<uint16_t>(left - PIECES[i].value);
        }
    }
    if (used == 0 || used > 6) return false;

    toRoman(value, roman, sizeof(roman));
    int at = snprintf(out, outLen, "%s = ", roman);
    if (at < 0 || static_cast<size_t>(at) >= outLen) return false;
    for (uint8_t i = 0; i < used; ++i) {
        at += snprintf(out + at, outLen - at, "%s%s", i == 0 ? "" : " + ", symbols[i]);
        if (static_cast<size_t>(at) >= outLen) return false;
    }
    at += snprintf(out + at, outLen - at, " = ");
    if (static_cast<size_t>(at) >= outLen) return false;
    for (uint8_t i = 0; i < used; ++i) {
        at += snprintf(out + at, outLen - at, "%s%u",
                       i == 0 ? "" : " + ", static_cast<unsigned>(values[i]));
        if (static_cast<size_t>(at) >= outLen) return false;
    }
    if (used > 1) {
        snprintf(out + at, outLen - at, " = %u", static_cast<unsigned>(value));
    }
    return true;
}

/* The symbols in play at each level, and the one rule that matters there.
 * Both lines are on the screen for as long as the level is, because looking a
 * symbol up is the skill at this stage, not remembering it. */
struct LevelKey {
    const char* symbols;
    const char* rule;
};

const LevelKey LEVEL_KEYS[RC::LEVEL_COUNT] = {
    {"I=1   V=5   X=10",
     "Count up: I, II, III. Then IV is one before V."},
    {"I=1   V=5   X=10",
     "A smaller sign BEFORE a bigger one is taken away."},
    {"I=1   V=5   X=10   L=50",
     "XL is 10 before 50 = 40. LX is 50 and 10 = 60."},
    {"I=1  V=5  X=10  L=50  C=100",
     "Only I, X and C are ever put in front to subtract."},
    {"I=1  V=5  X=10  L=50  C=100  D=500",
     "CD is 100 before 500 = 400. DC is 600."},
    {"I=1 V=5 X=10 L=50 C=100 D=500 M=1000",
     "CM is 100 before 1000 = 900. MC is 1100."},
    {"I=1 V=5 X=10 L=50 C=100 D=500 M=1000",
     "Biggest first; never four of one sign in a row."},
    {"I=1 V=5 X=10 L=50 C=100 D=500 M=1000",
     "I before V, X; X before L, C; C before D, M."},
};
}

const AppMetadata& romanAppMetadata() {
    return ROMAN_METADATA;
}

const char* RomanGame::title() const {
    return romanAppMetadata().screenTitle != nullptr
        ? romanAppMetadata().screenTitle
        : romanAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the tally starts over,
 * the level does not. It used to be the visit's score, so every visit began
 * with I, V and X. */
void RomanGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    best_ = static_cast<uint16_t>(host.getScore(romanAppMetadata().score->bestKey, 0));
    ladder_.start(RC::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announce_ = false;
    newQuestion();
    markDirty();
}

/* An answered question is something to come back to. No resume(): nothing
 * here runs on a clock. */
Game::Keep RomanGame::keepsGame() const {
    return score_ > 0 || streak_ > 0 || answered_ ? Keep::Ask : Keep::No;
}

void RomanGame::startLevel() {
    deck_.reset(RC::poolSize(ladder_.level()));
    recent_.reset();
}

Rect RomanGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152),
                static_cast<int16_t>(144 + row * 46), 132, 38};
}

Rect RomanGame::keyRect() const {
    return Rect{10, 50, static_cast<int16_t>(GAME_CANVAS_WIDTH - 20), 22};
}

Rect RomanGame::questionRect() const {
    return Rect{20, 74, 280, 50};
}

Rect RomanGame::noteRect() const {
    const Rect q = questionRect();
    const int16_t top = static_cast<int16_t>(q.y + q.h + 1);
    return Rect{14, top, static_cast<int16_t>(GAME_CANVAS_WIDTH - 28),
                static_cast<int16_t>(answerRect(0).y - top - 1)};
}

void RomanGame::newQuestion() {
    const uint8_t lvl = ladder_.level();
    if (RC::numbered(lvl)) {
        item_ = deck_.next(esp_random());
        RC::make(lvl, item_, esp_random(), q_);
    } else {
        for (uint8_t tries = 0; tries < 20; ++tries) {
            RC::makeDrawn(lvl, esp_random(), q_);
            if (!recent_.recentlyUsed(RC::token(q_))) break;
        }
        recent_.remember(RC::token(q_));
    }
    value_ = q_.value;
    askNumber_ = q_.askNumber;
    snprintf(subject_, sizeof(subject_), "%s", q_.subject);
    for (uint8_t i = 0; i < 4; ++i) {
        snprintf(options_[i], TEXT_MAX, "%s", q_.options[i]);
    }
    correctButton_ = q_.correct;
    banner_ = announce_;
    announce_ = false;
    selected_ = -1;
    answered_ = false;

    /* Everything a new question changes. The button trackers are in here
     * because a button that was never highlighted goes from state 0 to state 0
     * and would otherwise keep the previous question's label -- the trap
     * docs/RENDER_AUDIT.md warns about. The key strip belongs to the level and
     * is repainted when the level the question was dealt from changes. */
    drawnQuestion_ = false;
    drawnHeader_ = false;
    drawnNote_ = 0xFF;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

void RomanGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        selected_ = static_cast<int8_t>(i);
        answered_ = true;
        const bool right = i == correctButton_;
        if (right) {
            ++score_;
            ++streak_;
            if (host.saveBestScore(romanAppMetadata().score->bestKey, score_, false)) {
                best_ = score_;
            }
        } else {
            streak_ = 0;
            if (RC::numbered(q_.level)) deck_.missed(item_);
        }
        /* One try, so the first press is the only one the ladder hears. */
        const Course::Ladder::Step step = ladder_.answer(right);
        if (step != Course::Ladder::Step::Stay) {
            host.setCourseLevel(ladder_.level());
            startLevel();
        }
        if (step == Course::Ladder::Step::Up) {
            announce_ = true;
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(Sound::LevelUp);
        } else if (right) {
            host.beepOk();
        } else {
            host.beepError();
        }
        markDirty();
        return;
    }
}

void RomanGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnHeader_ = false;
    drawnQuestion_ = false;
    drawnKeyLevel_ = 0xFF;
    drawnNote_ = 0xFF;
}

void RomanGame::drawKey(Ui::Renderer& tft) {
    const Rect r = keyRect();
    /* Cleared whole: the symbol line grows a term per level and the rule line
     * is a different length at each of the five, so neither can be relied on
     * to paint over its predecessor. */
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    const LevelKey& key = LEVEL_KEYS[level() - 1];
    tft.setTextDatum(TC_DATUM);
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(key.symbols, cx, r.y, 1);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(key.rule, cx, static_cast<int16_t>(r.y + 11), 1);
    tft.setTextDatum(TL_DATUM);
}

void RomanGame::drawQuestion(Ui::Renderer& tft) {
    const Rect r = questionRect();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, Ui::panel());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, Ui::outline());

    tft.setTextDatum(TC_DATUM);
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    tft.setTextColor(Ui::muted(), Ui::panel());
    tft.drawString(askNumber_ ? "What number is this?" : "Write this in Roman numerals",
                   cx, static_cast<int16_t>(r.y + 3), 2);
    tft.setTextColor(Ui::text(), Ui::panel());
    /* MMMDCCCLXXXVIII at font 4 is wider than the panel, so the subject drops
     * a font rather than being cut mid-numeral by the driver -- which would
     * leave a numeral that is a different number. */
    const uint8_t font = tft.textWidth(subject_, 4) <= r.w - 16 ? 4 : 2;
    tft.drawString(subject_, cx, static_cast<int16_t>(r.y + (font == 4 ? 21 : 26)), font);
    tft.setTextDatum(TL_DATUM);
}

void RomanGame::drawNote(Ui::Renderer& tft) {
    const Rect r = noteRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    tft.setTextDatum(TC_DATUM);
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);

    if (!answered_) {
        char line[RC::LEVEL_LINE_MAX + 1];
        if (banner_) {
            RC::levelLine(q_.level, line, sizeof(line));
        } else {
            snprintf(line, sizeof(line), "%s", q_.level == 8 ? "Which is written right?" : "Tap the answer");
        }
        tft.setTextColor(YELLOW, Ui::bg());
        tft.drawString(line, cx, static_cast<int16_t>(r.y + 1), 2);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    char working[80];
    if (!explain(value_, working, sizeof(working))) {
        char roman[TEXT_MAX];
        toRoman(value_, roman, sizeof(roman));
        snprintf(working, sizeof(working), "%s = %u", roman, static_cast<unsigned>(value_));
    }
    char lines[2][Ui::WRAP_LINE_MAX];
    const uint8_t needed = Ui::wrapLines(tft, working, r.w, 1, lines, 2);
    const uint8_t shown = min<uint8_t>(needed, 2);
    tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], cx, static_cast<int16_t>(r.y + i * 10), 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void RomanGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (drawnKeyLevel_ != level()) {
        drawKey(tft);
        drawnKeyLevel_ = level();
    }

    if (!drawnQuestion_) {
        drawQuestion(tft);
        drawnQuestion_ = true;
    }

    /* One header row, cleared at both ends before either is written: Correct
     * grows and Level is redrawn beside it, and a right-aligned string that
     * gets narrower leaves its tail at the LEFT. */
    if (!drawnHeader_ || score_ != drawnScore_ || drawnKeyLevel_ != level()) {
        tft.fillRect(10, 32, 120, 18, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 10 - 190, 32, 190, 18, Ui::bg());
        char buf[40];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(level()));
        tft.drawString(buf, 10, 33, 2);
        tft.setTextDatum(TR_DATUM);
        if (best_ > 0) {
            snprintf(buf, sizeof(buf), "Correct %u   Best %u",
                     static_cast<unsigned>(score_), static_cast<unsigned>(best_));
        } else {
            snprintf(buf, sizeof(buf), "Correct %u", static_cast<unsigned>(score_));
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 33, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnHeader_ = true;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (answered_) {
            if (i == correctButton_) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t ink = state == 0 ? TFT_WHITE : TFT_BLACK;
        const Rect r = answerRect(i);
        const uint8_t font = tft.textWidth(options_[i], 4) <= r.w - 10 ? 4 : 2;
        Ui::drawButton(tft, r, options_[i], fill, TFT_DARKGREY, ink, false, font);
        drawnButton_[i] = state;
    }

    const uint8_t note = answered_ ? (selected_ == correctButton_ ? 1 : 2) : 0;
    if (note != drawnNote_) {
        drawNote(tft);
        drawnNote_ = note;
    }
}
