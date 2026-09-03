#include "CoinFlipGame.h"
#include "engine/AppRegistry.h"
#include "engine/Entropy.h"

#include <math.h>

namespace {
constexpr AppMetadata COIN_FLIP_METADATA = {
    "coinflip",
    "Coin Flip",
    nullptr,
    "heads or tails",
    "Coin Flip",
    "Spin a coin, best of 1, 3 or 5.",
    nullptr,
    LauncherIcon::CoinFlip,
    29,
    true,
};

constexpr int16_t COIN_CX = SCREEN_WIDTH / 2;
constexpr int16_t COIN_CY = 116;
constexpr int16_t COIN_R = 34;

uint16_t coinFace() { return Ui::rgb(226, 182, 72); }
uint16_t coinEdge() { return Ui::rgb(150, 116, 34); }
}   // namespace

const AppMetadata& coinFlipAppMetadata() {
    return COIN_FLIP_METADATA;
}

const char* CoinFlipGame::title() const {
    return coinFlipAppMetadata().screenTitle != nullptr
        ? coinFlipAppMetadata().screenTitle
        : coinFlipAppMetadata().title;
}

void CoinFlipGame::begin(AppContext& host) {
    (void)host;
    choice_ = 1;
    for (uint8_t i = 0; i < MAX_FLIPS; ++i) {
        heads_[i] = false;
    }
    revealed_ = 0;
    busy_ = false;
    spinning_ = false;
    finished_ = false;
    spinFrame_ = 0;
    phaseUntilMs_ = 0;
    nextSpinStepMs_ = 0;
    markFullDirty();
}

Rect CoinFlipGame::choiceRect(uint8_t index) const {
    return Rect{static_cast<int16_t>(86 + index * 52), 46, 44, 26};
}

Rect CoinFlipGame::flipRect() const {
    return Rect{85, 198, 150, 34};
}

Rect CoinFlipGame::activeBand() const {
    return Rect{0, 78, SCREEN_WIDTH, 118};
}

uint8_t CoinFlipGame::headsCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < revealed_; ++i) {
        if (heads_[i]) {
            ++n;
        }
    }
    return n;
}

void CoinFlipGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if (busy_) {
        if (spinning_) {
            if (now >= phaseUntilMs_) {
                // Settle this coin. Only this draw decides the result; the
                // spin frames before it carry no state.
                // A fair coin from hardware entropy -- see engine/Entropy.h.
                heads_[revealed_] = Entropy::coin();
                ++revealed_;
                spinning_ = false;
                phaseUntilMs_ = now + HOLD_MS;
                /* A coin coming down. Reveal, not the "correct answer" beep
                 * -- nothing here is right or wrong, a hidden thing has just
                 * become visible, and with five coins in a row the old two
                 * rising notes five times over sounded like five right
                 * answers to a question nobody asked. */
                host.playSound(Sound::Reveal);
                markDirty();
            } else if (now >= nextSpinStepMs_) {
                nextSpinStepMs_ = now + SPIN_STEP_MS;
                ++spinFrame_;
                markDirty();
            }
        } else if (now >= phaseUntilMs_) {
            if (revealed_ >= flipsFor(choice_)) {
                busy_ = false;
                finished_ = true;
                host.pulseRgb(90, 200, 120, 300);
            } else {
                spinning_ = true;
                spinFrame_ = 0;
                phaseUntilMs_ = now + SPIN_MS;
                nextSpinStepMs_ = 0;
            }
            markDirty();
        }
        return;   // no input mid-sequence
    }

    if (!touch.justPressed) {
        return;
    }

    for (uint8_t i = 0; i < CHOICES; ++i) {
        if (!choiceRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        if (i != choice_) {
            choice_ = i;
            revealed_ = 0;
            finished_ = false;
            host.playSound(Sound::Select);
            markFullDirty();
        }
        return;
    }

    if (flipRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        revealed_ = 0;
        finished_ = false;
        busy_ = true;
        spinning_ = true;
        spinFrame_ = 0;
        phaseUntilMs_ = now + SPIN_MS;
        nextSpinStepMs_ = 0;
        /* The throw. It is the one moment the coin is in the air and the
         * screen is showing frames that carry no state, so it is also the
         * only thing here worth a noise of its own. */
        host.playSound(Sound::Whoosh);
        markDirty();
    }
}

/* Draw the coin as an ellipse squashed to `halfWidth`, which is how a spinning
 * coin reads without any 3D: full width face-on, a bright line edge-on. There
 * is no fillEllipse in the renderer, so it is filled as vertical chords. */
void CoinFlipGame::drawCoin(Ui::Renderer& tft, int16_t cx, int16_t cy,
                            int16_t halfWidth, bool heads, bool edgeOn) const {
    if (halfWidth < 1) {
        halfWidth = 1;
    }
    for (int16_t dx = -halfWidth; dx <= halfWidth; ++dx) {
        const float ratio = static_cast<float>(dx) / static_cast<float>(halfWidth);
        const float scale = sqrtf(1.0f - ratio * ratio);
        const int16_t halfH = static_cast<int16_t>(COIN_R * scale);
        if (halfH <= 0) {
            continue;
        }
        const bool rim = (dx == -halfWidth || dx == halfWidth);
        tft.drawFastVLine(cx + dx, cy - halfH, static_cast<int16_t>(2 * halfH + 1),
                          rim ? coinEdge() : coinFace());
    }
    tft.drawFastVLine(cx - halfWidth, cy - 4, 9, coinEdge());
    tft.drawFastVLine(cx + halfWidth, cy - 4, 9, coinEdge());

    // Only label the coin when there is a face wide enough to hold a letter.
    if (!edgeOn && halfWidth > COIN_R / 2) {
        tft.setTextColor(coinEdge(), coinFace());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(heads ? "H" : "T", cx, cy, 4);
        tft.setTextDatum(TL_DATUM);
    }
}

void CoinFlipGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const uint8_t flips = flipsFor(choice_);

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Best of how many?", SCREEN_WIDTH / 2, 34, 1);

        for (uint8_t i = 0; i < CHOICES; ++i) {
            const bool on = (i == choice_);
            char label[2] = {static_cast<char>('0' + flipsFor(i)), '\0'};
            Ui::drawButton(tft, choiceRect(i), label,
                           on ? Ui::rgb(36, 132, 204) : Ui::panel(),
                           Ui::outline(), on ? TFT_WHITE : Ui::text(), false, 2);
        }

        Ui::drawButton(tft, flipRect(), "Flip", Ui::rgb(45, 154, 96),
                       Ui::outline(), TFT_WHITE, false, 4);
    }

    const Rect band = activeBand();
    tft.fillRect(band.x, band.y, band.w, band.h, Ui::bg());

    // ---- the coin ----
    if (busy_ && spinning_) {
        // |cos| of a steadily advancing angle: wide, edge-on, wide again.
        const float angle = static_cast<float>(spinFrame_) * 0.55f;
        const float squash = fabsf(cosf(angle));
        const int16_t halfWidth = static_cast<int16_t>(COIN_R * squash);
        const bool edgeOn = halfWidth <= COIN_R / 2;
        drawCoin(tft, COIN_CX, COIN_CY, halfWidth, (spinFrame_ % 2) == 0, edgeOn);
    } else if (revealed_ > 0) {
        drawCoin(tft, COIN_CX, COIN_CY, COIN_R, heads_[revealed_ - 1], false);
    } else {
        drawCoin(tft, COIN_CX, COIN_CY, COIN_R, true, false);
    }

    // ---- one pip per flip in the round, filled as each lands ----
    const int16_t pitch = 28;
    const int16_t span = static_cast<int16_t>((flips - 1) * pitch);
    const int16_t px0 = static_cast<int16_t>((SCREEN_WIDTH - span) / 2);
    tft.setTextDatum(MC_DATUM);
    for (uint8_t i = 0; i < flips; ++i) {
        const int16_t px = static_cast<int16_t>(px0 + i * pitch);
        if (i < revealed_) {
            tft.fillCircle(px, 166, 10, coinFace());
            tft.drawCircle(px, 166, 10, coinEdge());
            tft.setTextColor(coinEdge(), coinFace());
            tft.drawString(heads_[i] ? "H" : "T", px, 166, 1);
        } else {
            tft.drawCircle(px, 166, 10, Ui::outline());
        }
    }

    // ---- verdict ----
    char line[28];
    const uint8_t heads = headsCount();
    const uint8_t tails = static_cast<uint8_t>(revealed_ - heads);
    if (busy_) {
        snprintf(line, sizeof(line), "Spin %u of %u",
                 static_cast<unsigned>(revealed_ + (spinning_ ? 1 : 0)),
                 static_cast<unsigned>(flips));
        tft.setTextColor(Ui::muted(), Ui::bg());
    } else if (finished_) {
        if (flips == 1) {
            snprintf(line, sizeof(line), "%s!", heads > 0 ? "Heads" : "Tails");
        } else {
            snprintf(line, sizeof(line), "%s wins %u-%u",
                     heads > tails ? "Heads" : "Tails",
                     static_cast<unsigned>(heads > tails ? heads : tails),
                     static_cast<unsigned>(heads > tails ? tails : heads));
        }
        tft.setTextColor(Ui::text(), Ui::bg());
    } else {
        snprintf(line, sizeof(line), "Tap Flip to spin");
        tft.setTextColor(Ui::muted(), Ui::bg());
    }
    tft.drawString(line, SCREEN_WIDTH / 2, 187, 2);
    tft.setTextDatum(TL_DATUM);
}
