#include "TimeGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t CLOCK_FACE = 0xF7BE;
constexpr uint16_t HAND = 0x0843;
constexpr uint16_t MINUTE_HAND = 0xE8E4;

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
    4,
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
    constexpr int16_t cx = SCREEN_WIDTH / 2;
    constexpr int16_t cy = 91;
    constexpr int16_t radius = 49;

    tft.fillCircle(cx, cy, radius + 4, Ui::surface());
    tft.fillCircle(cx, cy, radius, CLOCK_FACE);
    tft.drawCircle(cx, cy, radius, Ui::outline());

    tft.setTextColor(HAND, CLOCK_FACE);
    tft.setTextDatum(MC_DATUM);
    tft.drawString("12", cx, cy - 36, 2);
    tft.drawString("3", cx + 36, cy, 2);
    tft.drawString("6", cx, cy + 36, 2);
    tft.drawString("9", cx - 36, cy, 2);

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
    const int16_t hx = cx + static_cast<int16_t>(cosf(hourAngle) * 25);
    const int16_t hy = cy + static_cast<int16_t>(sinf(hourAngle) * 25);
    const int16_t mx = cx + static_cast<int16_t>(cosf(minuteAngle) * 36);
    const int16_t my = cy + static_cast<int16_t>(sinf(minuteAngle) * 36);
    tft.drawLine(cx, cy, hx, hy, HAND);
    tft.drawLine(cx + 1, cy, hx + 1, hy, HAND);
    tft.drawLine(cx, cy, mx, my, MINUTE_HAND);
    tft.fillCircle(cx, cy, 4, HAND);
    tft.setTextDatum(TL_DATUM);
}

void TimeGame::render(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    char leftBuf[20];
    snprintf(leftBuf, sizeof(leftBuf), "Level %u", level());
    tft.drawString(leftBuf, 10, 35, 2);
    snprintf(leftBuf, sizeof(leftBuf), "Score %u", score_);
    tft.drawString(leftBuf, 10, 52, 2);
    tft.setTextDatum(TR_DATUM);
    char rightBuf[20];
    snprintf(rightBuf, sizeof(rightBuf), "Streak %u", streak_);
    tft.drawString(rightBuf, SCREEN_WIDTH - 10, 35, 2);
    snprintf(rightBuf, sizeof(rightBuf), "Best %u", bestStreak_);
    tft.drawString(rightBuf, SCREEN_WIDTH - 10, 52, 2);

    drawClock(tft);
    Ui::drawLabel(tft, Rect{8, 133, 304, 16}, "Which time is shown?", Ui::text(), 2, Align::Center);

    for (uint8_t i = 0; i < 4; ++i) {
        uint16_t fill = BLUE;
        uint16_t text = TFT_WHITE;
        if (answered_) {
            if (i == correctButton_) {
                fill = GREEN;
                text = TFT_BLACK;
            } else if (i == selected_) {
                fill = RED;
                text = TFT_BLACK;
            }
        }
        Ui::drawButton(tft, answerRect(i), formatTime(options_[i]), fill, Ui::outline(), text, false, 2);
    }

    if (answered_) {
        tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
        tft.setTextDatum(MC_DATUM);
        tft.drawString(selected_ == correctButton_ ? "Correct - tap for next" : "Try the green time next", SCREEN_WIDTH / 2, 226, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
