#pragma once

#include "engine/Game.h"
#include "ui/GameLayout.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& moneyAppMetadata();

class MoneyGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void render(AppContext& host) override;

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
    void markCorrect(AppContext& host);
    void markWrong(int8_t flashIndex);

    /* Four modes share one screen, so the bands are named rather than being
     * four sets of literals. All are hung off the live panel; on a 320x240
     * one they reproduce the authored rects. */
    Rect optionsBand(const Ui::Frame& f) const;
    Rect optionRect(const Ui::Frame& f, uint8_t index) const;
    Rect totalCoinRect(const Ui::Frame& f) const;

    Rect makeActionRow(const Ui::Frame& f) const;
    Rect clearRect(const Ui::Frame& f) const;
    Rect doneRect(const Ui::Frame& f) const;
    Rect trayRect(const Ui::Frame& f, uint8_t index) const;
    Rect makeCoinRect(const Ui::Frame& f) const;

    Rect moreButtonRow(const Ui::Frame& f) const;
    Rect moreRect(const Ui::Frame& f, uint8_t index) const;
    Rect moreGroupRect(const Ui::Frame& f, uint8_t index) const;

    Rect pricePanelRect(const Ui::Frame& f) const;
    Rect correctBannerRect(const Ui::Frame& f) const;

    void drawCoin(Ui::Renderer& tft, int16_t cx, int16_t cy, uint8_t value) const;
    void drawCoinGroup(Ui::Renderer& tft, const Rect& r, const uint8_t* coins, uint8_t count) const;
    void drawOptions(Ui::Renderer& tft, const Ui::Frame& f) const;
    void drawMake(Ui::Renderer& tft, const Ui::Frame& f) const;
    void drawMore(Ui::Renderer& tft, const Ui::Frame& f) const;

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
