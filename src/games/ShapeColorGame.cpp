// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ShapeColorGame.h"
#include "engine/AppRegistry.h"

#include <math.h>

namespace {
constexpr uint16_t SELECTED = 0xFEC0;
constexpr uint16_t EMPTY_TARGET = 0xD6BA;

constexpr AppScoreInfo SHAPE_COLOR_SCORE = {
    "shapecolor", "Shapes", "shapeBest", "taps", true
};

constexpr AppMetadata SHAPE_COLOR_METADATA = {
    "shapecolor",
    "Shapes",
    "Shape & Color",
    "match outlines",
    "Shapes",
    "Match shapes to outlines, four levels.",
    &SHAPE_COLOR_SCORE,
    LauncherIcon::ShapeColor,
    14,
    true,
};

/* What a shape IS, beside how it is drawn. `concave` is the level 4 lesson
 * and `sides` is the level 2 one, so both are facts about the shape rather
 * than something a level decides -- a star is concave in every round it
 * appears in, including the first one. */
struct ShapeFact {
    const char* name;
    uint8_t sides;      // 0 for anything with a curve in it
    bool concave;
};

/* Indexed by Shape, and asserted below to stay that way. */
const ShapeFact SHAPE_FACTS[] = {
    {"circle",    0, false},
    {"square",    4, false},
    {"triangle",  3, false},
    {"star",      10, true},
    {"pentagon",  5, false},
    {"hexagon",   6, false},
    {"heptagon",  7, false},
    {"octagon",   8, false},
    {"oval",      0, false},
    {"rectangle", 4, false},
    {"diamond",   4, false},
    {"trapezium", 4, false},
    {"cross",     12, true},
    {"arrow",     7, true},
    {"pinwheel",  8, true},
};
static_assert(sizeof(SHAPE_FACTS) / sizeof(SHAPE_FACTS[0]) ==
                  static_cast<size_t>(ShapeColorGame::Shape::Count),
              "SHAPE_FACTS must have one row per Shape, in enum order");

/* Polygons, as hundredths of the radius, walked anticlockwise from twelve
 * o'clock. Every one of these is star-shaped about its own centre, which is
 * what lets a single triangle fan fill it -- including the concave ones. A
 * shape that is not (a spiral, a crescent) cannot be added to this table
 * without a different fill, and the fan would quietly paint outside it. */
struct PolyPoint { int8_t x; int8_t y; };

const PolyPoint STAR5[10] = {
    {0, -100}, {22, -31}, {95, -31}, {36, 12}, {59, 81},
    {0, 38},   {-59, 81}, {-36, 12}, {-95, -31}, {-22, -31},
};

const PolyPoint STAR4[8] = {
    {0, -100}, {26, -26}, {100, 0}, {26, 26},
    {0, 100},  {-26, 26}, {-100, 0}, {-26, -26},
};

const PolyPoint CROSS[12] = {
    {-33, -100}, {33, -100}, {33, -33}, {100, -33}, {100, 33}, {33, 33},
    {33, 100},   {-33, 100}, {-33, 33}, {-100, 33}, {-100, -33}, {-33, -33},
};

/* A block arrow pointing right. The kernel is the middle of the shaft, which
 * is also the centre the fan spreads from. */
const PolyPoint ARROW[7] = {
    {-90, -40}, {20, -40}, {20, -90}, {100, 0}, {20, 90}, {20, 40}, {-90, 40},
};

const PolyPoint TRAPEZIUM[4] = {
    {-55, -60}, {55, -60}, {100, 60}, {-100, 60},
};

void fillPoly(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t size,
              const PolyPoint* pts, uint8_t count, uint16_t color, bool filled) {
    auto px = [&](uint8_t i) {
        return static_cast<int16_t>(cx + pts[i].x * size / 100);
    };
    auto py = [&](uint8_t i) {
        return static_cast<int16_t>(cy + pts[i].y * size / 100);
    };
    if (filled) {
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t j = static_cast<uint8_t>((i + 1) % count);
            tft.fillTriangle(cx, cy, px(i), py(i), px(j), py(j), color);
        }
        return;
    }
    /* Every edge three times, offset by a pixel in each axis. Scaling the
     * whole polygon down by one instead leaves the outline single-pixel
     * wherever a vertex sits near the centre line, because the scaling is
     * proportional and those coordinates are small -- and a one-pixel diagonal
     * on this panel breaks up and reads as an artefact. */
    static const int8_t OFFSET[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    for (uint8_t o = 0; o < 3; ++o) {
        for (uint8_t i = 0; i < count; ++i) {
            const uint8_t j = static_cast<uint8_t>((i + 1) % count);
            tft.drawLine(static_cast<int16_t>(px(i) + OFFSET[o][0]),
                         static_cast<int16_t>(py(i) + OFFSET[o][1]),
                         static_cast<int16_t>(px(j) + OFFSET[o][0]),
                         static_cast<int16_t>(py(j) + OFFSET[o][1]), color);
        }
    }
}

/* A regular n-gon, and the oval, which is the same walk with the two axes
 * scaled differently. Flat side at the bottom for the polygons: an upright
 * hexagon is the one a child has seen. */
void regularPoly(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t size,
                 uint8_t n, uint16_t color, bool filled,
                 int16_t xScale = 100, int16_t yScale = 100,
                 bool flatBottom = true) {
    PolyPoint pts[20];
    if (n > 20) n = 20;
    const float turn = 2.0f * PI / n;
    /* An even-sided polygon gets a half-turn so it stands on a flat edge --
     * an upright hexagon is the one a child has seen, and a square on a
     * corner is a diamond, not a square. Which is exactly why the diamond
     * asks for that offset NOT to be applied. */
    const float start = -PI / 2.0f + ((n % 2 == 0 && flatBottom) ? turn / 2.0f : 0.0f);
    for (uint8_t i = 0; i < n; ++i) {
        pts[i].x = static_cast<int8_t>(cosf(start + i * turn) * xScale);
        pts[i].y = static_cast<int8_t>(sinf(start + i * turn) * yScale);
    }
    fillPoly(tft, cx, cy, size, pts, n, color, filled);
}
}

const AppMetadata& shapeColorAppMetadata() {
    return SHAPE_COLOR_METADATA;
}

const char* ShapeColorGame::title() const {
    return shapeColorAppMetadata().screenTitle != nullptr
        ? shapeColorAppMetadata().screenTitle
        : shapeColorAppMetadata().title;
}

void ShapeColorGame::begin(AppContext& host) {
    bestTaps_ = static_cast<uint16_t>(host.getScore(shapeColorAppMetadata().score->bestKey, 0));
    rounds_ = 0;
    newRound();
    markDirty();
}

uint8_t ShapeColorGame::level() const {
    return min<uint8_t>(4, static_cast<uint8_t>(1 + rounds_));
}

void ShapeColorGame::newRound() {
    static const uint16_t ROUND_COLORS[6] = {
        Ui::rgb(225, 60, 80), Ui::rgb(42, 117, 213), Ui::rgb(39, 157, 112),
        Ui::rgb(244, 188, 48), Ui::rgb(168, 96, 220), Ui::rgb(244, 130, 60),
    };
    const uint8_t lvl = level();
    Shape deal[4];

    switch (lvl) {
        case 1:
            deal[0] = Shape::Circle;
            deal[1] = Shape::Square;
            deal[2] = Shape::Triangle;
            deal[3] = Shape::Star5;
            break;
        case 2:
            deal[0] = Shape::Pentagon;
            deal[1] = Shape::Hexagon;
            deal[2] = Shape::Heptagon;
            deal[3] = Shape::Octagon;
            break;
        case 3:
            deal[0] = Shape::Oval;
            deal[1] = Shape::Rectangle;
            deal[2] = Shape::Diamond;
            deal[3] = Shape::Trapezium;
            break;
        default: {
            /* Two of each family, always. Four concave shapes would make the
             * word on the row useless -- it is only a lesson when it tells
             * two rows apart. */
            static const Shape CONCAVE[4] = {Shape::Cross, Shape::Arrow,
                                             Shape::Star5, Shape::Star4};
            static const Shape CONVEX[4] = {Shape::Pentagon, Shape::Hexagon,
                                            Shape::Octagon, Shape::Diamond};
            const uint8_t a = static_cast<uint8_t>(random(4));
            const uint8_t b = static_cast<uint8_t>((a + 1 + random(3)) % 4);
            const uint8_t c = static_cast<uint8_t>(random(4));
            const uint8_t d = static_cast<uint8_t>((c + 1 + random(3)) % 4);
            deal[0] = CONCAVE[a];
            deal[1] = CONVEX[c];
            deal[2] = CONCAVE[b];
            deal[3] = CONVEX[d];
            break;
        }
    }

    /* Shuffle the deal so the same shape is not always on the same row, then
     * hand each one a colour. Level 1 pins its colours to its names. */
    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const Shape tmp = deal[i];
        deal[i] = deal[j];
        deal[j] = tmp;
    }

    uint8_t colorPick[6] = {0, 1, 2, 3, 4, 5};
    for (uint8_t i = 5; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const uint8_t tmp = colorPick[i];
        colorPick[i] = colorPick[j];
        colorPick[j] = tmp;
    }

    static const char* const LEVEL1_NAMES[4] = {
        "red circle", "blue square", "green triangle", "yellow star"
    };
    static const Shape LEVEL1_SHAPES[4] = {
        Shape::Circle, Shape::Square, Shape::Triangle, Shape::Star5
    };

    for (uint8_t i = 0; i < 4; ++i) {
        items_[i].shape = deal[i];
        if (lvl == 1) {
            /* Name and colour have to agree, so level 1 looks its shape up in
             * its own table rather than taking whatever the shuffle dealt. */
            for (uint8_t k = 0; k < 4; ++k) {
                if (LEVEL1_SHAPES[k] == deal[i]) {
                    snprintf(items_[i].name, sizeof(items_[i].name), "%s", LEVEL1_NAMES[k]);
                    items_[i].color = ROUND_COLORS[k];
                    break;
                }
            }
        } else {
            snprintf(items_[i].name, sizeof(items_[i].name), "%s",
                     SHAPE_FACTS[static_cast<uint8_t>(deal[i])].name);
            items_[i].color = ROUND_COLORS[colorPick[i]];
        }
        targetOrder_[i] = i;
        matched_[i] = false;
    }

    for (int i = 3; i > 0; --i) {
        const uint8_t j = random(i + 1);
        const uint8_t tmp = targetOrder_[i];
        targetOrder_[i] = targetOrder_[j];
        targetOrder_[j] = tmp;
    }
    selected_ = -1;
    taps_ = 0;

    /* Nothing on the panel survives a new round: four new shapes, four new
     * labels, a new level in the header and a win card that has to go. That is
     * a layout change by any reading, and it is the one place on this screen
     * where a full repaint is the honest answer. */
    for (uint8_t i = 0; i < 4; ++i) {
        drawnChoice_[i] = 0xFF;
        drawnTarget_[i] = 0xFF;
    }
    drawnTaps_ = 0xFFFF;
    drawnWin_ = false;
}

bool ShapeColorGame::allMatched() const {
    for (bool matched : matched_) {
        if (!matched) {
            return false;
        }
    }
    return true;
}

Rect ShapeColorGame::choiceRect(uint8_t index) const {
    return Rect{16, static_cast<int16_t>(58 + index * 43), 136, 38};
}

Rect ShapeColorGame::targetRect(uint8_t index) const {
    return Rect{168, static_cast<int16_t>(58 + index * 43), 136, 38};
}

/* Right-aligned and it shrinks -- "Level 2  Taps 12  Best 8" down to
 * "Level 3  Taps 0" -- so it is cleared leftward from the margin rather than
 * relying on the glyph backgrounds. */
Rect ShapeColorGame::statsRect() const {
    /* Starts at 50, not 48: the instruction line above is centred in
     * Rect{10, 32, 300, 18} and a font 2 glyph there reaches row 49. That line
     * is STATIC -- nothing would ever have put back the rows this clear took
     * off it. */
    return Rect{static_cast<int16_t>(GAME_CANVAS_WIDTH - 8 - 220), 50, 220, 8};
}

uint8_t ShapeColorGame::choiceState(uint8_t index) const {
    if (matched_[index]) return 2;
    return selected_ == static_cast<int8_t>(index) ? 1 : 0;
}

void ShapeColorGame::drawShape(Ui::Renderer& tft, Shape shape, int16_t cx, int16_t cy, int16_t size, uint16_t color, bool filled) const {
    switch (shape) {
        case Shape::Circle:
            if (filled) {
                tft.fillCircle(cx, cy, size, color);
            } else {
                tft.drawCircle(cx, cy, size, color);
                tft.drawCircle(cx, cy, size - 1, color);
            }
            break;
        case Shape::Square:
            if (filled) {
                tft.fillRoundRect(cx - size, cy - size, size * 2, size * 2, 3, color);
            } else {
                tft.drawRoundRect(cx - size, cy - size, size * 2, size * 2, 3, color);
                tft.drawRoundRect(cx - size + 1, cy - size + 1, size * 2 - 2, size * 2 - 2, 3, color);
            }
            break;
        case Shape::Triangle:
            Ui::drawTriangleShape(tft, cx, cy, size, color, filled);
            break;
        case Shape::Star5:
            fillPoly(tft, cx, cy, size, STAR5, 10, color, filled);
            break;
        case Shape::Star4:
            fillPoly(tft, cx, cy, size, STAR4, 8, color, filled);
            break;
        case Shape::Cross:
            fillPoly(tft, cx, cy, size, CROSS, 12, color, filled);
            break;
        case Shape::Arrow:
            fillPoly(tft, cx, cy, size, ARROW, 7, color, filled);
            break;
        case Shape::Trapezium:
            fillPoly(tft, cx, cy, size, TRAPEZIUM, 4, color, filled);
            break;
        case Shape::Pentagon:
            regularPoly(tft, cx, cy, size, 5, color, filled);
            break;
        case Shape::Hexagon:
            regularPoly(tft, cx, cy, size, 6, color, filled);
            break;
        case Shape::Heptagon:
            regularPoly(tft, cx, cy, size, 7, color, filled);
            break;
        case Shape::Octagon:
            regularPoly(tft, cx, cy, size, 8, color, filled);
            break;
        case Shape::Diamond:
            regularPoly(tft, cx, cy, size, 4, color, filled, 76, 100, false);
            break;
        case Shape::Oval:
            /* Twenty sides is enough that the flats are invisible at this size,
             * and it keeps every shape on this screen going through one piece
             * of code. There is no ellipse primitive in Ui::Renderer. */
            regularPoly(tft, cx, cy, size, 20, color, filled, 100, 58);
            break;
        case Shape::Rectangle:
            if (filled) {
                tft.fillRoundRect(cx - size, cy - size * 6 / 10, size * 2, size * 12 / 10, 3, color);
            } else {
                tft.drawRoundRect(cx - size, cy - size * 6 / 10, size * 2, size * 12 / 10, 3, color);
                tft.drawRoundRect(cx - size + 1, cy - size * 6 / 10 + 1, size * 2 - 2, size * 12 / 10 - 2, 3, color);
            }
            break;
        case Shape::Count:
            break;
    }
}

void ShapeColorGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (allMatched()) {
        if (rounds_ < 250) {
            ++rounds_;
        }
        newRound();
        host.beepOk();
        markFullDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (!matched_[i] && choiceRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            ++taps_;
            selected_ = i;
            host.beepOk();
            markDirty();
            return;
        }
    }

    if (selected_ < 0) {
        return;
    }

    for (uint8_t target = 0; target < 4; ++target) {
        if (targetRect(target).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            ++taps_;
            if (targetOrder_[target] == selected_) {
                matched_[selected_] = true;
                selected_ = -1;
                host.beepOk();
                if (allMatched() && host.saveBestScore(shapeColorAppMetadata().score->bestKey, taps_, true)) {
                    bestTaps_ = taps_;
                }
            } else {
                host.beepError();
            }
            markDirty();
            return;
        }
    }
}

void ShapeColorGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    /* The only thing on this screen that is the same from the first frame to
     * the last. Everything else belongs to a round or to a tap. */
    Ui::drawLabel(tft, Rect{10, 32, 300, 18}, "Tap a shape, then its matching outline",
                  Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < 4; ++i) {
        drawnChoice_[i] = 0xFF;
        drawnTarget_[i] = 0xFF;
    }
    drawnTaps_ = 0xFFFF;
    drawnWin_ = false;
}

void ShapeColorGame::drawStats(Ui::Renderer& tft) {
    const Rect r = statsRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    char stats[48];
    if (bestTaps_ > 0) {
        snprintf(stats, sizeof(stats), "Level %u   Taps %u   Best %u",
                 static_cast<unsigned>(level()), static_cast<unsigned>(taps_),
                 static_cast<unsigned>(bestTaps_));
    } else {
        snprintf(stats, sizeof(stats), "Level %u   Taps %u",
                 static_cast<unsigned>(level()), static_cast<unsigned>(taps_));
    }
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TR_DATUM);
    tft.drawString(stats, static_cast<int16_t>(r.x + r.w), r.y, 1);
    tft.setTextDatum(TL_DATUM);
}

/* Each row paints its own rect opaquely, so a state change erases what was
 * there without a clear of its own -- the same property the answer buttons on
 * the quiz screens rely on. */
void ShapeColorGame::drawChoice(Ui::Renderer& tft, uint8_t index) {
    const Rect r = choiceRect(index);
    const uint16_t fill = selected_ == static_cast<int8_t>(index) ? SELECTED : Ui::panel();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6,
                      matched_[index] ? Ui::rgb(45, 154, 96) : TFT_DARKGREY);
    /* Level 4 has to fit the concave/convex word on the same row, so the
     * shape gives up two pixels of radius and the label starts four earlier.
     * Measured, not eyeballed: the longest level 4 name is "pinwheel" at 8
     * characters, which is 48px of font 1 and ends at x + 86; the word
     * "concave" is 42px and starts at x + 89. */
    const bool tight = level() >= 4;
    if (!matched_[index]) {
        drawShape(tft, items_[index].shape, r.x + (tight ? 22 : 24), r.y + r.h / 2,
                  tight ? 11 : 13, items_[index].color, true);
    }
    tft.setTextColor(selected_ == static_cast<int8_t>(index) ? TFT_BLACK : Ui::text(), fill);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(matched_[index] ? "matched" : items_[index].name,
                   r.x + (tight ? 38 : 42), r.y + r.h / 2, matched_[index] ? 2 : 1);
    /* Level 4's whole lesson, and it only earns room when it tells two rows
     * apart -- so it is drawn nowhere else. Right-aligned inside the row,
     * clear of the longest name. */
    if (tight && !matched_[index]) {
        tft.setTextColor(Ui::muted(), fill);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(SHAPE_FACTS[static_cast<uint8_t>(items_[index].shape)].concave
                           ? "concave" : "convex",
                       static_cast<int16_t>(r.x + r.w - 5), r.y + r.h / 2, 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void ShapeColorGame::drawTarget(Ui::Renderer& tft, uint8_t index) {
    const uint8_t itemIndex = targetOrder_[index];
    const Rect r = targetRect(index);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, EMPTY_TARGET);
    drawShape(tft, items_[itemIndex].shape, r.x + 28, r.y + r.h / 2, 13,
              matched_[itemIndex] ? items_[itemIndex].color : TFT_DARKGREY,
              matched_[itemIndex]);
    tft.setTextColor(Ui::muted(), Ui::surface());
    tft.setTextDatum(ML_DATUM);
    tft.drawString("target", r.x + 54, r.y + r.h / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

void ShapeColorGame::drawWin(Ui::Renderer& tft) {
    tft.fillRoundRect(54, 94, 212, 52, 8, Ui::panel());
    tft.drawRoundRect(54, 94, 212, 52, 8, Ui::success());
    tft.setTextColor(Ui::success(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Great matching!", GAME_CANVAS_WIDTH / 2, 112, 4);
    tft.drawString(level() < 4 ? "Tap for a harder round" : "Tap to play again",
                   GAME_CANVAS_WIDTH / 2, 137, 2);
    tft.setTextDatum(TL_DATUM);
}

void ShapeColorGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (taps_ != drawnTaps_) {
        drawStats(tft);
        drawnTaps_ = taps_;
    }

    /* A tap changes one left-hand row, or one of each. Repainting the pair
     * that moved is about an eighth of what this screen used to redraw for
     * every press. */
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t state = choiceState(i);
        if (state != drawnChoice_[i]) {
            drawChoice(tft, i);
            drawnChoice_[i] = state;
        }
    }
    for (uint8_t i = 0; i < 4; ++i) {
        const uint8_t state = matched_[targetOrder_[i]] ? 1 : 0;
        if (state != drawnTarget_[i]) {
            drawTarget(tft, i);
            drawnTarget_[i] = state;
        }
    }

    /* The card goes on TOP of the rows, so it is drawn after them and needs no
     * erase: the only thing that takes it away is a new round, which repaints
     * the screen whole. */
    if (allMatched() && !drawnWin_) {
        drawWin(tft);
        drawnWin_ = true;
    }
}
