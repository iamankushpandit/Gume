#include "ShapeColorGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t PROMPT_Y = TOP_BAR_HEIGHT + 2;
constexpr int16_t STATS_Y = TOP_BAR_HEIGHT + 20;
constexpr int16_t ROWS_TOP = TOP_BAR_HEIGHT + 28;
constexpr int16_t ROW_H = 38;
constexpr uint8_t ROW_COUNT = 4;

constexpr uint16_t SELECTED = 0xFEC0;
constexpr uint16_t EMPTY_TARGET = 0xD6BA;

constexpr AppScoreInfo SHAPE_COLOR_SCORE = {
    "shapecolor", "Shapes", "shapeBest", "taps", true
};

constexpr AppMetadata SHAPE_COLOR_METADATA = {
    "shapecolor",
    "Shapes",
    "Shape & Color",
    "match outlines",
    "Shapes",
    "Match each shape to its outline.",
    &SHAPE_COLOR_SCORE,
    LauncherIcon::ShapeColor,
    8,
    true,
};

void drawMatchStar(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius,
                   uint16_t color, bool filled) {
    const int16_t inner = static_cast<int16_t>(max<int16_t>(3, radius / 2));
    int16_t px[10];
    int16_t py[10];
    for (uint8_t i = 0; i < 10; ++i) {
        const float angle = -PI / 2.0f + i * PI / 5.0f;
        const int16_t rr = (i % 2 == 0) ? radius : inner;
        px[i] = static_cast<int16_t>(cx + cosf(angle) * rr);
        py[i] = static_cast<int16_t>(cy + sinf(angle) * rr);
    }

    if (filled) {
        for (uint8_t i = 0; i < 10; ++i) {
            const uint8_t next = static_cast<uint8_t>((i + 1) % 10);
            tft.fillTriangle(cx, cy, px[i], py[i], px[next], py[next], color);
        }
    }
    for (uint8_t i = 0; i < 10; ++i) {
        const uint8_t next = static_cast<uint8_t>((i + 1) % 10);
        tft.drawLine(px[i], py[i], px[next], py[next], color);
    }
}
}

const AppMetadata& shapeColorAppMetadata() {
    return SHAPE_COLOR_METADATA;
}

const char* ShapeColorGame::title() const {
    return shapeColorAppMetadata().screenTitle != nullptr
        ? shapeColorAppMetadata().screenTitle
        : shapeColorAppMetadata().title;
}

void ShapeColorGame::begin(AppContext& host) {
    items_[0] = Item{Shape::Circle, Ui::rgb(225, 60, 80), "red circle"};
    items_[1] = Item{Shape::Square, Ui::rgb(42, 117, 213), "blue square"};
    items_[2] = Item{Shape::Triangle, Ui::rgb(39, 157, 112), "green triangle"};
    items_[3] = Item{Shape::Star, Ui::rgb(244, 188, 48), "yellow star"};
    bestTaps_ = static_cast<uint16_t>(host.getScore(shapeColorAppMetadata().score->bestKey, 0));
    newRound();
    markDirty();
}

void ShapeColorGame::newRound() {
    for (uint8_t i = 0; i < 4; ++i) {
        targetOrder_[i] = i;
        matched_[i] = false;
    }
    for (int i = 3; i > 0; --i) {
        const uint8_t j = random(i + 1);
        const uint8_t tmp = targetOrder_[i];
        targetOrder_[i] = targetOrder_[j];
        targetOrder_[j] = tmp;
    }
    selected_ = -1;
    taps_ = 0;
}

bool ShapeColorGame::allMatched() const {
    for (bool matched : matched_) {
        if (!matched) {
            return false;
        }
    }
    return true;
}

Rect ShapeColorGame::columnRect(const Ui::Frame& f, uint8_t index, bool rightColumn) const {
    /* Two 136px columns need 272px of width, which 240 does not have. Split
     * what is there instead: 136 each in landscape, 96 each in portrait. */
    const int16_t colW = static_cast<int16_t>((f.w - 48) / 2);
    const int16_t x = rightColumn ? static_cast<int16_t>(32 + colW) : 16;
    const int16_t bottom = static_cast<int16_t>(f.h - 12);
    const int16_t pitch = static_cast<int16_t>((bottom - ROWS_TOP - ROW_H) / (ROW_COUNT - 1));
    return Rect{x, static_cast<int16_t>(ROWS_TOP + index * pitch), colW, ROW_H};
}

Rect ShapeColorGame::choiceRect(const Ui::Frame& f, uint8_t index) const {
    return columnRect(f, index, false);
}

Rect ShapeColorGame::targetRect(const Ui::Frame& f, uint8_t index) const {
    return columnRect(f, index, true);
}

void ShapeColorGame::drawShape(Ui::Renderer& tft, Shape shape, int16_t cx, int16_t cy, int16_t size, uint16_t color, bool filled) const {
    switch (shape) {
        case Shape::Circle:
            if (filled) {
                tft.fillCircle(cx, cy, size, color);
            } else {
                tft.drawCircle(cx, cy, size, color);
                tft.drawCircle(cx, cy, size - 1, color);
            }
            break;
        case Shape::Square:
            if (filled) {
                tft.fillRoundRect(cx - size, cy - size, size * 2, size * 2, 3, color);
            } else {
                tft.drawRoundRect(cx - size, cy - size, size * 2, size * 2, 3, color);
                tft.drawRoundRect(cx - size + 1, cy - size + 1, size * 2 - 2, size * 2 - 2, 3, color);
            }
            break;
        case Shape::Triangle:
            Ui::drawTriangleShape(tft, cx, cy, size, color, filled);
            break;
        case Shape::Star:
            drawMatchStar(tft, cx, cy, size, color, filled);
            break;
    }
}

void ShapeColorGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (allMatched()) {
        newRound();
        host.beepOk();
        markDirty();
        return;
    }

    const Ui::Frame f = Ui::frame(host.display());
    for (uint8_t i = 0; i < 4; ++i) {
        if (!matched_[i] && choiceRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            ++taps_;
            selected_ = i;
            host.beepOk();
            markDirty();
            return;
        }
    }

    if (selected_ < 0) {
        return;
    }

    for (uint8_t target = 0; target < 4; ++target) {
        if (targetRect(f, target).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            ++taps_;
            if (targetOrder_[target] == selected_) {
                matched_[selected_] = true;
                selected_ = -1;
                host.beepOk();
                if (allMatched() && host.saveBestScore(shapeColorAppMetadata().score->bestKey, taps_, true)) {
                    bestTaps_ = taps_;
                }
            } else {
                host.beepError();
            }
            markDirty();
            return;
        }
    }
}

void ShapeColorGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());
    Ui::drawLabel(tft, Rect{10, PROMPT_Y, static_cast<int16_t>(f.w - 20), 18},
                  "Tap a shape, then its matching outline", Ui::text(), 2, Align::Center);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    char stats[32];
    if (bestTaps_ > 0) {
        snprintf(stats, sizeof(stats), "Taps %u Best %u", taps_, bestTaps_);
    } else {
        snprintf(stats, sizeof(stats), "Taps %u", taps_);
    }
    tft.drawString(stats, f.w - 8, STATS_Y, 1);

    for (uint8_t i = 0; i < 4; ++i) {
        const Rect choice = choiceRect(f, i);
        const uint16_t fill = selected_ == i ? SELECTED : Ui::panel();
        tft.fillRoundRect(choice.x, choice.y, choice.w, choice.h, 6, fill);
        tft.drawRoundRect(choice.x, choice.y, choice.w, choice.h, 6, matched_[i] ? Ui::rgb(45, 154, 96) : TFT_DARKGREY);
        if (!matched_[i]) {
            drawShape(tft, items_[i].shape, choice.x + 24, choice.y + choice.h / 2, 13, items_[i].color, true);
        }
        tft.setTextColor(selected_ == i ? TFT_BLACK : Ui::text(), fill);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(matched_[i] ? "matched" : items_[i].name, choice.x + 46, choice.y + choice.h / 2, matched_[i] ? 2 : 1);

        const uint8_t itemIndex = targetOrder_[i];
        const Rect target = targetRect(f, i);
        tft.fillRoundRect(target.x, target.y, target.w, target.h, 6, Ui::surface());
        tft.drawRoundRect(target.x, target.y, target.w, target.h, 6, EMPTY_TARGET);
        drawShape(tft, items_[itemIndex].shape, target.x + 28, target.y + target.h / 2, 13, matched_[itemIndex] ? items_[itemIndex].color : TFT_DARKGREY, matched_[itemIndex]);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString("target", target.x + 54, target.y + target.h / 2, 2);
    }

    if (allMatched()) {
        tft.fillRoundRect(54, 94, 212, 52, 8, Ui::panel());
        tft.drawRoundRect(54, 94, 212, 52, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        /* 39% down the content area, which is y=112 on a 240px panel -- where
         * this banner has always sat -- and stays visually centred at 320. */
        const int16_t bannerY = static_cast<int16_t>(
            TOP_BAR_HEIGHT + (f.h - TOP_BAR_HEIGHT) * 39 / 100);
        tft.drawString("Great matching!", f.cx(), bannerY, 4);
        tft.drawString("Tap to play again", f.cx(), bannerY + 25, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
