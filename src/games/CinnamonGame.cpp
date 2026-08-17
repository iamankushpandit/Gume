#include "CinnamonGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr int16_t PAD_TOP = TOP_BAR_HEIGHT + 44;
constexpr int16_t PAD_BAND_MAX_H = 150;

constexpr uint16_t DARK_RED    = 0x6000;
constexpr uint16_t DARK_BLUE   = 0x0008;
constexpr uint16_t DARK_GREEN  = 0x0200;
constexpr uint16_t DARK_YELLOW = 0x8400;
constexpr uint16_t LIT_RED     = 0xF800;
constexpr uint16_t LIT_BLUE    = 0x041F;
constexpr uint16_t LIT_GREEN   = 0x07E0;
constexpr uint16_t LIT_YELLOW  = 0xFFE0;

constexpr AppScoreInfo CINNAMON_SCORE = {
    "cinnamon", "Cinnamon", "cinnamonBest", "steps", false
};

constexpr AppMetadata CINNAMON_METADATA = {
    "cinnamon",
    "Cinnamon",
    "Cinnamon Says",
    "repeat pattern",
    "Cinnamon",
    "Watch the pattern, then repeat it.",
    &CINNAMON_SCORE,
    LauncherIcon::Cinnamon,
    6,
    true,
};
}

const AppMetadata& cinnamonAppMetadata() {
    return CINNAMON_METADATA;
}

const char* CinnamonGame::title() const {
    return cinnamonAppMetadata().screenTitle != nullptr
        ? cinnamonAppMetadata().screenTitle
        : cinnamonAppMetadata().title;
}

void CinnamonGame::begin(AppContext& host) {
    fullRedraw_ = true;
    statusDrawn_ = "";
    bestScore_ = static_cast<uint16_t>(host.getScore(cinnamonAppMetadata().score->bestKey, 0));
    length_ = 0;
    score_ = 0;
    appendStep();
    startShowing();
    markDirty();
}

void CinnamonGame::appendStep() {
    if (length_ < sizeof(sequence_)) {
        sequence_[length_++] = random(4);
    }
}

void CinnamonGame::startShowing() {
    phase_ = Phase::Showing;
    showIndex_ = 0;
    inputIndex_ = 0;
    litPad_ = -1;
    nextAt_ = millis() + 450UL;
}

Rect CinnamonGame::padBand(const Ui::Frame& f) const {
    /* Capped, because portrait leaves 200px here and four pads that tall stop
     * reading as a 2x2 pattern to memorise. */
    int16_t h = static_cast<int16_t>(f.h - 46 - PAD_TOP);
    if (h > PAD_BAND_MAX_H) {
        h = PAD_BAND_MAX_H;
    }
    return Rect{30, PAD_TOP, static_cast<int16_t>(f.w - 60), h};
}

Rect CinnamonGame::padRect(const Ui::Frame& f, uint8_t index) const {
    return Ui::gridCell(padBand(f), 2, 2, index, 8);
}

int8_t CinnamonGame::touchedPad(const Ui::Frame& f, int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 4; ++i) {
        if (padRect(f, i).contains(x, y, TOUCH_HIT_SLOP)) {
            return i;
        }
    }
    return -1;
}

uint16_t CinnamonGame::padColor(uint8_t index, bool lit) const {
    static constexpr uint16_t dark[4] = {DARK_RED, DARK_BLUE, DARK_GREEN, DARK_YELLOW};
    static constexpr uint16_t bright[4] = {LIT_RED, LIT_BLUE, LIT_GREEN, LIT_YELLOW};
    return lit ? bright[index] : dark[index];
}

void CinnamonGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (phase_ == Phase::Showing && now >= nextAt_) {
        if (litPad_ >= 0) {
            litPad_ = -1;
            ++showIndex_;
            nextAt_ = now + 450UL;  // gap between colours
            if (showIndex_ >= length_) {
                phase_ = Phase::Waiting;
            }
        } else if (showIndex_ < length_) {
            litPad_ = sequence_[showIndex_];
            nextAt_ = now + 600UL;  // a touch slower: calmer to follow
            // Echo the pad on the case LED so the cue does not rely on the
            // screen changing brightness.
            static const uint8_t PAD_RGB[4][3] = {
                {255, 40, 40}, {40, 90, 255}, {40, 255, 90}, {255, 220, 40}
            };
            const uint8_t* c = PAD_RGB[litPad_ % 4];
            host.pulseRgb(c[0], c[1], c[2], 600);
        }
        markDirty();
        return;
    }

    if (phase_ == Phase::Good && now >= nextAt_) {
        appendStep();
        startShowing();
        markDirty();
        return;
    }

    if (phase_ == Phase::Waiting && litPad_ >= 0 && now >= nextAt_) {
        litPad_ = -1;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (phase_ == Phase::Failed) {
        begin(host);
        return;
    }

    if (litPad_ >= 0) {
        return;
    }

    if (phase_ != Phase::Waiting) {
        return;
    }

    const int8_t pad = touchedPad(Ui::frame(host.display()), touch.x, touch.y);
    if (pad < 0) {
        return;
    }

    litPad_ = pad;
    nextAt_ = now + 120UL;
    if (pad == sequence_[inputIndex_]) {
        ++inputIndex_;
        if (inputIndex_ >= length_) {
            score_ = length_;
            if (host.saveBestScore(cinnamonAppMetadata().score->bestKey, score_, false)) {
                bestScore_ = score_;
            }
            phase_ = Phase::Good;
            litPad_ = pad;
            nextAt_ = now + 520UL;
            host.beepOk();
        }
    } else {
        phase_ = Phase::Failed;
        litPad_ = pad;
        fullRedraw_ = true;   // the failure line needs a clean background
    }
    markDirty();
}

void CinnamonGame::drawPad(Ui::Renderer& tft, const Ui::Frame& f, uint8_t index, bool lit) const {
    const Rect r = padRect(f, index);
    tft.fillRoundRect(r.x + 2, r.y + 3, r.w, r.h, 8, Ui::surface());
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, padColor(index, lit));
    if (lit) {
        // Bold dark ring so the active pad is obvious without needing the rest
        // of the screen to change.
        tft.drawRoundRect(static_cast<int16_t>(r.x - 3), static_cast<int16_t>(r.y - 3),
                          static_cast<int16_t>(r.w + 6), static_cast<int16_t>(r.h + 6), 11, TFT_BLACK);
        tft.drawRoundRect(static_cast<int16_t>(r.x - 4), static_cast<int16_t>(r.y - 4),
                          static_cast<int16_t>(r.w + 8), static_cast<int16_t>(r.h + 8), 12, TFT_BLACK);
    } else {
        tft.drawRoundRect(static_cast<int16_t>(r.x - 4), static_cast<int16_t>(r.y - 4),
                          static_cast<int16_t>(r.w + 8), static_cast<int16_t>(r.h + 8), 12, Ui::bg());
        tft.drawRoundRect(static_cast<int16_t>(r.x - 3), static_cast<int16_t>(r.y - 3),
                          static_cast<int16_t>(r.w + 6), static_cast<int16_t>(r.h + 6), 11, Ui::bg());
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, TFT_DARKGREY);
    }
}

void CinnamonGame::render(AppContext& host) {
    const Ui::Theme savedTheme = Ui::currentTheme();
    Ui::setTheme(Ui::Theme::Light);          // Cinnamon always renders light
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);

    /* needsFullRender() is the base class's flag, set by requestRender() when
     * the runtime turns the panel or comes back to this screen. Cinnamon
     * keeps its own fullRedraw_ for its partial-repaint logic, and without
     * this the chrome would survive a rotation laid out against the old
     * axis. */
    if (fullRedraw_ || needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        tft.drawString(String("Score ") + score_, 10, 36, 2);
        tft.setTextDatum(TR_DATUM);
        tft.drawString(String("Best ") + bestScore_, f.w - 10, TOP_BAR_HEIGHT + 6, 2);
        for (uint8_t i = 0; i < 4; ++i) {
            const bool lit = (litPad_ == i) ||
                             (phase_ == Phase::Failed && i == sequence_[inputIndex_]);
            drawPad(tft, f, i, lit);
            litDrawn_[i] = lit;
        }
        statusDrawn_ = "";
        fullRedraw_ = false;
    } else {
        // Only the pads that actually changed state get touched.
        for (uint8_t i = 0; i < 4; ++i) {
            const bool lit = (litPad_ == i) ||
                             (phase_ == Phase::Failed && i == sequence_[inputIndex_]);
            if (lit != litDrawn_[i]) {
                drawPad(tft, f, i, lit);
                litDrawn_[i] = lit;
            }
        }
    }

    String status;
    if (phase_ == Phase::Showing)      status = "Watch";
    else if (phase_ == Phase::Waiting) status = String("Repeat ") + (inputIndex_ + 1) + "/" + length_;
    else if (phase_ == Phase::Good)    status = "Nice";
    else                               status = "Oops - tap to retry";

    if (status != statusDrawn_) {
        // Repaint just the status strip, never the whole screen.
        const Rect strip{20, static_cast<int16_t>(TOP_BAR_HEIGHT + 22),
                         static_cast<int16_t>(f.w - 40), 18};
        tft.fillRect(strip.x, strip.y, strip.w, strip.h, Ui::bg());
        Ui::drawLabel(tft, strip, status,
                      phase_ == Phase::Failed ? Ui::error() : Ui::text(), 2, Align::Center);
        statusDrawn_ = status;
    }

    if (phase_ == Phase::Failed) {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Bright pad was next", f.cx(),
                       static_cast<int16_t>(padBand(f).y + padBand(f).h + 16), 2);
    }
    tft.setTextDatum(TL_DATUM);
    Ui::setTheme(savedTheme);
}
