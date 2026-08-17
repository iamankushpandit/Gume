#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& oddOneOutAppMetadata();

class OddOneOutGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

private:
    enum class Shape : uint8_t {
        Circle,
        Square,
        Triangle
    };

    enum class Difference : uint8_t {
        Color,
        Shape,
        Size,
        Orientation
    };

    /* A 3x3 tray of shapes. The tray keeps its 3 columns in both orientations
     * -- the odd-one-out reading depends on comparing a row at a glance -- and
     * takes whatever height is left above the verdict banner. */
    Rect itemBand(const Ui::Frame& f) const;
    Rect itemRect(const Ui::Frame& f, uint8_t index) const;
    Rect verdictRect(const Ui::Frame& f) const;
    int8_t touchedItem(const Ui::Frame& f, int16_t x, int16_t y) const;
    void newRound();
    void drawItem(Ui::Renderer& tft, const Rect& r, bool odd) const;
    void drawShape(Ui::Renderer& tft, Shape shape, int16_t cx, int16_t cy, int16_t size, uint16_t color, bool inverted) const;

    Shape baseShape_ = Shape::Circle;
    Shape oddShape_ = Shape::Square;
    uint16_t baseColor_ = 0;
    uint16_t oddColor_ = 0;
    int16_t baseSize_ = 13;
    int16_t oddSize_ = 13;
    bool baseInverted_ = false;
    bool oddInverted_ = false;
    Difference difference_ = Difference::Color;
    uint8_t oddIndex_ = 0;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    bool flashError_ = false;
    bool flashCorrect_ = false;
    uint32_t flashUntil_ = 0;
};

