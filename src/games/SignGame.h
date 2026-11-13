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
#include "engine/Progress.h"
#include "engine/RecentQuestions.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& signAppMetadata();

/*
 * Sign ABC -- the American Sign Language manual alphabet.
 *
 * Most players will never have seen a sign, so this is a course before it is
 * a quiz: a quiz about something you have never been shown is a test, not a
 * lesson. The alphabet is seven lessons of four letters (Y-Z is two), in
 * alphabetical order because that is the order a young player already knows.
 *
 *   Lessons   the home of the game. Lesson N opens once lesson N-1 is passed;
 *             passed lessons stay open to look at again. Practice opens after
 *             the first lesson and quizzes every letter learnt so far.
 *   Teach     one new hand at a time, big, with its letter. Back and Next;
 *             the last card's Next is "Quiz me".
 *   Quiz      six questions on that lesson, alternating "which letter is this
 *             hand?" and "which hand is this letter?". From lesson 2 on, two of
 *             the six revisit earlier letters. Five right passes.
 *   Result    passed: on to the next lesson. Not yet: look at the cards again,
 *             or try the quiz again.
 *   Practice  the same two questions, endless, over every letter learnt,
 *             with Progress steering towards the ones that get missed.
 *
 * Wrong answers are only ever letters the player has been taught -- the
 * lessons are alphabetical, so that is everything up to the end of the
 * current lesson -- and the more of the alphabet is mastered, the more often
 * they are the hands that look alike (A E M N S T are all fists). The hands
 * come from map-n-flag (mnf_sign()), drawn in the theme's ink on a card.
 *
 * Files: SignGame.cpp (the course, questions, input), SignDraw.cpp (pixels).
 */
class SignGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;
    void end(AppContext& host) override;

private:
    enum class Stage : uint8_t { Lessons, Teach, Quiz, Result, Practice };
    /* Read: a hand, four letters. Find: a letter, four hands. */
    enum class Kind : uint8_t { Read, Find };
    enum class Phase : uint8_t { Asking, Feedback };

    static constexpr uint8_t LETTERS = 26;
    static constexpr uint8_t LESSON_SIZE = 4;
    static constexpr uint8_t LESSONS = (LETTERS + LESSON_SIZE - 1) / LESSON_SIZE;
    static constexpr uint8_t QUIZ_LEN = 6;
    static constexpr uint8_t PASS_MARK = 5;
    static constexpr uint8_t REVIEW_IN_QUIZ = 2;
    static constexpr uint8_t OPTION_COUNT = 4;
    static constexpr uint8_t NOT_DRAWN = 0xFF;
    static_assert(LESSON_SIZE >= OPTION_COUNT, "lesson 1 cannot fill four answers");

    // ---- geometry (SignDraw.cpp) ----
    static Rect lessonRect(uint8_t i);      // LESSONS lesson tiles, then Practice
    static Rect lessonsChipRect();
    static Rect bigCardRect();
    static Rect letterRect(uint8_t i);
    static Rect handCardRect(uint8_t i);
    static Rect backRect();
    static Rect nextRect();
    static Rect resultRect(uint8_t i);

    // ---- the course (SignGame.cpp) ----
    static uint8_t lessonStart(uint8_t lesson);
    static uint8_t lessonEnd(uint8_t lesson);   // one past the last letter
    void goTo(Stage stage);
    void startLesson(uint8_t lesson);
    void startQuiz();
    void finishQuiz(AppContext& host);
    void newQuestion();
    void makeOptions(uint8_t poolEnd);
    bool hasOption(uint8_t letter, uint8_t upTo) const;
    void answer(AppContext& host, uint8_t choice);
    void updateLessons(AppContext& host, const TouchPoint& touch);
    void updateTeach(AppContext& host, const TouchPoint& touch);
    void updateResult(AppContext& host, const TouchPoint& touch);
    static bool inPracticePool(uint16_t index, void* ctx);
    static bool inReviewPool(uint16_t index, void* ctx);

    // ---- drawing (SignDraw.cpp) ----
    void drawHeader(Ui::Renderer& tft);
    void drawLessons(Ui::Renderer& tft);
    void drawTeach(Ui::Renderer& tft);
    void drawQuestion(Ui::Renderer& tft);
    void drawRead(Ui::Renderer& tft, bool fresh);
    void drawFind(Ui::Renderer& tft, bool fresh);
    void drawResult(Ui::Renderer& tft);
    void drawPrompt(Ui::Renderer& tft, const Rect& r, const char* line, uint16_t color);
    uint8_t optionState(uint8_t i) const;
    uint8_t promptState() const;

    Progress progress_;
    RecentQuestions recent_;

    Stage stage_ = Stage::Lessons;
    uint8_t passed_ = 0;          // lessons passed; lesson `passed_` is the next one
    uint8_t lesson_ = 0;
    uint8_t teachIndex_ = 0;

    uint8_t quizLetters_[QUIZ_LEN] = {};
    uint8_t quizNum_ = 0;
    uint8_t quizRight_ = 0;
    bool lastPassed_ = false;

    Kind kind_ = Kind::Read;
    Phase phase_ = Phase::Asking;
    uint8_t current_ = 0;                  // letter index, 0 = 'A'
    uint8_t options_[OPTION_COUNT] = {};
    uint8_t correctSlot_ = 0;
    int8_t chosen_ = -1;
    uint32_t feedbackUntil_ = 0;

    uint16_t score_ = 0;          // Practice only
    uint16_t rounds_ = 0;
    uint16_t streak_ = 0;         // across quizzes and Practice; the best is kept

    /* What is on the panel. The question number rather than the letter, since
     * the same letter can come back and must still repaint. */
    uint16_t question_ = 0;
    uint16_t drawnQuestion_ = 0xFFFF;
    uint8_t drawnKind_ = NOT_DRAWN;
    uint8_t drawnOption_[OPTION_COUNT] = {};
    uint8_t drawnPrompt_ = NOT_DRAWN;
    char drawnHeader_[32] = {};
    uint8_t drawnTeach_ = NOT_DRAWN;
};
