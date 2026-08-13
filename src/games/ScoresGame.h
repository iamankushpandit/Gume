#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

/*
 * Best and worst result per game for the current child.
 *
 * Only games that have actually been played appear -- an empty list of "--"
 * rows tells a child nothing. Games where a lower number is better (Maze time,
 * Slide moves) are labelled so the pair is not read backwards.
 */
class ScoresGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    static constexpr uint8_t ROWS_PER_PAGE = 5;

    Rect rowRect(uint8_t row) const;
    Rect prevRect() const;
    Rect nextRect() const;
    Rect switchRect() const;
    uint8_t playedCount(GameHost& host) const;

    uint8_t page_ = 0;
};
