#include "MoneyGame.h"

namespace {
constexpr uint8_t COIN_VALUES[] = {1, 5, 10, 25, 50};
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
}

const char* MoneyGame::title() const {
    return "Money";
}

void MoneyGame::begin(GameHost& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.board().getScore("moneyBest", 0));
    newRound();
    markDirty();
}

uint8_t MoneyGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 4);
}

uint8_t MoneyGame::allowedCoinCount() const {
    const uint8_t currentLevel = level();
    if (currentLevel == 1) {
        return 2;
    }
    if (currentLevel == 2) {
        return 3;
    }
    if (currentLevel < 5) {
        return 4;
    }
    return 5;
}

uint16_t MoneyGame::maxTotal() const {
    switch (level()) {
        case 1:
            return 15;
        case 2:
            return 35;
        case 3:
            return 75;
        case 4:
            return 100;
        default:
            return 150;
    }
}

uint8_t MoneyGame::randomCoinValue(uint16_t maxCents) const {
    uint8_t candidates[5] = {};
    uint8_t count = 0;
    const uint8_t allowed = allowedCoinCount();
    for (uint8_t i = 0; i < allowed; ++i) {
        if (COIN_VALUES[i] <= maxCents) {
            candidates[count++] = COIN_VALUES[i];
        }
    }
    if (count == 0) {
        return 1;
    }
    return candidates[random(count)];
}

String MoneyGame::centsText(uint16_t cents) const {
    if (cents < 100) {
        return String(cents) + "c";
    }
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "$%u.%02u", cents / 100, cents % 100);
    return String(buffer);
}

const char* MoneyGame::modeName() const {
    switch (mode_) {
        case Mode::Total:
            return "How much is this?";
        case Mode::Make:
            return "Make this amount";
        case Mode::More:
            return "Which is more?";
        default:
            return "Make change";
    }
}

uint16_t MoneyGame::groupValue(const uint8_t* coins, uint8_t count) const {
    uint16_t total = 0;
    for (uint8_t i = 0; i < count; ++i) {
        total += coins[i];
    }
    return total;
}

void MoneyGame::makeRandomGroup(uint8_t* coins, uint8_t& count, uint8_t minCoins, uint8_t maxCoins, uint16_t limit) {
    count = 0;
    uint16_t total = 0;
    const uint8_t desired = static_cast<uint8_t>(random(minCoins, maxCoins + 1));
    uint8_t attempts = 0;
    while (count < desired && attempts < 80) {
        const uint8_t coin = randomCoinValue(limit > total ? limit - total : 0);
        if (total + coin <= limit) {
            coins[count++] = coin;
            total += coin;
        }
        ++attempts;
    }
    if (count == 0) {
        coins[count++] = 1;
    }
}

bool MoneyGame::optionExists(uint16_t value, uint8_t upTo) const {
    for (uint8_t i = 0; i < upTo; ++i) {
        if (options_[i] == value) {
            return true;
        }
    }
    return false;
}

void MoneyGame::makeOptions(uint16_t correct) {
    correctButton_ = random(4);
    for (uint8_t i = 0; i < 4; ++i) {
        options_[i] = 0;
    }
    options_[correctButton_] = correct;

    const int16_t spread = level() <= 2 ? 12 : (level() <= 4 ? 25 : 50);
    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) {
            continue;
        }
        uint16_t candidate = correct;
        uint8_t attempts = 0;
        while ((candidate == correct || optionExists(candidate, i)) && attempts < 50) {
            const int16_t delta = random(-spread, spread + 1);
            int16_t next = static_cast<int16_t>(correct) + delta;
            if (next < 0) {
                next = abs(next);
            }
            candidate = static_cast<uint16_t>(next);
            ++attempts;
        }
        while (candidate == correct || optionExists(candidate, i)) {
            candidate += 5;
        }
        options_[i] = candidate;
    }
}

void MoneyGame::setupTotalRound() {
    makeRandomGroup(groupA_, groupACount_, level() <= 2 ? 3 : 4, level() <= 3 ? 6 : 8, maxTotal());
    target_ = groupValue(groupA_, groupACount_);
    makeOptions(target_);
}

void MoneyGame::setupMakeRound() {
    makeRandomGroup(groupA_, groupACount_, 2, level() <= 3 ? 5 : 7, maxTotal());
    target_ = groupValue(groupA_, groupACount_);
    built_ = 0;
    makeCount_ = 0;
}

void MoneyGame::setupMoreRound() {
    uint16_t left = 0;
    uint16_t right = 0;
    do {
        makeRandomGroup(groupA_, groupACount_, 2, level() <= 3 ? 5 : 7, maxTotal());
        makeRandomGroup(groupB_, groupBCount_, 2, level() <= 3 ? 5 : 7, maxTotal());
        left = groupValue(groupA_, groupACount_);
        right = groupValue(groupB_, groupBCount_);
    } while (left == right);
    correctButton_ = left > right ? 0 : 1;
}

void MoneyGame::setupChangeRound() {
    const uint16_t payChoices[] = {25, 50, 100, 150};
    paid_ = payChoices[random(level() >= 5 ? 4 : 3)];
    const uint8_t step = level() >= 5 ? 1 : 5;
    do {
        price_ = static_cast<uint16_t>(random(1, paid_ / step) * step);
    } while (price_ >= paid_);
    target_ = paid_ - price_;
    makeOptions(target_);
}

void MoneyGame::newRound() {
    const uint8_t unlockedModes = level() == 1 ? 1 : (level() == 2 ? 2 : (level() == 3 ? 3 : 4));
    mode_ = static_cast<Mode>(score_ % unlockedModes);
    selected_ = -1;
    flashIndex_ = -1;
    flashUntil_ = 0;
    roundComplete_ = false;
    groupACount_ = 0;
    groupBCount_ = 0;
    makeCount_ = 0;
    built_ = 0;

    switch (mode_) {
        case Mode::Total:
            setupTotalRound();
            break;
        case Mode::Make:
            setupMakeRound();
            break;
        case Mode::More:
            setupMoreRound();
            break;
        case Mode::Change:
            setupChangeRound();
            break;
    }
}

void MoneyGame::markCorrect(GameHost& host) {
    ++score_;
    ++streak_;
    if (host.board().saveBestScore("moneyBest", streak_, false)) {
        bestStreak_ = streak_;
    }
    roundComplete_ = true;
    flashIndex_ = -1;
}

void MoneyGame::markWrong(int8_t flashIndex) {
    streak_ = 0;
    flashIndex_ = flashIndex;
    flashUntil_ = millis() + 420UL;
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

void MoneyGame::update(GameHost& host, const TouchPoint& touch) {
    if (flashIndex_ >= 0 && millis() > flashUntil_) {
        flashIndex_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (roundComplete_) {
        newRound();
        markDirty();
        return;
    }

    if (mode_ == Mode::Make) {
        if (clearRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            built_ = 0;
            makeCount_ = 0;
            markDirty();
            return;
        }
        if (doneRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            if (built_ == target_) {
                markCorrect(host);
            } else {
                markWrong(6);
            }
            markDirty();
            return;
        }
        const uint8_t allowed = allowedCoinCount();
        for (uint8_t i = 0; i < allowed; ++i) {
            if (trayRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && makeCount_ < sizeof(makeCoins_) / sizeof(makeCoins_[0])) {
                const uint8_t coin = COIN_VALUES[i];
                makeCoins_[makeCount_++] = coin;
                built_ += coin;
                markDirty();
                return;
            }
        }
        return;
    }

    if (mode_ == Mode::More) {
        for (uint8_t i = 0; i < 2; ++i) {
            if (moreRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                selected_ = i;
                if (i == correctButton_) {
                    markCorrect(host);
                } else {
                    markWrong(static_cast<int8_t>(4 + i));
                }
                markDirty();
                return;
            }
        }
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (optionRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = i;
            if (i == correctButton_) {
                markCorrect(host);
            } else {
                markWrong(i);
            }
            markDirty();
            return;
        }
    }
}

void MoneyGame::drawCoin(TFT_eSPI& tft, int16_t cx, int16_t cy, uint8_t value) const {
    tft.fillCircle(cx, cy, 12, coinFill(value));
    tft.drawCircle(cx, cy, 12, COIN_OUTLINE);
    tft.setTextColor(coinText(value), coinFill(value));
    tft.setTextDatum(MC_DATUM);
    tft.drawString(centsText(value), cx, cy, 1);
    tft.setTextDatum(TL_DATUM);
}

void MoneyGame::drawCoinGroup(TFT_eSPI& tft, const Rect& r, const uint8_t* coins, uint8_t count) const {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    const uint8_t cols = max<uint8_t>(1, r.w / 34);
    for (uint8_t i = 0; i < count; ++i) {
        const int16_t x = static_cast<int16_t>(r.x + 18 + (i % cols) * 34);
        const int16_t y = static_cast<int16_t>(r.y + 18 + (i / cols) * 30);
        if (y < r.y + r.h - 8) {
            drawCoin(tft, x, y, coins[i]);
        }
    }
}

void MoneyGame::drawOptions(TFT_eSPI& tft) const {
    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (roundComplete_ && i == correctButton_) {
            fill = GREEN;
            text = TFT_BLACK;
        } else if (flashIndex_ == i) {
            fill = RED;
            text = TFT_BLACK;
        }
        Ui::drawButton(tft, optionRect(i), centsText(options_[i]), fill, Ui::outline(), text, false, 2);
    }
}

void MoneyGame::drawMake(TFT_eSPI& tft) const {
    drawCoinGroup(tft, Rect{18, 82, 284, 54}, makeCoins_, makeCount_);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String("Make ") + centsText(target_) + "   Built " + centsText(built_), SCREEN_WIDTH / 2, 70, 2);

    const uint8_t allowed = allowedCoinCount();
    for (uint8_t i = 0; i < allowed; ++i) {
        const Rect r = trayRect(i);
        Ui::drawButton(tft, r, "", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        drawCoin(tft, r.x + r.w / 2, r.y + r.h / 2, COIN_VALUES[i]);
    }
    Ui::drawButton(tft, clearRect(), "Clear", Ui::surface(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, doneRect(), "Done", flashIndex_ == 6 ? RED : BLUE, Ui::outline(), flashIndex_ == 6 ? TFT_BLACK : TFT_WHITE, false, 2);
}

void MoneyGame::drawMore(TFT_eSPI& tft) const {
    drawCoinGroup(tft, Rect{14, 72, 140, 88}, groupA_, groupACount_);
    drawCoinGroup(tft, Rect{166, 72, 140, 88}, groupB_, groupBCount_);
    for (uint8_t i = 0; i < 2; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (roundComplete_ && i == correctButton_) {
            fill = GREEN;
            text = TFT_BLACK;
        } else if (flashIndex_ == static_cast<int8_t>(4 + i)) {
            fill = RED;
            text = TFT_BLACK;
        }
        Ui::drawButton(tft, moreRect(i), i == 0 ? "Left" : "Right", fill, Ui::outline(), text, false, 2);
    }
}

void MoneyGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Level ") + level(), 8, 35, 2);
    tft.drawString(String("Score ") + score_, 8, 51, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Streak ") + streak_, SCREEN_WIDTH - 8, 35, 2);
    tft.drawString(String("Best ") + bestStreak_, SCREEN_WIDTH - 8, 51, 1);

    if (mode_ != Mode::Make) {
        Ui::drawLabel(tft, Rect{8, 58, 304, 12}, modeName(), Ui::muted(), 1, Align::Center);
    }
    if (mode_ == Mode::Total) {
        drawCoinGroup(tft, Rect{18, 78, 284, 70}, groupA_, groupACount_);
        drawOptions(tft);
    } else if (mode_ == Mode::Make) {
        drawMake(tft);
    } else if (mode_ == Mode::More) {
        drawMore(tft);
    } else {
        tft.fillRoundRect(32, 82, 256, 56, 6, Ui::surface());
        tft.drawRoundRect(32, 82, 256, 56, 6, Ui::outline());
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(String("Price ") + centsText(price_), SCREEN_WIDTH / 2, 99, 2);
        tft.drawString(String("Pay ") + centsText(paid_), SCREEN_WIDTH / 2, 123, 2);
        drawOptions(tft);
    }

    if (roundComplete_) {
        tft.fillRoundRect(62, 126, 196, 42, 8, Ui::panel());
        tft.drawRoundRect(62, 126, 196, 42, 8, GREEN);
        Ui::drawLabel(tft, Rect{64, 132, 192, 28}, "Correct - tap next", GREEN, 2, Align::Center);
    } else if (flashIndex_ >= 0) {
        tft.setTextColor(RED, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Try again", SCREEN_WIDTH / 2, 226, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
