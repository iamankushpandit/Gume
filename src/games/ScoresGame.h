#pragma once

#include "engine/Game.h"
#include "hal/Board.h"
#include "ui/Ui.h"

/*
 * Best and worst result per game for the current child (Mine tab).
 * Device-wide best and its holder (Device tab).
 *
 * Only games that have actually been played appear. Games where a lower
 * number is better (Maze time, Slide moves) are labelled so the pair is not
 * read backwards.
 */
class ScoresGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum Tab : uint8_t {
        Mine = 0,
        Device = 1,
    };

    static constexpr uint8_t ROWS_PER_PAGE = 5;
    static constexpr uint8_t MAX_DEVICE_ROWS = 32;  // >= SCORE_CATALOG_COUNT

    struct DeviceBest {
        uint8_t catalogIndex;
        uint32_t value;
        uint8_t holder;  // profile index, 0xFF = nobody
    };

    Rect rowRect(uint8_t row) const;
    Rect mineTabRect() const;
    Rect deviceTabRect() const;
    Rect prevRect() const;
    Rect nextRect() const;
    Rect switchRect() const;
    uint8_t playedCount(GameHost& host) const;
    void buildDeviceTable(GameHost& host);
    uint8_t deviceRowCount() const;

    Tab activeTab_ = Tab::Mine;
    uint8_t page_ = 0;

    DeviceBest deviceRows_[MAX_DEVICE_ROWS];
    uint8_t deviceRowCount_ = 0;
    char holderNames_[Board::MAX_KIDS][Board::PROFILE_NAME_MAX + 1];
    bool deviceStale_ = true;
};
