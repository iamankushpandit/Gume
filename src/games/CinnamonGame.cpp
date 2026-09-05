#include "CinnamonGame.h"
#include "engine/AppRegistry.h"

namespace {
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

/* Each pad's note, in the same order as the colours: red, blue, green,
 * yellow, rising in pitch left-to-right and top-to-bottom. The mapping is
 * fixed for the life of the game and must stay that way -- the whole reason
 * this game has sound is that the sequence becomes a tune a player can hear
 * back, and a pad whose note moved between rounds would be worse than
 * silence. The four pitches themselves live in hal/Sound.h with the rest of
 * the vocabulary, so they stay in key with the cues around them. */
Sound padSound(uint8_t pad) {
    static constexpr Sound PAD_NOTE[4] = {
        Sound::Pad1, Sound::Pad2, Sound::Pad3, Sound::Pad4
    };
    return PAD_NOTE[pad % 4];
}
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
    markFullDirty();
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

Rect CinnamonGame::padRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(30 + col * 150), static_cast<int16_t>(74 + row * 66), 112, 54};
}

int8_t CinnamonGame::touchedPad(int16_t x, int16_t y) const {
    for (uint8_t i = 0; i < 4; ++i) {
        if (padRect(i).contains(x, y, TOUCH_HIT_SLOP)) {
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
            host.playSound(padSound(litPad_));
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

    const int8_t pad = touchedPad(touch.x, touch.y);
    if (pad < 0) {
        return;
    }

    litPad_ = pad;
    nextAt_ = now + 120UL;
    /* The pad's own note, on the way in as well as on the way out. This is
     * the point of the game: the sequence is a tune, and a player who can
     * hear it back is not relying on remembering four colours in order. It
     * sounds before the right/wrong test so that pressing a pad always makes
     * that pad's sound -- a wrong press that stayed silent would give the
     * answer away before the screen did. */
    host.playSound(padSound(static_cast<uint8_t>(pad)));
    if (pad == sequence_[inputIndex_]) {
        ++inputIndex_;
        if (inputIndex_ >= length_) {
            score_ = length_;
            const bool newBest =
                host.saveBestScore(cinnamonAppMetadata().score->bestKey, score_, false);
            if (newBest) {
                bestScore_ = score_;
            }
            phase_ = Phase::Good;
            litPad_ = pad;
            nextAt_ = now + 520UL;
            /* The round was repeated back correctly, so the sequence is about
             * to get one step longer. HighScore when that is a new personal
             * length, LevelUp when it is not; both land after the pad's own
             * note, which is the ordering a player expects. */
            host.pulseRgb(0, 255, 40, 450);
            host.playSound(newBest ? Sound::HighScore : Sound::LevelUp);
        }
    } else {
        phase_ = Phase::Failed;
        litPad_ = pad;
        host.pulseRgb(255, 0, 0, 600);
        host.playSound(Sound::GameOver);
        markFullDirty();   // the failure line needs a clean background
    }
    markDirty();
}

void CinnamonGame::drawPad(Ui::Renderer& tft, uint8_t index, bool lit) const {
    const Rect r = padRect(index);
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

/* CINNAMON ALWAYS RENDERS LIGHT, AND EACH HALF HAS TO SAY SO SEPARATELY.
 *
 * The theme is global state in Ui, and the two halves are two calls: on a
 * partial repaint renderStatic() does not run at all, so a save/force in one
 * and a restore in the other would leave the rest of the firmware drawing in
 * Cinnamon's theme. Each brackets its own drawing instead. */
void CinnamonGame::renderStatic(AppContext& host) {
    const Ui::Theme savedTheme = Ui::currentTheme();
    Ui::setTheme(Ui::Theme::Light);
    Ui::Renderer& tft = host.display();

    Ui::clear(tft);
    host.drawTopBar(title());
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    char buf[20];
    snprintf(buf, sizeof(buf), "Score %u", static_cast<unsigned>(score_));
    tft.drawString(buf, 10, 36, 2);
    tft.setTextDatum(TR_DATUM);
    snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(bestScore_));
    tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 36, 2);
    for (uint8_t i = 0; i < 4; ++i) {
        const bool lit = (litPad_ == i) ||
                         (phase_ == Phase::Failed && i == sequence_[inputIndex_]);
        drawPad(tft, i, lit);
        litDrawn_[i] = lit;
    }
    statusDrawn_ = "";
    tft.setTextDatum(TL_DATUM);
    Ui::setTheme(savedTheme);
}

void CinnamonGame::renderDynamic(AppContext& host) {
    const Ui::Theme savedTheme = Ui::currentTheme();
    Ui::setTheme(Ui::Theme::Light);
    Ui::Renderer& tft = host.display();

    /* Only the pads that actually changed state get touched -- and on a full
     * paint renderStatic() has just set litDrawn_[] to what it drew, so this
     * loop correctly finds nothing to do. */
    for (uint8_t i = 0; i < 4; ++i) {
        const bool lit = (litPad_ == i) ||
                         (phase_ == Phase::Failed && i == sequence_[inputIndex_]);
        if (lit != litDrawn_[i]) {
            drawPad(tft, i, lit);
            litDrawn_[i] = lit;
        }
    }

    String status;
    if (phase_ == Phase::Showing)      status = "Watch";
    else if (phase_ == Phase::Waiting) status = String("Repeat ") + (inputIndex_ + 1) + "/" + length_;
    else if (phase_ == Phase::Good)    status = "Nice";
    else                               status = "Oops - tap to retry";

    if (status != statusDrawn_) {
        // Repaint just the status strip, never the whole screen.
        tft.fillRect(20, 52, 280, 18, Ui::bg());
        Ui::drawLabel(tft, Rect{20, 52, 280, 18}, status,
                      phase_ == Phase::Failed ? Ui::error() : Ui::text(), 2, Align::Center);
        statusDrawn_ = status;
    }

    if (phase_ == Phase::Failed) {
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Bright pad was next", GAME_CANVAS_WIDTH / 2, 214, 2);
    }
    tft.setTextDatum(TL_DATUM);
    Ui::setTheme(savedTheme);
}
