#include "LudoGame.h"

/* The lobby: who sits in each seat, and how hard the computer plays.
 *
 * Four chips, one per colour, each cycling Empty -> Player -> Computer on a
 * tap; the level toggle; a hint that says why Start is greyed when it is; and
 * Start. Everything here is redrawn whole on any change -- the lobby is a
 * handful of fixed rectangles, each painted by an idempotent function, so
 * there is nothing to be gained by tracking which one changed. */

namespace {

/* Chips sit in the same corners as the yards they stand for. */
constexpr int16_t CHIP_W = 148;
constexpr int16_t CHIP_H = 44;
/* drawLabel() puts left-aligned text at the rect's top, so this is the 16px
 * line centred on the level buttons' 30px row rather than the row itself. */
constexpr Rect LEVEL_LABEL{8, 145, 100, 16};
constexpr Rect HINT_RECT{8, 176, 304, 18};
constexpr Rect START_RECT{100, 198, 120, 34};
static_assert(START_RECT.y + START_RECT.h + Ui::BUTTON_SHADOW_DY <= GAME_CANVAS_HEIGHT,
              "Start's shadow falls off the canvas");

}   // namespace

Rect LudoGame::seatChipRect(uint8_t seat) {
    /* Laid out like the yards: Red top left, Green top right, Yellow bottom
     * right, Blue bottom left -- so a child finds a colour where the board
     * keeps it. */
    const int16_t col = (seat == 1 || seat == 2) ? 1 : 0;
    const int16_t row = seat >= 2 ? 1 : 0;
    return Rect{static_cast<int16_t>(8 + col * (CHIP_W + 8)),
                static_cast<int16_t>(38 + row * (CHIP_H + 6)), CHIP_W, CHIP_H};
}

Rect LudoGame::levelRect(uint8_t level) {
    return Rect{static_cast<int16_t>(112 + level * 100), 138, 94, 30};
}

Rect LudoGame::startRect() { return START_RECT; }

void LudoGame::drawSeatChip(Ui::Renderer& tft, uint8_t seat) const {
    const Rect r = seatChipRect(seat);
    const bool empty = kind_[seat] == SeatKind::Empty;
    const uint16_t fill = empty ? Ui::surface() : seatColour(seat);
    const uint16_t text = empty ? Ui::muted() : seatText(seat);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    const int16_t tx = static_cast<int16_t>(r.x + 20);
    const int16_t ty = static_cast<int16_t>(r.y + r.h / 2);
    tft.fillCircle(tx, ty, 12, paperColour());
    drawToken(tft, tx, ty, seat, 8, 1);
    static const char* const KINDS[3] = {"Empty", "Player", "Computer"};
    tft.setTextColor(text);
    tft.drawString(seatName(seat), r.x + 40, r.y + 5, 2);
    tft.drawString(KINDS[static_cast<uint8_t>(kind_[seat])], r.x + 40, r.y + 23, 2);
}

void LudoGame::drawLevel(Ui::Renderer& tft) const {
    static const char* const LEVELS[2] = {"Easy", "Normal"};
    Ui::drawLabel(tft, LEVEL_LABEL, "Computer", Ui::text(), 2, Align::Left);
    for (uint8_t lv = 0; lv < 2; ++lv) {
        const Rect r = levelRect(lv);
        const bool on = static_cast<uint8_t>(level_) == lv;
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
        /* The chosen one inverted, which has contrast in every theme without
         * borrowing a colour that means something else. */
        Ui::drawButton(tft, r, LEVELS[lv], on ? Ui::text() : Ui::surface(), Ui::outline(),
                       on ? Ui::bg() : Ui::text());
    }
}

void LudoGame::drawLobbyHint(Ui::Renderer& tft) const {
    tft.fillRect(HINT_RECT.x, HINT_RECT.y, HINT_RECT.w, HINT_RECT.h, Ui::bg());
    uint8_t seats = 0;
    bool player = false;
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        seats = static_cast<uint8_t>(seats + (kind_[s] != SeatKind::Empty ? 1 : 0));
        player = player || kind_[s] == SeatKind::Player;
    }
    const char* hint = seats < 2 ? "Two seats are needed to play"
                       : !player ? "Someone has to be a Player"
                                 : "Tap a seat to change who sits there";
    Ui::drawLabel(tft, HINT_RECT, hint, canStart() ? Ui::muted() : Ui::warning(), 2,
                  Align::Center);
}

void LudoGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    for (uint8_t s = 0; s < Ludo::SEATS; ++s) {
        drawSeatChip(tft, s);
    }
    drawLevel(tft);
    drawLobbyHint(tft);
    tft.fillRect(START_RECT.x, START_RECT.y, START_RECT.w + Ui::BUTTON_SHADOW_DX,
                 START_RECT.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    Ui::drawPagerButton(tft, START_RECT, "Start", canStart());
}
