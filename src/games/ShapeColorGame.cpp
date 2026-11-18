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
    "Shapes by outline, sides, name and turn.",
    &SHAPE_COLOR_SCORE,
    LauncherIcon::ShapeColor,
    14,
    true,
};

namespace SC = ShapesCourse;
using Shape = ShapeColorGame::Shape;

/* The round colours, in ShapesCourse::COLOUR_NAMES order: level 1 names the
 * colour, so the two must agree. */
uint16_t roundColour(uint8_t i) {
    static const uint16_t COLOURS[SC::COLOUR_COUNT] = {
        Ui::rgb(225, 60, 80), Ui::rgb(42, 117, 213), Ui::rgb(39, 157, 112),
        Ui::rgb(244, 188, 48), Ui::rgb(168, 96, 220), Ui::rgb(244, 130, 60),
    };
    return COLOURS[i % SC::COLOUR_COUNT];
}

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

/* Leaning right, so a half turn is the same shape -- which is why Shapes
 * never turns it half way (ShapesCourse::FACTS). */
const PolyPoint PARALLELOGRAM[4] = {
    {-55, -55}, {100, -55}, {55, 55}, {-100, 55},
};

/* Point up, the long point down. */
const PolyPoint KITE[4] = {
    {0, -100}, {62, -30}, {0, 100}, {-62, -30},
};

/* A quarter turn clockwise on the panel, where y grows downwards. */
PolyPoint turned(PolyPoint p, uint8_t turns) {
    for (uint8_t t = 0; t < (turns & 3); ++t) {
        const int8_t x = p.x;
        p.x = static_cast<int8_t>(-p.y);
        p.y = x;
    }
    return p;
}

void fillPoly(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t size,
              const PolyPoint* pts, uint8_t count, uint16_t color, bool filled,
              uint8_t turns = 0) {
    auto px = [&](uint8_t i) {
        return static_cast<int16_t>(cx + turned(pts[i], turns).x * size / 100);
    };
    auto py = [&](uint8_t i) {
        return static_cast<int16_t>(cy + turned(pts[i], turns).y * size / 100);
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
 * hexagon is the one a young player has seen. */
void regularPoly(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t size,
                 uint8_t n, uint16_t color, bool filled,
                 int16_t xScale = 100, int16_t yScale = 100,
                 bool flatBottom = true, uint8_t turns = 0) {
    PolyPoint pts[20];
    if (n > 20) n = 20;
    const float turn = 2.0f * PI / n;
    /* An even-sided polygon gets a half-turn so it stands on a flat edge --
     * an upright hexagon is the one a young player has seen, and a square on a
     * corner is a diamond, not a square. Which is exactly why the diamond
     * asks for that offset NOT to be applied. */
    const float start = -PI / 2.0f + ((n % 2 == 0 && flatBottom) ? turn / 2.0f : 0.0f);
    for (uint8_t i = 0; i < n; ++i) {
        pts[i].x = static_cast<int8_t>(cosf(start + i * turn) * xScale);
        pts[i].y = static_cast<int8_t>(sinf(start + i * turn) * yScale);
    }
    fillPoly(tft, cx, cy, size, pts, n, color, filled, turns);
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

/* A new visit, or New game from the Carry on card: the level is the
 * player's, kept by the runtime. It used to be the rounds played this visit,
 * so every visit dealt the same three rounds in the same order. */
void ShapeColorGame::begin(AppContext& host) {
    bestTaps_ = static_cast<uint16_t>(host.getScore(shapeColorAppMetadata().score->bestKey, 0));
    rounds_ = 0;
    climbed_ = false;
    ladder_.start(SC::LEVEL_COUNT, host.courseLevel());
    deck_.reset(SC::poolSize(ladder_.level()));
    newRound();
    markDirty();
}

/* A round with a tap in it, or a round finished: the level is kept by the
 * runtime now, but the round on the panel and the tally are this screen's.
 * Nothing here runs on a clock, so there is nothing for resume() to do. */
Game::Keep ShapeColorGame::keepsGame() const {
    return (taps_ > 0 || rounds_ > 0) ? Keep::Ask : Keep::No;
}

void ShapeColorGame::newRound() {
    SC::deal(ladder_.level(), deck_, esp_random(), round_);
    for (uint8_t i = 0; i < 4; ++i) {
        matched_[i] = false;
    }
    selected_ = -1;
    taps_ = 0;
    wrongs_ = 0;

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

/* One verdict a round, when its last shape is matched: right with no wrong
 * match, wrong with two or more. A round is minutes of play rather than one
 * press, so it is the round the ladder hears about, not each tap. */
void ShapeColorGame::finishRound(AppContext& host) {
    climbed_ = false;
    const SC::Verdict verdict = SC::judge(wrongs_);
    if (verdict == SC::Verdict::Neither) {
        host.beepOk();
        return;
    }
    const Course::Ladder::Step step = ladder_.answer(verdict == SC::Verdict::Right);
    if (step != Course::Ladder::Step::Stay) {
        host.setCourseLevel(ladder_.level());
        deck_.reset(SC::poolSize(ladder_.level()));
    }
    if (step == Course::Ladder::Step::Up) {
        climbed_ = true;
        host.pulseRgb(0, 255, 40, 450);
        host.playSound(Sound::LevelUp);
    } else {
        host.beepOk();
    }
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

void ShapeColorGame::drawShape(Ui::Renderer& tft, Shape shape, int16_t cx, int16_t cy,
                               int16_t size, uint16_t color, bool filled, uint8_t turns) const {
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
            /* Upright, the triangle every round has drawn; turned, the
             * same three points walked as a polygon. */
            if (turns == 0) {
                Ui::drawTriangleShape(tft, cx, cy, size, color, filled);
            } else {
                regularPoly(tft, cx, cy, size, 3, color, filled, 100, 100, true, turns);
            }
            break;
        case Shape::Star5:
            fillPoly(tft, cx, cy, size, STAR5, 10, color, filled, turns);
            break;
        case Shape::Star4:
            fillPoly(tft, cx, cy, size, STAR4, 8, color, filled, turns);
            break;
        case Shape::Cross:
            fillPoly(tft, cx, cy, size, CROSS, 12, color, filled, turns);
            break;
        case Shape::Arrow:
            fillPoly(tft, cx, cy, size, ARROW, 7, color, filled, turns);
            break;
        case Shape::Trapezium:
            fillPoly(tft, cx, cy, size, TRAPEZIUM, 4, color, filled, turns);
            break;
        case Shape::Parallelogram:
            fillPoly(tft, cx, cy, size, PARALLELOGRAM, 4, color, filled, turns);
            break;
        case Shape::Kite:
            fillPoly(tft, cx, cy, size, KITE, 4, color, filled, turns);
            break;
        case Shape::Pentagon:
            regularPoly(tft, cx, cy, size, 5, color, filled, 100, 100, true, turns);
            break;
        case Shape::Hexagon:
            regularPoly(tft, cx, cy, size, 6, color, filled, 100, 100, true, turns);
            break;
        case Shape::Heptagon:
            regularPoly(tft, cx, cy, size, 7, color, filled, 100, 100, true, turns);
            break;
        case Shape::Octagon:
            regularPoly(tft, cx, cy, size, 8, color, filled);
            break;
        case Shape::Diamond:
            regularPoly(tft, cx, cy, size, 4, color, filled, 76, 100, false, turns);
            break;
        case Shape::Oval:
            /* Twenty sides is enough that the flats are invisible at this size,
             * and it keeps every shape on this screen going through one piece
             * of code. There is no ellipse primitive in Ui::Renderer. */
            regularPoly(tft, cx, cy, size, 20, color, filled, 100, 58, true, turns);
            break;
        case Shape::Rectangle: {
            /* A quarter turn stands it on its short side. */
            const bool upright = (turns & 1U) != 0;
            const int16_t hw = upright ? static_cast<int16_t>(size * 6 / 10) : size;
            const int16_t hh = upright ? size : static_cast<int16_t>(size * 6 / 10);
            if (filled) {
                tft.fillRoundRect(cx - hw, cy - hh, hw * 2, hh * 2, 3, color);
            } else {
                tft.drawRoundRect(cx - hw, cy - hh, hw * 2, hh * 2, 3, color);
                tft.drawRoundRect(cx - hw + 1, cy - hh + 1, hw * 2 - 2, hh * 2 - 2, 3, color);
            }
            break;
        }
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
            if (round_.targetOrder[target] == selected_) {
                matched_[selected_] = true;
                selected_ = -1;
                if (allMatched()) {
                    if (host.saveBestScore(shapeColorAppMetadata().score->bestKey, taps_, true)) {
                        bestTaps_ = taps_;
                    }
                    finishRound(host);
                } else {
                    host.beepOk();
                }
            } else {
                /* The shape matched wrongly comes round again a few shapes
                 * later. The ladder hears about the round, not this tap. */
                if (wrongs_ < 250) ++wrongs_;
                deck_.missed(round_.items[selected_]);
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
    Ui::drawLabel(tft, Rect{10, 32, 300, 18}, SC::instruction(round_.level),
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
                 static_cast<unsigned>(round_.level), static_cast<unsigned>(taps_),
                 static_cast<unsigned>(bestTaps_));
    } else {
        snprintf(stats, sizeof(stats), "Level %u   Taps %u",
                 static_cast<unsigned>(round_.level), static_cast<unsigned>(taps_));
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
    const SC::Level& level = SC::levelOf(round_.level);
    const uint16_t fill = selected_ == static_cast<int8_t>(index) ? SELECTED : Ui::panel();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6,
                      matched_[index] ? Ui::rgb(45, 154, 96) : TFT_DARKGREY);
    char name[SC::ROW_NAME_MAX + 1];
    SC::rowName(round_, index, name, sizeof(name));
    /* Concave or convex has to fit the word on the same row, so the shape
     * gives up two pixels of radius and the label starts four earlier.
     * Measured, not eyeballed: the longest name there is eight characters,
     * 48px of font 1, ending at x + 86; "concave" is 42px from x + 89. On
     * Shape names the row carries no name, so the shape stands in its
     * middle. */
    const bool tight = level.concaveConvex;
    const bool unnamed = name[0] == '\0';
    if (!matched_[index]) {
        const int16_t cx = unnamed ? static_cast<int16_t>(r.x + r.w / 2)
                                   : static_cast<int16_t>(r.x + (tight ? 22 : 24));
        drawShape(tft, round_.shapes[index], cx, r.y + r.h / 2, tight ? 11 : 13,
                  roundColour(round_.colours[index]), true);
    }
    tft.setTextColor(selected_ == static_cast<int8_t>(index) ? TFT_BLACK : Ui::text(), fill);
    tft.setTextDatum(ML_DATUM);
    if (matched_[index]) {
        tft.drawString("matched", r.x + 42, r.y + r.h / 2, 2);
    } else if (!unnamed) {
        tft.drawString(name, r.x + (tight ? 38 : 42), r.y + r.h / 2, 1);
    }
    /* Concave or convex: the word is the lesson, and it only earns room
     * when it tells two rows apart -- so it is drawn nowhere else. */
    if (tight && !matched_[index]) {
        tft.setTextColor(Ui::muted(), fill);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(SC::fact(round_.shapes[index]).concave ? "concave" : "convex",
                       static_cast<int16_t>(r.x + r.w - 5), r.y + r.h / 2, 1);
    }
    tft.setTextDatum(TL_DATUM);
}

/* A target is the shape's outline -- turned, on Turned shapes -- or words:
 * how many sides it has, or its name. Once matched it shows the shape filled
 * in its colour beside the words, or in place of the outline. */
void ShapeColorGame::drawTarget(Ui::Renderer& tft, uint8_t index) {
    const uint8_t item = round_.targetOrder[index];
    const Rect r = targetRect(index);
    const SC::Level& level = SC::levelOf(round_.level);
    const bool matched = matched_[item];
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, EMPTY_TARGET);
    const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
    if (level.target == SC::Target::Outline) {
        drawShape(tft, round_.shapes[item], r.x + 28, cy, 13,
                  matched ? roundColour(round_.colours[item]) : TFT_DARKGREY, matched,
                  round_.turns[item]);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.drawString("target", r.x + 54, cy, 2);
    } else {
        char text[24];
        SC::targetText(round_, item, text, sizeof(text));
        int16_t left = r.x;
        if (matched) {
            drawShape(tft, round_.shapes[item], r.x + 18, cy, 11,
                      roundColour(round_.colours[item]), true);
            left = static_cast<int16_t>(r.x + 32);
        }
        const int16_t width = static_cast<int16_t>(r.x + r.w - left);
        const uint8_t font = tft.textWidth(text, 2) <= width - 8 ? 2 : 1;
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(text, static_cast<int16_t>(left + width / 2), cy, font);
    }
    tft.setTextDatum(TL_DATUM);
}

void ShapeColorGame::drawWin(Ui::Renderer& tft) {
    tft.fillRoundRect(54, 94, 212, 52, 8, Ui::panel());
    tft.drawRoundRect(54, 94, 212, 52, 8, Ui::success());
    tft.setTextColor(Ui::success(), Ui::panel());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(wrongs_ == 0 ? "Great matching!" : "All matched!", GAME_CANVAS_WIDTH / 2, 112, 4);
    char line[SC::LEVEL_LINE_MAX + 1];
    if (climbed_) {
        SC::levelLine(ladder_.level(), line, sizeof(line));
        tft.setTextColor(SELECTED, Ui::panel());
    } else {
        snprintf(line, sizeof(line), "Tap for the next round");
    }
    tft.drawString(line, GAME_CANVAS_WIDTH / 2, 137, 2);
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
        const uint8_t state = matched_[round_.targetOrder[i]] ? 1 : 0;
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
