#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& microkuAppMetadata();

class MicrokuGame : public AppGame {
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
    void loadStage(uint8_t stageIndex);
    Rect cellRect(uint8_t row, uint8_t col) const;
    Rect numberRect(uint8_t value) const;
    int8_t touchedCell(int16_t x, int16_t y) const;
    int8_t touchedNumber(int16_t x, int16_t y) const;
    bool complete() const;
    void advanceAfterSolve(AppContext& host);

    uint8_t stageIndex_ = 0;
    uint8_t size_ = 2;
    uint8_t boxRows_ = 1;
    uint8_t boxCols_ = 2;
    uint8_t board_[36] = {};
    uint8_t solution_[36] = {};
    bool fixed_[36] = {};
    int8_t selected_ = -1;
    uint16_t solvedThisRun_ = 0;
    uint8_t bestSize_ = 0;
    String message_;
    uint32_t messageUntil_ = 0;
    bool solved_ = false;
};
