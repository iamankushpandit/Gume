#include "FractionGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t HUD_TOP = TOP_BAR_HEIGHT + 5;
constexpr int16_t HUD_SECOND = TOP_BAR_HEIGHT + 21;
constexpr int16_t PROMPT_Y = TOP_BAR_HEIGHT + 36;
constexpr int16_t TEXT_Y = TOP_BAR_HEIGHT + 59;
constexpr int16_t PIE_CY = TOP_BAR_HEIGHT + 85;
constexpr int16_t OPTION_TOP = TOP_BAR_HEIGHT + 134;
constexpr int16_t PIE_OPTION_TOP = TOP_BAR_HEIGHT + 82;
constexpr int16_t COMPARE_TOP = TOP_BAR_HEIGHT + 146;
constexpr uint8_t OPTION_COUNT = 4;

constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t PIE_EMPTY = 0x39EF;
constexpr uint16_t PIE_FILL = 0xFEC0;
constexpr uint16_t PIE_LINE = 0x0843;
constexpr uint8_t DENOMS_L1[] = {2, 4};
constexpr uint8_t DENOMS_L3[] = {2, 3, 4, 8};
constexpr uint8_t DENOMS_L5[] = {2, 3, 4, 5, 6, 8};

constexpr AppScoreInfo FRACTION_SCORE = {
    "fractions", "Fractions", "fracBest", "pts", false
};

constexpr AppMetadata FRACTION_METADATA = {
    "fractions",
    "Fractions",
    nullptr,
    "pie slices",
    "Fractions",
    "Match the pie chart.",
    &FRACTION_SCORE,
    LauncherIcon::Fractions,
    11,
    true,
};
}

const AppMetadata& fractionAppMetadata() {
    return FRACTION_METADATA;
}

const char* FractionGame::title() const {
    return fractionAppMetadata().screenTitle != nullptr
        ? fractionAppMetadata().screenTitle
        : fractionAppMetadata().title;
}

void FractionGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(fractionAppMetadata().score->bestKey, 0));
    newRound();
    markDirty();
}

uint8_t FractionGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 4);
}

FractionGame::Mode FractionGame::nextMode() const {
    const uint8_t currentLevel = level();
    if (currentLevel == 1) {
        return Mode::PickText;
    }
    if (currentLevel == 2) {
        return score_ % 2 == 0 ? Mode::PickText : Mode::PickPie;
    }
    if (currentLevel == 3) {
        return static_cast<Mode>(score_ % 3);
    }
    return static_cast<Mode>((score_ + 1) % 3);
}

FractionGame::Fraction FractionGame::randomFraction() const {
    const uint8_t currentLevel = level();
    const uint8_t* denoms = DENOMS_L1;
    uint8_t denomCount = sizeof(DENOMS_L1) / sizeof(DENOMS_L1[0]);
    if (currentLevel >= 5) {
        denoms = DENOMS_L5;
        denomCount = sizeof(DENOMS_L5) / sizeof(DENOMS_L5[0]);
    } else if (currentLevel >= 3) {
        denoms = DENOMS_L3;
        denomCount = sizeof(DENOMS_L3) / sizeof(DENOMS_L3[0]);
    }
    Fraction fraction;
    fraction.denominator = denoms[random(denomCount)];
    fraction.numerator = static_cast<uint8_t>(random(1, fraction.denominator));
    return fraction;
}

FractionGame::Fraction FractionGame::randomComparableFraction(const Fraction& other, bool allowSameDenominator) const {
    Fraction candidate;
    uint8_t attempts = 0;
    do {
        candidate = randomFraction();
        if (!allowSameDenominator && candidate.denominator == other.denominator) {
            candidate.denominator = other.denominator == 8 ? 3 : 8;
            candidate.numerator = static_cast<uint8_t>(random(1, candidate.denominator));
        }
        ++attempts;
    } while ((sameFraction(candidate, other) || candidate.denominator == 0) && attempts < 80);
    return candidate;
}

bool FractionGame::sameFraction(const Fraction& a, const Fraction& b) const {
    return static_cast<uint16_t>(a.numerator) * b.denominator == static_cast<uint16_t>(b.numerator) * a.denominator;
}

bool FractionGame::greaterThan(const Fraction& a, const Fraction& b) const {
    return static_cast<uint16_t>(a.numerator) * b.denominator > static_cast<uint16_t>(b.numerator) * a.denominator;
}

String FractionGame::fractionText(const Fraction& fraction) const {
    return String(fraction.numerator) + "/" + fraction.denominator;
}

bool FractionGame::optionExists(const Fraction& fraction, uint8_t upTo) const {
    for (uint8_t i = 0; i < upTo; ++i) {
        if (sameFraction(options_[i], fraction)) {
            return true;
        }
    }
    return false;
}

void FractionGame::makeOptions(const Fraction& correct) {
    correctButton_ = random(4);
    for (uint8_t i = 0; i < 4; ++i) {
        options_[i] = Fraction{};
    }
    options_[correctButton_] = correct;

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) {
            continue;
        }
        Fraction candidate;
        uint8_t attempts = 0;
        do {
            candidate = randomFraction();
            if (level() <= 2 && random(2) == 0) {
                candidate.denominator = random(2) == 0 ? 2 : 4;
            }
            candidate.numerator = static_cast<uint8_t>(random(0, candidate.denominator + 1));
            ++attempts;
        } while ((sameFraction(candidate, correct) || optionExists(candidate, i)) && attempts < 80);

        while (sameFraction(candidate, correct) || optionExists(candidate, i)) {
            candidate.denominator = level() <= 2 ? 4 : candidate.denominator;
            candidate.numerator = static_cast<uint8_t>((candidate.numerator + 1) % (candidate.denominator + 1));
        }
        options_[i] = candidate;
    }
}

void FractionGame::newRound() {
    mode_ = nextMode();
    selected_ = -1;
    flashIndex_ = -1;
    flashUntil_ = 0;
    roundComplete_ = false;
    target_ = randomFraction();

    if (mode_ == Mode::Compare) {
        const bool allowSame = level() < 5;
        other_ = randomComparableFraction(target_, allowSame);
        correctButton_ = greaterThan(target_, other_) ? 0 : 1;
    } else {
        makeOptions(target_);
    }
}

void FractionGame::markCorrect(AppContext& host) {
    ++score_;
    ++streak_;
    if (host.saveBestScore(fractionAppMetadata().score->bestKey, streak_, false)) {
        bestStreak_ = streak_;
    }
    roundComplete_ = true;
    flashIndex_ = -1;
    host.beepOk();
}

void FractionGame::markWrong(int8_t index) {
    streak_ = 0;
    flashIndex_ = index;
    flashUntil_ = millis() + 420UL;
}

Rect FractionGame::optionRect(const Ui::Frame& f, uint8_t index) const {
    const Rect band{18, OPTION_TOP, static_cast<int16_t>(f.w - 36),
                    static_cast<int16_t>(f.h - OPTION_TOP - 16)};
    const uint8_t cols = Ui::answerColumns(f, OPTION_COUNT);
    const uint8_t rows = static_cast<uint8_t>(OPTION_COUNT / cols);
    return Ui::gridCell(band, cols, rows, index, 6);
}

Rect FractionGame::pieOptionRect(const Ui::Frame& f, uint8_t index) const {
    int16_t h = static_cast<int16_t>(f.h - PIE_OPTION_TOP - 16);
    if (h > 110) {
        h = 110;
    }
    const Rect band{34, PIE_OPTION_TOP, static_cast<int16_t>(f.w - 68), h};
    return Ui::gridCell(band, 2, 2, index, 8);
}

Rect FractionGame::compareRect(const Ui::Frame& f, uint8_t index) const {
    /* Two side-by-side choices; portrait has room for both at 104 wide, so
     * this one keeps its pair rather than stacking. */
    const Rect band{34, COMPARE_TOP, static_cast<int16_t>(f.w - 68), 34};
    return Ui::gridCell(band, 2, 1, index, 12);
}

void FractionGame::update(AppContext& host, const TouchPoint& touch) {
    const Ui::Frame f = Ui::frame(host.display());
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

    if (mode_ == Mode::PickText) {
        for (uint8_t i = 0; i < 4; ++i) {
            if (optionRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                selected_ = i;
                if (i == correctButton_) {
                    markCorrect(host);
                } else {
                    markWrong(i);
                    host.beepError();
                }
                markDirty();
                return;
            }
        }
    } else if (mode_ == Mode::PickPie) {
        for (uint8_t i = 0; i < 4; ++i) {
            if (pieOptionRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                selected_ = i;
                if (i == correctButton_) {
                    markCorrect(host);
                } else {
                    markWrong(i);
                    host.beepError();
                }
                markDirty();
                return;
            }
        }
    } else {
        for (uint8_t i = 0; i < 2; ++i) {
            if (compareRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                selected_ = i;
                if (i == correctButton_) {
                    markCorrect(host);
                } else {
                    markWrong(i);
                    host.beepError();
                }
                markDirty();
                return;
            }
        }
    }
}

void FractionGame::fillSlice(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, float startAngle, float endAngle, uint16_t color) const {
    const float step = PI / 24.0f;
    float angle = startAngle;
    while (angle < endAngle) {
        const float next = min(endAngle, angle + step);
        const int16_t x1 = cx + static_cast<int16_t>(cosf(angle) * radius);
        const int16_t y1 = cy + static_cast<int16_t>(sinf(angle) * radius);
        const int16_t x2 = cx + static_cast<int16_t>(cosf(next) * radius);
        const int16_t y2 = cy + static_cast<int16_t>(sinf(next) * radius);
        tft.fillTriangle(cx, cy, x1, y1, x2, y2, color);
        angle = next;
    }
}

void FractionGame::drawPie(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, const Fraction& fraction, bool selected) const {
    tft.fillCircle(cx, cy, radius + 3, selected ? Ui::warning() : Ui::surface());
    tft.fillCircle(cx, cy, radius, PIE_EMPTY);
    for (uint8_t i = 0; i < fraction.numerator; ++i) {
        const float start = -PI / 2.0f + i * TWO_PI / fraction.denominator;
        const float end = -PI / 2.0f + (i + 1) * TWO_PI / fraction.denominator;
        fillSlice(tft, cx, cy, radius, start, end, PIE_FILL);
    }
    tft.drawCircle(cx, cy, radius, PIE_LINE);
    for (uint8_t i = 0; i < fraction.denominator; ++i) {
        const float angle = -PI / 2.0f + i * TWO_PI / fraction.denominator;
        const int16_t x = cx + static_cast<int16_t>(cosf(angle) * radius);
        const int16_t y = cy + static_cast<int16_t>(sinf(angle) * radius);
        tft.drawLine(cx, cy, x, y, PIE_LINE);
    }
}

void FractionGame::drawTextOptions(Ui::Renderer& tft, const Ui::Frame& f) const {
    drawPie(tft, f.cx(), PIE_CY, 38, target_);
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
        Ui::drawButton(tft, optionRect(f, i), fractionText(options_[i]), fill, Ui::outline(), text, false, 2);
    }
}

void FractionGame::drawPieOptions(Ui::Renderer& tft, const Ui::Frame& f) const {
    tft.fillRoundRect(96, 74, 128, 30, 6, Ui::panel());
    tft.drawRoundRect(96, 74, 128, 30, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(fractionText(target_), f.cx(), TEXT_Y, 4);
    for (uint8_t i = 0; i < 4; ++i) {
        const Rect r = pieOptionRect(f, i);
        bool selected = roundComplete_ ? i == correctButton_ : flashIndex_ == i;
        if (flashIndex_ == i) {
            tft.fillRoundRect(r.x - 4, r.y - 4, r.w + 8, r.h + 8, 6, RED);
        }
        drawPie(tft, r.x + r.w / 2, r.y + r.h / 2, 18, options_[i], selected);
    }
}

void FractionGame::drawCompare(Ui::Renderer& tft, const Ui::Frame& f) const {
    drawPie(tft, 92, 112, 38, target_, roundComplete_ && correctButton_ == 0);
    drawPie(tft, 228, 112, 38, other_, roundComplete_ && correctButton_ == 1);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(fractionText(target_), 92, 156, 2);
    tft.drawString(fractionText(other_), 228, 156, 2);
    for (uint8_t i = 0; i < 2; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (roundComplete_ && i == correctButton_) {
            fill = GREEN;
            text = TFT_BLACK;
        } else if (flashIndex_ == i) {
            fill = RED;
            text = TFT_BLACK;
        }
        Ui::drawButton(tft, compareRect(f, i), i == 0 ? "Left" : "Right", fill, Ui::outline(), text, false, 2);
    }
}

void FractionGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Level ") + level(), 8, 35, 2);
    tft.drawString(String("Score ") + score_, 8, 51, 1);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Streak ") + streak_, f.w - 8, HUD_TOP, 2);
    tft.drawString(String("Best ") + bestStreak_, f.w - 8, HUD_SECOND, 1);

    const char* prompt = mode_ == Mode::PickText ? "Pick the matching fraction" : (mode_ == Mode::PickPie ? "Pick the matching pie" : "Which fraction is bigger?");
    Ui::drawLabel(tft, Rect{8, PROMPT_Y, static_cast<int16_t>(f.w - 16), 12},
                  prompt, Ui::muted(), 1, Align::Center);

    if (mode_ == Mode::PickText) {
        drawTextOptions(tft, f);
    } else if (mode_ == Mode::PickPie) {
        drawPieOptions(tft, f);
    } else {
        drawCompare(tft, f);
    }

    if (roundComplete_) {
        tft.fillRoundRect(62, 126, 196, 42, 8, Ui::panel());
        tft.drawRoundRect(62, 126, 196, 42, 8, GREEN);
        Ui::drawLabel(tft, Ui::centreIn(Rect{0, static_cast<int16_t>(PIE_CY + 17), f.w, 28}, 192, 28),
                      "Correct - tap next", GREEN, 2, Align::Center);
    } else if (flashIndex_ >= 0) {
        tft.setTextColor(RED, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Try again", f.cx(), static_cast<int16_t>(f.h - 14), 2);
    }
    tft.setTextDatum(TL_DATUM);
}
