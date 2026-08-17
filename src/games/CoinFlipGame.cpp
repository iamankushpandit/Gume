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

/* Fixed offsets from the top bar. The band below BAND_TOP is shared out at
 * render time, because its height depends on where the bottom edge is. */
constexpr int16_t PROMPT_Y = TOP_BAR_HEIGHT + 4;
constexpr int16_t CHOICE_ROW_Y = TOP_BAR_HEIGHT + 16;
constexpr int16_t BAND_TOP = TOP_BAR_HEIGHT + 48;
constexpr int16_t FLIP_FROM_BOTTOM = 42;
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

Rect CoinFlipGame::choiceRect(const Ui::Frame& f, uint8_t index) const {
    constexpr int16_t w = 44;
    constexpr int16_t gap = 8;
    const int16_t span = static_cast<int16_t>(CHOICES * w + (CHOICES - 1) * gap);
    const int16_t x0 = static_cast<int16_t>((f.w - span) / 2);
    return Rect{static_cast<int16_t>(x0 + index * (w + gap)), CHOICE_ROW_Y, w, 26};
}

Rect CoinFlipGame::flipRect(const Ui::Frame& f) const {
    return Ui::centreIn(Rect{0, static_cast<int16_t>(f.h - FLIP_FROM_BOTTOM), f.w, 34},
                        150, 34);
}

Rect CoinFlipGame::activeBand(const Ui::Frame& f) const {
    const int16_t bottom = static_cast<int16_t>(f.h - FLIP_FROM_BOTTOM - 2);
    return Rect{0, BAND_TOP, f.w, static_cast<int16_t>(bottom - BAND_TOP)};
}

/* 32%, then 32px and 11px up from the foot of the band. In landscape that is
 * 116, 166 and 187 -- the numbers this screen was drawn with -- and in
 * portrait the same three spread down the extra 80px instead of leaving it
 * blank under the verdict. */
int16_t CoinFlipGame::coinCentreY(const Ui::Frame& f) const {
    const Rect band = activeBand(f);
    return static_cast<int16_t>(band.y + band.h * 32 / 100);
}

int16_t CoinFlipGame::pipRowY(const Ui::Frame& f) const {
    const Rect band = activeBand(f);
    return static_cast<int16_t>(band.y + band.h - 32);
}

int16_t CoinFlipGame::verdictY(const Ui::Frame& f) const {
    const Rect band = activeBand(f);
    return static_cast<int16_t>(band.y + band.h - 11);
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
                host.beepOk();
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

    const Ui::Frame f = Ui::frame(host.display());
    for (uint8_t i = 0; i < CHOICES; ++i) {
        if (!choiceRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        if (i != choice_) {
            choice_ = i;
            revealed_ = 0;
            finished_ = false;
            host.beepOk();
            markFullDirty();
        }
        return;
    }

    if (flipRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        revealed_ = 0;
        finished_ = false;
        busy_ = true;
        spinning_ = true;
        spinFrame_ = 0;
        phaseUntilMs_ = now + SPIN_MS;
        nextSpinStepMs_ = 0;
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
    const Ui::Frame f = Ui::frame(tft);
    const uint8_t flips = flipsFor(choice_);
    const int16_t coinCx = f.cx();
    const int16_t coinCy = coinCentreY(f);
    const int16_t pipY = pipRowY(f);

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Best of how many?", f.cx(), PROMPT_Y, 1);

        for (uint8_t i = 0; i < CHOICES; ++i) {
            const bool on = (i == choice_);
            char label[2] = {static_cast<char>('0' + flipsFor(i)), '\0'};
            Ui::drawButton(tft, choiceRect(f, i), label,
                           on ? Ui::rgb(36, 132, 204) : Ui::panel(),
                           Ui::outline(), on ? TFT_WHITE : Ui::text(), false, 2);
        }

        Ui::drawButton(tft, flipRect(f), "Flip", Ui::rgb(45, 154, 96),
                       Ui::outline(), TFT_WHITE, false, 4);
    }

    const Rect band = activeBand(f);
    tft.fillRect(band.x, band.y, band.w, band.h, Ui::bg());

    // ---- the coin ----
    if (busy_ && spinning_) {
        // |cos| of a steadily advancing angle: wide, edge-on, wide again.
        const float angle = static_cast<float>(spinFrame_) * 0.55f;
        const float squash = fabsf(cosf(angle));
        const int16_t halfWidth = static_cast<int16_t>(COIN_R * squash);
        const bool edgeOn = halfWidth <= COIN_R / 2;
        drawCoin(tft, coinCx, coinCy, halfWidth, (spinFrame_ % 2) == 0, edgeOn);
    } else if (revealed_ > 0) {
        drawCoin(tft, coinCx, coinCy, COIN_R, heads_[revealed_ - 1], false);
    } else {
        drawCoin(tft, coinCx, coinCy, COIN_R, true, false);
    }

    // ---- one pip per flip in the round, filled as each lands ----
    const int16_t pitch = 28;
    const int16_t span = static_cast<int16_t>((flips - 1) * pitch);
    const int16_t px0 = static_cast<int16_t>((f.w - span) / 2);
    tft.setTextDatum(MC_DATUM);
    for (uint8_t i = 0; i < flips; ++i) {
        const int16_t px = static_cast<int16_t>(px0 + i * pitch);
        if (i < revealed_) {
            tft.fillCircle(px, pipY, 10, coinFace());
            tft.drawCircle(px, pipY, 10, coinEdge());
            tft.setTextColor(coinEdge(), coinFace());
            tft.drawString(heads_[i] ? "H" : "T", px, pipY, 1);
        } else {
            tft.drawCircle(px, pipY, 10, Ui::outline());
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
    tft.drawString(line, f.cx(), verdictY(f), 2);
    tft.setTextDatum(TL_DATUM);
}
