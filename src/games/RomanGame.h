#pragma once

#include "engine/Game.h"
#include "engine/RecentQuestions.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& romanAppMetadata();

/* Roman numerals, both ways round, with the key on the screen.
 *
 * This is a TEACHING screen, so two things are deliberate. The symbol key is
 * always visible and grows as the level does -- a child meeting L for the
 * first time should be able to look it up rather than guess, because the skill
 * being built is reading the notation, not remembering seven letters. And
 * every answer is followed by the number taken apart: "XIV = X + IV = 10 + 4".
 * That decomposition is the whole idea; being told only that you were wrong
 * teaches nothing about where the four came from.
 *
 * The wrong answers are chosen rather than random, for the same reason. The
 * classic mistake is reading a subtractive pair as an addition -- XIV as
 * 10 + 1 + 5 -- so that misreading is one of the four buttons whenever it
 * differs from the right answer, and writing 4 as IIII is offered whenever the
 * question runs the other way. A distractor a player would never pick teaches
 * nothing either. */
class RomanGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    static constexpr uint8_t TEXT_MAX = 24;

    Rect answerRect(uint8_t index) const;
    Rect keyRect() const;
    Rect questionRect() const;
    Rect noteRect() const;

    uint8_t level() const;
    uint16_t levelCeiling() const;
    uint16_t levelFloor() const;
    void newQuestion();
    void makeOptions();
    bool optionExists(const char* candidate, uint8_t upTo) const;
    void drawKey(Ui::Renderer& tft);
    void drawQuestion(Ui::Renderer& tft);
    void drawNote(Ui::Renderer& tft);

    RecentQuestions recent_;

    uint16_t value_ = 1;
    /* True: the numeral is shown and the answer is a number. False: the number
     * is shown and the answer is a numeral. Both directions from the first
     * level -- reading and writing are different skills and a player who has
     * only ever read them cannot write one. */
    bool askNumber_ = true;
    char subject_[TEXT_MAX] = {};
    char options_[4][TEXT_MAX] = {};
    uint8_t correctButton_ = 0;

    uint8_t drawnButton_[4] = {};
    uint16_t drawnScore_ = 0xFFFF;
    uint8_t drawnKeyLevel_ = 0xFF;
    bool drawnHeader_ = false;
    bool drawnQuestion_ = false;
    uint8_t drawnNote_ = 0xFF;

    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t best_ = 0;
    int8_t selected_ = -1;
    bool answered_ = false;
};
