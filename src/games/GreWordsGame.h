#pragma once

#include "engine/Game.h"
#include "engine/Progress.h"
#include "ui/Ui.h"

/*
 * GRE vocabulary trainer: a study mode and a scored quiz.
 *
 * The odd one out in this catalog -- every other screen is aimed at a small
 * child, this one at whoever is sitting the test. Denser text and smaller
 * fonts are appropriate here; the 30px minimum touch target is not, and still
 * applies.
 *
 * Word selection is weighted by engine/Progress, so a word that was missed
 * comes back soon and one that is known fades out.
 */
class GreWordsGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;
    void end(AppContext& host) override;

private:
    enum class Mode : uint8_t { Flip, Quiz };

    static constexpr uint8_t OPTION_COUNT = 4;
    static constexpr uint8_t WRAP_LINES   = 4;
    static constexpr uint8_t WRAP_COLS    = 44;   // fits ~280px at font 1

    Rect flipTabRect(int16_t w) const;
    Rect quizTabRect(int16_t w) const;
    Rect cardRect() const;
    Rect optionRect(uint8_t i) const;
    Rect knewRect() const;
    Rect missedRect() const;
    Rect nextRect() const;

    void nextWord();
    void buildOptions();
    /* Wrap once per round into fixed buffers. Re-wrapping every frame is the
     * classic "rebuild content that did not change" mistake, and drawing via
     * Arduino String would allocate on every line of every frame. */
    void rewrapIfStale();
    static void wrapInto(const char* src, char dst[WRAP_LINES][WRAP_COLS + 1],
                         uint8_t& lineCount);

    void answer(AppContext& host, bool correct);

    void renderFlip(AppContext& host);
    void renderQuiz(AppContext& host);

    Progress progress_;
    Mode     mode_        = Mode::Flip;
    uint16_t currentWord_ = 0;
    bool     cardFlipped_ = false;
    uint32_t score_       = 0;
    uint16_t streak_      = 0;

    uint16_t optionIndices_[OPTION_COUNT] = {};
    uint8_t  correctOption_ = 0;
    int8_t   chosenOption_  = -1;      // -1 = unanswered this round
    uint32_t revealUntilMs_ = 0;

    char    meaningLines_[WRAP_LINES][WRAP_COLS + 1] = {};
    uint8_t meaningLineCount_ = 0;
    char    exampleLines_[WRAP_LINES][WRAP_COLS + 1] = {};
    uint8_t exampleLineCount_ = 0;
    bool    wrapStale_ = true;
};
