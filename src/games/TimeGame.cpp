#include "TimeGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t CLOCK_FACE = 0xF7BE;
constexpr uint16_t HAND = 0x0843;
constexpr uint16_t MINUTE_HAND = 0xE8E4;

/* The dial, at file scope because two functions need to agree about where it
 * is. drawClock() draws it; renderDynamic() has to clear the score header
 * without erasing it, and for a long time it did not -- see HEADER_W. */
constexpr int16_t CLOCK_CX = GAME_CANVAS_WIDTH / 2;
constexpr int16_t CLOCK_CY = 86;
constexpr int16_t CLOCK_R = 43;
/* drawClock() fills one ring wider than the face, to cover the previous
 * hands. That ring is what the header has to stay clear of, not the face. */
constexpr int16_t CLOCK_OUTER = CLOCK_R + 4;

/* The score header: "Level/Score" top left, "Streak/Best" top right, each
 * cleared to background before being rewritten.
 *
 * The width is derived, and this is the whole bug. Both strips were a
 * hand-typed 140px wide -- far wider than the text needs -- so they reached
 * x=150 and x=170 while the dial spans 113..207. Every time the score changed,
 * 37px was erased off each shoulder of the clock, leaving the face as a narrow
 * vertical strip with two square bites out of it. renderDynamic() draws the
 * clock first and the header second, so this happened on the very first frame
 * and on every board; the 4-inch panel made it 48px a side and unmissable.
 *
 * Deriving the width from the dial means the two cannot drift apart again. If
 * a longer score ever needs more room, the dial has to give it up explicitly
 * rather than by being quietly painted over. */
constexpr int16_t HEADER_MARGIN = 10;
constexpr int16_t HEADER_Y = 33;
constexpr int16_t HEADER_H = 36;
/* A few pixels of daylight rather than exact adjacency. The two are scaled
 * independently on a panel that is not the canvas size -- positions by the
 * axis scale, the dial's radius by the smaller of the two -- and each rounds
 * on its own. Touching exactly would be correct arithmetic that one rounding
 * change turns back into the bug above. */
constexpr int16_t HEADER_GAP = 3;
constexpr int16_t HEADER_W = CLOCK_CX - CLOCK_OUTER - HEADER_MARGIN - HEADER_GAP;
static_assert(HEADER_W > 90,
              "The score header needs room for 'Streak 0' at font 2. If the "
              "dial has grown enough to squeeze it, move the dial down or "
              "shrink it -- do not let the header clear over it again.");

constexpr AppScoreInfo TIME_GAME_SCORE = {
    "time", "Time", "timeBest", "pts", false
};

constexpr AppMetadata TIME_GAME_METADATA = {
    "time",
    "Time",
    nullptr,
    "read clocks",
    "Time",
    "Read the clock and pick the time.",
    &TIME_GAME_SCORE,
    LauncherIcon::Time,
    11,
    true,
};
}

const AppMetadata& timeGameAppMetadata() {
    return TIME_GAME_METADATA;
}

const char* TimeGame::title() const {
    return timeGameAppMetadata().title;
}

void TimeGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    bestStreak_ = static_cast<uint16_t>(host.getScore(timeGameAppMetadata().score->bestKey, 0));
    newQuestion();
    markDirty();
}

uint8_t TimeGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 4);
}

Rect TimeGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152), static_cast<int16_t>(152 + row * 40), 132, 34};
}

String TimeGame::formatTime(uint16_t minutes) const {
    minutes %= 12 * 60;
    uint8_t hour = minutes / 60;
    const uint8_t minute = minutes % 60;
    if (hour == 0) {
        hour = 12;
    }
    char buffer[8];
    snprintf(buffer, sizeof(buffer), "%u:%02u", hour, minute);
    return String(buffer);
}

bool TimeGame::optionExists(uint16_t minutes, uint8_t upTo) const {
    for (uint8_t i = 0; i < upTo; ++i) {
        if (options_[i] == minutes) {
            return true;
        }
    }
    return false;
}

void TimeGame::newQuestion() {
    /* New hands on the clock. drawClock() opens with a fillCircle covering the
     * whole face, so the old hands cannot survive it -- which is what lets the
     * clock be dynamic and a new question cost a repaint of the face rather
     * than of the screen.
     *
     * The buttons are not optional here: the loop repaints one only when its
     * state changed, and the two that were never highlighted go 0 -> 0, so
     * without this they would keep the previous question's times on them. */
    markDirty();
    drawnClock_ = false;
    drawnHeader_ = false;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    selected_ = -1;
    answered_ = false;

    const uint8_t currentLevel = level();
    const uint8_t hour = random(12);
    uint8_t minute = 0;
    if (currentLevel == 1) {
        minute = 0;
    } else if (currentLevel == 2) {
        minute = random(2) == 0 ? 0 : 30;
    } else if (currentLevel == 3) {
        minute = random(4) * 15;
    } else if (currentLevel == 4) {
        minute = random(12) * 5;
    } else {
        minute = random(60);
    }
    answerMinutes_ = static_cast<uint16_t>(hour * 60 + minute);
    makeOptions();
}

void TimeGame::makeOptions() {
    correctButton_ = random(4);
    for (uint8_t i = 0; i < 4; ++i) {
        options_[i] = 0;
    }
    options_[correctButton_] = answerMinutes_;

    const uint8_t currentLevel = level();
    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) {
            continue;
        }

        uint16_t candidate = answerMinutes_;
        uint8_t attempts = 0;
        while ((candidate == answerMinutes_ || optionExists(candidate, i)) && attempts < 50) {
            int16_t delta = 60;
            if (currentLevel <= 2) {
                delta = static_cast<int16_t>((random(5) - 2) * 60);
                if (delta == 0) {
                    delta = 60;
                }
            } else if (currentLevel == 3) {
                delta = static_cast<int16_t>((random(7) - 3) * 15);
                if (delta == 0) {
                    delta = 15;
                }
            } else if (currentLevel == 4) {
                delta = static_cast<int16_t>((random(9) - 4) * 5);
                if (delta == 0) {
                    delta = 5;
                }
            } else {
                delta = static_cast<int16_t>(random(-18, 19));
                if (delta == 0) {
                    delta = 1;
                }
            }
            candidate = static_cast<uint16_t>((static_cast<int16_t>(answerMinutes_) + delta + 12 * 60) % (12 * 60));
            ++attempts;
        }
        while (candidate == answerMinutes_ || optionExists(candidate, i)) {
            candidate = static_cast<uint16_t>((candidate + 5) % (12 * 60));
        }
        options_[i] = candidate;
    }
}

void TimeGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            selected_ = i;
            answered_ = true;
            if (i == correctButton_) {
                ++score_;
                ++streak_;
                if (host.saveBestScore(timeGameAppMetadata().score->bestKey, streak_, false)) {
                    bestStreak_ = streak_;
                }
                host.beepOk();
            } else {
                streak_ = 0;
                host.beepError();
            }
            markDirty();
            return;
        }
    }
}

void TimeGame::drawClock(Ui::Renderer& tft) const {
    /* The vertical budget on this screen is tight and was overdrawn.
     *
     * The answer buttons start at y=152 and the question label is 16px tall, so
     * everything the clock occupies has to end by 136. At cy=91 with radius 49
     * the outer fill reached y=144 and printed the top of "Which time is
     * shown?" out of existence -- worst at the horizontal centre, where a
     * circle reaches lowest and the centred sentence has its middle. On the
     * 4-inch panel it swallowed the line almost whole.
     *
     * cy=86 with radius 43 puts the outer fill at 39..133, clear of the label
     * at 136 with three rows to spare. Everything drawn against the face --
     * numerals, tick lengths, hands -- is proportional to the radius and moved
     * with it; if you change one, change them all, or the hands leave the
     * dial. */
    constexpr int16_t cx = CLOCK_CX;
    constexpr int16_t cy = CLOCK_CY;
    constexpr int16_t radius = CLOCK_R;
    constexpr int16_t numeralInset = 32;   // was 36 at radius 49

    tft.fillCircle(cx, cy, CLOCK_OUTER, Ui::surface());
    tft.fillCircle(cx, cy, radius, CLOCK_FACE);
    tft.drawCircle(cx, cy, radius, Ui::outline());

    tft.setTextColor(HAND, CLOCK_FACE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("12", cx, cy - numeralInset, 2);
    tft.drawString("3", cx + numeralInset, cy, 2);
    tft.drawString("6", cx, cy + numeralInset, 2);
    tft.drawString("9", cx - numeralInset, cy, 2);

    for (uint8_t i = 0; i < 12; ++i) {
        const float angle = -PI / 2.0f + i * PI / 6.0f;
        const int16_t x1 = cx + static_cast<int16_t>(cosf(angle) * (radius - 8));
        const int16_t y1 = cy + static_cast<int16_t>(sinf(angle) * (radius - 8));
        const int16_t x2 = cx + static_cast<int16_t>(cosf(angle) * (radius - 3));
        const int16_t y2 = cy + static_cast<int16_t>(sinf(angle) * (radius - 3));
        tft.drawLine(x1, y1, x2, y2, HAND);
    }

    const uint8_t hour = answerMinutes_ / 60;
    const uint8_t minute = answerMinutes_ % 60;
    const float hourAngle = -PI / 2.0f + (hour + minute / 60.0f) * PI / 6.0f;
    const float minuteAngle = -PI / 2.0f + minute * PI / 30.0f;
    const int16_t hx = cx + static_cast<int16_t>(cosf(hourAngle) * 22);
    const int16_t hy = cy + static_cast<int16_t>(sinf(hourAngle) * 22);
    const int16_t mx = cx + static_cast<int16_t>(cosf(minuteAngle) * 32);
    const int16_t my = cy + static_cast<int16_t>(sinf(minuteAngle) * 32);
    tft.drawLine(cx, cy, hx, hy, HAND);
    tft.drawLine(cx + 1, cy, hx + 1, hy, HAND);
    tft.drawLine(cx, cy, mx, my, MINUTE_HAND);
    tft.fillCircle(cx, cy, 4, HAND);
    tft.setTextDatum(TL_DATUM);
}

void TimeGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    Ui::drawLabel(tft, Rect{8, 136, 304, 16}, "Which time is shown?",
                  Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnAnswered_ = !answered_;
    drawnHeader_ = false;
    drawnClock_ = false;
}

void TimeGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    /* The clock face is the question: a circle, twelve ticks and two hands.
     * None of it moves while the player is choosing, so it is gated -- but a
     * new question moves the hands, and that must not cost a full repaint.
     * drawClock() fills the whole face before drawing anything on it, so the
     * previous hands are covered rather than left behind. */
    if (!drawnClock_) {
        drawClock(tft);
        drawnClock_ = true;
    }

    /* THIS SCREEN CANNOT REPAINT JUST THE TWO BUTTONS THAT CHANGED.
     *
     * Its prompt is drawn at y=226 and answerRect() puts the lower row at
     * 192..226, so the two overlap by eight rows. Clearing the prompt strip
     * therefore takes the bottom off the lower buttons -- including the two
     * whose colour did not change and which nothing would put back.
     *
     * So when the prompt changes, every button is invalidated and the order is
     * fixed: clear the strip, repaint all four buttons over it, then write the
     * prompt on top, which is the layering the screen already had. It still
     * leaves the clock face alone, which is the win worth having here. */
    const bool answerChanged = (answered_ != drawnAnswered_);
    if (answerChanged) {
        tft.fillRect(20, 216, GAME_CANVAS_WIDTH - 40, 20, Ui::bg());
        for (uint8_t i = 0; i < 4; ++i) {
            drawnButton_[i] = 0xFF;
        }
    }

    if (!drawnHeader_ || score_ != drawnScore_ || streak_ != drawnStreak_) {
        tft.fillRect(HEADER_MARGIN, HEADER_Y, HEADER_W, HEADER_H, Ui::bg());
        tft.fillRect(static_cast<int16_t>(GAME_CANVAS_WIDTH - HEADER_MARGIN - HEADER_W),
                     HEADER_Y, HEADER_W, HEADER_H, Ui::bg());
        char buf[20];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", level());
        tft.drawString(buf, 10, 35, 2);
        snprintf(buf, sizeof(buf), "Score %u", score_);
        tft.drawString(buf, 10, 52, 2);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Streak %u", streak_);
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 35, 2);
        snprintf(buf, sizeof(buf), "Best %u", bestStreak_);
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 52, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnHeader_ = true;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (answered_) {
            if (i == correctButton_) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t text = state == 0 ? TFT_WHITE : TFT_BLACK;
        Ui::drawButton(tft, answerRect(i), formatTime(options_[i]), fill,
                       Ui::outline(), text, false, 2);
        drawnButton_[i] = state;
    }

    if (answerChanged) {
        if (answered_) {
            tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
            tft.setTextDatum(MC_DATUM);
            tft.drawString(selected_ == correctButton_ ? "Correct - tap for next"
                                                       : "Try the green time next",
                           GAME_CANVAS_WIDTH / 2, 226, 2);
            tft.setTextDatum(TL_DATUM);
        }
        drawnAnswered_ = answered_;
    }
}
