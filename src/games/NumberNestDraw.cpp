// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "NumberNestGame.h"
#include "NumberNestLayout.h"

/* Everything NumberNest puts on the panel. The other half of the screen,
 * NumberNestGame.cpp, decides WHEN something changed; this file is how each
 * region is painted, and nothing here reads a touch or moves the game on. */

using namespace NumberNestLayout;

namespace {

/* ---- small painters ----------------------------------------------------
 *
 * Every one of these covers, with an opaque fill, everything in its box that
 * can have changed, before it writes. Calling it again with different content
 * is therefore an overdraw: nothing of the old content can survive, and
 * nothing outside the box is touched. Text is formatted into stack buffers
 * and drawn through the primitives.
 *
 * STROKES ARE FILLED SHAPES, NOT LINES. The runtime scales positions and
 * radii up on a larger panel and a line stays one physical pixel wide, so a
 * rim made of concentric outlines, or a stroke made of two lines side by
 * side, opens up into two thin lines with ground between them at 1.5x. A
 * filled shape scales with the canvas.
 *
 * AND NOTHING IS PAINTED TWICE THAT NEED NOT BE. A layer that the next layer
 * will cover is SPI time spent on pixels nobody sees: at 480x320 a group
 * change measured 72,000 pixels, 31 ms, when every disc was laid down whole
 * in its rim colour first and every feedback line cleared its whole strip
 * under the pill. So a rim is painted once, when the screen is, and a
 * repaint lays only the fill and the number inside it; and a pill clears
 * only the strip either side of itself. */

/* A rounded button with a centred label. */
void paintChip(Ui::Renderer& tft, const Rect& r, const char* label, uint16_t fill,
               uint16_t ink) {
    const uint8_t radius = Ui::cornerRadius();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, radius, Ui::outline());
    tft.setTextColor(ink, fill);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(label, r.x + r.w / 2, r.y + r.h / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

/* One arm of the multiplication sign: a slanted bar two pixels wide, as two
 * triangles. `rise` is +1 for the arm that falls to the right, -1 for the
 * one that climbs. */
void paintArm(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t rise, uint16_t ink) {
    const int16_t top = static_cast<int16_t>(cy - 4 * rise);
    const int16_t bottom = static_cast<int16_t>(cy + 4 * rise);
    tft.fillTriangle(cx - 5, top, cx - 3, top, cx + 5, bottom, ink);
    tft.fillTriangle(cx - 5, top, cx + 5, bottom, cx + 3, bottom, ink);
}

/* The operation's sign, drawn rather than typed: the built-in fonts stop at
 * ASCII and have no multiplication sign, and a lowercase x beside a number
 * reads as a letter. Two pixels thick, like every stroke on a launcher icon. */
void paintSign(Ui::Renderer& tft, int16_t cx, int16_t cy, bool multiply, uint16_t ink) {
    if (multiply) {
        paintArm(tft, cx, cy, 1, ink);
        paintArm(tft, cx, cy, -1, ink);
    } else {
        tft.fillRect(cx - 5, cy - 1, 10, 2, ink);
        tft.fillRect(cx - 1, cy - 5, 2, 10, ink);
    }
}

/* The selected operation is the chip in INVERTED ink: filled with the text
 * colour and written in whatever reads on that. Not the accent role -- a
 * selection has to be unmistakable against the unselected chip beside it on
 * every theme, and text against panel is the pairing every palette is
 * checked for. */
void paintModeChip(Ui::Renderer& tft, const Rect& r, bool multiply, bool selected) {
    const uint16_t fill = selected ? Ui::text() : Ui::panel();
    const uint16_t ink = selected ? Ui::onFill(fill) : Ui::text();
    const uint8_t radius = Ui::cornerRadius();
    const int16_t midY = static_cast<int16_t>(r.y + r.h / 2);
    tft.fillRoundRect(r.x, r.y, r.w, r.h, radius, fill);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, radius, Ui::outline());
    paintSign(tft, static_cast<int16_t>(r.x + 15), midY, multiply, ink);
    tft.setTextColor(ink, fill);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(multiply ? "Multiply" : "Add", r.x + 27, midY, 2);
    tft.setTextDatum(TL_DATUM);
}

/* A circle, with a number in it or empty: the one shape every number on this
 * screen takes.
 *
 * The rim is the disc in the outline colour with the fill laid inside it, and
 * it never changes -- what a circle means is said by its FILL -- so `rimToo`
 * is true only when the panel has just been wiped. Every later repaint is the
 * inner disc and the number, which is everything that can have changed. */
void paintDisc(Ui::Renderer& tft, int16_t cx, int16_t cy, int16_t radius, int16_t rimWidth,
               uint16_t fill, bool rimToo) {
    if (rimToo) {
        tft.fillCircle(cx, cy, radius, Ui::outline());
    }
    tft.fillCircle(cx, cy, radius - rimWidth, fill);
}

void paintValue(Ui::Renderer& tft, int16_t cx, int16_t cy, uint16_t value, uint16_t fill,
                uint16_t ink, uint8_t font) {
    char text[8];
    snprintf(text, sizeof(text), "%u", static_cast<unsigned>(value));
    tft.setTextColor(ink, fill);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, cx, cy, font);
    tft.setTextDatum(TL_DATUM);
}

/* A line of text centred in a strip, so that a shorter line cannot leave the
 * ends of a longer one behind.
 *
 * Plain text: the whole strip is cleared first. On a pill: the pill covers
 * its own box, so only the strip either side of it is cleared -- reaching in
 * under the pill's rounded corners, which the pill does not paint. The pill
 * is as tall as the strip, so there is nothing above or below it. */
void paintLine(Ui::Renderer& tft, const Rect& strip, const Rect* pill, const char* text,
               uint16_t fill, uint16_t ink, uint8_t font) {
    if (pill != nullptr) {
        const uint8_t themed = Ui::cornerRadius();
        const int16_t radius = themed < 4 ? themed : 4;
        const int16_t leftEnd = static_cast<int16_t>(pill->x + radius);
        const int16_t rightStart = static_cast<int16_t>(pill->x + pill->w - radius);
        tft.fillRect(strip.x, strip.y, leftEnd - strip.x, strip.h, Ui::bg());
        tft.fillRect(rightStart, strip.y, strip.x + strip.w - rightStart, strip.h, Ui::bg());
        tft.fillRoundRect(pill->x, pill->y, pill->w, pill->h, radius, fill);
    } else {
        tft.fillRect(strip.x, strip.y, strip.w, strip.h, Ui::bg());
    }
    tft.setTextColor(ink, pill != nullptr ? fill : Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(text, strip.x + strip.w / 2, strip.y + strip.h / 2, font);
    tft.setTextDatum(TL_DATUM);
}

/* One row of the rules page: the operation, five small numbers with the
 * answer marked, and the equation that makes it. */
void paintExample(Ui::Renderer& tft, int16_t cy, bool multiply, const uint16_t* value,
                  uint8_t first, uint8_t second, uint8_t answer) {
    paintSign(tft, 18, cy, multiply, Ui::text());
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(ML_DATUM);
    tft.drawString(multiply ? "Multiply" : "Add", 30, cy, 2);
    tft.setTextDatum(TL_DATUM);
    for (uint8_t i = 0; i < Nest::GROUP_SIZE; ++i) {
        /* The same three looks as the play screen: the answer in the success
         * colour, the two it was made from in inverted ink, the rest plain.
         * Radius 13 with a one-pixel rim is what holds two digits at font 2:
         * the opaque text cell is 16 by 16, and its corner is 11.3 from the
         * centre. */
        const bool isAnswer = i == answer;
        const bool isOperand = i == first || i == second;
        const uint16_t fill = isAnswer ? Ui::success() : (isOperand ? Ui::text() : Ui::panel());
        const uint16_t ink = (isAnswer || isOperand) ? Ui::onFill(fill) : Ui::text();
        const int16_t cx = static_cast<int16_t>(EXAMPLE_CX0 + i * EXAMPLE_PITCH);
        paintDisc(tft, cx, cy, EXAMPLE_R, 1, fill, true);
        paintValue(tft, cx, cy, value[i], fill, ink, 2);
    }
    char text[24];
    snprintf(text, sizeof(text), "%u %c %u = %u", static_cast<unsigned>(value[first]),
             multiply ? 'x' : '+', static_cast<unsigned>(value[second]),
             static_cast<unsigned>(value[answer]));
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(ML_DATUM);
    tft.drawString(text, EXAMPLE_TEXT_X, cy, 2);
    tft.setTextDatum(TL_DATUM);
}

}  // namespace

void NumberNestGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    /* The panel has just been wiped, so nothing the last View describes is
     * on it any more. */
    paintedValid_ = false;

    if (showRules_) {
        paintRules(tft);
        return;
    }
    tft.fillRoundRect(NEST_RECT.x, NEST_RECT.y, NEST_RECT.w, NEST_RECT.h, Ui::cornerRadius(),
                      Ui::surface());
    /* Outlined as well as filled: on High Contrast the surface IS the
     * ground, and a tray drawn by its fill alone is not there. */
    tft.drawRoundRect(NEST_RECT.x, NEST_RECT.y, NEST_RECT.w, NEST_RECT.h, Ui::cornerRadius(),
                      Ui::outline());
    paintChip(tft, RULES_RECT, "Rules", Ui::panel(), Ui::text());
}

void NumberNestGame::renderDynamic(AppContext& host) {
    if (showRules_) {
        return;   // the rules page is static from top to bottom
    }
    Ui::Renderer& tft = host.display();

    const Nest::View want = Nest::viewOf(session_);
    const uint16_t regions = paintedValid_ ? Nest::changedRegions(painted_, want)
                                           : Nest::REGION_ALL;
    /* Nothing valid on the panel means it has just been wiped: the one time
     * the circles' rims have to be painted. */
    const bool rims = !paintedValid_;

    if (regions & Nest::REGION_MODE) {
        paintModes(tft, want);
    }
    if (regions & Nest::REGION_LEVEL) {
        paintLevel(tft, want);
    }
    if (regions & Nest::REGION_STATUS) {
        paintStatus(tft, want);
    }
    if (regions & Nest::REGION_CAPTION) {
        paintCaption(tft, want);
    }
    for (uint8_t i = 0; i < Nest::GROUP_SIZE; ++i) {
        if (regions & Nest::numberRegion(i)) {
            paintNumber(tft, want, i, rims);
        }
        if (regions & Nest::slotRegion(i)) {
            paintSlot(tft, want, i, rims);
        }
    }
    if (regions & Nest::REGION_FOOTER) {
        paintFooter(tft, want);
    }

    painted_ = want;
    paintedValid_ = true;
}

void NumberNestGame::paintModes(Ui::Renderer& tft, const Nest::View& v) const {
    paintModeChip(tft, ADD_RECT, false, v.op == Nest::opIndex(Nest::Op::Add));
    paintModeChip(tft, MUL_RECT, true, v.op == Nest::opIndex(Nest::Op::Mul));
}

void NumberNestGame::paintLevel(Ui::Renderer& tft, const Nest::View& v) const {
    char text[12];
    snprintf(text, sizeof(text), "Level %u", static_cast<unsigned>(v.level));
    paintChip(tft, LEVEL_RECT, text, Ui::panel(), Ui::text());
}

void NumberNestGame::paintStatus(Ui::Renderer& tft, const Nest::View& v) const {
    const bool multiply = v.op == Nest::opIndex(Nest::Op::Mul);
    char text[48];
    /* The prompt is plain text on the ground. Everything that is feedback --
     * wrong, right, finished -- goes on a pill in the colour that says which. */
    bool feedback = true;
    uint16_t fill = Ui::success();
    switch (static_cast<Nest::Status>(v.status)) {
        case Nest::Status::TryAgain:
            snprintf(text, sizeof(text), "Not that one - have another look");
            fill = Ui::error();
            break;
        case Nest::Status::Equation:
            snprintf(text, sizeof(text), "%u %c %u = %u",
                     static_cast<unsigned>(v.equation[0]), multiply ? 'x' : '+',
                     static_cast<unsigned>(v.equation[1]),
                     static_cast<unsigned>(v.equation[2]));
            break;
        case Nest::Status::Complete:
            snprintf(text, sizeof(text), "Nest complete! %u slip%s on the way",
                     static_cast<unsigned>(v.mistakes), v.mistakes == 1 ? "" : "s");
            break;
        case Nest::Status::CompleteClean:
            snprintf(text, sizeof(text), "Nest complete! Not one slip");
            break;
        case Nest::Status::LevelUp:
            snprintf(text, sizeof(text), "Level up! The next nest is level %u",
                     static_cast<unsigned>(v.level));
            break;
        case Nest::Status::Prompt:
        default:
            snprintf(text, sizeof(text), "%sTap the %s of two others",
                     v.stage >= Nest::FINAL_STAGE ? "Final group! " : "",
                     multiply ? "product" : "sum");
            feedback = false;
            break;
    }
    if (feedback) {
        paintLine(tft, STATUS_RECT, &STATUS_PILL, text, fill, Ui::onFill(fill), 2);
    } else {
        paintLine(tft, STATUS_RECT, nullptr, text, Ui::bg(), Ui::text(), 2);
    }
}

void NumberNestGame::paintCaption(Ui::Renderer& tft, const Nest::View& v) const {
    if (v.done) {
        /* The final group's equation, which is the one the nest was built
         * for. The five before it had the status line to themselves for a
         * moment; this one shares the screen with "Nest complete", so it is
         * said here. */
        char text[40];
        snprintf(text, sizeof(text), "%u %c %u = %u - NEST COMPLETE",
                 static_cast<unsigned>(v.equation[0]),
                 v.op == Nest::opIndex(Nest::Op::Mul) ? 'x' : '+',
                 static_cast<unsigned>(v.equation[1]), static_cast<unsigned>(v.equation[2]));
        paintLine(tft, CAPTION_RECT, nullptr, text, Ui::bg(), Ui::text(), 1);
    } else if (v.stage >= Nest::FINAL_STAGE) {
        const uint16_t fill = Ui::warning();
        paintLine(tft, CAPTION_RECT, &CAPTION_BAND,
                  "FINAL GROUP - THE FIVE ANSWERS FROM YOUR NEST", fill, Ui::onFill(fill), 1);
    } else {
        char text[24];
        snprintf(text, sizeof(text), "GROUP %u OF %u", static_cast<unsigned>(v.stage + 1),
                 static_cast<unsigned>(Nest::GROUP_SIZE));
        paintLine(tft, CAPTION_RECT, nullptr, text, Ui::bg(), Ui::muted(), 1);
    }
}

void NumberNestGame::paintNumber(Ui::Renderer& tft, const Nest::View& v, uint8_t index,
                                 bool rimToo) const {
    uint16_t fill = Ui::panel();
    switch (static_cast<Nest::NumberLook>(v.numberLook[index])) {
        case Nest::NumberLook::Wrong:
            /* The button that was pressed, never the right one: a wrong tap
             * must not be a second way of being shown the answer. */
            fill = Ui::error();
            break;
        case Nest::NumberLook::Answer:
            fill = Ui::success();
            break;
        case Nest::NumberLook::Operand:
            /* INVERTED INK, like the selected operation above. The pair was
             * first marked by a rim in the success colour, and on Pocket that
             * colour and the outline are the same dark green, so the mark
             * was one extra pixel of rim; on Classic and High Contrast the
             * text colour and the outline are the same too, so no rim colour
             * works everywhere. Text against panel is the one pairing every
             * palette is checked for, so a disc filled with the text colour
             * stands out from its neighbours on all nine. Which of the three
             * is the answer is said by the equation as well as by the
             * colour: on the status line for a group, in the caption when
             * the nest is finished. */
            fill = Ui::text();
            break;
        case Nest::NumberLook::Idle:
        default:
            break;
    }
    const uint16_t ink = fill == Ui::panel() ? Ui::text() : Ui::onFill(fill);
    const int16_t cx = numberCx(index);
    paintDisc(tft, cx, NUMBER_CY, NUMBER_R, NUMBER_RIM, fill, rimToo);
    paintValue(tft, cx, NUMBER_CY, v.number[index], fill, ink, 4);
}

void NumberNestGame::paintSlot(Ui::Renderer& tft, const Nest::View& v, uint8_t index,
                               bool rimToo) const {
    const int16_t cx = numberCx(index);
    const Nest::SlotLook look = static_cast<Nest::SlotLook>(v.slotLook[index]);
    if (look == Nest::SlotLook::Empty) {
        /* A hole in the tray: the ground showing through, inside the rim. */
        paintDisc(tft, cx, SLOT_CY, SLOT_R, SLOT_RIM, Ui::bg(), rimToo);
        return;
    }
    const uint16_t fill = look == Nest::SlotLook::Fresh ? Ui::success() : Ui::panel();
    const uint16_t ink = look == Nest::SlotLook::Fresh ? Ui::onFill(fill) : Ui::text();
    paintDisc(tft, cx, SLOT_CY, SLOT_R, SLOT_RIM, fill, rimToo);
    paintValue(tft, cx, SLOT_CY, v.slot[index], fill, ink, 2);
}

void NumberNestGame::paintFooter(Ui::Renderer& tft, const Nest::View& v) const {
    tft.fillRect(FOOTER_CLEAR.x, FOOTER_CLEAR.y, FOOTER_CLEAR.w, FOOTER_CLEAR.h, Ui::bg());
    if (!v.done) {
        return;
    }
    const uint16_t fill = Ui::accent();
    paintChip(tft, AGAIN_RECT, "Play again", fill, Ui::onFill(fill));
    paintChip(tft, HOME_RECT, "Home", Ui::panel(), Ui::text());
}

void NumberNestGame::paintRules(Ui::Renderer& tft) const {
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TC_DATUM);
    tft.drawString("How to play", GAME_CANVAS_WIDTH / 2, 36, 4);
    tft.drawString("Five numbers. One of them is made from", GAME_CANVAS_WIDTH / 2, 66, 2);
    tft.drawString("two of the others. Tap that one.", GAME_CANVAS_WIDTH / 2, 84, 2);
    tft.drawString("Solve five groups to fill the nest,", GAME_CANVAS_WIDTH / 2, 166, 2);
    tft.drawString("then solve the nest itself.", GAME_CANVAS_WIDTH / 2, 184, 2);
    tft.setTextDatum(TL_DATUM);

    static constexpr uint16_t ADD_EXAMPLE[Nest::GROUP_SIZE] = {3, 7, 10, 12, 18};
    static constexpr uint16_t MUL_EXAMPLE[Nest::GROUP_SIZE] = {2, 3, 6, 11, 17};
    /* The examples are held to the rule they illustrate. */
    static_assert(Nest::countRelations(Nest::Op::Add, ADD_EXAMPLE, Nest::GROUP_SIZE, nullptr) == 1 &&
                  Nest::countRelations(Nest::Op::Mul, MUL_EXAMPLE, Nest::GROUP_SIZE, nullptr) == 1,
                  "a rules-page example has no answer, or more than one");
    paintExample(tft, 118, false, ADD_EXAMPLE, 0, 1, 2);
    paintExample(tft, 148, true, MUL_EXAMPLE, 0, 1, 2);

    const uint16_t fill = Ui::accent();
    paintChip(tft, PLAY_RECT, "Tap to play", fill, Ui::onFill(fill));
}
