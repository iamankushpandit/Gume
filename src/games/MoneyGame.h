#pragma once

#include "engine/Game.h"
#include "ui/Ui.h"

class MoneyGame : public Game {
public:
    const char* title() const override;
    void begin(GameHost& host) override;
    void update(GameHost& host, const TouchPoint& touch) override;
    void render(GameHost& host) override;

private:
    enum class Mode : uint8_t {
        Total,
        Make,
        More,
        Change
    };

    uint8_t level() const;
    uint8_t allowedCoinCount() const;
    uint16_t maxTotal() const;
    uint8_t randomCoinValue(uint16_t maxCents) const;
    String centsText(uint16_t cents) const;
    const char* modeName() const;

    void newRound();
    void setupTotalRound();
    void setupMakeRound();
    void setupMoreRound();
    void setupChangeRound();
    void makeRandomGroup(uint8_t* coins, uint8_t& count, uint8_t minCoins, uint8_t maxCoins, uint16_t limit);
    void makeOptions(uint16_t correct);
    bool optionExists(uint16_t value, uint8_t upTo) const;
    uint16_t groupValue(const uint8_t* coins, uint8_t count) const;
    void markCorrect(GameHost& host);
    void markWrong(int8_t flashIndex);

    Rect optionRect(uint8_t index) const;
    Rect trayRect(uint8_t index) const;
    Rect clearRect() const;
    Rect doneRect() const;
    Rect moreRect(uint8_t index) const;

    void drawCoin(TFT_eSPI& tft, int16_t cx, int16_t cy, uint8_t value) const;
    void drawCoinGroup(TFT_eSPI& tft, const Rect& r, const uint8_t* coins, uint8_t count) const;
    void drawOptions(TFT_eSPI& tft) const;
    void drawMake(TFT_eSPI& tft) const;
    void drawMore(TFT_eSPI& tft) const;

    Mode mode_ = Mode::Total;
    uint8_t groupA_[12] = {};
    uint8_t groupB_[12] = {};
    uint8_t makeCoins_[16] = {};
    uint8_t groupACount_ = 0;
    uint8_t groupBCount_ = 0;
    uint8_t makeCount_ = 0;
    uint16_t target_ = 0;
    uint16_t built_ = 0;
    uint16_t paid_ = 0;
    uint16_t price_ = 0;
    uint16_t options_[4] = {};
    uint8_t correctButton_ = 0;
    uint16_t score_ = 0;
    uint16_t streak_ = 0;
    uint16_t bestStreak_ = 0;
    int8_t selected_ = -1;
    int8_t flashIndex_ = -1;
    uint32_t flashUntil_ = 0;
    bool roundComplete_ = false;
};
