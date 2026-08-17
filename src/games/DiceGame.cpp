#include "DiceGame.h"
#include "engine/AppRegistry.h"
#include "engine/Entropy.h"

namespace {
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

Rect DiceGame::countRect(uint8_t index) const {
    // Three 44px buttons on an 8px gap, centred on a 320px canvas.
    return Rect{static_cast<int16_t>(86 + index * 52), 46, 44, 26};
}

Rect DiceGame::rollRect() const {
    return Rect{85, 198, 150, 34};
}

Rect DiceGame::dieRect(uint8_t index) const {
    constexpr int16_t size = 68;
    constexpr int16_t gap = 14;
    const int16_t span = static_cast<int16_t>(count_ * size + (count_ - 1) * gap);
    const int16_t x0 = static_cast<int16_t>((SCREEN_WIDTH - span) / 2);
    return Rect{static_cast<int16_t>(x0 + index * (size + gap)), 88, size, size};
}

Rect DiceGame::activeBand() const {
    // Dice (88..156) plus the total line below them, but not the Roll button.
    return Rect{0, 84, SCREEN_WIDTH, 112};
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
            markFullDirty();
        } else if (now >= nextTumbleMs_) {
            nextTumbleMs_ = now + TUMBLE_STEP_MS;
            reroll();
            markFullDirty();
        }
        return;   // no input while the dice are in the air
    }

    if (!touch.justPressed) {
        return;
    }

    for (uint8_t i = 0; i < MAX_DICE; ++i) {
        if (!countRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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

    if (rollRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
    const uint16_t mask = PIP_MASK[face];
    for (uint8_t cell = 0; cell < 9; ++cell) {
        if ((mask & (1u << (8 - cell))) == 0) {
            continue;
        }
        tft.fillCircle(static_cast<int16_t>(x0 + (cell % 3) * step),
                       static_cast<int16_t>(y0 + (cell / 3) * step),
                       6, TFT_BLACK);
    }
}

void DiceGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());

        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.setTextDatum(TC_DATUM);
        tft.drawString("How many dice?", SCREEN_WIDTH / 2, 34, 1);

        for (uint8_t i = 0; i < MAX_DICE; ++i) {
            const bool on = (i + 1) == count_;
            char label[2] = {static_cast<char>('1' + i), '\0'};
            Ui::drawButton(tft, countRect(i), label,
                           on ? Ui::rgb(36, 132, 204) : Ui::panel(),
                           Ui::outline(), on ? TFT_WHITE : Ui::text(), false, 2);
        }

        Ui::drawButton(tft, rollRect(), "Roll", Ui::rgb(45, 154, 96),
                       Ui::outline(), TFT_WHITE, false, 4);
    }

    /* Everything below is repainted on every throw, so it is wiped first --
     * a three-dice layout leaves stale pips behind when you drop to one. */
    const Rect band = activeBand();
    tft.fillRect(band.x, band.y, band.w, band.h, Ui::bg());

    uint16_t total = 0;
    for (uint8_t i = 0; i < count_; ++i) {
        drawDie(tft, dieRect(i), faces_[i]);
        total = static_cast<uint16_t>(total + faces_[i]);
    }

    tft.setTextDatum(MC_DATUM);
    if (rolling_) {
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Rolling...", SCREEN_WIDTH / 2, 178, 4);
    } else if (thrown_) {
        char line[16];
        snprintf(line, sizeof(line), "Total %u", static_cast<unsigned>(total));
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(line, SCREEN_WIDTH / 2, 178, 4);
    } else {
        tft.setTextColor(Ui::muted(), Ui::bg());
        tft.drawString("Tap Roll to throw", SCREEN_WIDTH / 2, 178, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
