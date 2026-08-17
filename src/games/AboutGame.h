#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

class AboutGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    /* Hands out the baseline for each line of a page, spacing them to the
     * panel it was given rather than to a set of absolute Y values.
     *
     * The absolute values were the bug: the radio page ran to y=226 inside a
     * panel that ends at 196 on a 320x240 screen, so its last two lines drew
     * over the pager. A page states how many small and large lines it has and
     * gets a pitch that fits, whatever panel it is on. */
    class Flow {
    public:
        Flow(const Rect& panel, uint8_t smallLines, uint8_t largeLines);
        int16_t next(uint8_t font);

    private:
        int16_t y_;
        int16_t unit_;
    };

    Rect panelRect(const Ui::Frame& f) const;
    Rect prevRect(const Ui::Frame& f) const;
    Rect nextRect(const Ui::Frame& f) const;
    void drawLine(Ui::Renderer& tft, int16_t y, const String& text, uint8_t font = 2) const;

    uint8_t gamesPerPage(const Rect& panel) const;
    uint8_t gamePageCount(const Rect& panel) const;
    uint8_t pageCount(const Rect& panel) const;

    void renderIntro(Ui::Renderer& tft, const Rect& panel);
    void renderGames(Ui::Renderer& tft, const Rect& panel);
    void renderRadios(Ui::Renderer& tft, const Rect& panel, Board& board);
    void renderCredits(Ui::Renderer& tft, const Rect& panel);

    uint8_t page_ = 0;
};
