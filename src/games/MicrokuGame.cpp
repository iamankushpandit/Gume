#include "MicrokuGame.h"

namespace {
constexpr uint16_t GIVEN_FILL = 0x294D;
constexpr uint16_t USER_FILL = 0x18E8;
constexpr uint16_t SELECT_FILL = 0xFFE6;
constexpr uint16_t LINE = 0x4A49;
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;

struct StageDef {
    uint8_t size;
    uint8_t boxRows;
    uint8_t boxCols;
    const uint8_t* puzzle;
    const uint8_t* solution;
};

constexpr uint8_t PUZZLE_2[] = {
    1, 0,
    0, 1,
};
constexpr uint8_t SOLUTION_2[] = {
    1, 2,
    2, 1,
};

constexpr uint8_t PUZZLE_4[] = {
    1, 0, 0, 4,
    0, 4, 1, 0,
    0, 1, 4, 0,
    4, 0, 0, 1,
};
constexpr uint8_t SOLUTION_4[] = {
    1, 2, 3, 4,
    3, 4, 1, 2,
    2, 1, 4, 3,
    4, 3, 2, 1,
};

constexpr uint8_t PUZZLE_6[] = {
    1, 0, 0, 0, 5, 0,
    0, 5, 0, 1, 0, 3,
    0, 0, 4, 0, 6, 0,
    5, 0, 0, 2, 0, 0,
    0, 4, 0, 6, 0, 2,
    0, 0, 2, 0, 4, 0,
};
constexpr uint8_t SOLUTION_6[] = {
    1, 2, 3, 4, 5, 6,
    4, 5, 6, 1, 2, 3,
    2, 3, 4, 5, 6, 1,
    5, 6, 1, 2, 3, 4,
    3, 4, 5, 6, 1, 2,
    6, 1, 2, 3, 4, 5,
};

constexpr StageDef STAGES[] = {
    {2, 1, 2, PUZZLE_2, SOLUTION_2},
    {4, 2, 2, PUZZLE_4, SOLUTION_4},
    {6, 2, 3, PUZZLE_6, SOLUTION_6},
};
constexpr uint8_t STAGE_COUNT = sizeof(STAGES) / sizeof(STAGES[0]);
}

const char* MicrokuGame::title() const {
    return "Microku";
}

void MicrokuGame::begin(AppContext& host) {
    stageIndex_ = 0;
    solvedThisRun_ = 0;
    bestSize_ = static_cast<uint8_t>(host.getScore("microkuBest", 0));
    loadStage(stageIndex_);
    markDirty();
}

void MicrokuGame::loadStage(uint8_t stageIndex) {
    const StageDef& stage = STAGES[stageIndex];
    size_ = stage.size;
    boxRows_ = stage.boxRows;
    boxCols_ = stage.boxCols;
    selected_ = -1;
    solved_ = false;
    message_ = String(size_) + "x" + size_ + " puzzle";
    messageUntil_ = millis() + 1600UL;

    const uint8_t total = size_ * size_;
    for (uint8_t i = 0; i < total; ++i) {
        board_[i] = stage.puzzle[i];
        solution_[i] = stage.solution[i];
        fixed_[i] = board_[i] != 0;
    }
}

Rect MicrokuGame::cellRect(uint8_t row, uint8_t col) const {
    const int16_t cell = size_ == 6 ? 23 : (size_ == 4 ? 32 : 54);
    const int16_t gridSize = cell * size_;
    const int16_t startX = (SCREEN_WIDTH - gridSize) / 2;
    const int16_t startY = 58;
    return Rect{static_cast<int16_t>(startX + col * cell), static_cast<int16_t>(startY + row * cell), cell, cell};
}

Rect MicrokuGame::numberRect(uint8_t value) const {
    const int16_t gap = 4;
    const int16_t w = (SCREEN_WIDTH - 16 - gap * (size_ - 1)) / size_;
    return Rect{static_cast<int16_t>(8 + (value - 1) * (w + gap)), 205, w, 30};
}

int8_t MicrokuGame::touchedCell(int16_t x, int16_t y) const {
    for (uint8_t row = 0; row < size_; ++row) {
        for (uint8_t col = 0; col < size_; ++col) {
            if (cellRect(row, col).contains(x, y, TOUCH_HIT_SLOP)) {
                return static_cast<int8_t>(row * size_ + col);
            }
        }
    }
    return -1;
}

int8_t MicrokuGame::touchedNumber(int16_t x, int16_t y) const {
    for (uint8_t value = 1; value <= size_; ++value) {
        if (numberRect(value).contains(x, y, TOUCH_HIT_SLOP)) {
            return value;
        }
    }
    return -1;
}

bool MicrokuGame::complete() const {
    const uint8_t total = size_ * size_;
    for (uint8_t i = 0; i < total; ++i) {
        if (board_[i] != solution_[i]) {
            return false;
        }
    }
    return true;
}

void MicrokuGame::advanceAfterSolve(AppContext& host) {
    if (stageIndex_ + 1 < STAGE_COUNT) {
        ++stageIndex_;
        loadStage(stageIndex_);
    } else {
        stageIndex_ = 0;
        solvedThisRun_ = 0;
        loadStage(stageIndex_);
    }
    host.beepOk();
}

void MicrokuGame::update(AppContext& host, const TouchPoint& touch) {
    if (messageUntil_ > 0 && millis() > messageUntil_) {
        messageUntil_ = 0;
        markDirty();
    }

    if (!touch.justPressed) {
        return;
    }

    if (solved_) {
        advanceAfterSolve(host);
        markDirty();
        return;
    }

    const int8_t cell = touchedCell(touch.x, touch.y);
    if (cell >= 0) {
        if (!fixed_[cell]) {
            selected_ = cell;
            message_ = "Pick a number";
            messageUntil_ = millis() + 1200UL;
            markDirty();
        }
        return;
    }

    const int8_t number = touchedNumber(touch.x, touch.y);
    if (number < 0 || selected_ < 0) {
        return;
    }

    if (number == solution_[selected_]) {
        board_[selected_] = number;
        selected_ = -1;
        if (complete()) {
            solved_ = true;
            ++solvedThisRun_;
            if (host.saveBestScore("microkuBest", size_, false)) {
                bestSize_ = size_;
            }
            message_ = stageIndex_ + 1 < STAGE_COUNT ? "Solved - tap next" : "All solved - tap restart";
            messageUntil_ = 0;
        } else {
            message_ = "Correct";
            messageUntil_ = millis() + 800UL;
        }
        host.beepOk();
    } else {
        message_ = "Try again";
        messageUntil_ = millis() + 900UL;
        host.beepError();
    }
    markDirty();
}

void MicrokuGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString(String("Board ") + size_ + "x" + size_, 8, 34, 2);
    tft.setTextDatum(TR_DATUM);
    tft.drawString(bestSize_ > 0 ? String("Best ") + bestSize_ + "x" + bestSize_ : "Best --", SCREEN_WIDTH - 8, 34, 2);
    Ui::drawLabel(tft, Rect{24, 46, 272, 16}, message_, message_ == "Try again" ? RED : Ui::text(), 1, Align::Center);

    for (uint8_t row = 0; row < size_; ++row) {
        for (uint8_t col = 0; col < size_; ++col) {
            const uint8_t index = row * size_ + col;
            const Rect r = cellRect(row, col);
            const bool selected = selected_ == index;
            const uint16_t fill = selected ? SELECT_FILL : (fixed_[index] ? GIVEN_FILL : USER_FILL);
            tft.fillRect(r.x, r.y, r.w, r.h, fill);
            tft.drawRect(r.x, r.y, r.w, r.h, TFT_DARKGREY);
            if (board_[index] > 0) {
                tft.setTextColor(fixed_[index] ? Ui::text() : Ui::warning(), fill);
                tft.setTextDatum(MC_DATUM);
                tft.drawString(String(board_[index]), r.x + r.w / 2, r.y + r.h / 2, size_ == 6 ? 2 : 4);
            }
        }
    }

    const Rect first = cellRect(0, 0);
    const int16_t cell = first.w;
    const int16_t gridSize = cell * size_;
    for (uint8_t i = 0; i <= size_; ++i) {
        const bool thickCol = i % boxCols_ == 0;
        const bool thickRow = i % boxRows_ == 0;
        const int16_t x = first.x + i * cell;
        const int16_t y = first.y + i * cell;
        tft.drawFastVLine(x, first.y, gridSize, thickCol ? LINE : TFT_DARKGREY);
        if (thickCol) {
            tft.drawFastVLine(x + 1, first.y, gridSize, LINE);
        }
        tft.drawFastHLine(first.x, y, gridSize, thickRow ? LINE : TFT_DARKGREY);
        if (thickRow) {
            tft.drawFastHLine(first.x, y + 1, gridSize, LINE);
        }
    }

    for (uint8_t value = 1; value <= size_; ++value) {
        Ui::drawButton(tft, numberRect(value), String(value), solved_ ? GREEN : BLUE, TFT_DARKGREY, solved_ ? TFT_BLACK : TFT_WHITE, false, 2);
    }

    if (solved_) {
        tft.fillRoundRect(62, 93, 196, 48, 8, Ui::panel());
        tft.drawRoundRect(62, 93, 196, 48, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Microku solved", SCREEN_WIDTH / 2, 111, 4);
        tft.drawString(stageIndex_ + 1 < STAGE_COUNT ? "Tap next" : "Tap restart", SCREEN_WIDTH / 2, 134, 2);
    }
    tft.setTextDatum(TL_DATUM);
}
