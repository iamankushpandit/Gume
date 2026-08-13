#include "ScoresGame.h"
#include "engine/ScoreCatalog.h"

const char* ScoresGame::title() const { return "Scores"; }

void ScoresGame::begin(GameHost&) {
    page_ = 0;
    markFullDirty();
}

Rect ScoresGame::rowRect(uint8_t row) const {
    return Rect{8, static_cast<int16_t>(56 + row * 30), 304, 28};
}
Rect ScoresGame::prevRect()   const { return Rect{8, 208, 88, 26}; }
Rect ScoresGame::switchRect() const { return Rect{104, 208, 112, 26}; }
Rect ScoresGame::nextRect()   const { return Rect{224, 208, 88, 26}; }

uint8_t ScoresGame::playedCount(GameHost& host) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < SCORE_CATALOG_COUNT; ++i) {
        if (host.board().hasScore(SCORE_CATALOG[i].bestKey)) ++n;
    }
    return n;
}

void ScoresGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;

    const uint8_t total = playedCount(host);
    const uint8_t pages = max<uint8_t>(1, (total + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);

    if (prevRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
        --page_; markFullDirty(); return;
    }
    if (nextRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ + 1 < pages) {
        ++page_; markFullDirty(); return;
    }
    if (switchRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openProfiles();
        return;
    }
}

void ScoresGame::render(GameHost& host) {
    Board& board = host.board();
    TFT_eSPI& tft = board.display();
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(board.profileName(board.activeProfile()), 8, 34, 2);

    // Column headings, so "best" and "worst" are never guessed at.
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    tft.drawString("best", 244, 38, 1);
    tft.drawString("worst", 306, 38, 1);
    tft.setTextDatum(TL_DATUM);

    const uint8_t total = playedCount(host);
    if (total == 0) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20},
                      "No games played yet", Ui::muted(), 2, Align::Center);
    }

    // Walk the catalog, skipping games this child has not played.
    uint8_t seen = 0;
    uint8_t drawn = 0;
    const uint8_t first = static_cast<uint8_t>(page_ * ROWS_PER_PAGE);
    for (uint8_t i = 0; i < SCORE_CATALOG_COUNT && drawn < ROWS_PER_PAGE; ++i) {
        const ScoreEntry& e = SCORE_CATALOG[i];
        if (!board.hasScore(e.bestKey)) continue;
        if (seen++ < first) continue;

        const Rect r = rowRect(drawn++);
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());

        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.drawString(e.label, r.x + 8, r.y + r.h / 2, 2);

        const uint32_t best  = board.getScore(e.bestKey, 0);
        const uint32_t worst = board.worstScore(e.bestKey, best);

        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(Ui::success(), Ui::surface());
        tft.drawString(String(best) + e.unit, 244, r.y + r.h / 2, 2);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(String(worst) + e.unit, 306, r.y + r.h / 2, 2);

        if (e.lowerIsBetter) {
            // Otherwise a smaller "best" than "worst" reads like a mistake.
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString("lower is better", r.x + 100, r.y + r.h / 2, 1);
        }
    }

    const uint8_t pages = max<uint8_t>(1, (total + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
    const bool canPrev = page_ > 0;
    const bool canNext = page_ + 1 < pages;
    Ui::drawPagerButton(tft, prevRect(), "Prev", canPrev);
    Ui::drawButton(tft, switchRect(), "Switch player", Ui::rgb(36, 132, 204),
                   Ui::outline(), TFT_WHITE, false, 2);
    Ui::drawPagerButton(tft, nextRect(), "Next", canNext);
    tft.setTextDatum(TL_DATUM);
}
