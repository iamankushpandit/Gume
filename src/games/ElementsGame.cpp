#include "ElementsGame.h"
#include "ElementData.h"
#include "engine/AppRegistry.h"

#include <cstdio>
#include <cstring>

/*
 * Shell, chart and element card. The quiz and the level tab live in
 * ElementsQuiz.cpp -- one class, two translation units, because in one piece
 * this runs past the ~600 line ceiling CLAUDE.md sets.
 */

namespace {

constexpr AppScoreInfo ELEMENTS_SCORE = {
    "elements", "Elements", "elemBest", "pts", false
};

constexpr AppMetadata ELEMENTS_METADATA = {
    "elements",
    "Elements",
    nullptr,
    "periodic table",
    "Elements",
    "Explore the periodic table.",
    &ELEMENTS_SCORE,
    LauncherIcon::Elements,
    30,
    true,
};

/* NVS key for the pinned level. Plain name: Board::scopedKey() makes it
 * per-profile without this screen knowing about profiles at all. */
constexpr char TIER_KEY[] = "elemTier";

/* ---- chart geometry -------------------------------------------------------
 *
 * 18 columns have to fit 320px, which is 17px each: a 16px cell and a 1px
 * gutter, starting at x=7 so the 2px left over is split evenly.
 *
 * Vertically the chart is nine rows, not seven -- rows 8 and 9 are the
 * lanthanide and actinide strips every wall chart lifts out from under periods
 * 6 and 7. Nine rows at a 15px step plus the 6px break clears the bottom of
 * the screen with room for the score line, and 14px is still tall enough for
 * a font 1 symbol with a pixel of air above and below.
 *
 * A 16x14 cell is well under the 30px touch minimum, which is exactly why
 * neither tab commits on a single tap. See update(). */
constexpr int16_t TABLE_X0   = 7;
constexpr int16_t COL_STEP   = 17;
constexpr int16_t CELL_W     = 16;
constexpr int16_t TABLE_Y0   = 84;
constexpr int16_t ROW_STEP   = 15;
constexpr int16_t CELL_H     = 14;
constexpr int16_t F_BLOCK_GAP = 6;
constexpr int16_t SCORE_Y    = 228;

int16_t rowY(uint8_t row) {
    const int16_t base = static_cast<int16_t>(TABLE_Y0 + (row - 1) * ROW_STEP);
    return row <= 7 ? base : static_cast<int16_t>(base + F_BLOCK_GAP);
}

/* Ink drawn on top of a category fill. Every fill below is a light tint, so
 * one near-black works on all of them in both themes -- which is the point:
 * the chart reads the same whichever theme the device is in. */
uint16_t cellInk() { return Ui::rgb(20, 26, 36); }

}  // namespace

const AppMetadata& elementsAppMetadata() {
    return ELEMENTS_METADATA;
}

const char* ElementsGame::title() const {
    return elementsAppMetadata().screenTitle != nullptr
        ? elementsAppMetadata().screenTitle
        : elementsAppMetadata().title;
}

uint16_t ElementsGame::categoryColor(uint8_t category) {
    /* Ten families, ten tints. Through Ui::rgb rather than RGB565 literals for
     * the reason LauncherIcons.cpp gives: a hand-packed literal in a comment
     * claiming to be a colour was, once, simply not that colour. */
    switch (category) {
        case ELEM_ALKALI:         return Ui::rgb(255, 122,  92);
        case ELEM_ALKALINE_EARTH: return Ui::rgb(255, 190,  84);
        case ELEM_TRANSITION:     return Ui::rgb(166, 182, 204);
        case ELEM_METAL:          return Ui::rgb(126, 206, 194);
        case ELEM_METALLOID:      return Ui::rgb(192, 164, 244);
        case ELEM_NONMETAL:       return Ui::rgb(126, 220, 146);
        case ELEM_HALOGEN:        return Ui::rgb(248, 226, 112);
        case ELEM_NOBLE:          return Ui::rgb(134, 184, 255);
        case ELEM_RARE_EARTH:     return Ui::rgb(242, 154, 202);
        case ELEM_RADIOACTIVE:    return Ui::rgb(214, 136, 154);
        default:                  return Ui::rgb(180, 180, 180);
    }
}

// ---- geometry --------------------------------------------------------------

Rect ElementsGame::tabRect(int16_t w, uint8_t index) {
    const int16_t third = static_cast<int16_t>(w / 3);
    const int16_t x = static_cast<int16_t>(index * third);
    const int16_t width = (index == 2) ? static_cast<int16_t>(w - 2 * third) : third;
    return Rect{x, 30, width, 22};
}

Rect ElementsGame::stripRect(int16_t w) {
    return Rect{6, 56, static_cast<int16_t>(w - 12), 26};
}

Rect ElementsGame::cellRect(uint8_t col, uint8_t row) {
    return Rect{static_cast<int16_t>(TABLE_X0 + (col - 1) * COL_STEP),
                rowY(row), CELL_W, CELL_H};
}

Rect ElementsGame::optionRect(int16_t w, uint8_t index, uint8_t count) {
    /* Three options get the same slots as four, minus the last one, so the
     * state question does not shuffle the buttons under a player's finger
     * relative to every other question. */
    (void)count;
    return Rect{10, static_cast<int16_t>(90 + index * 36),
                static_cast<int16_t>(w - 20), 32};
}

Rect ElementsGame::levelRect(int16_t w, uint8_t index) {
    return Rect{14, static_cast<int16_t>(88 + index * 34),
                static_cast<int16_t>(w - 28), 32};
}

Rect ElementsGame::cardQuizRect(int16_t w) {
    return Rect{10, 198, static_cast<int16_t>((w - 30) / 2), 34};
}

Rect ElementsGame::cardBackRect(int16_t w) {
    const int16_t half = static_cast<int16_t>((w - 30) / 2);
    return Rect{static_cast<int16_t>(w - 10 - half), 198, half, 34};
}

uint8_t ElementsGame::cellHit(int16_t x, int16_t y) {
    for (uint8_t row = 1; row <= 9; ++row) {
        const int16_t top = rowY(row);
        if (y < top - 3 || y >= top + CELL_H + 3) continue;
        if (x < TABLE_X0 - 2) return 0xFF;
        const int16_t col = static_cast<int16_t>((x - TABLE_X0) / COL_STEP + 1);
        if (col < 1 || col > 18) return 0xFF;
        return elementAtCell(static_cast<uint8_t>(col), row);
    }
    return 0xFF;
}

// ---- level -----------------------------------------------------------------

uint8_t ElementsGame::tierMastery(uint8_t tier) const {
    uint16_t total = 0;
    uint16_t known = 0;
    for (uint16_t i = 0; i < ELEMENT_COUNT; ++i) {
        if (ELEMENTS[i].tier > tier) continue;
        ++total;
        if (progress_.score(i) > 0) ++known;
    }
    return total == 0 ? 0 : static_cast<uint8_t>((known * 100UL) / total);
}

uint8_t ElementsGame::activeTier() const {
    if (tierPin_ >= 1 && tierPin_ <= 3) return tierPin_;
    /* Auto. Deliberately measured over the tier being practised rather than
     * Progress::masteryPercent(), which counts only items already seen and so
     * would read 100% after two lucky answers. */
    if (tierMastery(1) < 80) return 1;
    if (tierMastery(2) < 70) return 2;
    return 3;
}

bool ElementsGame::inPool(uint16_t index, void* ctx) {
    const ElementsGame* self = static_cast<const ElementsGame*>(ctx);
    return index < ELEMENT_COUNT && ELEMENTS[index].tier <= self->activeTier();
}

// ---- lifecycle -------------------------------------------------------------

void ElementsGame::begin(AppContext& host) {
    progress_.begin(host, "elemProg", ELEMENT_COUNT);
    const uint32_t pin = host.getScore(TIER_KEY, 0);
    tierPin_ = pin <= 3 ? static_cast<uint8_t>(pin) : 0;

    tab_       = Tab::Explore;
    score_     = 0;
    streak_    = 0;
    cardOpen_  = false;
    selected_  = 0xFF;
    lastSelected_ = 0xFF;
    forcedElement_ = -1;
    wrappedFor_ = 0xFF;
    nextQuestion();
    markFullDirty();
}

void ElementsGame::end(AppContext& host) {
    (void)host;
    progress_.flush();
}

// ---- text wrapping ---------------------------------------------------------

void ElementsGame::wrapInto(const char* src, char dst[FACT_LINES][FACT_COLS + 1],
                            uint8_t& lineCount) {
    lineCount = 0;
    for (uint8_t i = 0; i < FACT_LINES; ++i) dst[i][0] = '\0';
    if (src == nullptr) return;

    uint16_t pos = 0;
    const uint16_t len = static_cast<uint16_t>(strlen(src));
    while (pos < len && lineCount < FACT_LINES) {
        uint16_t take = FACT_COLS;
        if (pos + take >= len) {
            take = static_cast<uint16_t>(len - pos);
        } else {
            uint16_t brk = take;
            while (brk > 0 && src[pos + brk] != ' ') --brk;
            if (brk > 0) take = brk;
        }
        memcpy(dst[lineCount], src + pos, take);
        dst[lineCount][take] = '\0';
        ++lineCount;
        pos = static_cast<uint16_t>(pos + take);
        while (pos < len && src[pos] == ' ') ++pos;
    }
}

void ElementsGame::rewrapIfStale() {
    if (selected_ >= ELEMENT_COUNT || wrappedFor_ == selected_) return;
    wrapInto(ELEMENTS[selected_].fact, factLines_, factLineCount_);
    wrappedFor_ = selected_;
}

// ---- update ----------------------------------------------------------------

void ElementsGame::update(AppContext& host, const TouchPoint& touch) {
    const int16_t w = host.display().width();

    if (revealUntilMs_ != 0 && millis() > revealUntilMs_) {
        nextQuestion();
        return;
    }

    if (!touch.justPressed) return;

    /* The card covers the tab strip, so it gets first refusal on every touch.
     * Otherwise a tap meant for "Back" could land on a tab underneath it. */
    if (cardOpen_) {
        if (cardQuizRect(w).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            forcedElement_ = static_cast<int16_t>(selected_);
            cardOpen_ = false;
            tab_ = Tab::Quiz;
            nextQuestion();
            markFullDirty();
            return;
        }
        if (cardBackRect(w).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cardOpen_ = false;
            markFullDirty();
        }
        return;
    }

    for (uint8_t i = 0; i < 3; ++i) {
        if (!tabRect(w, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        const Tab wanted = static_cast<Tab>(i);
        if (wanted != tab_) {
            tab_ = wanted;
            markFullDirty();
        }
        return;
    }

    switch (tab_) {
        case Tab::Explore: {
            if (stripRect(w).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                if (selected_ < ELEMENT_COUNT) {
                    cardOpen_ = true;
                    host.beepOk();
                    markFullDirty();
                }
                return;
            }
            const uint8_t hit = cellHit(touch.x, touch.y);
            if (hit == 0xFF || hit == selected_) return;
            /* Two-step: the first tap only moves the selection, because a
             * 16x14 square is smaller than a fingertip and an accidental open
             * would be a screen change the player did not ask for. */
            lastSelected_ = selected_;
            selected_ = hit;
            markDirty();
            return;
        }
        case Tab::Quiz:
            updateQuiz(host, touch);
            return;
        case Tab::Level:
            updateLevel(host, touch);
            return;
    }
}

// ---- chart -----------------------------------------------------------------

void ElementsGame::drawCell(Ui::Renderer& tft, uint8_t index, CellStyle style) {
    if (index >= ELEMENT_COUNT) return;
    const ElementFact& e = ELEMENTS[index];
    const Rect r = cellRect(e.col, e.row);

    uint16_t fill = categoryColor(e.category);
    uint16_t ink  = cellInk();
    switch (style) {
        case CellStyle::Dimmed:
            /* Out of the current quiz level: still legible, still tappable,
             * just visibly not what is being asked about. Both the fill and
             * the symbol are shades of the family's own colour, so a dimmed
             * cell still says which family it belongs to. */
            ink  = Ui::shade(fill, 155);
            fill = Ui::shade(fill, 38);
            break;
        case CellStyle::Correct: fill = Ui::success(); break;
        case CellStyle::Wrong:   fill = Ui::error(); ink = TFT_WHITE; break;
        default: break;
    }

    tft.fillRect(r.x, r.y, r.w, r.h, fill);
    if (style == CellStyle::Selected || style == CellStyle::Pending) {
        // A ring in the gutter, so highlighting never eats the symbol.
        const uint16_t edge = (style == CellStyle::Pending) ? Ui::warning() : Ui::text();
        tft.drawRect(r.x - 1, r.y - 1, r.w + 2, r.h + 2, edge);
        tft.drawRect(r.x, r.y, r.w, r.h, edge);
    }

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(ink, fill);
    tft.drawString(e.symbol, r.x + r.w / 2, r.y + r.h / 2, 1);
    tft.setTextDatum(TL_DATUM);
}

void ElementsGame::drawCellResting(Ui::Renderer& tft, uint8_t index) {
    if (index >= ELEMENT_COUNT) return;
    drawCell(tft, index, ELEMENTS[index].tier > activeTier() ? CellStyle::Dimmed
                                                             : CellStyle::Normal);
}

void ElementsGame::drawTable(Ui::Renderer& tft, bool full) {
    if (!full) return;
    const uint8_t tier = activeTier();
    for (uint8_t i = 0; i < ELEMENT_COUNT; ++i) {
        drawCell(tft, i, ELEMENTS[i].tier > tier ? CellStyle::Dimmed : CellStyle::Normal);
    }
    /* The two dashes that stand for the strips lifted out of periods 6 and 7.
     * Without them the f-block rows look like a separate table. */
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    for (uint8_t row = 6; row <= 7; ++row) {
        const Rect r = cellRect(3, row);
        tft.drawString("*", r.x + r.w / 2, r.y + r.h / 2, 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void ElementsGame::drawStrip(Ui::Renderer& tft, const char* text, uint16_t ink) {
    const Rect r = stripRect(tft.width());
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, Ui::surface());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, Ui::outline());
    tft.setTextColor(ink, Ui::surface());
    tft.setTextDatum(MC_DATUM);
    /* Font 2 where it fits, font 1 where it does not. The fact prompts are the
     * long ones and they are the reason this is measured rather than assumed. */
    const uint8_t font = tft.textWidth(text, 2) <= r.w - 12 ? 2 : 1;
    tft.drawString(text, r.x + r.w / 2, r.y + r.h / 2, font);
    tft.setTextDatum(TL_DATUM);
}

// ---- explore ---------------------------------------------------------------

void ElementsGame::renderExplore(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (needsFullRender()) {
        drawTable(tft, true);
    } else if (lastSelected_ != selected_) {
        /* Moving the selection is two cells of work, not 118. Repainting the
         * whole chart at 27fps would be ~20ms of SPI for a one-square move. */
        drawCellResting(tft, lastSelected_);
    }
    if (selected_ < ELEMENT_COUNT) {
        drawCell(tft, selected_, CellStyle::Selected);
    }
    lastSelected_ = selected_;

    char buf[64];
    if (selected_ >= ELEMENT_COUNT) {
        snprintf(buf, sizeof(buf), "Tap any square to explore");
        drawStrip(tft, buf, Ui::muted());
    } else {
        const ElementFact& e = ELEMENTS[selected_];
        snprintf(buf, sizeof(buf), "%s  %s  %u  -  tap here", e.symbol, e.name,
                 static_cast<unsigned>(e.z));
        drawStrip(tft, buf, Ui::text());
    }
}

// ---- element card ----------------------------------------------------------

void ElementsGame::renderCard(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const int16_t w = tft.width();
    if (selected_ >= ELEMENT_COUNT) return;
    const ElementFact& e = ELEMENTS[selected_];

    if (!needsFullRender()) return;   // the card has no moving parts

    // Covers the tab strip too: the card is a place, not an overlay panel.
    tft.fillRect(0, 30, w, static_cast<int16_t>(tft.height() - 30), Ui::bg());

    const uint16_t fill = categoryColor(e.category);
    tft.fillRoundRect(12, 44, 76, 76, 6, fill);
    tft.setTextColor(cellInk(), fill);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(e.symbol, 50, 84, 4);
    tft.setTextDatum(TL_DATUM);
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(e.z));
    tft.drawString(buf, 17, 48, 2);

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(e.name, 100, 48, 4);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(elementCategoryName(e.category), 100, 78, 2);
    snprintf(buf, sizeof(buf), "%s  -  %u protons", elementStateName(e.state),
             static_cast<unsigned>(e.z));
    tft.drawString(buf, 100, 98, 2);

    rewrapIfStale();
    tft.setTextColor(Ui::text(), Ui::bg());
    for (uint8_t i = 0; i < factLineCount_; ++i) {
        tft.drawString(factLines_[i], 14, static_cast<int16_t>(132 + i * 20), 2);
    }

    /* Drawn here rather than through Ui::drawButton, which takes a String and
     * would allocate a temporary on every repaint. */
    const Rect quiz = cardQuizRect(w);
    tft.fillRoundRect(quiz.x, quiz.y, quiz.w, quiz.h, 6, Ui::rgb(45, 154, 96));
    tft.drawRoundRect(quiz.x, quiz.y, quiz.w, quiz.h, 6, Ui::outline());
    tft.setTextColor(TFT_WHITE, Ui::rgb(45, 154, 96));
    tft.setTextDatum(MC_DATUM);
    tft.drawString("Quiz me", quiz.x + quiz.w / 2, quiz.y + quiz.h / 2, 2);

    const Rect back = cardBackRect(w);
    tft.fillRoundRect(back.x, back.y, back.w, back.h, 6, Ui::panel());
    tft.drawRoundRect(back.x, back.y, back.w, back.h, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::panel());
    tft.drawString("Back", back.x + back.w / 2, back.y + back.h / 2, 2);
    tft.setTextDatum(TL_DATUM);
}

// ---- render ----------------------------------------------------------------

void ElementsGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const int16_t w = tft.width();

    if (cardOpen_) {
        renderCard(host);
        return;
    }

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        const Rect explore = tabRect(w, 0);
        const Rect quiz    = tabRect(w, 1);
        const Rect level   = tabRect(w, 2);
        Ui::drawTab(tft, explore, "Explore", tab_ == Tab::Explore);
        Ui::drawTab(tft, quiz,    "Quiz",    tab_ == Tab::Quiz);
        Ui::drawTab(tft, level,   "Level",   tab_ == Tab::Level);
        const Rect& active = tab_ == Tab::Explore ? explore
                           : tab_ == Tab::Quiz    ? quiz : level;
        Ui::drawTabBaseline(tft, 52, 0, w, active);
    }

    switch (tab_) {
        case Tab::Explore: renderExplore(host); break;
        case Tab::Quiz:    renderQuiz(host);    break;
        case Tab::Level:   renderLevel(host);   break;
    }

    if (tab_ == Tab::Quiz) {
        char buf[32];
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "%u pts  x%u", static_cast<unsigned>(score_),
                 static_cast<unsigned>(streak_));
        tft.fillRect(8, SCORE_Y, 150, 12, Ui::bg());
        tft.drawString(buf, 8, SCORE_Y, 1);
    }
}
