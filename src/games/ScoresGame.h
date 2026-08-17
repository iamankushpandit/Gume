#pragma once

#include "engine/Game.h"
#include "hal/Board.h"
#include "ui/GameLayout.h"
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

    /* Rows per page is derived from the panel, not fixed: a 320x240 panel
     * fits the authored five, a 320x480 one fits thirteen. The cap only has
     * to stay at or above what the tallest supported panel can show. */
    static constexpr uint8_t MAX_ROWS_PER_PAGE = 16;
    static constexpr uint8_t MAX_DEVICE_ROWS = 32;  // >= scored playable apps

    struct DeviceBest {
        uint8_t catalogIndex;
        uint32_t value;
        uint8_t holder;  // profile index, 0xFF = nobody
    };

    Rect rowRect(const Ui::Frame& f, uint8_t row) const;
    Rect mineTabRect() const;
    Rect deviceTabRect() const;
    Rect pagerRow(const Ui::Frame& f) const;
    Rect prevRect(const Ui::Frame& f) const;
    Rect nextRect(const Ui::Frame& f) const;
    Rect switchRect(const Ui::Frame& f) const;
    uint8_t rowsPerPage(const Ui::Frame& f) const;
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
