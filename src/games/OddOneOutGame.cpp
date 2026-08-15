#include "OddOneOutGame.h"

namespace {
constexpr uint16_t COLORS[] = {
    0xF9EA, 0x5D9F, 0x37F0, 0xFFE6, 0xF81F, 0xFC00
};
constexpr uint8_t COLOR_COUNT = sizeof(COLORS) / sizeof(COLORS[0]);
}

const char* OddOneOutGame::title() const {
    return "Odd One Out";
}

void OddOneOutGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore("oddBest", 0));
    newRound();
    markDirty();
}

Rect OddOneOutGame::itemRect(uint8_t index) const {
    const uint8_t col = index % 3;
    const uint8_t row = index / 3;
    return Rect{static_cast<int16_t>(36 + col * 86), static_cast<int16_t>(70 + row * 50), 66, 40};
}

void OddOneOutGame::newRound() {
    oddIndex_ = random(9);
    baseShape_ = static_cast<Shape>(random(3));
    oddShape_ = baseShape_;
    baseColor_ = COLORS[random(COLOR_COUNT)];
    oddColor_ = baseColor_;
    baseSize_ = 13;
    oddSize_ = 13;
    baseInverted_ = false;
    oddInverted_ = false;

    if (score_ < 5) {
        difference_ = Difference::Color;
    } else if (score_ < 10) {
        difference_ = random(2) == 0 ? Difference::Color : Difference::Shape;
    } else if (score_ < 16) {
        difference_ = static_cast<Difference>(random(3));
    } else {
        difference_ = static_cast<Difference>(random(4));
    }

    switch (difference_) {
        case Difference::Color:
            while (oddColor_ == baseColor_) {
                oddColor_ = COLORS[random(COLOR_COUNT)];
            }
            break;
        case Difference::Shape:
            while (oddShape_ == baseShape_) {
                oddShape_ = static_cast<Shape>(random(3));
            }
            break;
        case Difference::Size:
            oddSize_ = score_ < 16 ? 18 : 16;
            break;
        case Difference::Orientation:
            baseShape_ = Shape::Triangle;
            oddShape_ = Shape::Triangle;
            oddInverted_ = true;
            break;
    }
    flashError_ = false;
    flashCorrect_ = false;
    flashUntil_ = 0;
}

int8_t OddOneOutGame::touchedItem(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 9; ++i) {
        if (itemRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

void OddOneOutGame::drawShape(TFT_eSPI& tft, Shape shape, int16_t cx, int16_t cy, int16_t size, uint16_t color, bool inverted) const {
    switch (shape) {
        case Shape::Circle:
            tft.fillCircle(cx, cy, size, color);
            tft.drawCircle(cx, cy, size, Ui::outline());
            break;
        case Shape::Square:
            tft.fillRoundRect(cx - size, cy - size, size * 2, size * 2, 3, color);
            tft.drawRoundRect(cx - size, cy - size, size * 2, size * 2, 3, Ui::outline());
            break;
        case Shape::Triangle:
            if (inverted) {
                tft.fillTriangle(cx, cy + size, cx - size, cy - size, cx + size, cy - size, color);
                tft.drawTriangle(cx, cy + size, cx - size, cy - size, cx + size, cy - size, Ui::outline());
            } else {
                Ui::drawTriangleShape(tft, cx, cy, size, color, true);
                Ui::drawTriangleShape(tft, cx, cy, size, Ui::outline(), false);
            }
            break;
    }
}

void OddOneOutGame::drawItem(TFT_eSPI& tft, const Rect& r, bool odd) const {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    drawShape(
        tft,
        odd ? oddShape_ : baseShape_,
        r.x + r.w / 2,
        r.y + r.h / 2,
        odd ? oddSize_ : baseSize_,
        odd ? oddColor_ : baseColor_,
        odd ? oddInverted_ : baseInverted_);
}

void OddOneOutGame::update(AppContext& host, const TouchPoint& touch) {
    if ((flashCorrect_ || flashError_) && millis() > flashUntil_) {
        if (flashCorrect_) {
            newRound();
        } else {
            flashError_ = false;
            flashUntil_ = 0;
        }
        markDirty();
    }

    if (!touch.justPressed || flashCorrect_) {
        return;
    }

    const int8_t item = touchedItem(touch.x, touch.y);
    if (item < 0) {
        return;
    }
    if (item == oddIndex_) {
        ++score_;
        ++streak_;
        if (host.saveBestScore("oddBest", streak_, false)) {
            bestStreak_ = streak_;
        }
        flashCorrect_ = true;
        flashError_ = false;
        flashUntil_ = millis() + 650UL;
    } else {
        streak_ = 0;
        flashError_ = true;
        flashUntil_ = millis() + 450UL;
    }
    markDirty();
}

void OddOneOutGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Score ") + score_, 10, 35, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Best ") + bestStreak_, SCREEN_WIDTH - 8, 35, 2);
    Ui::drawLabel(tft, Rect{8, 52, 304, 16}, "Tap the one that is different", Ui::muted(), 1, Align::Center);

    for (uint8_t i = 0; i < 9; ++i) {
        drawItem(tft, itemRect(i), i == oddIndex_);
    }

    if (flashCorrect_ || flashError_) {
        const uint16_t color = flashCorrect_ ? Ui::success() : Ui::error();
        tft.fillRoundRect(84, 196, 152, 30, 8, color);
        tft.setTextColor(TFT_BLACK, color);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(flashCorrect_ ? "Correct" : "Try again", SCREEN_WIDTH / 2, 211, 2);
    }
    tft.setTextDatum(TL_DATUM);
}

