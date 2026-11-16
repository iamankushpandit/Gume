// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "MoneyGame.h"
#include "engine/AppRegistry.h"

namespace {
namespace MC = MoneyCourse;
constexpr uint16_t YELLOW = 0xFEC0;
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t COIN_OUTLINE = 0x0843;

uint16_t coinFill(uint8_t value) {
    switch (value) {
        case 1:
            return Ui::rgb(184, 96, 52);
        case 5:
            return Ui::rgb(160, 170, 176);
        case 10:
            return Ui::rgb(210, 218, 224);
        case 25:
            return Ui::rgb(128, 146, 166);
        default:
            return Ui::rgb(222, 184, 82);
    }
}

uint16_t coinText(uint8_t value) {
    return value == 1 || value == 25 ? TFT_WHITE : TFT_BLACK;
}

constexpr AppScoreInfo MONEY_SCORE = {
    "money", "Money", "moneyBest", "pts", false
};

constexpr AppMetadata MONEY_METADATA = {
    "money",
    "Money",
    nullptr,
    "count coins",
    "Money",
    "Count coins and make change.",
    &MONEY_SCORE,
    LauncherIcon::Money,
    1,
    true,
};
}

const AppMetadata& moneyAppMetadata() {
    return MONEY_METADATA;
}

const char* MoneyGame::title() const {
    return moneyAppMetadata().screenTitle != nullptr
        ? moneyAppMetadata().screenTitle
        : moneyAppMetadata().title;
}

/* A new visit, or New game from the Carry on card: the tally starts over,
 * the level does not. It used to be the visit's score, and the kind of
 * question too, so every visit began with ones and fives. */
void MoneyGame::begin(AppContext& host) {
    score_ = 0;
    rounds_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(moneyAppMetadata().score->bestKey, 0));
    ladder_.start(MC::LEVEL_COUNT, host.courseLevel());
    startLevel();
    announce_ = false;
    newRound();
    markDirty();
}

/* An answered round, or coins half-built, is something to come back to. */
Game::Keep MoneyGame::keepsGame() const {
    return (rounds_ > 0 || makeCount_ > 0) ? Keep::Ask : Keep::No;
}

/* Nothing here runs on a clock any more: an answer stays until the next
 * tap. */
void MoneyGame::resume(AppContext&, uint32_t) {}

void MoneyGame::startLevel() {
    deck_.reset(MC::numbered(ladder_.level()) ? MC::poolSize(ladder_.level()) : 0);
    recent_.reset();
}

void MoneyGame::newRound() {
    const uint8_t level = ladder_.level();
    if (MC::numbered(level)) {
        item_ = deck_.next(esp_random());
        MC::make(level, item_, esp_random(), round_);
    } else {
        for (uint8_t tries = 0; tries < 20; ++tries) {
            MC::makeDrawn(level, esp_random(), round_);
            if (!recent_.recentlyUsed(MC::token(round_))) break;
        }
        recent_.remember(MC::token(round_));
    }
    banner_ = announce_;
    announce_ = false;
    selected_ = -1;
    roundComplete_ = false;
    recorded_ = false;
    line_ = Line::Prompt;
    makeCount_ = 0;
    built_ = 0;
}

void MoneyGame::record(AppContext& host, bool right) {
    if (recorded_) {
        right ? host.beepOk() : host.beepError();
        return;
    }
    recorded_ = true;
    ++rounds_;
    if (right) {
        ++score_;
        ++streak_;
        if (host.saveBestScore(moneyAppMetadata().score->bestKey, streak_, false)) {
            bestStreak_ = streak_;
        }
    } else {
        streak_ = 0;
        if (MC::numbered(round_.level)) deck_.missed(item_);
    }
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
}

Rect MoneyGame::optionRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152), static_cast<int16_t>(160 + row * 36), 132, 30};
}

Rect MoneyGame::trayRect(uint8_t index) const {
    return Rect{static_cast<int16_t>(14 + index * 59), 146, 52, 34};
}

Rect MoneyGame::clearRect() const {
    return Rect{36, 198, 96, 30};
}

Rect MoneyGame::doneRect() const {
    return Rect{188, 198, 96, 30};
}

Rect MoneyGame::moreRect(uint8_t index) const {
    return index == 0 ? Rect{32, 176, 110, 34} : Rect{178, 176, 110, 34};
}

void MoneyGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (roundComplete_) {
        newRound();
        markDirty();
        return;
    }

    if (round_.mode == MC::Mode::Make) {
        if (clearRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            built_ = 0;
            makeCount_ = 0;
            line_ = Line::Prompt;
            markDirty();
            return;
        }
        if (doneRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            const bool amount = built_ == round_.target;
            const bool fewest = !round_.fewest || makeCount_ == MC::fewestCoins(round_.target);
            /* Only the first Done of a round reaches the ladder; after a miss
             * the coins can be put right, which teaches, and says nothing. */
            record(host, amount && fewest);
            if (amount && fewest) {
                roundComplete_ = true;
                line_ = Line::Right;
            } else {
                line_ = amount ? Line::MakeFewer : Line::MakeShort;
            }
            markDirty();
            return;
        }
        const uint8_t kinds = MC::levelOf(round_.level).coinKinds;
        for (uint8_t i = 0; i < kinds; ++i) {
            if (trayRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && makeCount_ < MC::MAX_BUILT) {
                makeCoins_[makeCount_++] = MC::COINS[i];
                built_ = static_cast<uint16_t>(built_ + MC::COINS[i]);
                markDirty();
                return;
            }
        }
        return;
    }

    /* Total, More and Change: one try. The right answer is shown either way;
     * any number of taps used to find it. */
    const uint8_t buttons = round_.mode == MC::Mode::More ? 2 : 4;
    for (uint8_t i = 0; i < buttons; ++i) {
        const Rect r = round_.mode == MC::Mode::More ? moreRect(i) : optionRect(i);
        if (!r.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        selected_ = static_cast<int8_t>(i);
        const bool right = i == (round_.mode == MC::Mode::More ? round_.more : round_.correct);
        record(host, right);
        roundComplete_ = true;
        line_ = right ? Line::Right : Line::Wrong;
        markDirty();
        return;
    }
}

void MoneyGame::drawCoin(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t value) const {
    char label[8];
    MC::cents(value, label, sizeof(label));
    tft.fillCircle(cx, cy, 12, coinFill(value));
    tft.drawCircle(cx, cy, 12, COIN_OUTLINE);
    tft.setTextColor(coinText(value), coinFill(value));
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, cx, cy, 1);
    tft.setTextDatum(TL_DATUM);
}

/* A row is drawn only if its coins fit inside the box: the Make box used to
 * show the first eight coins built and hide the rest. */
void MoneyGame::drawCoinGroup(Ui::Renderer& tft, const Rect& r, const uint8_t* coins,
                              uint8_t count, int16_t rowPitch) const {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    const uint8_t cols = max<uint8_t>(1, r.w / 34);
    for (uint8_t i = 0; i < count; ++i) {
        const int16_t x = static_cast<int16_t>(r.x + 18 + (i % cols) * 34);
        const int16_t y = static_cast<int16_t>(r.y + 14 + (i / cols) * rowPitch);
        if (y + 12 <= r.y + r.h) {
            drawCoin(tft, x, y, coins[i]);
        }
    }
}

void MoneyGame::drawOptions(Ui::Renderer& tft) const {
    char label[12];
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (roundComplete_ && i == round_.correct) {
            fill = GREEN;
            text = TFT_BLACK;
        } else if (roundComplete_ && i == selected_) {
            fill = RED;
            text = TFT_BLACK;
        }
        MC::cents(round_.options[i], label, sizeof(label));
        Ui::drawButton(tft, optionRect(i), label, fill, Ui::outline(), text, false, 2);
    }
}

void MoneyGame::drawMake(Ui::Renderer& tft) const {
    /* Two rows of eight: the box is 54 tall and the rows 25 apart. */
    drawCoinGroup(tft, Rect{18, 84, 284, 54}, makeCoins_, makeCount_, 25);
    char want[12];
    char have[12];
    char line[40];
    MC::cents(round_.target, want, sizeof(want));
    MC::cents(built_, have, sizeof(have));
    snprintf(line, sizeof(line), "Make %s   Built %s", want, have);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(line, GAME_CANVAS_WIDTH / 2, 75, 2);

    const uint8_t kinds = MC::levelOf(round_.level).coinKinds;
    for (uint8_t i = 0; i < kinds; ++i) {
        const Rect r = trayRect(i);
        Ui::drawButton(tft, r, "", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        drawCoin(tft, r.x + r.w / 2, r.y + r.h / 2, MC::COINS[i]);
    }
    Ui::drawButton(tft, clearRect(), "Clear", Ui::surface(), Ui::outline(), Ui::text(), false, 2);
    const bool done = roundComplete_;
    Ui::drawButton(tft, doneRect(), "Done", done ? GREEN : BLUE, Ui::outline(),
                   done ? TFT_BLACK : TFT_WHITE, false, 2);
}

void MoneyGame::drawMore(Ui::Renderer& tft) const {
    drawCoinGroup(tft, Rect{14, 72, 140, 88}, round_.a, round_.aCount, 28);
    drawCoinGroup(tft, Rect{166, 72, 140, 88}, round_.b, round_.bCount, 28);
    for (uint8_t i = 0; i < 2; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (roundComplete_ && i == round_.more) {
            fill = GREEN;
            text = TFT_BLACK;
        } else if (roundComplete_ && i == selected_) {
            fill = RED;
            text = TFT_BLACK;
        }
        Ui::drawButton(tft, moreRect(i), i == 0 ? "Left" : "Right", fill, Ui::outline(), text, false, 2);
    }
}

void MoneyGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
}

void MoneyGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    /* Still the whole content area on every change, as before: a tray tap
     * repaints the coins, the line and the tray. */
    tft.fillRect(0, TOP_BAR_HEIGHT, GAME_CANVAS_WIDTH,
                 GAME_CANVAS_HEIGHT - TOP_BAR_HEIGHT, Ui::bg());

    char buf[40];
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(round_.level));
    tft.drawString(buf, 8, 35, 2);
    snprintf(buf, sizeof(buf), "Score %u", static_cast<unsigned>(score_));
    tft.drawString(buf, 8, 51, 1);
    tft.setTextDatum(TR_DATUM);
    snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak_));
    tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 35, 2);
    snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(bestStreak_));
    tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 51, 1);

    /* The line under the header: the question, a new level's name over its
     * first question, or how the answer went. It replaces the panel that
     * used to cover the top of the answer buttons. */
    uint16_t colour = Ui::muted();
    const char* text = MC::prompt(round_);
    switch (line_) {
        case Line::Right:
            text = "Correct - tap for the next";
            colour = GREEN;
            break;
        case Line::Wrong: {
            char amount[12];
            if (round_.mode == MC::Mode::More) {
                snprintf(buf, sizeof(buf), "The %s pile is worth more - tap",
                         round_.more == 0 ? "left" : "right");
            } else {
                MC::cents(round_.target, amount, sizeof(amount));
                snprintf(buf, sizeof(buf), "It is %s - tap for the next", amount);
            }
            text = buf;
            colour = RED;
            break;
        }
        case Line::MakeShort:
            text = "Not that amount - try again";
            colour = RED;
            break;
        case Line::MakeFewer:
            text = "Right amount - now with fewer coins";
            colour = RED;
            break;
        case Line::Prompt:
        default:
            if (banner_) {
                MC::levelLine(round_.level, buf, sizeof(buf));
                text = buf;
                colour = YELLOW;
            }
            break;
    }
    tft.setTextColor(colour, Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, GAME_CANVAS_WIDTH / 2, 62, 1);

    if (round_.mode == MC::Mode::Total) {
        drawCoinGroup(tft, Rect{18, 78, 284, 70}, round_.a, round_.aCount, 30);
        drawOptions(tft);
    } else if (round_.mode == MC::Mode::Make) {
        drawMake(tft);
    } else if (round_.mode == MC::Mode::More) {
        drawMore(tft);
    } else {
        char price[12];
        char price2[12];
        char paid[12];
        MC::cents(round_.price, price, sizeof(price));
        MC::cents(round_.paid, paid, sizeof(paid));
        if (round_.price2 > 0) {
            MC::cents(round_.price2, price2, sizeof(price2));
            snprintf(buf, sizeof(buf), "Price %s + %s", price, price2);
        } else {
            snprintf(buf, sizeof(buf), "Price %s", price);
        }
        tft.fillRoundRect(32, 82, 256, 56, 6, Ui::surface());
        tft.drawRoundRect(32, 82, 256, 56, 6, Ui::outline());
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(buf, GAME_CANVAS_WIDTH / 2, 99, 2);
        snprintf(buf, sizeof(buf), "Pay %s", paid);
        tft.drawString(buf, GAME_CANVAS_WIDTH / 2, 123, 2);
        drawOptions(tft);
    }
    tft.setTextDatum(TL_DATUM);
}
