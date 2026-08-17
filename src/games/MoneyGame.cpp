#include "MoneyGame.h"
#include "engine/AppRegistry.h"

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
    10,
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

void MoneyGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(moneyAppMetadata().score->bestKey, 0));
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

void MoneyGame::markCorrect(AppContext& host) {
    ++score_;
    ++streak_;
    if (host.saveBestScore(moneyAppMetadata().score->bestKey, streak_, false)) {
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

/* The four answer buttons stay a 2x2 grid in both orientations. The labels
 * are short money amounts, so two columns read fine even on a 240px panel,
 * and stacking four of them would eat the height the coins need. */
Rect MoneyGame::optionsBand(const Ui::Frame& f) const {
    return Rect{14, static_cast<int16_t>(f.h - 80), static_cast<int16_t>(f.w - 28), 68};
}
Rect MoneyGame::optionRect(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(optionsBand(f), 2, 2, index, 8);
}

/* The coin tray fills whatever is left between the mode label and the
 * answers, so the coins spread out on a bigger panel instead of crowding
 * into the top 70px of it. drawCoinGroup already derives its column count
 * from the width it is given. */
Rect MoneyGame::totalCoinRect(const Ui::Frame& f) const {
    return Rect{18, 78, static_cast<int16_t>(f.w - 36),
                static_cast<int16_t>(optionsBand(f).y - 90)};
}

Rect MoneyGame::makeActionRow(const Ui::Frame& f) const {
    return Rect{36, static_cast<int16_t>(f.h - 42), static_cast<int16_t>(f.w - 72), 30};
}
Rect MoneyGame::clearRect(const Ui::Frame& f) const {
    return Ui::gridCell(makeActionRow(f), 2, 1, 0, 20);
}
Rect MoneyGame::doneRect(const Ui::Frame& f) const {
    return Ui::gridCell(makeActionRow(f), 2, 1, 1, 20);
}

/* The tray holds between two and five coins depending on level, so it keeps
 * a fixed coin size and centres the row rather than stretching the coins to
 * fill the width -- a penny that changes size with the level reads as a
 * different coin. The pitch only shrinks if the row would not otherwise fit. */
Rect MoneyGame::trayRect(const Ui::Frame& f, uint8_t index) const {
    const uint8_t n = max<uint8_t>(1, allowedCoinCount());
    int16_t pitch = static_cast<int16_t>((f.w - 28) / n);
    if (pitch > 59) pitch = 59;
    int16_t w = static_cast<int16_t>(pitch - 7);
    if (w > 52) w = 52;
    const int16_t span = static_cast<int16_t>((n - 1) * pitch + w);
    const int16_t x0 = static_cast<int16_t>(f.cx() - span / 2);
    return Rect{static_cast<int16_t>(x0 + index * pitch),
                static_cast<int16_t>(makeActionRow(f).y - 52), w, 34};
}

Rect MoneyGame::makeCoinRect(const Ui::Frame& f) const {
    const int16_t top = 82;
    return Rect{18, top, static_cast<int16_t>(f.w - 36),
                static_cast<int16_t>(trayRect(f, 0).y - top - 10)};
}

/* On a 320px panel gridCell lands on the authored 32/178 at 110 wide, and
 * the two coin groups on 14/166 at 140 wide. */
Rect MoneyGame::moreButtonRow(const Ui::Frame& f) const {
    return Rect{32, static_cast<int16_t>(f.h - 64), static_cast<int16_t>(f.w - 64), 34};
}
Rect MoneyGame::moreRect(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(moreButtonRow(f), 2, 1, index, 36);
}
Rect MoneyGame::moreGroupRect(const Ui::Frame& f, uint8_t index) const {
    const Rect band{14, 72, static_cast<int16_t>(f.w - 28),
                    static_cast<int16_t>(moreButtonRow(f).y - 88)};
    return Ui::gridCell(band, 2, 1, index, 12);
}

Rect MoneyGame::pricePanelRect(const Ui::Frame& f) const {
    int16_t w = static_cast<int16_t>(f.w - 64);
    if (w > 256) w = 256;
    return Rect{static_cast<int16_t>(f.cx() - w / 2), 82, w, 56};
}

/* Sits just above the answers, whatever height the panel is. */
Rect MoneyGame::correctBannerRect(const Ui::Frame& f) const {
    int16_t w = static_cast<int16_t>(f.w - 40);
    if (w > 196) w = 196;
    return Rect{static_cast<int16_t>(f.cx() - w / 2),
                static_cast<int16_t>(optionsBand(f).y - 34), w, 42};
}

void MoneyGame::update(AppContext& host, const TouchPoint& touch) {
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

    const Ui::Frame f = Ui::frame(host.display());

    if (mode_ == Mode::Make) {
        if (clearRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            built_ = 0;
            makeCount_ = 0;
            markDirty();
            return;
        }
        if (doneRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
            if (trayRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && makeCount_ < sizeof(makeCoins_) / sizeof(makeCoins_[0])) {
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
            if (moreRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
        if (optionRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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

void MoneyGame::drawCoin(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t value) const {
    tft.fillCircle(cx, cy, 12, coinFill(value));
    tft.drawCircle(cx, cy, 12, COIN_OUTLINE);
    tft.setTextColor(coinText(value), coinFill(value));
    tft.setTextDatum(MC_DATUM);
    tft.drawString(centsText(value), cx, cy, 1);
    tft.setTextDatum(TL_DATUM);
}

void MoneyGame::drawCoinGroup(Ui::Renderer& tft, const Rect& r, const uint8_t* coins, uint8_t count) const {
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

void MoneyGame::drawOptions(Ui::Renderer& tft, const Ui::Frame& f) const {
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
        Ui::drawButton(tft, optionRect(f, i), centsText(options_[i]), fill, Ui::outline(), text, false, 2);
    }
}

void MoneyGame::drawMake(Ui::Renderer& tft, const Ui::Frame& f) const {
    drawCoinGroup(tft, makeCoinRect(f), makeCoins_, makeCount_);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(String("Make ") + centsText(target_) + "   Built " + centsText(built_), f.cx(), 70, 2);

    const uint8_t allowed = allowedCoinCount();
    for (uint8_t i = 0; i < allowed; ++i) {
        const Rect r = trayRect(f, i);
        Ui::drawButton(tft, r, "", Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        drawCoin(tft, r.x + r.w / 2, r.y + r.h / 2, COIN_VALUES[i]);
    }
    Ui::drawButton(tft, clearRect(f), "Clear", Ui::surface(), Ui::outline(), Ui::text(), false, 2);
    Ui::drawButton(tft, doneRect(f), "Done", flashIndex_ == 6 ? RED : BLUE, Ui::outline(), flashIndex_ == 6 ? TFT_BLACK : TFT_WHITE, false, 2);
}

void MoneyGame::drawMore(Ui::Renderer& tft, const Ui::Frame& f) const {
    drawCoinGroup(tft, moreGroupRect(f, 0), groupA_, groupACount_);
    drawCoinGroup(tft, moreGroupRect(f, 1), groupB_, groupBCount_);
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
        Ui::drawButton(tft, moreRect(f, i), i == 0 ? "Left" : "Right", fill, Ui::outline(), text, false, 2);
    }
}

void MoneyGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Level ") + level(), 8, 35, 2);
    tft.drawString(String("Score ") + score_, 8, 51, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Streak ") + streak_, static_cast<int16_t>(f.w - 8), 35, 2);
    tft.drawString(String("Best ") + bestStreak_, static_cast<int16_t>(f.w - 8), 51, 1);

    if (mode_ != Mode::Make) {
        Ui::drawLabel(tft, Rect{8, 58, static_cast<int16_t>(f.w - 16), 12},
                      modeName(), Ui::muted(), 1, Align::Center);
    }
    if (mode_ == Mode::Total) {
        drawCoinGroup(tft, totalCoinRect(f), groupA_, groupACount_);
        drawOptions(tft, f);
    } else if (mode_ == Mode::Make) {
        drawMake(tft, f);
    } else if (mode_ == Mode::More) {
        drawMore(tft, f);
    } else {
        const Rect price = pricePanelRect(f);
        tft.fillRoundRect(price.x, price.y, price.w, price.h, 6, Ui::surface());
        tft.drawRoundRect(price.x, price.y, price.w, price.h, 6, Ui::outline());
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(String("Price ") + centsText(price_), f.cx(),
                       static_cast<int16_t>(price.y + 17), 2);
        tft.drawString(String("Pay ") + centsText(paid_), f.cx(),
                       static_cast<int16_t>(price.y + 41), 2);
        drawOptions(tft, f);
    }

    if (roundComplete_) {
        const Rect banner = correctBannerRect(f);
        tft.fillRoundRect(banner.x, banner.y, banner.w, banner.h, 8, Ui::panel());
        tft.drawRoundRect(banner.x, banner.y, banner.w, banner.h, 8, GREEN);
        Ui::drawLabel(tft, Rect{static_cast<int16_t>(banner.x + 2),
                                static_cast<int16_t>(banner.y + 6),
                                static_cast<int16_t>(banner.w - 4), 28},
                      "Correct - tap next", GREEN, 2, Align::Center);
    } else if (flashIndex_ >= 0) {
        tft.setTextColor(RED, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Try again", f.cx(), static_cast<int16_t>(f.h - 14), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
