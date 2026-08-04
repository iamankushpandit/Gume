#include "ShapeColorGame.h"

namespace {
constexpr uint16_t SELECTED = 0xFEC0;
constexpr uint16_t EMPTY_TARGET = 0xD6BA;
}

const char* ShapeColorGame::title() const {
    return "Shape & Color";
}

void ShapeColorGame::begin(GameHost& host) {
    items_[0] = Item{Shape::Circle, Ui::rgb(225, 60, 80), "red circle"};
    items_[1] = Item{Shape::Square, Ui::rgb(42, 117, 213), "blue square"};
    items_[2] = Item{Shape::Triangle, Ui::rgb(39, 157, 112), "green triangle"};
    items_[3] = Item{Shape::Star, Ui::rgb(244, 188, 48), "yellow star"};
    bestTaps_ = static_cast<uint16_t>(host.board().getScore("shapeBest", 0));
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

Rect ShapeColorGame::choiceRect(uint8_t index) const {
    return Rect{16, static_cast<int16_t>(58 + index * 43), 136, 38};
}

Rect ShapeColorGame::targetRect(uint8_t index) const {
    return Rect{168, static_cast<int16_t>(58 + index * 43), 136, 38};
}

void ShapeColorGame::drawShape(TFT_eSPI& tft, Shape shape, int16_t cx, int16_t cy, int16_t size, uint16_t color, bool filled) const {
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
            Ui::drawStarShape(tft, cx, cy, size, color, filled);
            break;
    }
}

void ShapeColorGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (allMatched()) {
        newRound();
        host.board().beepOk();
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (!matched_[i] && choiceRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            ++taps_;
            selected_ = i;
            host.board().beepOk();
            markDirty();
            return;
        }
    }

    if (selected_ < 0) {
        return;
    }

    for (uint8_t target = 0; target < 4; ++target) {
        if (targetRect(target).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            ++taps_;
            if (targetOrder_[target] == selected_) {
                matched_[selected_] = true;
                selected_ = -1;
                host.board().beepOk();
                if (allMatched() && host.board().saveBestScore("shapeBest", taps_, true)) {
                    bestTaps_ = taps_;
                }
            } else {
                host.board().beepError();
            }
            markDirty();
            return;
        }
    }
}

void ShapeColorGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    Ui::clear(tft);
    Ui::drawTopBar(tft, title());
    Ui::drawLabel(tft, Rect{10, 32, 300, 18}, "Tap a shape, then its matching outline", Ui::text(), 2, Align::Center);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    tft.drawString(bestTaps_ > 0 ? String("Taps ") + taps_ + " Best " + bestTaps_ : String("Taps ") + taps_, SCREEN_WIDTH - 8, 50, 1);

    for (uint8_t i = 0; i < 4; ++i) {
        const Rect choice = choiceRect(i);
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
        const Rect target = targetRect(i);
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
        tft.drawString("Great matching!", SCREEN_WIDTH / 2, 112, 4);
        tft.drawString("Tap to play again", SCREEN_WIDTH / 2, 137, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
