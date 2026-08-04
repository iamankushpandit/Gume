#include <Arduino.h>
#include <esp_system.h>
#include "engine/Game.h"
#include "games/AboutGame.h"
#include "games/CountingGame.h"
#include "games/ColorMixGame.h"
#include "games/FractionGame.h"
#include "games/MazeGame.h"
#include "games/MathGame.h"
#include "games/MemoryGame.h"
#include "games/MoneyGame.h"
#include "games/MultiplicationGame.h"
#include "games/OddOneOutGame.h"
#include "games/ShapeColorGame.h"
#include "games/SimonGame.h"
#include "games/SlidingPuzzleGame.h"
#include "games/SortGame.h"
#include "games/SudokuGame.h"
#include "games/TimeGame.h"
#include "games/TicTacToeGame.h"
#include "games/WhackAMoleGame.h"
#include "hal/Board.h"
#include "hal/Clock.h"
#include "ui/Ui.h"

namespace {
constexpr Rect HOME_BUTTON{0, 0, 44, TOP_BAR_HEIGHT};

#ifndef CYD_BRINGUP_ONLY
Rect launcherTileRect(uint8_t slot) {
    const uint8_t col = slot % 2;
    const uint8_t row = slot / 2;
    return Rect{static_cast<int16_t>(10 + col * 155), static_cast<int16_t>(42 + row * 56), 145, 48};
}
#endif
}

#ifdef CYD_BRINGUP_ONLY

Board board;
bool redraw = true;

void drawBringup(const TouchPoint& touch = TouchPoint{}) {
    TFT_eSPI& tft = board.display();
    Ui::clear(tft);
    tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_HEIGHT, Ui::surface());
    tft.setTextColor(TFT_WHITE, Ui::surface());
    tft.setTextDatum(ML_DATUM);
    tft.drawString("Hello CYD", 10, TOP_BAR_HEIGHT / 2, 4);
    tft.setTextDatum(MR_DATUM);
    tft.drawString(Clock::timeText(), SCREEN_WIDTH - 6, TOP_BAR_HEIGHT / 2, 2);

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString("Display: OK", 18, 46, 4);
    tft.drawString(String("SD card: ") + (board.sdReady() ? "mounted" : "not found"), 18, 78, 4);
    tft.drawString(String("Touch: ") + (board.hasTouchCalibration() ? "calibrated" : "needs calibration"), 18, 110, 4);
    tft.drawString(String("Last touch: ") + touch.x + "," + touch.y, 18, 142, 2);
    Ui::drawButton(tft, Rect{28, 178, 124, 42}, "Calibrate", Ui::rgb(36, 132, 204), TFT_DARKGREY, TFT_WHITE);
    Ui::drawButton(tft, Rect{168, 178, 124, 42}, "SD Retry", Ui::rgb(45, 154, 96), TFT_DARKGREY, TFT_WHITE);
}

void setup() {
    board.begin();
    Clock::begin();
    if (!board.hasTouchCalibration()) {
        board.runTouchCalibration();
    }
    drawBringup();
}

void loop() {
    const TouchPoint touch = board.pollTouch();
    if (touch.justPressed) {
        if (Rect{28, 178, 124, 42}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.runTouchCalibration();
            redraw = true;
        } else if (Rect{168, 178, 124, 42}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            board.mountSd();
            redraw = true;
        }
    }
    if (touch.justPressed || touch.justReleased) {
        redraw = true;
    }
    if (redraw) {
        drawBringup(touch);
        redraw = false;
    }
    delay(20);
}

#else

class KidsPlatformApp : public GameHost {
public:
    Board& board() override {
        return board_;
    }

    ContentLoader& content() override {
        return content_;
    }

    void begin() {
        board_.begin();
        if (!board_.hasTouchCalibration()) {
            board_.runTouchCalibration();
        }
        randomSeed(static_cast<uint32_t>(esp_random()));
        content_.begin(board_);
        Clock::begin();
        lastClockMinute_ = Clock::minuteKey();
        goHome();
    }

    void loop() {
        const TouchPoint touch = board_.pollTouch();
        const uint32_t minuteNow = Clock::minuteKey();
        if (minuteNow != lastClockMinute_) {
            lastClockMinute_ = minuteNow;
            if (view_ == View::Launcher) {
                launcherDirty_ = true;
            } else if (activeGame_ != nullptr) {
                activeGame_->requestRender();
            }
        }
        if (view_ == View::Launcher) {
            handleLauncherTouch(touch);
            if (launcherDirty_) {
                renderLauncher();
                launcherDirty_ = false;
            }
        } else if (activeGame_ != nullptr) {
            if (touch.justPressed && HOME_BUTTON.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                goHome();
            } else {
                activeGame_->update(*this, touch);
            }
            if (activeGame_ != nullptr && activeGame_->needsRender()) {
                activeGame_->render(*this);
                activeGame_->clearDirty();
            }
        }
        delay(18);
    }

    void goHome() override {
        activeGame_ = nullptr;
        view_ = View::Launcher;
        content_.scan();
        launcherDirty_ = true;
    }

    void relaunchActiveGame() override {
        if (activeGame_ != nullptr) {
            activeGame_->begin(*this);
        }
    }

private:
    enum class View {
        Launcher,
        Game
    };

    enum class EntryKind : uint8_t {
        TicTacToe,
        Memory,
        Math,
        Multiplication,
        Time,
        WhackAMole,
        Simon,
        Sudoku,
        ShapeColor,
        Counting,
        Money,
        Fractions,
        Maze,
        Sort,
        ColorMix,
        SlidingPuzzle,
        OddOneOut,
        About
    };

    struct LauncherEntry {
        EntryKind kind;
        uint8_t data;
        String title;
        String subtitle;
    };

    uint8_t launcherEntryCount() const {
        return 18;
    }

    LauncherEntry launcherEntry(uint8_t index) const {
        switch (index) {
            case 0:
                return LauncherEntry{EntryKind::TicTacToe, 0, "Tic-Tac-Toe", "2 player"};
            case 1:
                return LauncherEntry{EntryKind::Memory, 0, "Memory", "match pairs"};
            case 2:
                return LauncherEntry{EntryKind::Math, 0, "Math", "add & subtract"};
            case 3:
                return LauncherEntry{EntryKind::Multiplication, 0, "Multiply", "times tables"};
            case 4:
                return LauncherEntry{EntryKind::Time, 0, "Time", "read clock"};
            case 5:
                return LauncherEntry{EntryKind::WhackAMole, 0, "Whack", "tap smiles"};
            case 6:
                return LauncherEntry{EntryKind::Simon, 0, "Simon", "repeat colors"};
            case 7:
                return LauncherEntry{EntryKind::Sudoku, 0, "Sudoku", "2x2 to 6x6"};
            case 8:
                return LauncherEntry{EntryKind::ShapeColor, 0, "Shapes", "match color"};
            case 9:
                return LauncherEntry{EntryKind::Counting, 0, "Counting", "tap number"};
            case 10:
                return LauncherEntry{EntryKind::Money, 0, "Money", "count coins"};
            case 11:
                return LauncherEntry{EntryKind::Fractions, 0, "Fractions", "pie slices"};
            case 12:
                return LauncherEntry{EntryKind::Maze, 0, "Maze", "drag dot"};
            case 13:
                return LauncherEntry{EntryKind::Sort, 0, "Sorting", "order nums"};
            case 14:
                return LauncherEntry{EntryKind::ColorMix, 0, "Color Mix", "mix colors"};
            case 15:
                return LauncherEntry{EntryKind::SlidingPuzzle, 0, "Slide", "number puzzle"};
            case 16:
                return LauncherEntry{EntryKind::OddOneOut, 0, "Odd One", "find different"};
            default:
                return LauncherEntry{EntryKind::About, 0, "About", "company info"};
        }
    }

    void handleLauncherTouch(const TouchPoint& touch) {
        if (!touch.justPressed) {
            return;
        }
        const uint8_t pages = max<uint8_t>(1, (launcherEntryCount() + 5) / 6);
        if (Rect{8, 214, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) && launcherPage_ > 0) {
            --launcherPage_;
            launcherDirty_ = true;
            return;
        }
        if (Rect{238, 214, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) && launcherPage_ + 1 < pages) {
            ++launcherPage_;
            launcherDirty_ = true;
            return;
        }

        const uint8_t start = launcherPage_ * 6;
        const uint8_t count = launcherEntryCount();
        for (uint8_t slot = 0; slot < 6; ++slot) {
            const uint8_t index = start + slot;
            if (index >= count) {
                break;
            }
            if (launcherTileRect(slot).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                launch(launcherEntry(index));
                return;
            }
        }
    }

    void launch(const LauncherEntry& entry) {
        switch (entry.kind) {
            case EntryKind::TicTacToe:
                activeGame_ = &ticTacToe_;
                break;
            case EntryKind::Memory:
                activeGame_ = &memory_;
                break;
            case EntryKind::Math:
                activeGame_ = &math_;
                break;
            case EntryKind::Multiplication:
                activeGame_ = &multiplication_;
                break;
            case EntryKind::Time:
                activeGame_ = &time_;
                break;
            case EntryKind::WhackAMole:
                activeGame_ = &whackAMole_;
                break;
            case EntryKind::Simon:
                activeGame_ = &simon_;
                break;
            case EntryKind::Sudoku:
                activeGame_ = &sudoku_;
                break;
            case EntryKind::ShapeColor:
                activeGame_ = &shapeColor_;
                break;
            case EntryKind::Counting:
                activeGame_ = &counting_;
                break;
            case EntryKind::Money:
                activeGame_ = &money_;
                break;
            case EntryKind::Fractions:
                activeGame_ = &fractions_;
                break;
            case EntryKind::Maze:
                activeGame_ = &maze_;
                break;
            case EntryKind::Sort:
                activeGame_ = &sort_;
                break;
            case EntryKind::ColorMix:
                activeGame_ = &colorMix_;
                break;
            case EntryKind::SlidingPuzzle:
                activeGame_ = &slidingPuzzle_;
                break;
            case EntryKind::OddOneOut:
                activeGame_ = &oddOneOut_;
                break;
            case EntryKind::About:
                activeGame_ = &about_;
                break;
        }
        view_ = View::Game;
        activeGame_->begin(*this);
        activeGame_->render(*this);
        activeGame_->clearDirty();
    }

    void drawLauncherIcon(TFT_eSPI& tft, const LauncherEntry& entry, const Rect& r, uint16_t fill) {
        const int16_t cx = r.x + 24;
        const int16_t cy = r.y + 22;
        switch (entry.kind) {
            case EntryKind::TicTacToe:
                tft.drawLine(cx - 10, cy - 14, cx - 10, cy + 14, TFT_WHITE);
                tft.drawLine(cx + 10, cy - 14, cx + 10, cy + 14, TFT_WHITE);
                tft.drawLine(cx - 16, cy - 5, cx + 16, cy - 5, TFT_WHITE);
                tft.drawLine(cx - 16, cy + 7, cx + 16, cy + 7, TFT_WHITE);
                break;
            case EntryKind::Memory:
                tft.fillRoundRect(cx - 14, cy - 12, 18, 24, 3, TFT_WHITE);
                tft.fillRoundRect(cx - 2, cy - 10, 18, 24, 3, Ui::rgb(255, 246, 178));
                break;
            case EntryKind::Math:
                tft.fillRoundRect(cx - 16, cy - 14, 32, 28, 4, TFT_WHITE);
                tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("+", cx - 7, cy - 5, 2);
                tft.drawString("-", cx + 8, cy + 6, 2);
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::Multiplication:
                tft.fillRoundRect(cx - 17, cy - 14, 34, 28, 4, TFT_WHITE);
                tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("x", cx, cy - 3, 2);
                tft.drawString("12", cx + 2, cy + 10, 1);
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::Time:
                tft.fillCircle(cx, cy, 16, TFT_WHITE);
                tft.drawCircle(cx, cy, 16, Ui::rgb(36, 132, 204));
                tft.drawLine(cx, cy, cx, cy - 10, Ui::rgb(36, 132, 204));
                tft.drawLine(cx, cy, cx + 9, cy + 4, Ui::rgb(222, 83, 83));
                tft.fillCircle(cx, cy, 2, Ui::rgb(36, 132, 204));
                break;
            case EntryKind::WhackAMole:
                tft.drawRect(cx - 16, cy - 16, 32, 32, TFT_WHITE);
                tft.drawLine(cx - 5, cy - 16, cx - 5, cy + 16, TFT_WHITE);
                tft.drawLine(cx + 6, cy - 16, cx + 6, cy + 16, TFT_WHITE);
                tft.drawLine(cx - 16, cy - 5, cx + 16, cy - 5, TFT_WHITE);
                tft.drawLine(cx - 16, cy + 6, cx + 16, cy + 6, TFT_WHITE);
                tft.fillCircle(cx + 11, cy - 10, 5, Ui::rgb(255, 246, 178));
                break;
            case EntryKind::Simon:
                tft.fillCircle(cx - 10, cy - 8, 7, Ui::rgb(255, 112, 112));
                tft.fillCircle(cx + 10, cy - 8, 7, Ui::rgb(94, 190, 255));
                tft.fillCircle(cx - 10, cy + 10, 7, Ui::rgb(108, 232, 148));
                tft.fillCircle(cx + 10, cy + 10, 7, Ui::rgb(255, 232, 94));
                break;
            case EntryKind::Sudoku:
                tft.fillRoundRect(cx - 16, cy - 16, 32, 32, 2, TFT_WHITE);
                tft.drawLine(cx, cy - 16, cx, cy + 16, Ui::rgb(36, 132, 204));
                tft.drawLine(cx - 16, cy, cx + 16, cy, Ui::rgb(36, 132, 204));
                tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("1", cx - 8, cy - 8, 1);
                tft.drawString("4", cx + 8, cy + 8, 1);
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::ShapeColor:
                tft.fillCircle(cx - 7, cy - 1, 10, TFT_WHITE);
                Ui::drawTriangleShape(tft, cx + 10, cy + 8, 10, Ui::rgb(255, 246, 178), true);
                break;
            case EntryKind::Counting:
                for (uint8_t i = 0; i < 5; ++i) {
                    tft.fillCircle(cx - 12 + i * 6, cy + (i % 2) * 8 - 4, 4, TFT_WHITE);
                }
                break;
            case EntryKind::Money:
                tft.fillCircle(cx - 10, cy - 2, 9, Ui::rgb(184, 96, 52));
                tft.fillCircle(cx + 6, cy + 4, 11, Ui::rgb(160, 170, 176));
                tft.setTextColor(TFT_BLACK, Ui::rgb(160, 170, 176));
                tft.setTextDatum(MC_DATUM);
                tft.drawString("5", cx + 6, cy + 4, 1);
                tft.setTextColor(TFT_WHITE, Ui::rgb(184, 96, 52));
                tft.drawString("1", cx - 10, cy - 2, 1);
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::Fractions:
                tft.fillCircle(cx, cy, 16, TFT_WHITE);
                tft.fillTriangle(cx, cy, cx, cy - 16, cx + 16, cy, Ui::rgb(255, 202, 84));
                tft.fillTriangle(cx, cy, cx + 16, cy, cx, cy + 16, Ui::rgb(255, 202, 84));
                tft.drawCircle(cx, cy, 16, Ui::rgb(36, 132, 204));
                tft.drawLine(cx, cy - 16, cx, cy + 16, Ui::rgb(36, 132, 204));
                tft.drawLine(cx - 16, cy, cx + 16, cy, Ui::rgb(36, 132, 204));
                break;
            case EntryKind::Maze:
                tft.drawRect(cx - 16, cy - 14, 32, 28, TFT_WHITE);
                tft.drawLine(cx - 6, cy - 14, cx - 6, cy + 8, TFT_WHITE);
                tft.drawLine(cx + 6, cy - 2, cx + 16, cy - 2, TFT_WHITE);
                tft.fillCircle(cx - 10, cy + 8, 4, Ui::rgb(255, 246, 178));
                break;
            case EntryKind::Sort:
                tft.setTextColor(TFT_WHITE, fill);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("1", cx - 12, cy + 8, 2);
                tft.drawString("2", cx, cy, 2);
                tft.drawString("3", cx + 12, cy - 8, 2);
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::ColorMix:
                tft.fillCircle(cx - 10, cy, 10, 0xF800);
                tft.fillCircle(cx + 10, cy, 10, 0x041F);
                tft.fillCircle(cx, cy + 10, 10, 0x9813);
                break;
            case EntryKind::SlidingPuzzle:
                for (uint8_t i = 0; i < 4; ++i) {
                    const int16_t ox = (i % 2) * 16 - 16;
                    const int16_t oy = (i / 2) * 16 - 16;
                    if (i < 3) {
                        tft.fillRoundRect(cx + ox, cy + oy, 14, 14, 2, TFT_WHITE);
                    }
                }
                break;
            case EntryKind::OddOneOut:
                for (uint8_t i = 0; i < 5; ++i) {
                    tft.fillCircle(cx - 16 + i * 8, cy, 4, i == 3 ? Ui::rgb(255, 246, 178) : TFT_WHITE);
                }
                break;
            case EntryKind::About:
                tft.fillCircle(cx, cy, 16, TFT_WHITE);
                tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("i", cx, cy + 1, 4);
                tft.setTextDatum(TL_DATUM);
                break;
        }
    }

    void renderLauncher() {
        TFT_eSPI& tft = board_.display();
        Ui::clear(tft);
        tft.fillRect(0, 0, SCREEN_WIDTH, 34, Ui::surface());
        tft.setTextColor(TFT_WHITE, Ui::surface());
        tft.setTextDatum(ML_DATUM);
        tft.drawString("GoodTime Kids!", 12, 17, 4);
        tft.setTextDatum(MR_DATUM);
        tft.drawString(Clock::timeText(), SCREEN_WIDTH - 6, 9, 2);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.setTextDatum(TR_DATUM);
        tft.drawString("(C) GoodTime Micro™", SCREEN_WIDTH - 6, 22, 1);

        const uint8_t start = launcherPage_ * 6;
        const uint8_t total = launcherEntryCount();
        for (uint8_t slot = 0; slot < 6; ++slot) {
            const uint8_t index = start + slot;
            if (index >= total) {
                break;
            }
            const LauncherEntry entry = launcherEntry(index);
            const Rect r = launcherTileRect(slot);
            const uint16_t fill = slot % 3 == 0 ? Ui::rgb(36, 132, 204) : (slot % 3 == 1 ? Ui::rgb(45, 154, 96) : Ui::rgb(222, 83, 83));
            Ui::drawButton(tft, r, "", fill, TFT_DARKGREY, TFT_WHITE);
            drawLauncherIcon(tft, entry, r, fill);
            tft.setTextColor(TFT_WHITE, fill);
            tft.setTextDatum(ML_DATUM);
            String titleText = entry.title;
            while (titleText.length() > 2 && tft.textWidth(titleText, 2) > r.w - 54) {
                titleText.remove(titleText.length() - 1);
            }
            tft.drawString(titleText, r.x + 48, r.y + 17, 2);
            tft.setTextColor(Ui::rgb(235, 245, 255), fill);
            tft.drawString(entry.subtitle, r.x + 48, r.y + 34, 1);
        }

        const uint8_t pages = max<uint8_t>(1, (total + 5) / 6);
        if (pages > 1) {
            Ui::drawButton(tft, Rect{8, 214, 74, 24}, "Prev", launcherPage_ > 0 ? Ui::panel() : Ui::surface(), Ui::outline(), Ui::text(), false, 2);
            Ui::drawButton(tft, Rect{238, 214, 74, 24}, "Next", launcherPage_ + 1 < pages ? Ui::panel() : Ui::surface(), Ui::outline(), Ui::text(), false, 2);
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.setTextDatum(MC_DATUM);
            tft.drawString(String(launcherPage_ + 1) + "/" + pages, SCREEN_WIDTH / 2, 226, 2);
        }
        tft.setTextDatum(TL_DATUM);
    }

    Board board_;
    ContentLoader content_;
    TicTacToeGame ticTacToe_;
    MemoryGame memory_;
    MathGame math_;
    MultiplicationGame multiplication_;
    TimeGame time_;
    WhackAMoleGame whackAMole_;
    SimonGame simon_;
    SudokuGame sudoku_;
    ShapeColorGame shapeColor_;
    CountingGame counting_;
    MoneyGame money_;
    FractionGame fractions_;
    MazeGame maze_;
    SortGame sort_;
    ColorMixGame colorMix_;
    SlidingPuzzleGame slidingPuzzle_;
    OddOneOutGame oddOneOut_;
    AboutGame about_;
    Game* activeGame_ = nullptr;
    View view_ = View::Launcher;
    uint8_t launcherPage_ = 0;
    uint32_t lastClockMinute_ = 0;
    bool launcherDirty_ = true;
};

KidsPlatformApp app;

void setup() {
    app.begin();
}

void loop() {
    app.loop();
}

#endif
