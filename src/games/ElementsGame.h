// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "engine/Game.h"
#include "engine/Course.h"
#include "ElementsCourse.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& elementsAppMetadata();

/*
 * The periodic table, for a player who has never taken chemistry.
 *
 * Three tabs, and the order matters: Explore first, because a quiz about
 * something you have never seen is a test, not a lesson.
 *
 *   Explore  the real 118-cell wall chart. Tap a square to select it, tap the
 *            strip above to open a card with the name, the atomic number, what
 *            kind of element it is and one line about where the player has
 *            already met it. From the card, "Quiz me" starts a round about
 *            that element.
 *   Quiz     six question shapes generated from the same table, so the bank
 *            cannot drift from the data. One of them ("tap it in the table")
 *            reuses the chart itself, which is the only one that teaches where
 *            things live rather than what they are called.
 *   Level    which elements the quiz may ask about. Auto follows mastery;
 *            Easy / Common / All pin it.
 *
 * The level only ever narrows the *quiz*. Explore always shows and opens all
 * 118 -- out-of-level squares are drawn dimmed, not hidden. A player who wants
 * to go and read about Oganesson is doing exactly the thing this screen is
 * for, and nothing here should stop them.
 */
class ElementsGame : public AppGame {
public:
#if GUME_HAS_NAV_KEYS
    /* Puts back what the d-pad selector's ring covered (Game.h). */
    bool repaintUnderRing(AppContext& host, const Rect& box) override;
#endif
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;
    Keep keepsGame() const override;
    void resume(AppContext& host, uint32_t awayMs) override;

private:
    enum class Tab : uint8_t { Explore, Quiz, Level };

    /* Question shapes. All six are generated from ElementData, so adding an
     * element adds its questions and nothing has to be written twice. */
    enum class CellStyle : uint8_t { Normal, Dimmed, Selected, Pending, Correct, Wrong };

    static constexpr uint8_t MAX_OPTIONS = 4;
    /* The fact is capped at 46 chars by tools/gen_elements.py, which is two
     * lines here with room to spare. Wrapped once per card, not per frame. */
    static constexpr uint8_t FACT_LINES = 2;
    static constexpr uint8_t FACT_COLS  = 34;

    // ---- geometry (see ElementsGame.cpp for why these numbers) ----
    static Rect tabRect(int16_t w, uint8_t index);
    static Rect stripRect(int16_t w);
    static Rect cellRect(uint8_t col, uint8_t row);
    static Rect optionRect(int16_t w, uint8_t index, uint8_t count);
    static Rect levelRect(int16_t w, uint8_t index);
    static Rect cardQuizRect(int16_t w);
    static Rect cardBackRect(int16_t w);
    /** Element index under (x, y), or 0xFF if the touch missed every cell. */
    static uint8_t cellHit(int16_t x, int16_t y);

    static uint16_t categoryColor(uint8_t category);

    // ---- level ----
    /** The level questions are asked at: the ladder's, or 3 / 6 / 9 while
     * the Level tab pins Easy / Common / All. */
    uint8_t quizLevel() const;
    /** 1..3: the elements that level asks about. */
    uint8_t activeTier() const;
    /** Percent of elements at or below `tier` that have been answered right. */

    // ---- quiz ----
    void nextQuestion();
    void buildOptions();
    void writePrompt();
    void answer(AppContext& host, bool correct);
    /** True when the current question is answered on the chart rather than on
     *  four buttons. Drives whether a tab switch needs a full repaint. */
    bool questionUsesTable() const { return q_.kind == ElementsCourse::Kind::FindIt; }
    void optionLabel(uint8_t index, char* out, size_t len) const;
    void updateQuiz(AppContext& host, const TouchPoint& touch);
    void updateLevel(AppContext& host, const TouchPoint& touch);

    // ---- render ----
    void drawTable(Ui::Renderer& tft, bool full);
    void drawCell(Ui::Renderer& tft, uint8_t index, CellStyle style);
    /** Repaint one cell in its resting style: Normal, or Dimmed if out of level. */
    void drawCellResting(Ui::Renderer& tft, uint8_t index);
    void drawStrip(Ui::Renderer& tft, const char* text, uint16_t ink);
    void renderExplore(AppContext& host);
    void renderCard(AppContext& host);
    void renderQuiz(AppContext& host);
    void renderLevel(AppContext& host);

    void rewrapIfStale();
    static void wrapInto(const char* src, char dst[FACT_LINES][FACT_COLS + 1],
                         uint8_t& lineCount);

    Course::Ladder ladder_;
    Course::QuestionDeck deck_;
    uint8_t  deckTier_ = 0;           // the pool the deck was dealt for
    uint8_t  kindTurn_[ELEMENT_COUNT] = {};   // each element gets each kind in turn
    ElementsCourse::Question q_ = {};
    bool     announce_ = false;       // the score line names the new level
    Tab      tab_       = Tab::Explore;
    uint8_t  tierPin_   = 0;          // 0 = auto, else 1..3
    uint32_t score_     = 0;
    uint16_t streak_    = 0;

    // Explore
    uint8_t selected_     = 0xFF;     // element index, 0xFF = nothing yet
    uint8_t lastSelected_ = 0xFF;     // so a move repaints two cells, not 118
    bool    cardOpen_     = false;

    // Quiz
    uint8_t  qElement_      = 0;
    uint8_t  optionCount_   = MAX_OPTIONS;
    uint8_t  optionValue_[MAX_OPTIONS] = {};   // element index, or state id for StateOf
    uint8_t  correctOption_ = 0;
    int8_t   chosenOption_  = -1;
    uint8_t  pendingCell_   = 0xFF;   // FindIt: tapped once, waiting to confirm
    /* Cells currently drawn in something other than their resting style. The
     * next partial repaint puts these two back, which is what lets a FindIt
     * round change question without repainting all 118 squares. */
    uint8_t  restoreA_      = 0xFF;
    uint8_t  restoreB_      = 0xFF;
    int16_t  forcedElement_ = -1;     // set by the card's "Quiz me"
    uint32_t revealUntilMs_ = 0;
    char     prompt_[72]    = {};

    char    factLines_[FACT_LINES][FACT_COLS + 1] = {};
    uint8_t factLineCount_ = 0;
    uint8_t wrappedFor_    = 0xFF;    // which element factLines_ describes
};
