#include "DiceGame.h"
#include "engine/AppRegistry.h"
#include "engine/Entropy.h"

namespace {
/* Fixed distances from the top bar; everything else is measured off the live
 * panel. These three reproduce the authored landscape rows exactly. */
constexpr int16_t PROMPT_Y = TOP_BAR_HEIGHT + 4;
constexpr int16_t COUNT_ROW_Y = TOP_BAR_HEIGHT + 16;
constexpr int16_t DICE_ROW_Y = TOP_BAR_HEIGHT + 58;

constexpr AppMetadata DICE_METADATA = {
    "dice",
    "Dice",
    nullptr,
    "roll the dice",
    "Dice",
    "Throw one, two or three dice.",
    nullptr,
    LauncherIcon::Dice,
    28,
    true,
};

// Die faces are laid out on the usual 3x3 grid; a pip is on when its bit is
// set, indexed left to right, top to bottom.
constexpr uint16_t PIP_MASK[7] = {
    0,          // unused: faces run 1..6
    0b000010000,
    0b100000001,
    0b100010001,
    0b101000101,
    0b101010101,
    0b101101101,
};
}   // namespace

const AppMetadata& diceAppMetadata() {
    return DICE_METADATA;
}

const char* DiceGame::title() const {
    return diceAppMetadata().screenTitle != nullptr
        ? diceAppMetadata().screenTitle
        : diceAppMetadata().title;
}

void DiceGame::begin(AppContext& host) {
    (void)host;
    count_ = 2;
    for (uint8_t i = 0; i < MAX_DICE; ++i) {
        faces_[i] = 1;
    }
    rolling_ = false;
    thrown_ = false;
    rollUntilMs_ = 0;
    nextTumbleMs_ = 0;
    markFullDirty();
}

Rect DiceGame::countRect(const Ui::Frame& f, uint8_t index) const {
    // Three 44px buttons on an 8px gap, centred on whatever width we have.
    constexpr int16_t w = 44;
    constexpr int16_t gap = 8;
    const int16_t span = static_cast<int16_t>(MAX_DICE * w + (MAX_DICE - 1) * gap);
    const int16_t x0 = static_cast<int16_t>((f.w - span) / 2);
    return Rect{static_cast<int16_t>(x0 + index * (w + gap)), COUNT_ROW_Y, w, 26};
}

Rect DiceGame::rollRect(const Ui::Frame& f) const {
    /* Anchored to the bottom edge rather than to y=198, which was the bottom
     * edge only while the panel was 240 tall. */
    return Ui::centreIn(Rect{0, static_cast<int16_t>(f.h - 42), f.w, 34}, 150, 34);
}

Rect DiceGame::dieRect(const Ui::Frame& f, uint8_t index) const {
    constexpr int16_t gap = 14;
    constexpr int16_t maxSize = 68;
    /* Three 68px dice on 14px gaps span 232px, which fits 320 comfortably and
     * 240 only just. Sizing off the width keeps a margin in portrait instead
     * of leaving 4px either side. */
    const int16_t room = static_cast<int16_t>(f.w - 16 - (count_ - 1) * gap);
    int16_t size = static_cast<int16_t>(room / count_);
    if (size > maxSize) {
        size = maxSize;
    }
    const int16_t span = static_cast<int16_t>(count_ * size + (count_ - 1) * gap);
    const int16_t x0 = static_cast<int16_t>((f.w - span) / 2);
    return Rect{static_cast<int16_t>(x0 + index * (size + gap)), DICE_ROW_Y, size, size};
}

int16_t DiceGame::statusBaseline(const Ui::Frame& f) const {
    const int16_t diceBottom = static_cast<int16_t>(DICE_ROW_Y + 68);
    const int16_t rollTop = static_cast<int16_t>(f.h - 42);
    return static_cast<int16_t>((diceBottom + rollTop) / 2);
}

Rect DiceGame::activeBand(const Ui::Frame& f) const {
    // The dice and the total line, but never the Roll button below them.
    const int16_t top = static_cast<int16_t>(DICE_ROW_Y - 4);
    return Rect{0, top, f.w, static_cast<int16_t>(f.h - 44 - top)};
}

/* Hardware entropy, not Arduino random(): on a dice game the draw is the
 * product, and Entropy::inRange cannot be switched to a software PRNG from
 * elsewhere in the tree the way random() could. */
void DiceGame::reroll() {
    for (uint8_t i = 0; i < count_; ++i) {
        faces_[i] = static_cast<uint8_t>(Entropy::inRange(1, 6));
    }
}

void DiceGame::update(AppContext& host, const TouchPoint& touch) {
    if (rolling_) {
        const uint32_t now = millis();
        if (now >= rollUntilMs_) {
            // The settling throw is the one that counts; every tumble before
            // it was only for show.
            reroll();
            rolling_ = false;
            host.beepOk();
            markDirty();
        } else if (now >= nextTumbleMs_) {
            nextTumbleMs_ = now + TUMBLE_STEP_MS;
            reroll();
            markDirty();
        }
        return;   // no input while the dice are in the air
    }

    if (!touch.justPressed) {
        return;
    }

    const Ui::Frame f = Ui::frame(host.display());
    for (uint8_t i = 0; i < MAX_DICE; ++i) {
        if (!countRect(f, i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        const uint8_t picked = static_cast<uint8_t>(i + 1);
        if (picked != count_) {
            count_ = picked;
            thrown_ = false;   // the old total described a different throw
            host.beepOk();
            markFullDirty();
        }
        return;
    }

    if (rollRect(f).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        rolling_ = true;
        thrown_ = true;
        rollUntilMs_ = millis() + TUMBLE_MS;
        nextTumbleMs_ = 0;
        host.pulseRgb(255, 190, 60, 200);
        markDirty();
    }
}

void DiceGame::drawDie(Ui::Renderer& tft, const Rect& r, uint8_t face) const {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, TFT_WHITE);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 10, Ui::outline());

    if (face < 1 || face > 6) {
        return;
    }
    const int16_t step = static_cast<int16_t>(r.w / 4);
    const int16_t x0 = static_cast<int16_t>(r.x + step);
    const int16_t y0 = static_cast<int16_t>(r.y + step);
    /* Pip radius follows the die: a flat 6 looked right at 68px and crowded
     * at anything smaller. */
    const int16_t pip = static_cast<int16_t>(r.w / 11 + 1);
    const uint16_t mask = PIP_MASK[face];
    for (uint8_t cell = 0; cell < 9; ++cell) {
        if ((mask & (1u << (8 - cell))) == 0) {
            continue;
        }
        tft.fillCircle(static_cast<int16_t>(x0 + (cell % 3) * step),
                       static_cast<int16_t>(y0 + (cell / 3) * step),
                       pip, TFT_BLACK);
    }
}

void DiceGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    const Ui::Frame f = Ui::frame(tft);

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("How many dice?", f.cx(), PROMPT_Y, 1);

        for (uint8_t i = 0; i < MAX_DICE; ++i) {
            const bool on = (i + 1) == count_;
            char label[2] = {static_cast<char>('1' + i), '\0'};
            Ui::drawButton(tft, countRect(f, i), label,
                           on ? Ui::rgb(36, 132, 204) : Ui::panel(),
                           Ui::outline(), on ? TFT_WHITE : Ui::text(), false, 2);
        }

        Ui::drawButton(tft, rollRect(f), "Roll", Ui::rgb(45, 154, 96),
                       Ui::outline(), TFT_WHITE, false, 4);
    }

    /* Everything below is repainted on every throw, so it is wiped first --
     * a three-dice layout leaves stale pips behind when you drop to one. */
    const Rect band = activeBand(f);
    tft.fillRect(band.x, band.y, band.w, band.h, Ui::bg());

    uint16_t total = 0;
    for (uint8_t i = 0; i < count_; ++i) {
        drawDie(tft, dieRect(f, i), faces_[i]);
        total = static_cast<uint16_t>(total + faces_[i]);
    }

    const int16_t statusY = statusBaseline(f);
    tft.setTextDatum(MC_DATUM);
    if (rolling_) {
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Rolling...", f.cx(), statusY, 4);
    } else if (thrown_) {
        char line[16];
        snprintf(line, sizeof(line), "Total %u", static_cast<unsigned>(total));
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(line, f.cx(), statusY, 4);
    } else {
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Tap Roll to throw", f.cx(), statusY, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
