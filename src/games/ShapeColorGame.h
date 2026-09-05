#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& shapeColorAppMetadata();

class ShapeColorGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase render. renderStatic() is the clear and the top bar;
     * renderDynamic() is everything else. Split mechanically by
     * tools/split_render.py -- see the note in the .cpp. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    enum class Shape : uint8_t {
        Circle,
        Square,
        Triangle,
        Star
    };

    struct Item {
        Shape shape;
        uint16_t color;
        const char* name;
    };

    Rect choiceRect(uint8_t index) const;
    Rect targetRect(uint8_t index) const;
    void newRound();
    bool allMatched() const;
    void drawShape(Ui::Renderer& tft, Shape shape, int16_t cx, int16_t cy, int16_t size, uint16_t color, bool filled) const;

    Item items_[4];
    uint8_t targetOrder_[4] = {};
    bool matched_[4] = {};
    int8_t selected_ = -1;
    uint16_t taps_ = 0;
    uint16_t bestTaps_ = 0;
};
