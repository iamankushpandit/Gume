#include "ui/LauncherIcons.h"

/* Launcher tile artwork.
 *
 * Four rules, all of them learned from tiles that were wrong on the device and
 * looked fine in the source:
 *
 * 1. **Stay inside ICON_HALF of (cx, cy).** Landscape tiles are 145x46 and the
 *    icon is centred at (r.x + 24, r.y + 22), so the vertical budget is the
 *    binding one: anything past cy +/- 22 leaves the tile entirely, and past
 *    +/- 18 it crowds the rounded border. Portrait is roomier; design for
 *    landscape and portrait comes free. The Coin Flip icon shipped reaching
 *    cy + 24 and drew over the tile edge.
 *
 * 2. **No font 1 text.** Six by eight pixels of antialiased-nothing on a
 *    saturated tile is a smudge, not a glyph. Microku's "1"/"4", Money's
 *    "5"/"1", Multiplication's "12", Percent's "%" and Calendar's stacked
 *    "Mon"/"Tue"/"Wed" were all unreadable at arm's length. Font 2 is the
 *    smallest thing worth drawing, and only for one or two characters; past
 *    that, draw the shape.
 *
 * 3. **The failure is two tiles that look alike, not one tile that looks
 *    plain.** A child navigates this grid by silhouette. Fractions and Percent
 *    were both a white circle with a yellow wedge and a blue rim; Flags and
 *    State Flags were the same flag in two colours; Tic-Tac-Toe and Whack were
 *    both a white grid. Those pairs are now a quartered pie against a ring, a
 *    tricolour against a star field, and a grid against a mole.
 *
 * 4. **Leave the text datum where you found it.** Several cases set MC_DATUM
 *    and returned without restoring TL_DATUM, which quietly re-aligned whatever
 *    the launcher drew next. Every case that touches the datum resets it. */

namespace {

/* Half-width of the box every icon must fit inside. See rule 1. */
constexpr int16_t ICON_HALF = 18;

constexpr uint16_t PIP_BLACK = TFT_BLACK;

/* Through Ui::rgb rather than a hand-written RGB565 literal: the first draft of
 * this file carried 0x2418 in a comment claiming to be rgb(36, 132, 204), and
 * it was not -- the real value is 0x2439. Let the packer do it. */
uint16_t ink()    { return Ui::rgb(36, 132, 204); }
uint16_t gold()   { return Ui::rgb(255, 202, 84); }
uint16_t cream()  { return Ui::rgb(255, 246, 178); }
uint16_t leaf()   { return Ui::rgb(108, 232, 148); }
uint16_t rose()   { return Ui::rgb(255, 112, 112); }
uint16_t sky()    { return Ui::rgb(94, 190, 255); }
uint16_t slate()  { return Ui::rgb(120, 128, 134); }
uint16_t silver() { return Ui::rgb(196, 204, 212); }

/* Unit five-point star, x100, interleaved outer/inner from twelve o'clock.
 * Kept as a table so the icons cost no runtime trigonometry. */
constexpr int16_t STAR_XY[10][2] = {
    {   0, -100}, {  22,  -31}, {  95,  -31}, {  36,   12}, {  59,   81},
    {   0,   38}, { -59,   81}, { -36,   12}, { -95,  -31}, { -22,  -31},
};

void fillStar(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    for (uint8_t i = 0; i < 10; ++i) {
        const uint8_t j = static_cast<uint8_t>((i + 1) % 10);
        tft.fillTriangle(cx, cy,
                         static_cast<int16_t>(cx + STAR_XY[i][0] * radius / 100),
                         static_cast<int16_t>(cy + STAR_XY[i][1] * radius / 100),
                         static_cast<int16_t>(cx + STAR_XY[j][0] * radius / 100),
                         static_cast<int16_t>(cy + STAR_XY[j][1] * radius / 100),
                         color);
    }
}

/* A die face on the usual 3x3 grid, bit 8 = top-left, used by Dice. */
void drawDieFace(Ui::Renderer& tft, int16_t x, int16_t y, int16_t size,
                 uint16_t face, uint16_t body, uint16_t edge) {
    tft.fillRoundRect(x, y, size, size, 3, body);
    tft.drawRoundRect(x, y, size, size, 3, edge);
    const int16_t step = static_cast<int16_t>(size / 4);
    for (uint8_t cell = 0; cell < 9; ++cell) {
        if ((face & (1u << (8 - cell))) == 0) continue;
        tft.fillCircle(static_cast<int16_t>(x + step + (cell % 3) * step),
                       static_cast<int16_t>(y + step + (cell / 3) * step),
                       2, PIP_BLACK);
    }
}

/* Upper half of a circle, plotted straight from the circle equation.
 *
 * The Wi-Fi tile used to draw whole circles and paint the lower halves out with
 * a rectangle in the tile colour. The mask was three pixels short, so the very
 * bottom of the outer ring survived -- and at cy + 24 that landed outside a
 * 46px landscape tile. Explicit geometry cannot spill the way a mask can miss.
 * Integer square root, because there is no reason to pull in floating point for
 * a seventeen-pixel radius. */
void drawArcTop(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius,
                uint16_t color) {
    for (int16_t dx = -radius; dx <= radius; ++dx) {
        const int32_t inside = static_cast<int32_t>(radius) * radius -
                               static_cast<int32_t>(dx) * dx;
        int32_t dy = 0;
        while ((dy + 1) * (dy + 1) <= inside) {
            ++dy;
        }
        tft.drawPixel(cx + dx, static_cast<int16_t>(cy - dy), color);
    }
}

void drawBoldCross(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t reach,
                   uint16_t color) {
    for (int16_t t = -1; t <= 1; ++t) {
        tft.drawLine(cx - reach, cy - reach + t, cx + reach, cy + reach + t, color);
        tft.drawLine(cx + reach, cy - reach + t, cx - reach, cy + reach + t, color);
    }
}

}   // namespace

void drawLauncherIcon(Ui::Renderer& tft, LauncherIcon icon, const Rect& r,
                      uint16_t fill, int16_t cx, int16_t cy) {
    (void)r;
    switch (icon) {
        case LauncherIcon::TicTacToe: {
            // A grid that has been *played in*: the marks are what stop this
            // reading as the same white lattice as Whack A Mole.
            const int16_t g = ICON_HALF - 2;
            tft.drawLine(cx - 5, cy - g, cx - 5, cy + g, TFT_WHITE);
            tft.drawLine(cx + 6, cy - g, cx + 6, cy + g, TFT_WHITE);
            tft.drawLine(cx - g, cy - 5, cx + g, cy - 5, TFT_WHITE);
            tft.drawLine(cx - g, cy + 6, cx + g, cy + 6, TFT_WHITE);
            drawBoldCross(tft, cx - 11, cy - 11, 4, sky());
            tft.drawCircle(cx, cy, 4, gold());
            tft.drawCircle(cx, cy, 3, gold());
            break;
        }
        case LauncherIcon::Memory:
            // One card face-down, one turned over showing its pip.
            tft.fillRoundRect(cx - 16, cy - 13, 17, 25, 3, silver());
            tft.drawRoundRect(cx - 16, cy - 13, 17, 25, 3, slate());
            tft.drawLine(cx - 13, cy - 8, cx - 5, cy + 6, slate());
            tft.drawLine(cx - 13, cy + 6, cx - 5, cy - 8, slate());
            tft.fillRoundRect(cx + 1, cy - 11, 17, 25, 3, TFT_WHITE);
            tft.drawRoundRect(cx + 1, cy - 11, 17, 25, 3, slate());
            tft.fillCircle(cx + 9, cy + 1, 5, rose());
            break;
        case LauncherIcon::Math:
            // Plus over minus: the two operations the game actually asks for.
            tft.fillRoundRect(cx - 17, cy - 15, 34, 30, 4, TFT_WHITE);
            tft.fillRect(cx - 11, cy - 9, 12, 3, ink());
            tft.fillRect(cx - 6, cy - 14, 3, 13, ink());
            tft.fillRect(cx + 1, cy + 7, 12, 3, ink());
            break;
        case LauncherIcon::Multiplication:
            // A drawn cross rather than the letter x, which at font 2 on a
            // white card was indistinguishable from the Math plus.
            tft.fillRoundRect(cx - 17, cy - 15, 34, 30, 4, TFT_WHITE);
            drawBoldCross(tft, cx, cy, 9, ink());
            break;
        case LauncherIcon::Time:
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.drawCircle(cx, cy, 16, ink());
            for (uint8_t q = 0; q < 4; ++q) {
                const int16_t dx = (q == 1) ? 13 : (q == 3 ? -13 : 0);
                const int16_t dy = (q == 2) ? 13 : (q == 0 ? -13 : 0);
                tft.fillCircle(cx + dx, cy + dy, 1, ink());
            }
            tft.drawLine(cx, cy, cx, cy - 9, ink());
            tft.drawLine(cx + 1, cy, cx + 1, cy - 9, ink());
            tft.drawLine(cx, cy, cx + 8, cy + 4, Ui::rgb(222, 83, 83));
            tft.fillCircle(cx, cy, 2, ink());
            break;
        case LauncherIcon::WhackAMole:
            // A mole in its hole, not a grid. This tile and Tic-Tac-Toe were
            // the same white lattice with a dot added.
            tft.fillRoundRect(cx - 17, cy + 4, 34, 14, 6, Ui::rgb(92, 66, 44));
            tft.fillCircle(cx - 2, cy - 1, 11, Ui::rgb(168, 134, 96));
            tft.fillCircle(cx - 6, cy - 4, 2, PIP_BLACK);
            tft.fillCircle(cx + 2, cy - 4, 2, PIP_BLACK);
            tft.fillCircle(cx - 2, cy + 2, 3, Ui::rgb(232, 150, 150));
            tft.fillRoundRect(cx + 6, cy - 17, 12, 7, 2, silver());
            tft.fillRect(cx + 10, cy - 10, 4, 8, Ui::rgb(150, 116, 34));
            break;
        case LauncherIcon::Cinnamon: {
            // Four pads with one lit, which is the whole of the game.
            tft.fillCircle(cx - 10, cy - 9, 7, rose());
            tft.fillCircle(cx + 10, cy - 9, 7, Ui::rgb(30, 90, 130));
            tft.fillCircle(cx - 10, cy + 9, 7, Ui::rgb(28, 110, 70));
            tft.fillCircle(cx + 10, cy + 9, 7, gold());
            tft.drawCircle(cx + 10, cy - 9, 8, sky());
            tft.fillCircle(cx + 10, cy - 9, 7, sky());
            break;
        }
        case LauncherIcon::Microku: {
            // Coloured cells in a 2x2 block. The digits it used to draw were
            // font 1 and unreadable, and a bare grid reads as Tic-Tac-Toe.
            tft.fillRoundRect(cx - 16, cy - 16, 32, 32, 2, TFT_WHITE);
            const uint16_t cell[4] = {sky(), gold(), leaf(), rose()};
            for (uint8_t i = 0; i < 4; ++i) {
                if (i == 2) continue;   // one blank square: the one to solve
                tft.fillRect(static_cast<int16_t>(cx - 14 + (i % 2) * 15),
                             static_cast<int16_t>(cy - 14 + (i / 2) * 15), 13, 13,
                             cell[i]);
            }
            tft.drawLine(cx, cy - 16, cx, cy + 16, ink());
            tft.drawLine(cx - 16, cy, cx + 16, cy, ink());
            break;
        }
        case LauncherIcon::ShapeColor:
            // Three shapes in three colours: both halves of "red circle".
            tft.fillCircle(cx - 9, cy - 6, 9, TFT_WHITE);
            tft.fillRect(cx + 3, cy - 14, 15, 15, cream());
            Ui::drawTriangleShape(tft, cx, cy + 10, 9, leaf(), true);
            break;
        case LauncherIcon::Counting:
            // Five dots as a countable group -- three over two -- rather than
            // the zig-zag scatter, which read as noise.
            for (uint8_t i = 0; i < 3; ++i) {
                tft.fillCircle(static_cast<int16_t>(cx - 12 + i * 12), cy - 8, 5, TFT_WHITE);
            }
            for (uint8_t i = 0; i < 2; ++i) {
                tft.fillCircle(static_cast<int16_t>(cx - 6 + i * 12), cy + 6, 5, cream());
            }
            break;
        case LauncherIcon::Money:
            // Copper behind silver, milled edges, no font 1 denominations.
            tft.fillCircle(cx - 7, cy - 5, 11, Ui::rgb(198, 116, 62));
            tft.drawCircle(cx - 7, cy - 5, 11, Ui::rgb(140, 78, 40));
            tft.drawCircle(cx - 7, cy - 5, 7, Ui::rgb(140, 78, 40));
            tft.fillCircle(cx + 6, cy + 6, 11, silver());
            tft.drawCircle(cx + 6, cy + 6, 11, slate());
            tft.drawCircle(cx + 6, cy + 6, 7, slate());
            break;
        case LauncherIcon::Fractions:
            // A pie cut into quarters with one taken. Percent is a ring, so
            // the two no longer collapse into the same white-circle-and-wedge.
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.fillTriangle(cx, cy, cx, cy - 16, cx + 16, cy - 16, gold());
            tft.fillTriangle(cx, cy, cx + 16, cy - 16, cx + 16, cy, gold());
            tft.drawCircle(cx, cy, 16, ink());
            tft.drawLine(cx, cy - 16, cx, cy + 16, ink());
            tft.drawLine(cx - 16, cy, cx + 16, cy, ink());
            break;
        case LauncherIcon::Maze:
            tft.drawRect(cx - 17, cy - 15, 34, 30, TFT_WHITE);
            tft.drawLine(cx - 7, cy - 15, cx - 7, cy + 6, TFT_WHITE);
            tft.drawLine(cx + 5, cy - 6, cx + 5, cy + 15, TFT_WHITE);
            tft.drawLine(cx + 5, cy - 6, cx + 17, cy - 6, TFT_WHITE);
            tft.drawLine(cx - 17, cy + 6, cx - 7, cy + 6, TFT_WHITE);
            tft.fillCircle(cx - 12, cy - 10, 3, cream());
            tft.fillCircle(cx + 11, cy + 10, 3, leaf());
            break;
        case LauncherIcon::Sort:
            /* Bars sorted short-to-long, laid out horizontally. Scores is also
             * bars, so the axis is what separates them: Sort reads as a list,
             * Scores as a chart. */
            for (uint8_t b = 0; b < 3; ++b) {
                tft.fillRoundRect(cx - 16, static_cast<int16_t>(cy - 14 + b * 11),
                                  static_cast<int16_t>(12 + b * 10), 8, 2,
                                  b == 2 ? cream() : TFT_WHITE);
            }
            break;
        case LauncherIcon::ColorMix:
            tft.fillCircle(cx - 8, cy - 6, 10, 0xF800);
            tft.fillCircle(cx + 8, cy - 6, 10, 0x041F);
            tft.fillCircle(cx, cy + 7, 10, 0x9813);
            break;
        case LauncherIcon::SlidingPuzzle:
            /* Three tiles and the gap that makes it a sliding puzzle. The old
             * loop ran a fourth time to draw nothing. */
            for (uint8_t i = 0; i < 3; ++i) {
                tft.fillRoundRect(static_cast<int16_t>(cx - 16 + (i % 2) * 17),
                                  static_cast<int16_t>(cy - 16 + (i / 2) * 17),
                                  15, 15, 2, TFT_WHITE);
            }
            tft.drawRoundRect(cx + 1, cy + 1, 15, 15, 2, silver());
            break;
        case LauncherIcon::OddOneOut:
            /* The odd one differs in shape as well as colour -- that is the
             * game, and colour alone is the weaker cue on a coloured tile. */
            for (uint8_t i = 0; i < 4; ++i) {
                const int16_t x = static_cast<int16_t>(cx - 12 + i * 8);
                if (i == 2) {
                    tft.fillRect(x - 5, cy - 5, 11, 11, cream());
                } else {
                    tft.fillCircle(x, cy, 5, TFT_WHITE);
                }
            }
            break;
        case LauncherIcon::Settings:
            Ui::drawGearIcon(tft, Rect{static_cast<int16_t>(cx - 13),
                                       static_cast<int16_t>(cy - 13), 26, 26});
            break;
        case LauncherIcon::WiFi:
            // Two fan arcs over the source dot. Drawn as arcs, not as circles
            // with their bottoms painted out -- see drawArcTop.
            drawArcTop(tft, cx, cy + 8, 14, TFT_WHITE);
            drawArcTop(tft, cx, cy + 8, 13, TFT_WHITE);
            drawArcTop(tft, cx, cy + 8, 8, TFT_WHITE);
            drawArcTop(tft, cx, cy + 8, 7, TFT_WHITE);
            tft.fillCircle(cx, cy + 8, 3, TFT_WHITE);
            break;
        case LauncherIcon::ObjectAdd:
            tft.fillCircle(cx - 11, cy - 7, 7, TFT_WHITE);
            tft.fillCircle(cx - 11, cy + 8, 7, leaf());
            tft.fillCircle(cx + 3, cy + 8, 7, cream());
            tft.fillRect(cx + 6, cy - 9, 12, 3, TFT_WHITE);
            tft.fillRect(cx + 10, cy - 13, 3, 12, TFT_WHITE);
            break;
        case LauncherIcon::FingerCount:
            // Three fingers up, thumb across: a hand, not a bar chart.
            for (uint8_t f = 0; f < 4; ++f) {
                const bool up = f < 3;
                tft.fillRoundRect(static_cast<int16_t>(cx - 13 + f * 7),
                                  static_cast<int16_t>(up ? cy - 15 : cy - 4),
                                  5, static_cast<int16_t>(up ? 17 : 6), 2,
                                  up ? TFT_WHITE : silver());
            }
            tft.fillRoundRect(cx - 14, cy + 2, 27, 12, 3, TFT_WHITE);
            tft.fillRoundRect(cx + 11, cy - 1, 7, 7, 3, TFT_WHITE);
            break;
        case LauncherIcon::Sequence:
            /* A calendar page. It used to stack "Mon"/"Tue"/"Wed" at font 1,
             * three lines of six-pixel text on a saturated tile. */
            tft.fillRoundRect(cx - 16, cy - 14, 32, 29, 3, TFT_WHITE);
            tft.fillRect(cx - 16, cy - 14, 32, 8, Ui::rgb(222, 83, 83));
            tft.fillRect(cx - 10, cy - 17, 3, 6, silver());
            tft.fillRect(cx + 7, cy - 17, 3, 6, silver());
            for (uint8_t row = 0; row < 2; ++row) {
                for (uint8_t col = 0; col < 4; ++col) {
                    tft.fillRect(static_cast<int16_t>(cx - 12 + col * 7),
                                 static_cast<int16_t>(cy - 3 + row * 7), 4, 4,
                                 (row == 1 && col == 2) ? ink() : silver());
                }
            }
            break;
        case LauncherIcon::NumberLine:
            tft.drawFastHLine(cx - 17, cy + 6, 34, TFT_WHITE);
            tft.drawFastHLine(cx - 17, cy + 7, 34, TFT_WHITE);
            for (uint8_t t = 0; t < 5; ++t) {
                tft.drawFastVLine(static_cast<int16_t>(cx - 16 + t * 8), cy + 1, 5, TFT_WHITE);
            }
            tft.fillTriangle(cx + 8, cy - 1, cx + 3, cy - 9, cx + 13, cy - 9, cream());
            break;
        case LauncherIcon::Flag:
            // World flags: a tricolour, so it cannot be mistaken for the
            // star-field used by State Flags.
            tft.drawFastVLine(cx - 14, cy - 16, 33, silver());
            tft.drawFastVLine(cx - 13, cy - 16, 33, silver());
            tft.fillRect(cx - 12, cy - 15, 9, 19, Ui::rgb(60, 120, 220));
            tft.fillRect(cx - 3, cy - 15, 9, 19, TFT_WHITE);
            tft.fillRect(cx + 6, cy - 15, 9, 19, Ui::rgb(222, 83, 83));
            tft.drawRect(cx - 12, cy - 15, 27, 19, TFT_WHITE);
            break;
        case LauncherIcon::States:
            // Name the capital: a capital star on its marker.
            tft.fillCircle(cx, cy - 2, 15, TFT_WHITE);
            fillStar(tft, cx, cy - 2, 12, ink());
            tft.fillTriangle(cx - 6, cy + 8, cx + 6, cy + 8, cx, cy + 17, TFT_WHITE);
            break;
        case LauncherIcon::Trace:
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("A", cx - 6, cy - 1, 4);
            tft.setTextDatum(TL_DATUM);
            for (int16_t d = 0; d < 16; d += 4) {
                tft.fillCircle(cx + 12, static_cast<int16_t>(cy - 13 + d), 2, leaf());
            }
            break;
        case LauncherIcon::StateFlag:
            // A state flag: canton of stars, not the world tricolour.
            tft.drawFastVLine(cx - 14, cy - 16, 33, silver());
            tft.drawFastVLine(cx - 13, cy - 16, 33, silver());
            tft.fillRect(cx - 12, cy - 15, 27, 19, Ui::rgb(40, 70, 150));
            fillStar(tft, cx - 4, cy - 9, 6, TFT_WHITE);
            fillStar(tft, cx + 8, cy - 2, 5, TFT_WHITE);
            tft.drawRect(cx - 12, cy - 15, 27, 19, TFT_WHITE);
            break;
        case LauncherIcon::StateMap:
            // An outline to identify, with no flag and no star to confuse it
            // with the other two state games.
            tft.fillTriangle(cx - 16, cy - 12, cx + 6, cy - 14, cx + 2, cy + 2, TFT_WHITE);
            tft.fillTriangle(cx - 16, cy - 12, cx + 2, cy + 2, cx - 10, cy + 10, TFT_WHITE);
            tft.fillTriangle(cx + 2, cy + 2, cx + 16, cy - 4, cx + 8, cy + 15, TFT_WHITE);
            tft.fillTriangle(cx - 10, cy + 10, cx + 2, cy + 2, cx + 8, cy + 15, TFT_WHITE);
            break;
        case LauncherIcon::Percent:
            /* A progress ring reading about three quarters round. Fractions is
             * the cut pie; these two used to be the same picture. */
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.fillTriangle(cx, cy, cx, cy - 17, cx + 17, cy - 17, gold());
            tft.fillTriangle(cx, cy, cx + 17, cy - 17, cx + 17, cy + 17, gold());
            tft.fillTriangle(cx, cy, cx + 17, cy + 17, cx - 17, cy + 17, gold());
            tft.fillCircle(cx, cy, 9, fill);
            tft.drawCircle(cx, cy, 16, ink());
            tft.drawCircle(cx, cy, 9, ink());
            break;
        case LauncherIcon::GreWords:
            tft.fillRoundRect(cx - 11, cy - 10, 26, 24, 3, silver());
            tft.fillRoundRect(cx - 15, cy - 14, 26, 24, 3, TFT_WHITE);
            tft.setTextColor(ink(), TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("Aa", cx - 2, cy - 2, 2);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::Dice:
            /* Two dice, the far one tucked behind. Both faces are real: a
             * blank square is the one thing a die never shows. */
            drawDieFace(tft, cx - 2, cy - 17, 19, 0b100000001, silver(), slate());
            drawDieFace(tft, cx - 17, cy - 3, 21, 0b101010101, TFT_WHITE, slate());
            break;
        case LauncherIcon::CoinFlip:
            /* One coin face-on and one caught edge-on, which is what the game
             * draws mid-spin. The edge-on sliver used to run 24px below cy and
             * over the bottom of the landscape tile. */
            tft.fillCircle(cx + 4, cy - 1, 14, Ui::rgb(226, 182, 72));
            tft.drawCircle(cx + 4, cy - 1, 14, Ui::rgb(150, 116, 34));
            tft.drawCircle(cx + 4, cy - 1, 10, Ui::rgb(150, 116, 34));
            tft.fillRoundRect(cx - 16, cy - 9, 7, 18, 3, Ui::rgb(240, 206, 120));
            tft.drawRoundRect(cx - 16, cy - 9, 7, 18, 3, Ui::rgb(150, 116, 34));
            break;
        case LauncherIcon::Profiles:
            tft.fillCircle(cx - 6, cy - 6, 6, TFT_WHITE);
            tft.fillCircle(cx - 6, cy + 8, 10, TFT_WHITE);
            tft.fillRect(cx - 18, cy + 9, 24, 9, fill);
            tft.fillCircle(cx + 9, cy - 4, 5, Ui::rgb(255, 226, 90));
            tft.fillCircle(cx + 9, cy + 8, 8, Ui::rgb(255, 226, 90));
            tft.fillRect(cx + 1, cy + 9, 17, 9, fill);
            break;
        case LauncherIcon::Scores:
            // Vertical chart with a gold leader. Sort is the horizontal list.
            for (uint8_t b = 0; b < 3; ++b) {
                const int16_t h = static_cast<int16_t>(9 + b * 8);
                tft.fillRect(static_cast<int16_t>(cx - 15 + b * 11),
                             static_cast<int16_t>(cy + 14 - h), 9, h,
                             b == 2 ? Ui::rgb(255, 226, 90) : TFT_WHITE);
            }
            break;
        case LauncherIcon::About:
            tft.fillCircle(cx, cy, 16, TFT_WHITE);
            tft.setTextColor(ink(), TFT_WHITE);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("i", cx, cy + 1, 4);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::SystemInfo:
            // A screen with a heartbeat on it: diagnostics, not a slideshow.
            tft.fillRoundRect(cx - 17, cy - 13, 34, 24, 2, TFT_WHITE);
            tft.fillRect(cx - 4, cy + 11, 8, 4, silver());
            tft.fillRect(cx - 11, cy + 15, 22, 3, silver());
            tft.drawLine(cx - 13, cy - 1, cx - 6, cy - 1, Ui::rgb(222, 83, 83));
            tft.drawLine(cx - 6, cy - 1, cx - 3, cy - 8, Ui::rgb(222, 83, 83));
            tft.drawLine(cx - 3, cy - 8, cx + 1, cy + 5, Ui::rgb(222, 83, 83));
            tft.drawLine(cx + 1, cy + 5, cx + 4, cy - 1, Ui::rgb(222, 83, 83));
            tft.drawLine(cx + 4, cy - 1, cx + 13, cy - 1, Ui::rgb(222, 83, 83));
            break;
    }
}
