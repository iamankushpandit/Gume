#include "ColorMixGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t HUD_Y = TOP_BAR_HEIGHT + 5;
constexpr int16_t PROMPT_Y = TOP_BAR_HEIGHT + 22;
constexpr int16_t SWATCH_Y = TOP_BAR_HEIGHT + 48;
constexpr int16_t SWATCH_W = 60;
constexpr int16_t SWATCH_H = 44;
constexpr int16_t ANSWER_TOP = TOP_BAR_HEIGHT + 112;
constexpr uint8_t ANSWER_COUNT = 4;

constexpr ColorMixDefinition MIXES[] = {
    {"red", "blue", "purple", 0xF800, 0x041F, 0x9813},
    {"blue", "yellow", "green", 0x041F, 0xFFE0, 0x07E0},
    {"red", "yellow", "orange", 0xF800, 0xFFE0, 0xFC00},
    {"red", "white", "pink", 0xF800, 0xFFFF, 0xF81F},
    {"blue", "white", "light blue", 0x041F, 0xFFFF, 0x867F},
    {"black", "white", "gray", 0x0000, 0xFFFF, 0x8410},
};
constexpr uint8_t MIX_COUNT = sizeof(MIXES) / sizeof(MIXES[0]);

constexpr const char* DECOY_LABELS[] = {"purple", "green", "orange", "pink", "light blue", "gray", "brown"};
constexpr uint16_t DECOY_COLORS[] = {0x9813, 0x07E0, 0xFC00, 0xF81F, 0x867F, 0x8410, 0x8200};
constexpr uint8_t DECOY_COUNT = sizeof(DECOY_COLORS) / sizeof(DECOY_COLORS[0]);

constexpr AppScoreInfo COLOR_MIX_SCORE = {
    "colormix", "Color Mix", "mixBest", "pts", false
};

constexpr AppMetadata COLOR_MIX_METADATA = {
    "colormix",
    "Color Mix",
    nullptr,
    "mix colors",
    "Color Mix",
    "Mix two colours.",
    &COLOR_MIX_SCORE,
    LauncherIcon::ColorMix,
    14,
    true,
};
}

const AppMetadata& colorMixAppMetadata() {
    return COLOR_MIX_METADATA;
}

const char* ColorMixGame::title() const {
    return colorMixAppMetadata().screenTitle != nullptr
        ? colorMixAppMetadata().screenTitle
        : colorMixAppMetadata().title;
}

void ColorMixGame::begin(AppContext& host) {
    score_ = 0;
    attempts_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(colorMixAppMetadata().score->bestKey, 0));
    newQuestion();
    markDirty();
}

Rect ColorMixGame::swatchRect(const Ui::Frame& f, bool rightSwatch) const {
    /* Quarter of the width plus 6 puts the pair at x=56 and x=204 on a 320px
     * panel, which is where they were drawn, and keeps them clear of each
     * other and of the "+" between them at 240. */
    const int16_t offset = static_cast<int16_t>(f.w / 4 + 6);
    const int16_t cx = rightSwatch ? static_cast<int16_t>(f.cx() + offset)
                                   : static_cast<int16_t>(f.cx() - offset);
    return Rect{static_cast<int16_t>(cx - SWATCH_W / 2), SWATCH_Y, SWATCH_W, SWATCH_H};
}

Rect ColorMixGame::answerBand(const Ui::Frame& f) const {
    return Rect{26, ANSWER_TOP, static_cast<int16_t>(f.w - 52),
                static_cast<int16_t>(f.h - ANSWER_TOP - 12)};
}

Rect ColorMixGame::answerRect(const Ui::Frame& f, uint8_t index) const {
    const uint8_t cols = Ui::answerColumns(f, ANSWER_COUNT);
    const uint8_t rows = static_cast<uint8_t>(ANSWER_COUNT / cols);
    return Ui::gridCell(answerBand(f), cols, rows, index, 8);
}

void ColorMixGame::newQuestion() {
    active_ = &MIXES[random(MIX_COUNT)];
    correct_ = random(4);
    for (uint8_t i = 0; i < 4; ++i) {
        labels_[i] = nullptr;
        colors_[i] = 0;
    }
    labels_[correct_] = active_->result;
    colors_[correct_] = active_->resultColor;

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correct_) {
            continue;
        }
        bool unique = false;
        while (!unique) {
            const uint8_t pick = random(DECOY_COUNT);
            labels_[i] = DECOY_LABELS[pick];
            colors_[i] = DECOY_COLORS[pick];
            unique = strcmp(labels_[i], active_->result) != 0;
            for (uint8_t j = 0; j < i; ++j) {
                if (labels_[j] != nullptr && strcmp(labels_[j], labels_[i]) == 0) {
                    unique = false;
                }
            }
        }
    }
    correctFlash_ = false;
    wrongFlash_ = false;
    flashUntil_ = 0;
}

int8_t ColorMixGame::touchedAnswer(const Ui::Frame& f, int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < ANSWER_COUNT; ++i) {
        if (answerRect(f, i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

void ColorMixGame::update(AppContext& host, const TouchPoint& touch) {
    if (correctFlash_ && millis() > flashUntil_) {
        newQuestion();
        markDirty();
        return;
    }
    if (wrongFlash_ && millis() > flashUntil_) {
        wrongFlash_ = false;
        flashUntil_ = 0;
        markDirty();
    }

    if (!touch.justPressed || correctFlash_) {
        return;
    }

    const int8_t answer = touchedAnswer(Ui::frame(host.display()), touch.x, touch.y);
    if (answer < 0) {
        return;
    }
    ++attempts_;
    if (answer == correct_) {
        ++score_;
        ++streak_;
        if (host.saveBestScore(colorMixAppMetadata().score->bestKey, streak_, false)) {
            bestStreak_ = streak_;
        }
        correctFlash_ = true;
        wrongFlash_ = false;
        flashUntil_ = millis() + 750UL;
    } else {
        streak_ = 0;
        wrongFlash_ = true;
        flashUntil_ = millis() + 450UL;
    }
    markDirty();
}

void ColorMixGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Score ") + score_ + "/" + attempts_, 10, HUD_Y, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(String("Best ") + bestStreak_, f.w - 8, HUD_Y, 2);
    Ui::drawLabel(tft, Rect{10, PROMPT_Y, static_cast<int16_t>(f.w - 20), 18},
                  "What do you get?", Ui::text(), 2, Align::Center);

    const Rect left = swatchRect(f, false);
    const Rect right = swatchRect(f, true);
    tft.fillRoundRect(left.x, left.y, left.w, left.h, 6, active_->leftColor);
    tft.drawRoundRect(left.x, left.y, left.w, left.h, 6, Ui::outline());
    tft.fillRoundRect(right.x, right.y, right.w, right.h, 6, active_->rightColor);
    tft.drawRoundRect(right.x, right.y, right.w, right.h, 6, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);
    tft.drawString("+", f.cx(), SWATCH_Y + SWATCH_H / 2, 4);
    tft.drawString(active_->left, left.x + left.w / 2, left.y + left.h + 12, 1);
    tft.drawString(active_->right, right.x + right.w / 2, right.y + right.h + 12, 1);

    for (uint8_t i = 0; i < 4; ++i) {
        const Rect r = answerRect(f, i);
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, colors_[i]);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
        const uint16_t labelBg = colors_[i];
        const uint16_t labelText = (colors_[i] == 0x0000 || colors_[i] == 0x041F || colors_[i] == 0x9813 || colors_[i] == 0x8200) ? TFT_WHITE : TFT_BLACK;
        tft.setTextColor(labelText, labelBg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(labels_[i], r.x + r.w / 2, r.y + r.h / 2, 2);
    }

    if (correctFlash_ || wrongFlash_) {
        const uint16_t color = correctFlash_ ? Ui::success() : Ui::error();
        const Rect banner = Ui::centreIn(
            Rect{0, static_cast<int16_t>(SWATCH_Y + 18), f.w, 34}, 128, 34);
        tft.fillRoundRect(banner.x, banner.y, banner.w, banner.h, 8, color);
        tft.setTextColor(TFT_BLACK, color);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(correctFlash_ ? "Correct" : "Try again",
                       f.cx(), banner.y + banner.h / 2, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
