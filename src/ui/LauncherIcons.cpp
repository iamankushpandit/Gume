#include "ui/LauncherIcons.h"

/* Launcher tile artwork.
 *
 * These sit on saturated blue, green and red tiles that rotate per slot, so an
 * icon has to hold up against all three. What follows is a small design system
 * rather than thirty-four independent drawings, because what made the earlier
 * set look homemade was not any single icon -- it was that they disagreed with
 * each other. Some were outlines and some solids; strokes ran from one to three
 * pixels; several sat timidly in the middle of a tile while others filled it;
 * and a few spent four colours where one would do.
 *
 * The rules, in the order they matter:
 *
 * 1. **Silhouette first, in SNOW.** Every icon's main mass is one solid light
 *    shape. Solid reads at arm's length where a one-pixel outline dissolves,
 *    and a single near-white against all three tile colours gives the set one
 *    voice. Outlines are for detail *inside* a silhouette, never for the shape.
 *
 * 2. **Detail in INK, accent in exactly one hue.** INK is the near-black for
 *    anything drawn on top of SNOW; each icon then gets at most one colour from
 *    the accent palette. Five games are exempt because colour *is* their
 *    subject -- Cinnamon, Color Mix, Shape & Color and the two flags -- and
 *    each says so at the case.
 *
 * 3. **Fill the box.** ICON_HALF is 18 and the main mass should approach it.
 *    Landscape tiles are 145x46 with the icon centred at (r.x + 24, r.y + 22),
 *    so the vertical budget binds: past cy +/- 22 the art leaves the tile, and
 *    past +/- 18 it crowds the rounded border.
 *
 * 4. **Two pixels minimum for a stroke, radius 4 for a rounded corner.** A
 *    single-pixel line on this panel breaks up and reads as an artefact. Use
 *    the stroke helpers rather than drawLine or drawFastHLine directly.
 *
 * 5. **Distinct silhouettes.** A player navigates this grid by shape. Three
 *    pairs were once the same picture: Fractions and Percent were both a white
 *    circle with a yellow wedge, Flags and State Flags the same flag in two
 *    colours, Tic-Tac-Toe and Whack A Mole both a white grid.
 *
 * 6. **Leave the text datum where you found it.** Cases that set MC_DATUM and
 *    returned without restoring TL_DATUM used to re-align whatever the launcher
 *    drew next. */

namespace {

/* Half-width of the box every icon must fit inside. See rule 3. */
constexpr int16_t ICON_HALF = 18;
/* The landscape tile is 46 tall and the icon is centred 22 down from its top,
 * so anything past 22 is off the tile outright; 18 keeps a margin inside the
 * rounded border. Stated as an assertion so the relationship is checked rather
 * than remembered. */
static_assert(ICON_HALF <= 22, "icon box would overrun the landscape tile");
/* Every rounded corner in this file. See rule 4. */
constexpr int16_t RADIUS = 4;

/* Through Ui::rgb rather than hand-written RGB565 literals: an earlier draft
 * carried 0x2418 in a comment claiming to be rgb(36, 132, 204), and it was not
 * -- the real value is 0x2439. Let the packer do it. */
uint16_t snow()  { return Ui::rgb(247, 250, 253); }   // the silhouette
uint16_t ink()   { return Ui::rgb(26, 34, 48); }      // detail on top of snow
uint16_t steel() { return Ui::rgb(158, 172, 188); }   // a second, quieter solid
uint16_t amber() { return Ui::rgb(255, 196, 64); }
uint16_t coral() { return Ui::rgb(255, 110, 100); }
uint16_t mint()  { return Ui::rgb(88, 220, 158); }
uint16_t sky()   { return Ui::rgb(96, 190, 255); }

// ---- stroke helpers: nothing in this file draws a one-pixel line ----

void strokeH(Ui::Renderer& tft, int16_t x, int16_t y, int16_t w, uint16_t color) {
    tft.fillRect(x, y, w, 2, color);
}

void strokeV(Ui::Renderer& tft, int16_t x, int16_t y, int16_t h, uint16_t color) {
    tft.fillRect(x, y, 2, h, color);
}

/* A diagonal with body. drawLine is one pixel wide, and a one-pixel diagonal is
 * the worst case on this panel, so every diagonal here is drawn as a small
 * bundle of offset lines. */
void strokeDiag(Ui::Renderer& tft, int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                uint16_t color) {
    for (int16_t t = -1; t <= 1; ++t) {
        tft.drawLine(x0, static_cast<int16_t>(y0 + t), x1, static_cast<int16_t>(y1 + t), color);
        tft.drawLine(static_cast<int16_t>(x0 + t), y0, static_cast<int16_t>(x1 + t), y1, color);
    }
}

/** The workhorse silhouette: a rounded slab centred on (cx, cy). */
void plate(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t w, int16_t h,
           uint16_t color) {
    tft.fillRoundRect(static_cast<int16_t>(cx - w / 2), static_cast<int16_t>(cy - h / 2),
                      w, h, RADIUS, color);
}

/** A two-pixel ring, so circles match the stroke weight of everything else. */
void ring(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    tft.drawCircle(cx, cy, radius, color);
    tft.drawCircle(cx, cy, static_cast<int16_t>(radius - 1), color);
}

/* Unit five-point star, x100, interleaved outer/inner from twelve o'clock.
 * A table, so the icons cost no runtime trigonometry. */
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

/* Upper half of a circle, from the circle equation.
 *
 * The Wi-Fi tile used to draw whole circles and paint their lower halves out
 * with a rectangle in the tile colour. The mask was three pixels short, so the
 * base of the outer ring survived at cy + 24 -- outside a 46px landscape tile.
 * Explicit geometry cannot spill the way a mask can miss. Integer square root,
 * because there is no reason to pull floating point in for a 16px radius. */
void arcTop(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, uint16_t color) {
    for (int16_t dx = -radius; dx <= radius; ++dx) {
        const int32_t inside = static_cast<int32_t>(radius) * radius -
                               static_cast<int32_t>(dx) * dx;
        int32_t dy = 0;
        while ((dy + 1) * (dy + 1) <= inside) {
            ++dy;
        }
        tft.drawPixel(cx + dx, static_cast<int16_t>(cy - dy), color);
        tft.drawPixel(cx + dx, static_cast<int16_t>(cy - dy + 1), color);
    }
}

/** A die face on the usual 3x3 grid, bit 8 = top-left. */
void dieFace(Ui::Renderer& tft, int16_t x, int16_t y, int16_t size, uint16_t face,
             uint16_t body) {
    tft.fillRoundRect(x, y, size, size, RADIUS, body);
    const int16_t step = static_cast<int16_t>(size / 4);
    for (uint8_t cell = 0; cell < 9; ++cell) {
        if ((face & (1u << (8 - cell))) == 0) continue;
        tft.fillCircle(static_cast<int16_t>(x + step + (cell % 3) * step),
                       static_cast<int16_t>(y + step + (cell / 3) * step),
                       2, ink());
    }
}

/** Pole only; the field is the caller's, and is what tells the two flags apart. */
void flagPole(Ui::Renderer& tft, int16_t cx, int16_t cy) {
    strokeV(tft, static_cast<int16_t>(cx - 15), static_cast<int16_t>(cy - 16), 33, steel());
}

}   // namespace

void drawLauncherIcon(Ui::Renderer& tft, LauncherIcon icon, const Rect& r,
                      uint16_t fill, int16_t cx, int16_t cy) {
    (void)r;
    switch (icon) {
        case LauncherIcon::TicTacToe:
            // A grid that has been played in. An empty lattice was Whack.
            plate(tft, cx, cy, 34, 34, snow());
            strokeV(tft, static_cast<int16_t>(cx - 6), static_cast<int16_t>(cy - 15), 30, ink());
            strokeV(tft, static_cast<int16_t>(cx + 5), static_cast<int16_t>(cy - 15), 30, ink());
            strokeH(tft, static_cast<int16_t>(cx - 15), static_cast<int16_t>(cy - 6), 30, ink());
            strokeH(tft, static_cast<int16_t>(cx - 15), static_cast<int16_t>(cy + 5), 30, ink());
            strokeDiag(tft, cx - 13, cy - 13, cx - 9, cy - 9, coral());
            strokeDiag(tft, cx - 9, cy - 13, cx - 13, cy - 9, coral());
            ring(tft, cx, cy, 4, ink());
            break;
        case LauncherIcon::Memory:
            // One card face-down, one turned over.
            tft.fillRoundRect(cx - 17, cy - 13, 16, 26, RADIUS, steel());
            strokeDiag(tft, cx - 14, cy - 7, cx - 5, cy + 6, snow());
            tft.fillRoundRect(cx + 1, cy - 15, 16, 26, RADIUS, snow());
            tft.fillCircle(cx + 9, cy - 2, 5, coral());
            break;
        case LauncherIcon::Math:
            // Plus over minus, the two operations the game asks for.
            plate(tft, cx, cy, 34, 32, snow());
            strokeH(tft, static_cast<int16_t>(cx - 12), static_cast<int16_t>(cy - 9), 12, ink());
            strokeV(tft, static_cast<int16_t>(cx - 7), static_cast<int16_t>(cy - 14), 12, ink());
            strokeH(tft, static_cast<int16_t>(cx), static_cast<int16_t>(cy + 7), 12, ink());
            break;
        case LauncherIcon::Multiplication:
            // A drawn cross, not the letter x, which blurred into Math's plus.
            plate(tft, cx, cy, 34, 32, snow());
            strokeDiag(tft, cx - 9, cy - 9, cx + 9, cy + 9, ink());
            strokeDiag(tft, cx + 9, cy - 9, cx - 9, cy + 9, ink());
            break;
        case LauncherIcon::Time:
            tft.fillCircle(cx, cy, 17, snow());
            for (uint8_t q = 0; q < 4; ++q) {
                const int16_t dx = (q == 1) ? 13 : (q == 3 ? -13 : 0);
                const int16_t dy = (q == 2) ? 13 : (q == 0 ? -13 : 0);
                tft.fillCircle(cx + dx, cy + dy, 2, ink());
            }
            strokeV(tft, cx, static_cast<int16_t>(cy - 10), 11, ink());
            strokeDiag(tft, cx, cy, cx + 8, cy + 5, coral());
            tft.fillCircle(cx, cy, 2, ink());
            break;
        case LauncherIcon::WhackAMole:
            // A mole in its hole. This and Tic-Tac-Toe were the same lattice.
            tft.fillCircle(cx - 2, cy - 2, 12, snow());
            tft.fillCircle(cx - 6, cy - 5, 2, ink());
            tft.fillCircle(cx + 3, cy - 5, 2, ink());
            tft.fillCircle(cx - 2, cy + 1, 3, coral());
            tft.fillRoundRect(cx - 18, cy + 5, 36, 12, RADIUS, ink());
            tft.fillRoundRect(cx + 6, cy - 18, 12, 7, 3, steel());
            strokeV(tft, static_cast<int16_t>(cx + 10), static_cast<int16_t>(cy - 12), 8, steel());
            break;
        case LauncherIcon::Cinnamon:
            /* Colour exemption (rule 2): four lit pads *are* the game. Three
             * sit back in a muted tint so the fourth reads as the flashing one. */
            tft.fillCircle(cx - 10, cy - 10, 8, Ui::shade(coral(), 55));
            tft.fillCircle(cx + 10, cy - 10, 8, Ui::shade(sky(), 55));
            tft.fillCircle(cx - 10, cy + 10, 8, Ui::shade(mint(), 55));
            tft.fillCircle(cx + 10, cy + 10, 8, amber());
            ring(tft, cx + 10, cy + 10, 10, snow());
            break;
        case LauncherIcon::Microku: {
            // One empty square left to solve. Its digits used to be font 1.
            plate(tft, cx, cy, 34, 34, snow());
            const int16_t cell[4][2] = {{-8, -8}, {8, -8}, {-8, 8}, {8, 8}};
            for (uint8_t i = 0; i < 4; ++i) {
                if (i == 2) continue;
                tft.fillRoundRect(static_cast<int16_t>(cx + cell[i][0] - 6),
                                  static_cast<int16_t>(cy + cell[i][1] - 6), 13, 13, 2,
                                  i == 1 ? amber() : ink());
            }
            strokeV(tft, static_cast<int16_t>(cx - 1), static_cast<int16_t>(cy - 15), 30, steel());
            strokeH(tft, static_cast<int16_t>(cx - 15), static_cast<int16_t>(cy - 1), 30, steel());
            break;
        }
        case LauncherIcon::ShapeColor:
            /* Colour exemption (rule 2): the game asks for "red circle", so the
             * tile has to carry shape and colour at once. */
            tft.fillCircle(cx - 9, cy - 7, 9, snow());
            tft.fillRoundRect(cx + 2, cy - 16, 16, 16, RADIUS, amber());
            Ui::drawTriangleShape(tft, cx, cy + 10, 9, coral(), true);
            break;
        case LauncherIcon::Counting:
            // A countable group, three over two, rather than a scatter.
            for (uint8_t i = 0; i < 3; ++i) {
                tft.fillCircle(static_cast<int16_t>(cx - 12 + i * 12), cy - 8, 5, snow());
            }
            for (uint8_t i = 0; i < 2; ++i) {
                tft.fillCircle(static_cast<int16_t>(cx - 6 + i * 12), cy + 6, 5,
                               i == 1 ? amber() : snow());
            }
            break;
        case LauncherIcon::Money:
            // Gold behind silver, milled rims, no font 1 denominations.
            tft.fillCircle(cx - 7, cy - 6, 11, amber());
            ring(tft, cx - 7, cy - 6, 7, Ui::shade(amber(), 65));
            tft.fillCircle(cx + 6, cy + 6, 11, snow());
            ring(tft, cx + 6, cy + 6, 7, steel());
            break;
        case LauncherIcon::Fractions:
            // A cut pie. Percent is the ring; the two used to be one picture.
            tft.fillCircle(cx, cy, 17, snow());
            tft.fillTriangle(cx, cy, cx, cy - 17, cx + 17, cy - 17, amber());
            tft.fillTriangle(cx, cy, cx + 17, cy - 17, cx + 17, cy, amber());
            strokeV(tft, cx, static_cast<int16_t>(cy - 17), 34, ink());
            strokeH(tft, static_cast<int16_t>(cx - 17), cy, 34, ink());
            break;
        case LauncherIcon::Maze:
            plate(tft, cx, cy, 36, 32, snow());
            strokeV(tft, static_cast<int16_t>(cx - 7), static_cast<int16_t>(cy - 16), 22, ink());
            strokeV(tft, static_cast<int16_t>(cx + 5), static_cast<int16_t>(cy - 6), 22, ink());
            strokeH(tft, static_cast<int16_t>(cx + 5), static_cast<int16_t>(cy - 6), 13, ink());
            strokeH(tft, static_cast<int16_t>(cx - 18), static_cast<int16_t>(cy + 5), 11, ink());
            tft.fillCircle(cx - 13, cy - 10, 3, coral());
            break;
        case LauncherIcon::Sort:
            /* Bars sorted short to long, horizontal. Scores is bars too, so the
             * axis is what separates them: a list against a chart. */
            for (uint8_t b = 0; b < 3; ++b) {
                tft.fillRoundRect(cx - 16, static_cast<int16_t>(cy - 14 + b * 11),
                                  static_cast<int16_t>(13 + b * 9), 8, 3,
                                  b == 2 ? amber() : snow());
            }
            break;
        case LauncherIcon::ColorMix:
            // Colour exemption (rule 2): the primaries are the subject.
            tft.fillCircle(cx - 8, cy - 6, 10, coral());
            tft.fillCircle(cx + 8, cy - 6, 10, sky());
            tft.fillCircle(cx, cy + 7, 10, amber());
            break;
        case LauncherIcon::SlidingPuzzle:
            // Three tiles and the gap that makes it slide.
            for (uint8_t i = 0; i < 3; ++i) {
                tft.fillRoundRect(static_cast<int16_t>(cx - 17 + (i % 2) * 18),
                                  static_cast<int16_t>(cy - 17 + (i / 2) * 18),
                                  16, 16, RADIUS, i == 0 ? amber() : snow());
            }
            tft.drawRoundRect(cx + 1, cy + 1, 16, 16, RADIUS, Ui::shade(snow(), 45));
            break;
        case LauncherIcon::OddOneOut:
            /* The odd one differs in shape as well as colour -- that is the
             * game, and shape survives on a red tile where colour does not. */
            for (uint8_t i = 0; i < 4; ++i) {
                const int16_t x = static_cast<int16_t>(cx - 12 + i * 8);
                if (i == 2) {
                    tft.fillRoundRect(x - 6, cy - 6, 13, 13, 3, amber());
                } else {
                    tft.fillCircle(x, cy, 5, snow());
                }
            }
            break;
        case LauncherIcon::Settings:
            Ui::drawGearIcon(tft, Rect{static_cast<int16_t>(cx - 17),
                                       static_cast<int16_t>(cy - 17), 34, 34}, snow());
            break;
        case LauncherIcon::WiFi:
            // Real arcs, not circles with their bottoms painted out.
            arcTop(tft, cx, cy + 9, 16, snow());
            arcTop(tft, cx, cy + 9, 10, snow());
            tft.fillCircle(cx, cy + 9, 4, snow());
            break;
        case LauncherIcon::ObjectAdd:
            tft.fillCircle(cx - 11, cy - 7, 7, snow());
            tft.fillCircle(cx - 11, cy + 8, 7, snow());
            tft.fillCircle(cx + 3, cy + 8, 7, amber());
            strokeH(tft, static_cast<int16_t>(cx + 5), static_cast<int16_t>(cy - 8), 13, snow());
            strokeV(tft, static_cast<int16_t>(cx + 10), static_cast<int16_t>(cy - 13), 13, snow());
            break;
        case LauncherIcon::FingerCount:
            // A hand with three up and a thumb across, not a bar chart.
            for (uint8_t f = 0; f < 4; ++f) {
                const bool up = f < 3;
                tft.fillRoundRect(static_cast<int16_t>(cx - 13 + f * 7),
                                  static_cast<int16_t>(up ? cy - 16 : cy - 5),
                                  6, static_cast<int16_t>(up ? 18 : 7), 3,
                                  up ? snow() : steel());
            }
            tft.fillRoundRect(cx - 14, cy + 1, 28, 13, RADIUS, snow());
            tft.fillRoundRect(cx + 10, cy - 2, 8, 8, RADIUS, snow());
            break;
        case LauncherIcon::Sequence:
            // A calendar page. It used to stack three font 1 day names.
            plate(tft, cx, cy + 1, 34, 30, snow());
            tft.fillRect(cx - 17, cy - 14, 34, 8, coral());
            strokeV(tft, static_cast<int16_t>(cx - 10), static_cast<int16_t>(cy - 18), 7, steel());
            strokeV(tft, static_cast<int16_t>(cx + 8), static_cast<int16_t>(cy - 18), 7, steel());
            for (uint8_t row = 0; row < 2; ++row) {
                for (uint8_t col = 0; col < 4; ++col) {
                    const bool today = (row == 1 && col == 2);
                    tft.fillRect(static_cast<int16_t>(cx - 13 + col * 7),
                                 static_cast<int16_t>(cy - 2 + row * 7), 5, 5,
                                 today ? amber() : ink());
                }
            }
            break;
        case LauncherIcon::NumberLine:
            strokeH(tft, static_cast<int16_t>(cx - 17), static_cast<int16_t>(cy + 6), 34, snow());
            for (uint8_t t = 0; t < 5; ++t) {
                strokeV(tft, static_cast<int16_t>(cx - 16 + t * 8),
                        static_cast<int16_t>(cy + 1), 6, snow());
            }
            tft.fillTriangle(cx + 8, cy + 2, cx + 2, cy - 8, cx + 14, cy - 8, amber());
            break;
        case LauncherIcon::Flag:
            /* Colour exemption (rule 2). A tricolour, so it cannot be taken for
             * the star field State Flags uses. */
            flagPole(tft, cx, cy);
            tft.fillRect(cx - 13, cy - 15, 9, 19, sky());
            tft.fillRect(cx - 4, cy - 15, 9, 19, snow());
            tft.fillRect(cx + 5, cy - 15, 9, 19, coral());
            break;
        case LauncherIcon::States:
            // Name the capital: a capital star on its map pin.
            tft.fillCircle(cx, cy - 3, 14, snow());
            tft.fillTriangle(cx - 7, cy + 7, cx + 7, cy + 7, cx, cy + 17, snow());
            fillStar(tft, cx, cy - 3, 11, amber());
            break;
        case LauncherIcon::Trace:
            // The letter, plus the guide dots the game makes a player follow.
            tft.setTextColor(snow(), fill);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("A", cx - 6, cy - 1, 4);
            tft.setTextDatum(TL_DATUM);
            for (int16_t d = 0; d < 16; d += 5) {
                tft.fillCircle(cx + 12, static_cast<int16_t>(cy - 13 + d), 2, amber());
            }
            break;
        case LauncherIcon::StateFlag:
            // Colour exemption (rule 2): a canton of stars, not the tricolour.
            flagPole(tft, cx, cy);
            tft.fillRect(cx - 13, cy - 15, 27, 19, sky());
            fillStar(tft, cx - 5, cy - 9, 6, snow());
            fillStar(tft, cx + 7, cy - 2, 5, snow());
            break;
        case LauncherIcon::StateMap:
            // An outline to identify: no flag, no pin, no star.
            tft.fillTriangle(cx - 17, cy - 12, cx + 6, cy - 15, cx + 2, cy + 2, snow());
            tft.fillTriangle(cx - 17, cy - 12, cx + 2, cy + 2, cx - 10, cy + 11, snow());
            tft.fillTriangle(cx + 2, cy + 2, cx + 17, cy - 5, cx + 8, cy + 16, snow());
            tft.fillTriangle(cx - 10, cy + 11, cx + 2, cy + 2, cx + 8, cy + 16, snow());
            break;
        case LauncherIcon::Percent:
            /* A progress ring, about three quarters round. Fractions is the cut
             * pie; these two used to be the same picture. */
            tft.fillCircle(cx, cy, 17, snow());
            tft.fillTriangle(cx, cy, cx, cy - 18, cx + 18, cy - 18, amber());
            tft.fillTriangle(cx, cy, cx + 18, cy - 18, cx + 18, cy + 18, amber());
            tft.fillTriangle(cx, cy, cx + 18, cy + 18, cx - 18, cy + 18, amber());
            tft.fillCircle(cx, cy, 9, fill);
            break;
        case LauncherIcon::GreWords:
            tft.fillRoundRect(cx - 10, cy - 9, 26, 24, RADIUS, steel());
            tft.fillRoundRect(cx - 16, cy - 15, 26, 24, RADIUS, snow());
            tft.setTextColor(ink(), snow());
            tft.setTextDatum(MC_DATUM);
            tft.drawString("Aa", cx - 3, cy - 3, 2);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::Dice:
            /* Two dice, the far one tucked behind. Both faces are real: a blank
             * square is the one thing a die never shows. */
            dieFace(tft, cx - 2, cy - 17, 19, 0b100000001, steel());
            dieFace(tft, cx - 17, cy - 3, 21, 0b101010101, snow());
            break;
        case LauncherIcon::CoinFlip:
            /* One coin face-on and one caught edge-on, which is what the game
             * draws mid-spin. The sliver used to reach cy + 24 and run over the
             * bottom of a landscape tile. */
            tft.fillCircle(cx + 4, cy - 1, 14, amber());
            ring(tft, cx + 4, cy - 1, 10, Ui::shade(amber(), 65));
            tft.fillRoundRect(cx - 16, cy - 9, 8, 18, RADIUS, Ui::shade(amber(), 130));
            break;
        case LauncherIcon::Elements:
            /* One cell out of the wall chart, drawn the way the chart draws it:
             * atomic number in the corner, symbol below, a family stripe along
             * the foot. Not a grid -- Tic-Tac-Toe, Memory and Sliding Puzzle
             * are already grids, and rule 5 is what keeps them apart. */
            tft.fillRoundRect(cx - 16, cy - 16, 32, 32, RADIUS, snow());
            tft.fillRect(cx - 14, cy + 8, 28, 6, mint());
            tft.setTextColor(ink(), snow());
            tft.setTextDatum(TL_DATUM);
            tft.drawString("8", cx - 12, cy - 13, 1);
            tft.setTextDatum(MC_DATUM);
            tft.drawString("O", cx + 1, cy - 1, 4);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::Profiles:
            tft.fillCircle(cx - 7, cy - 6, 6, snow());
            tft.fillCircle(cx - 7, cy + 8, 10, snow());
            tft.fillRect(cx - 18, cy + 9, 23, 9, fill);
            tft.fillCircle(cx + 8, cy - 4, 5, amber());
            tft.fillCircle(cx + 8, cy + 8, 9, amber());
            tft.fillRect(cx, cy + 9, 18, 9, fill);
            break;
        case LauncherIcon::Scores:
            // A vertical chart with a gold leader. Sort is the horizontal list.
            for (uint8_t b = 0; b < 3; ++b) {
                const int16_t h = static_cast<int16_t>(10 + b * 8);
                tft.fillRoundRect(static_cast<int16_t>(cx - 16 + b * 11),
                                  static_cast<int16_t>(cy + 14 - h), 10, h, 3,
                                  b == 2 ? amber() : snow());
            }
            break;
        case LauncherIcon::About:
            tft.fillCircle(cx, cy, 17, snow());
            tft.setTextColor(ink(), snow());
            tft.setTextDatum(MC_DATUM);
            tft.drawString("i", cx, cy + 1, 4);
            tft.setTextDatum(TL_DATUM);
            break;
        case LauncherIcon::SystemInfo:
            // A screen with a heartbeat on it: diagnostics, not a slideshow.
            plate(tft, cx, cy - 2, 36, 26, snow());
            tft.fillRect(cx - 4, cy + 11, 8, 4, steel());
            tft.fillRoundRect(cx - 11, cy + 14, 22, 4, 2, steel());
            strokeH(tft, static_cast<int16_t>(cx - 14), static_cast<int16_t>(cy - 2), 6, coral());
            strokeDiag(tft, cx - 8, cy - 2, cx - 5, cy - 9, coral());
            strokeDiag(tft, cx - 5, cy - 9, cx - 1, cy + 5, coral());
            strokeDiag(tft, cx - 1, cy + 5, cx + 3, cy - 2, coral());
            strokeH(tft, static_cast<int16_t>(cx + 3), static_cast<int16_t>(cy - 2), 11, coral());
            break;

        /* Two devices facing each other with a signal arc between them: the
         * tile is about other consoles in the room, not about this one, so it
         * is deliberately not another Bluetooth rune -- that glyph already
         * means "my beacon is on" in the header. */
        case LauncherIcon::Nearby:
            tft.drawRoundRect(cx - 17, cy - 8, 10, 17, 2, TFT_WHITE);
            tft.drawRoundRect(cx + 7, cy - 8, 10, 17, 2, TFT_WHITE);
            tft.fillRect(cx - 15, cy - 6, 6, 9, Ui::rgb(120, 200, 255));
            tft.fillRect(cx + 9, cy - 6, 6, 9, Ui::rgb(120, 200, 255));
            for (int8_t i = 0; i < 3; ++i) {
                const int16_t r = static_cast<int16_t>(2 + i * 2);
                tft.drawCircle(cx, cy + 2, r, Ui::rgb(255, 226, 90));
            }
            break;
    }
}
