#include "GoGame.h"

#include <string.h>

/* Board geometry and every pixel of Go.
 *
 * Nothing on the board repaints wholesale during play. Each point has a
 * dirty bit and repaintPoint() paints its whole cell idempotently -- the
 * wood, the grid through it, a star, the stone, the last-move marker, the
 * ghost, a dead cross, a territory mark -- so a move repaints the two to
 * four cells it touched. The panel is parts, each repainted when what it
 * shows changed. A full repaint happens on entering the screen, a new game,
 * Undo, the marking phase and the result card.
 *
 * Every rectangle is derived from the board's step and margin, which are
 * the only two numbers that change between 9x9 and 19x19. */

namespace {

// ---- the board: every rectangle below is derived from these ----------------
constexpr int16_t BX = 5;
constexpr int16_t BY = 34;
constexpr int16_t STEP_SMALL = 23;    // 9x9: eight gaps
constexpr int16_t MARGIN_SMALL = 9;
constexpr int16_t STEP_BIG = 10;      // 19x19: eighteen gaps
constexpr int16_t MARGIN_BIG = 6;
constexpr int16_t PANEL_GAP = 4;
constexpr int16_t BOX_SMALL = 8 * STEP_SMALL + 2 * MARGIN_SMALL;
constexpr int16_t BOX_BIG = 18 * STEP_BIG + 2 * MARGIN_BIG;
static_assert(BY + BOX_SMALL <= GAME_CANVAS_HEIGHT, "the 9x9 board falls off the canvas");
static_assert(BY + BOX_BIG <= GAME_CANVAS_HEIGHT, "the 19x19 board falls off the canvas");
static_assert(GAME_CANVAS_WIDTH - (BX + BOX_SMALL + PANEL_GAP) - 4 >= 100,
              "the 9x9 panel is too narrow for Captured");
static_assert(GAME_CANVAS_WIDTH - (BX + BOX_BIG + PANEL_GAP) - 4 >= 100,
              "the 19x19 panel is too narrow for the magnifier");

/* The 9x9 panel, top to bottom. */
constexpr int16_t TURN_Y = 34, TURN_H = 22;
constexpr int16_t CAPT_Y = 60, CAPT_H = 32;
constexpr int16_t INFO_Y = 96, INFO_H = 34;
constexpr int16_t PASS_Y = 136, PASS_H = 26;
constexpr int16_t UNDO_Y = 166, UNDO_H = 24;
constexpr int16_t ACTION_Y = 194, ACTION_H = 26;
static_assert(ACTION_Y + ACTION_H + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "End's shadow falls off the canvas");

/* The 19x19 panel: the magnifier takes the captures' and the info box's
 * room, Place is a button of its own, Pass and Undo share a row. */
constexpr int16_t BTURN_Y = 34, BTURN_H = 18;
constexpr int16_t MAG_Y = 56, MAG_CELL = 18, MAG_CELLS = 5, MAG_H = MAG_CELL * MAG_CELLS;
constexpr int16_t PLACE_Y = 150, PLACE_H = 22;
constexpr int16_t BPASS_Y = 176, BPASS_H = 20;
constexpr int16_t BACTION_Y = 200, BACTION_H = 22;
static_assert(MAG_Y + MAG_H <= PLACE_Y, "the magnifier runs into Place");
static_assert(BACTION_Y + BACTION_H + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "End's shadow falls off the canvas on 19x19");

/* The lobby. */
constexpr int16_t CHIP_Y = 36, CHIP_H = 26;
constexpr int16_t ROW_Y = 70, ROW_H = 31, ROW_PITCH = 36;

/* The result card, over the board. */
constexpr Rect RESULT_RECT{30, 46, 260, 176};

// ---- colours: board art, fixed in every theme like Chess's squares ----------
uint16_t wood()      { return Ui::rgb(196, 152, 88); }
uint16_t grid()      { return Ui::rgb(92, 66, 34); }
uint16_t blackFill() { return Ui::rgb(38, 38, 48); }
uint16_t blackEdge() { return Ui::rgb(96, 96, 112); }
uint16_t whiteFill() { return Ui::rgb(244, 241, 232); }
uint16_t whiteEdge() { return Ui::rgb(150, 145, 132); }
uint16_t marker()    { return Ui::rgb(247, 61, 82); }
/* The ghost's ring: cyan, which is none of the board's own colours. */
uint16_t hilite()    { return Ui::rgb(120, 230, 255); }

uint16_t mix(uint16_t a, uint16_t b) {
    /* Halfway between two RGB565 colours, channel by channel. */
    const uint16_t r = static_cast<uint16_t>((((a >> 11) & 0x1F) + ((b >> 11) & 0x1F)) / 2);
    const uint16_t g = static_cast<uint16_t>((((a >> 5) & 0x3F) + ((b >> 5) & 0x3F)) / 2);
    const uint16_t bl = static_cast<uint16_t>(((a & 0x1F) + (b & 0x1F)) / 2);
    return static_cast<uint16_t>((r << 11) | (g << 5) | bl);
}

const char COLS[] = "ABCDEFGHJKLMNOPQRST";   // no I, as on a real board

}   // namespace

// ---- geometry ---------------------------------------------------------------------

int16_t GoGame::step() const { return big() ? STEP_BIG : STEP_SMALL; }
int16_t GoGame::margin() const { return big() ? MARGIN_BIG : MARGIN_SMALL; }

Rect GoGame::boardRect() const {
    const int16_t box = big() ? BOX_BIG : BOX_SMALL;
    return Rect{BX, BY, box, box};
}

int16_t GoGame::pointX(uint16_t p) const {
    return static_cast<int16_t>(BX + margin() + Go::colOf(state_.n, p) * step());
}

int16_t GoGame::pointY(uint16_t p) const {
    return static_cast<int16_t>(BY + margin() + Go::rowOf(state_.n, p) * step());
}

Rect GoGame::pointRect(uint16_t p) const {
    /* The cell: a step square centred on the point, clipped to the board so
     * an edge point's cell does not reach past the wood. */
    const Rect b = boardRect();
    const int16_t half = static_cast<int16_t>(step() / 2);
    int16_t x0 = static_cast<int16_t>(pointX(p) - half);
    int16_t y0 = static_cast<int16_t>(pointY(p) - half);
    int16_t x1 = static_cast<int16_t>(x0 + step());
    int16_t y1 = static_cast<int16_t>(y0 + step());
    if (x0 < b.x) x0 = b.x;
    if (y0 < b.y) y0 = b.y;
    if (x1 > b.x + b.w) x1 = static_cast<int16_t>(b.x + b.w);
    if (y1 > b.y + b.h) y1 = static_cast<int16_t>(b.y + b.h);
    return Rect{x0, y0, static_cast<int16_t>(x1 - x0), static_cast<int16_t>(y1 - y0)};
}

uint16_t GoGame::pointAt(int16_t x, int16_t y) const {
    if (!boardRect().contains(x, y, TOUCH_HIT_SLOP)) return Go::NO_POINT;
    /* The nearest point, never none: on a board every tap means somewhere,
     * and the ghost-then-confirm is what makes that safe. */
    int16_t col = static_cast<int16_t>((x - BX - margin() + step() / 2) / step());
    int16_t row = static_cast<int16_t>((y - BY - margin() + step() / 2) / step());
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= state_.n) col = static_cast<int16_t>(state_.n - 1);
    if (row >= state_.n) row = static_cast<int16_t>(state_.n - 1);
    return Go::point(state_.n, static_cast<uint8_t>(row), static_cast<uint8_t>(col));
}

bool GoGame::isStar(uint16_t p) const {
    const uint8_t r = Go::rowOf(state_.n, p);
    const uint8_t c = Go::colOf(state_.n, p);
    if (big()) {
        return (r == 3 || r == 9 || r == 15) && (c == 3 || c == 9 || c == 15);
    }
    return ((r == 2 || r == 6) && (c == 2 || c == 6)) || (r == 4 && c == 4);
}

int16_t GoGame::panelX() const {
    return static_cast<int16_t>(BX + boardRect().w + PANEL_GAP);
}

int16_t GoGame::panelW() const {
    return static_cast<int16_t>(GAME_CANVAS_WIDTH - panelX() - 4);
}

Rect GoGame::turnRect() const {
    return big() ? Rect{panelX(), BTURN_Y, panelW(), BTURN_H}
                 : Rect{panelX(), TURN_Y, panelW(), TURN_H};
}

Rect GoGame::capturesRect() const {
    return Rect{panelX(), CAPT_Y, panelW(), CAPT_H};
}

Rect GoGame::infoRect() const {
    if (big()) {
        const int16_t w = MAG_H;
        return Rect{static_cast<int16_t>(panelX() + (panelW() - w) / 2), MAG_Y, w, MAG_H};
    }
    return Rect{panelX(), INFO_Y, panelW(), INFO_H};
}

Rect GoGame::placeRect() const {
    return Rect{panelX(), PLACE_Y, panelW(), PLACE_H};
}

Rect GoGame::passRect() const {
    if (big()) {
        return Rect{panelX(), BPASS_Y, static_cast<int16_t>(panelW() / 2 - 2), BPASS_H};
    }
    return Rect{panelX(), PASS_Y, panelW(), PASS_H};
}

Rect GoGame::undoRect() const {
    if (big()) {
        return Rect{static_cast<int16_t>(panelX() + panelW() / 2 + 2), BPASS_Y,
                    static_cast<int16_t>(panelW() / 2 - 2), BPASS_H};
    }
    return Rect{panelX(), UNDO_Y, panelW(), UNDO_H};
}

Rect GoGame::actionRect() const {
    return big() ? Rect{panelX(), BACTION_Y, panelW(), BACTION_H}
                 : Rect{panelX(), ACTION_Y, panelW(), ACTION_H};
}

Rect GoGame::lobbyChipRect(uint8_t index, uint8_t count) {
    const int16_t w = static_cast<int16_t>((GAME_CANVAS_WIDTH - 16 - 5 * (count - 1)) / count);
    return Rect{static_cast<int16_t>(8 + index * (w + 5)), CHIP_Y, w, CHIP_H};
}

Rect GoGame::lobbyRowRect(uint8_t row) {
    return Rect{8, static_cast<int16_t>(ROW_Y + row * ROW_PITCH), GAME_CANVAS_WIDTH - 16, ROW_H};
}

Rect GoGame::resultRect() const { return RESULT_RECT; }

Rect GoGame::resultButtonRect(uint8_t index) const {
    const Rect r = RESULT_RECT;
    return Rect{static_cast<int16_t>(r.x + 14 + index * 122), static_cast<int16_t>(r.y + r.h - 36),
                110, 26};
}

void GoGame::coordinate(uint16_t p, char* out, size_t len) const {
    if (p == Go::NO_POINT || p == Go::PASS) {
        snprintf(out, len, "-");
        return;
    }
    snprintf(out, len, "%c%u", COLS[Go::colOf(state_.n, p)],
             static_cast<unsigned>(state_.n - Go::rowOf(state_.n, p)));
}

// ---- the board -----------------------------------------------------------------------

void GoGame::drawStone(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t r, uint8_t colour,
                       bool faded) const {
    uint16_t fill = colour == Go::BLACK ? blackFill() : whiteFill();
    uint16_t edge = colour == Go::BLACK ? blackEdge() : whiteEdge();
    if (faded) {
        fill = mix(fill, wood());
        edge = mix(edge, wood());
    } else if (r >= 6) {
        tft.fillCircle(cx + 1, cy + 2, r, Ui::shade(wood(), 55));   // a little shadow
    }
    tft.fillCircle(cx, cy, r, fill);
    tft.drawCircle(cx, cy, r, edge);
    if (r >= 8 && !faded) {
        tft.fillCircle(cx - r / 3, cy - r / 3, r / 4,
                       colour == Go::BLACK ? Ui::shade(fill, 160) : Ui::rgb(255, 255, 255));
    }
}

/* Everything that sits on a point, drawn over a cell that already shows the
 * wood and the grid. Idempotent: what it paints depends only on the state. */
namespace {
struct PointArt {
    bool stone;
    uint8_t colour;
};
}   // namespace

void GoGame::repaintPoint(Ui::Renderer& tft, uint16_t p) const {
    const Rect c = pointRect(p);
    const int16_t cx = pointX(p);
    const int16_t cy = pointY(p);
    tft.fillRect(c.x, c.y, c.w, c.h, wood());
    /* The grid through the cell, no further than the outer lines. */
    const int16_t gx0 = pointX(0);
    const int16_t gy0 = pointY(0);
    const int16_t gx1 = pointX(static_cast<uint16_t>(state_.n - 1));
    const int16_t gy1 = pointY(static_cast<uint16_t>(Go::points(state_.n) - 1));
    const int16_t hx0 = c.x < gx0 ? gx0 : c.x;
    const int16_t hx1 = static_cast<int16_t>(c.x + c.w - 1 > gx1 ? gx1 : c.x + c.w - 1);
    const int16_t vy0 = c.y < gy0 ? gy0 : c.y;
    const int16_t vy1 = static_cast<int16_t>(c.y + c.h - 1 > gy1 ? gy1 : c.y + c.h - 1);
    tft.drawFastHLine(hx0, cy, static_cast<int16_t>(hx1 - hx0 + 1), grid());
    tft.drawFastVLine(cx, vy0, static_cast<int16_t>(vy1 - vy0 + 1), grid());
    if (isStar(p)) tft.fillCircle(cx, cy, big() ? 1 : 2, grid());

    const int16_t r = static_cast<int16_t>(step() * 46 / 100);
    const bool showTerritory = phase_ == Phase::Marking || phase_ == Phase::Over;
    const bool dead = showTerritory && dead_[p] != 0;
    const uint8_t colour = state_.at[p];
    if (colour != Go::EMPTY) {
        drawStone(tft, cx, cy, r, colour, dead);
        if (dead) {
            const int16_t k = static_cast<int16_t>(r - 1);
            tft.drawLine(cx - k, cy - k, cx + k, cy + k, marker());
            tft.drawLine(cx - k, cy + k, cx + k, cy - k, marker());
        } else if (p == state_.last && phase_ != Phase::Over) {
            tft.drawCircle(cx, cy, r / 2 > 2 ? r / 2 : 2, marker());
        }
    }
    if (showTerritory && (colour == Go::EMPTY || dead) && own_[p] != Go::EMPTY) {
        const int16_t t = static_cast<int16_t>(step() * 16 / 100 > 1 ? step() * 16 / 100 : 1);
        tft.fillRect(cx - t, cy - t, 2 * t + 1, 2 * t + 1,
                     own_[p] == Go::BLACK ? blackFill() : whiteFill());
    }
    if (p == ghost_ && phase_ == Phase::Play) {
        const uint16_t fill = mix(state_.toMove == Go::BLACK ? blackFill() : whiteFill(), wood());
        tft.fillCircle(cx, cy, r, fill);
        tft.drawCircle(cx, cy, r, hilite());
        const int16_t a = static_cast<int16_t>(r + 1);
        const int16_t b = static_cast<int16_t>(r + 4);
        tft.drawFastHLine(cx - b, cy, b - a, hilite());
        tft.drawFastHLine(cx + a, cy, b - a, hilite());
        tft.drawFastVLine(cx, cy - b, b - a, hilite());
        tft.drawFastVLine(cx, cy + a, b - a, hilite());
    }
}

void GoGame::drawBoard(Ui::Renderer& tft) const {
    const Rect b = boardRect();
    tft.fillRect(b.x, b.y, b.w, b.h, wood());
    const uint16_t total = Go::points(state_.n);
    for (uint16_t p = 0; p < total; ++p) {
        /* Each cell whole, which draws the grid as it goes. Eighty-one or
         * three hundred and sixty-one small fills on a full repaint only. */
        repaintPoint(tft, p);
    }
}

// ---- the panel -----------------------------------------------------------------------

void GoGame::drawTurn(Ui::Renderer& tft) const {
    const Rect r = turnRect();
    tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    const uint8_t side = phase_ == Phase::Over ? state_.winner : state_.toMove;
    const int16_t sr = big() ? 6 : 8;
    drawStone(tft, r.x + 4 + sr, r.y + r.h / 2, sr, side == Go::EMPTY ? Go::BLACK : side, false);
    char line[16];
    if (phase_ == Phase::Over) {
        snprintf(line, sizeof(line), "%.8s wins", sideName(side));
    } else if (phase_ == Phase::Marking) {
        snprintf(line, sizeof(line), "Marking");
    } else if (mode_ == Mode::Waiting) {
        snprintf(line, sizeof(line), "Asking...");
    } else if (humanTurn() && mode_ != Mode::Local) {
        snprintf(line, sizeof(line), "Your turn");
    } else {
        snprintf(line, sizeof(line), "%.8s", sideName(side));
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.drawString(line, r.x + 8 + 2 * sr, r.y + r.h / 2, big() ? 1 : 2);
    if (big()) {
        /* The 19x19 panel has no captures block: the counts ride here. */
        char caps[12];
        snprintf(caps, sizeof(caps), "%u:%u", static_cast<unsigned>(state_.captured[Go::BLACK]),
                 static_cast<unsigned>(state_.captured[Go::WHITE]));
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(caps, r.x + r.w - 6, r.y + r.h / 2, 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void GoGame::drawCaptures(Ui::Renderer& tft) const {
    if (big()) return;
    const Rect r = capturesRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    const uint8_t target = Go::captureTarget(state_.rules);
    char label[16];
    if (target != 0) {
        snprintf(label, sizeof(label), "Captured of %u", static_cast<unsigned>(target));
    } else {
        snprintf(label, sizeof(label), "Captured");
    }
    tft.drawString(label, r.x, r.y, 1);
    const int16_t cy = static_cast<int16_t>(r.y + 22);
    drawStone(tft, r.x + 10, cy, 8, Go::BLACK, false);
    drawStone(tft, r.x + 58, cy, 8, Go::WHITE, false);
    char n[8];
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    snprintf(n, sizeof(n), "%u", static_cast<unsigned>(state_.captured[Go::BLACK]));
    tft.drawString(n, r.x + 24, cy, 4);
    snprintf(n, sizeof(n), "%u", static_cast<unsigned>(state_.captured[Go::WHITE]));
    tft.drawString(n, r.x + 72, cy, 4);
    tft.setTextDatum(TL_DATUM);
}

void GoGame::drawInfo(Ui::Renderer& tft) const {
    if (big()) {
        drawMagnifier(tft);
        return;
    }
    const Rect r = infoRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    tft.setTextDatum(TL_DATUM);
    if (ghost_ != Go::NO_POINT && phase_ == Phase::Play) {
        char at[8];
        coordinate(ghost_, at, sizeof(at));
        char line[24];
        const bool ok = Go::legal(state_, ghost_);
        const uint16_t taken = ok ? Go::captures(state_, ghost_) : 0;
        if (!ok) {
            snprintf(line, sizeof(line), "%s: not allowed", at);
        } else if (taken > 0) {
            snprintf(line, sizeof(line), "%s takes %u", at, static_cast<unsigned>(taken));
        } else {
            snprintf(line, sizeof(line), "%s", at);
        }
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(ok ? "Tap again to play" : "Try elsewhere", r.x + 6, r.y + 5, 1);
        tft.setTextColor(ok ? (taken > 0 ? Ui::success() : Ui::text()) : Ui::warning(),
                         Ui::surface());
        tft.drawString(line, r.x + 6, r.y + 16, 2);
        return;
    }
    Ui::drawWrappedText(tft, message_,
                        Rect{static_cast<int16_t>(r.x + 6), static_cast<int16_t>(r.y + 4),
                             static_cast<int16_t>(r.w - 12), static_cast<int16_t>(r.h - 8)},
                        Ui::text(), 1, Align::Left);
}

void GoGame::drawMagnifier(Ui::Renderer& tft) const {
    /* The five-by-five around the ghost, at 18px a cell, so a point that is
     * ten pixels from its neighbours on the board is something a finger can
     * check before Place. With no ghost, the centre of the board. */
    const Rect r = infoRect();
    tft.fillRect(panelX(), r.y, panelW(), r.h, Ui::bg());
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, wood());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
    const uint16_t centre = ghost_ != Go::NO_POINT ? ghost_ : Go::point(state_.n, 9, 9);
    const int16_t cr = Go::rowOf(state_.n, centre);
    const int16_t cc = Go::colOf(state_.n, centre);
    const int16_t half = MAG_CELLS / 2;
    for (int16_t dr = -half; dr <= half; ++dr) {
        for (int16_t dc = -half; dc <= half; ++dc) {
            const int16_t rr = static_cast<int16_t>(cr + dr);
            const int16_t col = static_cast<int16_t>(cc + dc);
            const int16_t x = static_cast<int16_t>(r.x + (dc + half) * MAG_CELL + MAG_CELL / 2);
            const int16_t y = static_cast<int16_t>(r.y + (dr + half) * MAG_CELL + MAG_CELL / 2);
            if (rr < 0 || col < 0 || rr >= state_.n || col >= state_.n) continue;
            /* Grid through this cell, stopping at the board's edge. */
            const int16_t x0 = col > 0 ? static_cast<int16_t>(x - MAG_CELL / 2) : x;
            const int16_t x1 = col + 1 < state_.n ? static_cast<int16_t>(x + MAG_CELL / 2) : x;
            const int16_t y0 = rr > 0 ? static_cast<int16_t>(y - MAG_CELL / 2) : y;
            const int16_t y1 = rr + 1 < state_.n ? static_cast<int16_t>(y + MAG_CELL / 2) : y;
            tft.drawFastHLine(x0, y, static_cast<int16_t>(x1 - x0 + 1), grid());
            tft.drawFastVLine(x, y0, static_cast<int16_t>(y1 - y0 + 1), grid());
            const uint16_t p = Go::point(state_.n, static_cast<uint8_t>(rr), static_cast<uint8_t>(col));
            if (isStar(p)) tft.fillCircle(x, y, 1, grid());
            if (state_.at[p] != Go::EMPTY) {
                drawStone(tft, x, y, MAG_CELL / 2 - 2, state_.at[p], false);
            } else if (p == ghost_) {
                tft.drawCircle(x, y, MAG_CELL / 2 - 2, hilite());
                tft.drawCircle(x, y, MAG_CELL / 2 - 3, hilite());
            }
        }
    }
    /* The coordinate, in the corner of the magnifier. */
    char at[8];
    coordinate(ghost_, at, sizeof(at));
    tft.setTextDatum(BL_DATUM);
    tft.setTextColor(Ui::text(), wood());
    tft.drawString(at, r.x + 3, r.y + r.h - 2, 2);
    tft.setTextDatum(TL_DATUM);
}

void GoGame::drawButtons(Ui::Renderer& tft, bool sure) const {
    const Rect rects[4] = {passRect(), undoRect(), actionRect(),
                           big() ? placeRect() : Rect{0, 0, 0, 0}};
    for (const Rect& r : rects) {
        if (r.w == 0) continue;
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    }
    const bool playing = phase_ == Phase::Play && mode_ != Mode::Waiting;
    const bool ours = playing && humanTurn();
    const uint8_t font = big() ? 1 : 2;
    if (phase_ == Phase::Marking) {
        Ui::drawPagerButton(tft, passRect(), "Resume", mode_ != Mode::Remote);
        Ui::drawPagerButton(tft, undoRect(), "Undo", false);
        if (big()) Ui::drawPagerButton(tft, placeRect(), "Place", false);
        /* "Agreed" means we have agreed and are waiting for somebody else,
         * which can only happen across consoles. Locally the next press is
         * the OTHER player's, so the button has to keep saying "Agree" --
         * it read "Agreed" while still wanting a press, which is the small
         * wrong behaviour the two encodings were hiding. */
        const bool waitingOthers =
            mode_ == Mode::Remote && (agreed_ & agreedBit(humanColour_)) != 0;
        Ui::drawButton(tft, actionRect(), waitingOthers ? "Agreed" : "Agree",
                       waitingOthers ? Ui::panel() : Ui::success(), Ui::outline(),
                       waitingOthers ? Ui::muted() : static_cast<uint16_t>(TFT_BLACK), false, font);
        return;
    }
    Ui::drawPagerButton(tft, passRect(), "Pass", ours && Go::legal(state_, Go::PASS));
    Ui::drawPagerButton(tft, undoRect(), "Undo",
                        ours && haveUndo_ && mode_ != Mode::Remote && mode_ != Mode::Waiting);
    if (big()) {
        const bool can = ours && ghost_ != Go::NO_POINT && Go::legal(state_, ghost_);
        if (can) {
            Ui::drawButton(tft, placeRect(), "Place", Ui::success(), Ui::outline(), TFT_BLACK, false,
                           font);
        } else {
            Ui::drawPagerButton(tft, placeRect(), "Place", false);
        }
    }
    const bool over = phase_ == Phase::Over;
    Ui::drawButton(tft, actionRect(), over ? "New game" : (sure ? "Sure?" : "End game"),
                   sure ? Ui::warning() : Ui::panel(), Ui::outline(),
                   sure ? Ui::bg() : Ui::text(), false, font);
}

void GoGame::drawPanel(Ui::Renderer& tft) {
    if (turnStale_) {
        drawTurn(tft);
        turnStale_ = false;
    }
    if (capturesStale_) {
        drawCaptures(tft);
        capturesStale_ = false;
    }
    if (infoStale_) {
        drawInfo(tft);
        infoStale_ = false;
    }
    const bool sure = phase_ == Phase::Play && millis() < confirmUntilMs_;
    if (buttonsStale_ || sure != confirmShown_) {
        drawButtons(tft, sure);
        confirmShown_ = sure;
        buttonsStale_ = false;
    }
}

// ---- the result --------------------------------------------------------------------------

void GoGame::drawResult(Ui::Renderer& tft) const {
    const Rect r = resultRect();
    tft.fillRoundRect(r.x + 2, r.y + Ui::BUTTON_SHADOW_DY, r.w, r.h, 8, Ui::shade(Ui::bg(), 60));
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, Ui::warning());

    char title[32];
    const uint8_t w = state_.winner;
    const uint8_t l = Go::other(w);
    if (Go::isCapture(state_.rules)) {
        snprintf(title, sizeof(title), "%.10s wins!", sideName(w));
    } else {
        const int16_t by = static_cast<int16_t>(score_.half[w] - score_.half[l]);
        snprintf(title, sizeof(title), "%.10s wins by %d.%d", sideName(w), by / 2, (by % 2) * 5);
    }
    Ui::drawLabel(tft, Rect{static_cast<int16_t>(r.x + 8), static_cast<int16_t>(r.y + 6),
                            static_cast<int16_t>(r.w - 16), 22},
                  title, Ui::warning(), 4, Align::Center);

    const int16_t colB = static_cast<int16_t>(r.x + 150);
    const int16_t colW = static_cast<int16_t>(r.x + 215);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.drawString("Black", colB, r.y + 40, 2);
    tft.drawString("White", colW, r.y + 40, 2);

    const char* labels[3];
    uint16_t b[3];
    uint16_t wv[3];
    uint8_t rows = 0;
    if (Go::isCapture(state_.rules)) {
        labels[rows] = "Stones captured";
        b[rows] = state_.captured[Go::BLACK];
        wv[rows] = state_.captured[Go::WHITE];
        ++rows;
        labels[rows] = "Needed to win";
        b[rows] = wv[rows] = Go::captureTarget(state_.rules);
        ++rows;
    } else if (state_.rules == Go::Rules::Area) {
        labels[0] = "Stones on the board";
        b[0] = score_.stones[Go::BLACK];
        wv[0] = score_.stones[Go::WHITE];
        labels[1] = "Points surrounded";
        b[1] = score_.territory[Go::BLACK];
        wv[1] = score_.territory[Go::WHITE];
        rows = 2;
    } else {
        labels[0] = "Points surrounded";
        b[0] = score_.territory[Go::BLACK];
        wv[0] = score_.territory[Go::WHITE];
        labels[1] = "Prisoners";
        b[1] = score_.prisoners[Go::BLACK];
        wv[1] = score_.prisoners[Go::WHITE];
        rows = 2;
    }
    int16_t y = static_cast<int16_t>(r.y + 60);
    char n[12];
    for (uint8_t i = 0; i < rows; ++i) {
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(labels[i], r.x + 14, y, 1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        snprintf(n, sizeof(n), "%u", static_cast<unsigned>(b[i]));
        tft.drawString(n, colB, y, 2);
        snprintf(n, sizeof(n), "%u", static_cast<unsigned>(wv[i]));
        tft.drawString(n, colW, y, 2);
        y = static_cast<int16_t>(y + 20);
    }
    if (!Go::isCapture(state_.rules)) {
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString("Komi", r.x + 14, y, 1);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString("-", colB, y, 2);
        snprintf(n, sizeof(n), "%d.%d", Go::KOMI_HALF / 2, (Go::KOMI_HALF % 2) * 5);
        tft.drawString(n, colW, y, 2);
        y = static_cast<int16_t>(y + 20);
        tft.drawFastHLine(r.x + 14, y - 4, r.w - 28, Ui::outline());
        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString("Total", r.x + 14, y + 6, 2);
        tft.setTextDatum(MC_DATUM);
        snprintf(n, sizeof(n), "%d%s", score_.half[Go::BLACK] / 2,
                 score_.half[Go::BLACK] % 2 ? ".5" : "");
        tft.setTextColor(w == Go::BLACK ? Ui::success() : Ui::text(), Ui::surface());
        tft.drawString(n, colB, y + 6, 4);
        snprintf(n, sizeof(n), "%d%s", score_.half[Go::WHITE] / 2,
                 score_.half[Go::WHITE] % 2 ? ".5" : "");
        tft.setTextColor(w == Go::WHITE ? Ui::success() : Ui::text(), Ui::surface());
        tft.drawString(n, colW, y + 6, 4);
    }
    tft.setTextDatum(TL_DATUM);
    Ui::drawButton(tft, resultButtonRect(0), mode_ == Mode::Remote ? "Lobby" : "Play again",
                   Ui::panel(), Ui::outline(), Ui::text());
    Ui::drawButton(tft, resultButtonRect(1), "Done", Ui::panel(), Ui::outline(), Ui::text());
}

// ---- the lobby ----------------------------------------------------------------------------

void GoGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* The chips: board size only where 19x19 is offered, rules, level. Each
     * is a label over a value with a small arrow, and a tap cycles it. */
    const uint8_t chips = BIG_BOARD_AVAILABLE ? 3 : 2;
    const char* labels[3] = {"Rules", "Level", "Board"};
    char values[3][16];
    snprintf(values[0], sizeof(values[0]), "%s", Go::rulesName(rules_));
    snprintf(values[1], sizeof(values[1]), "%s", level_ == Go::Level::Easy ? "Easy" : "Medium");
    snprintf(values[2], sizeof(values[2]), "%s", boardSize_ > 9 ? "19x19" : "9x9");
    for (uint8_t i = 0; i < chips; ++i) {
        const Rect c = lobbyChipRect(i, chips);
        tft.fillRoundRect(c.x, c.y, c.w, c.h, 6, Ui::surface());
        tft.drawRoundRect(c.x, c.y, c.w, c.h, 6, Ui::outline());
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(labels[i], c.x + 7, c.y + 3, 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(values[i], c.x + 7, c.y + 12, 2);
        tft.fillTriangle(c.x + c.w - 16, c.y + 11, c.x + c.w - 8, c.y + 11, c.x + c.w - 12,
                         c.y + 17, Ui::muted());
    }

    Ui::drawButton(tft, lobbyRowRect(0), "Pass and play", Ui::panel(), Ui::outline(), Ui::text());
    Ui::drawButton(tft, lobbyRowRect(1), "Play the computer", Ui::panel(), Ui::outline(),
                   Ui::text());
    char label[32];
    for (uint8_t i = 0; i < seatCount_ && i < 3; ++i) {
        const char* who = seats_[i].name[0] != 0 ? seats_[i].name : seats_[i].deviceId;
        if (seats_[i].inviting) {
            snprintf(label, sizeof(label), "%s invites you", who);
        } else {
            snprintf(label, sizeof(label), "Play %s nearby", who);
        }
        Ui::drawButton(tft, lobbyRowRect(static_cast<uint8_t>(i + 2)), label,
                       seats_[i].inviting ? Ui::success() : Ui::surface(), Ui::outline(),
                       seats_[i].inviting ? static_cast<uint16_t>(TFT_BLACK) : Ui::text());
    }
    /* Why a game just vanished; else a low battery, which outranks the usual
     * note; else what nearby play is and who can switch it on. */
    const bool low = lobbyNote_[0] == 0 && seatCount_ > 0 && host.batteryLow();
    const char* note = lobbyNote_[0] != 0 ? lobbyNote_
                       : low              ? "Battery low: a nearby game may not finish."
                       : seatCount_ > 0   ? "Moves travel by Bluetooth. Anyone near hears them."
                                          : "Nearby play: an adult can switch Beacon and Nearby on.";
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(lobbyNote_[0] != 0 || low ? Ui::warning() : Ui::muted(), Ui::bg());
    tft.drawString(note, GAME_CANVAS_WIDTH / 2, GAME_CANVAS_HEIGHT - 4, lobbyNote_[0] != 0 ? 2 : 1);
    tft.setTextDatum(TL_DATUM);
}

// ---- render -------------------------------------------------------------------------------

void GoGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    if (mode_ == Mode::Lobby) {
        lobbyStale_ = true;
        return;
    }
    drawBoard(tft);
    memset(dirty_, 0, sizeof(dirty_));
    anyDirty_ = false;
    turnStale_ = capturesStale_ = infoStale_ = buttonsStale_ = true;
    drawPanel(tft);
    if (phase_ == Phase::Over) {
        drawResult(tft);
        resultStale_ = false;
    }
    pausePainted_ = false;
    drawPause(tft);
}

void GoGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    if (mode_ == Mode::Lobby) {
        if (lobbyStale_) {
            renderLobby(host);
            lobbyStale_ = false;
        }
        return;
    }
    if (anyDirty_) {
        pausePainted_ = false;   // a point under the card
        const uint16_t total = Go::points(state_.n);
        for (uint16_t p = 0; p < total; ++p) {
            if (dirty_[p >> 3] & (1U << (p & 7))) repaintPoint(tft, p);
        }
        memset(dirty_, 0, sizeof(dirty_));
        anyDirty_ = false;
        if (big()) infoStale_ = true;   // the magnifier shows the board
    }
    drawPanel(tft);
    if (phase_ == Phase::Over && resultStale_) {
        drawResult(tft);
        resultStale_ = false;
    }
    drawPause(tft);
}
