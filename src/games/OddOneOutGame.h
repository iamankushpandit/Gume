#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& oddOneOutAppMetadata();

class OddOneOutGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    /* Two-phase. Within a round the nine shapes never change -- a new round is
     * a full repaint -- so the only thing moving is the flash panel on its
     * timer. See docs/RENDER_AUDIT.md. */
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

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

    Rect itemRect(uint8_t index) const;
    int8_t touchedItem(int16_t x, int16_t y) const;
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

    /* Whether the flash panel is currently painted. It is the only thing on
     * this screen that both appears AND disappears without a new round, so it
     * is the only thing that has to erase itself. */
    bool drawnFlash_ = false;
    uint16_t drawnScore_ = 0xFFFF;
    uint16_t drawnBest_ = 0xFFFF;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    bool flashError_ = false;
    bool flashCorrect_ = false;
    uint32_t flashUntil_ = 0;
};

