#include <Arduino.h>
#include <esp_system.h>
#include "engine/Game.h"
#include "engine/GameCatalog.h"
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
#include "games/ObjectAddGame.h"
#include "games/FingerCountGame.h"
#include "games/FlagGame.h"
#include "games/NumberLineGame.h"
#include "games/SequenceGame.h"
#include "games/ProfileGame.h"
#include "games/ScoresGame.h"
#include "games/SettingsGame.h"
#include "games/ShapeColorGame.h"
#include "games/SimonGame.h"
#include "games/SlidingPuzzleGame.h"
#include "games/SortGame.h"
#include "games/StatesGame.h"
#include "games/SudokuGame.h"
#include "games/TimeGame.h"
#include "games/TicTacToeGame.h"
#include "games/WifiGame.h"
#include "games/WhackAMoleGame.h"
#include "hal/Board.h"
#include "hal/Clock.h"
#include "ui/Ui.h"

namespace {
// Portrait rotation. 0 puts the USB connector at the bottom edge;
// rotation 2 (the previous value) put it at the top.
constexpr uint8_t CYD_PORTRAIT_ROTATION = 0;

constexpr Rect HOME_BUTTON{0, 0, 44, TOP_BAR_HEIGHT};
constexpr Rect SETTINGS_BUTTON{SCREEN_WIDTH - 34, 3, 26, 24};
constexpr Rect LAUNCHER_SETTINGS_BUTTON{SCREEN_WIDTH - 32, 18, 24, 22};

#ifndef CYD_BRINGUP_ONLY
// Portrait puts the gear under the clock, so it needs a taller header.
constexpr int16_t LAUNCHER_HEADER_H_TALL = 78;
constexpr int16_t LAUNCHER_HEADER_H_WIDE = 48;

int16_t launcherHeaderHeight(Board::LayoutMode mode) {
    return mode == Board::LayoutMode::Vertical ? LAUNCHER_HEADER_H_TALL
                                               : LAUNCHER_HEADER_H_WIDE;
}

// Gear sits beside the clock in landscape, but underneath it in portrait so
// the two never collide on the narrower 240px header.
Rect launcherGearRect(Board::LayoutMode mode, int16_t lW) {
    if (mode == Board::LayoutMode::Vertical) {
        return Rect{static_cast<int16_t>(lW - 32), 48, 26, 24};
    }
    return Rect{static_cast<int16_t>(lW - 30), 11, 26, 26};
}

Rect launcherTileRect(uint8_t slot, Board::LayoutMode mode) {
    if (mode == Board::LayoutMode::Vertical) {
        // Portrait: 2x2 large square tiles, 4 per page on a 240x320 screen.
        // Big targets for small fingers, and enough width that game names no
        // longer overflow the way they did on the old 8-per-page grid.
        const uint8_t col = slot % 2;
        const uint8_t row = slot / 2;
        return Rect{static_cast<int16_t>(8 + col * 116),
                    static_cast<int16_t>(LAUNCHER_HEADER_H_TALL + 8 + row * 104),
                    108, 96};
    } else {
        // Landscape: 2 columns of wide tiles.
        const uint8_t col = slot % 2;
        const uint8_t row = slot / 2;
        return Rect{static_cast<int16_t>(10 + col * 155), static_cast<int16_t>(52 + row * 53), 145, 46};
    }
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
        Serial.printf("[boot] rot=%d layout=%s (CYD_SCREEN_ROTATION=%d)\n",
                      (int)board_.displayRotation(),
                      board_.layoutMode() == Board::LayoutMode::Vertical ? "Vertical" : "Horizontal",
                      (int)CYD_SCREEN_ROTATION);
        Serial.printf("[boot] ntp=%d creds=%d ssid='%s' tzmin=%d\n",
                      (int)board_.ntpEnabled(), (int)board_.hasWifiCredentials(),
                      board_.wifiSsid().c_str(), (int)board_.tzOffsetMinutes());
        board_.beginTimeSync();   // Wi-Fi is used only to fetch NTP time
        lastClockMinute_ = Clock::minuteKey();
        lastActivityMs_ = millis();
        Ui::setTheme(board_.themeMode() == Board::ThemeMode::Light ? Ui::Theme::Light : Ui::Theme::Dark);
        // Ask who is playing before anything else, so scores land in the
        // right child's slot from the very first game.
        openProfiles();
    }

    void loop() {
        const TouchPoint rawTouch = board_.pollTouch();
        const uint32_t nowMs = millis();

        // Drop every event until the finger that dismissed the screen saver is
        // lifted, so it cannot also activate whatever is underneath it.
        TouchPoint touch = rawTouch;
        if (swallowTouch_) {
            if (!rawTouch.down) swallowTouch_ = false;
            touch = TouchPoint{};
        }

        board_.tickTimeSync();
        board_.tickRgb();
        
        // Reset idle timer on any touch activity
        if (rawTouch.down || rawTouch.justPressed || rawTouch.justReleased) {
            lastActivityMs_ = nowMs;
        }
        
        // Trigger screen saver after idle timeout
        if (view_ != View::ScreenSaver) {
            const uint32_t idleMs = nowMs - lastActivityMs_;
            const uint32_t timeoutMs = static_cast<uint32_t>(board_.screenSaverSeconds()) * 1000UL;
            if (timeoutMs > 0 && idleMs > timeoutMs) {
                enterScreenSaver();
            }
        }
        
        // Update clock on minute boundary
        const uint32_t minuteNow = Clock::minuteKey();
        if (minuteNow != lastClockMinute_) {
            lastClockMinute_ = minuteNow;
            if (view_ == View::Launcher) {
                launcherDirty_ = true;
            } else if (view_ == View::Game && activeGame_ != nullptr) {
                activeGame_->requestRender();
            }
        }
        
        if (view_ == View::Profiles) {
            profile_.update(*this, touch);
            if (view_ == View::Profiles && profile_.needsRender()) {
                profile_.render(*this);
                profile_.clearDirty();
            }
        } else if (view_ == View::ScreenSaver) {
            if (touch.justPressed) {
                exitScreenSaver();
            } else if (nowMs - ssav_lastFrameMs_ >= 40) { // ~25 fps
                ssav_lastFrameMs_ = nowMs;
                renderScreenSaver();
            }
        } else if (view_ == View::Launcher) {
            handleLauncherTouch(touch);
            if (launcherDirty_) {
                renderLauncher();
                launcherDirty_ = false;
            }
        } else if (activeGame_ != nullptr) {
            if (touch.justPressed && SETTINGS_BUTTON.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                openSettings();
            } else if (touch.justPressed && HOME_BUTTON.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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

    uint8_t effectiveRotation(bool landscape) {
        return landscape ? CYD_SCREEN_ROTATION : CYD_PORTRAIT_ROTATION;
    }

    void goHome() override {
        activeGame_ = nullptr;
        view_ = View::Launcher;
        clampLauncherPage();
        // Rotate display based on chosen layout mode
        board_.setDisplayRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
        content_.scan();
        launcherDirty_ = true;
    }

    void relaunchActiveGame() override {
        if (activeGame_ != nullptr) {
            activeGame_->begin(*this);
        }
    }

    void openSettings() override {
        launchKind(EntryKind::Settings);
    }

    void openWifi() override {
        launchKind(EntryKind::WiFi);
    }

    void openProfiles() override {
        // Always landscape: the picker is authored for the 320x240 canvas.
        board_.setDisplayRotation(effectiveRotation(true));
        view_ = View::Profiles;
        activeGame_ = &profile_;
        profile_.begin(*this);
        profile_.render(*this);
        profile_.clearDirty();
    }
    
    void enterScreenSaver() {
        board_.setRgbColor(0, 140, 255);   // start of the rally palette
        // Remember where we came from so we can go back there, not to the
        // launcher. Losing the open Settings screen was disorienting.
        ssavPrevView_ = view_;
        // Follow the chosen layout so the company mark is upright either way.
        board_.setDisplayRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
        view_ = View::ScreenSaver;
        screenSaverStartMs_ = millis();
        ssav_initialized_ = false;
    }
    
    void exitScreenSaver() {
        board_.setRgbColor(0, 0, 0);

        const bool backToGame = (ssavPrevView_ == View::Game && activeGame_ != nullptr);

        // Games always run landscape; only the launcher follows the Tall/Wide
        // setting. Restore whichever the destination view needs.
        board_.setDisplayRotation(
            effectiveRotation(backToGame || board_.layoutMode() != Board::LayoutMode::Vertical));

        /* setDisplayRotation() clears lastTouch_. With the finger still on the
         * glass the next poll then reports justPressed again, and that phantom
         * press landed on whatever was under the wake-up tap -- launching a
         * random game. Swallow input until the finger actually lifts. */
        swallowTouch_ = true;

        lastActivityMs_ = millis();

        if (backToGame) {
            view_ = View::Game;
            activeGame_->requestRender();
        } else {
            view_ = View::Launcher;
            clampLauncherPage();
            launcherDirty_ = true;
        }
    }

private:
    enum class View {
        Profiles,
        Launcher,
        Game,
        ScreenSaver
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
        ObjectAdd,
        FingerCount,
        Sequence,
        NumberLine,
        States,
        Scores,
        Flag,
        Settings,
        WiFi,
        About
    };

    struct LauncherEntry {
        EntryKind kind;
        uint8_t data;
        String title;
        String subtitle;
    };

    // Game IDs for visibility — must match SettingsGame::GAME_IDS order and indices 0-19
    // Ids, titles and Settings labels all come from engine/GameCatalog.cpp now,
    // so this file and SettingsGame can no longer drift apart.
    static constexpr uint8_t GAME_COUNT_TOTAL = GAME_CATALOG_COUNT;

    uint8_t launcherEntryCount() {
        uint8_t count = 4; // Scores, Settings, WiFi, About always shown
        for (uint8_t i = 0; i < GAME_COUNT_TOTAL; ++i) {
            if (board_.gameVisible(GAME_CATALOG[i].id)) ++count;
        }
        return count;
    }

    /* Switching Wide -> Tall shrinks the page size from 6 to 4, which could
     * leave launcherPage_ past the end and render an empty launcher. */
    void clampLauncherPage() {
        const uint8_t pageSize = launcherPageSize();
        const uint8_t pages = max<uint8_t>(1, (launcherEntryCount() + pageSize - 1) / pageSize);
        if (launcherPage_ >= pages) launcherPage_ = static_cast<uint8_t>(pages - 1);
    }

    uint8_t launcherPageSize() {
        // Portrait shows 4 large tiles; landscape keeps 6.
        return board_.layoutMode() == Board::LayoutMode::Vertical ? 4 : 6;
    }

    /* EntryKind in catalog order. The launcher titles and subtitles come from
     * GAME_CATALOG, so this file no longer repeats them -- only the mapping
     * from catalog slot to the concrete game object lives here. */
    static constexpr EntryKind CATALOG_KINDS[GAME_CATALOG_COUNT] = {
        EntryKind::TicTacToe, EntryKind::Memory,        EntryKind::Math,
        EntryKind::Multiplication, EntryKind::Time,     EntryKind::WhackAMole,
        EntryKind::Simon,     EntryKind::Sudoku,        EntryKind::ShapeColor,
        EntryKind::Counting,  EntryKind::Money,         EntryKind::Fractions,
        EntryKind::Maze,      EntryKind::Sort,          EntryKind::ColorMix,
        EntryKind::SlidingPuzzle, EntryKind::OddOneOut, EntryKind::ObjectAdd,
        EntryKind::FingerCount,   EntryKind::Sequence,  EntryKind::NumberLine,
        EntryKind::Flag,          EntryKind::States,
    };

    LauncherEntry allEntry(uint8_t raw) const {
        if (raw < GAME_CATALOG_COUNT) {
            return LauncherEntry{CATALOG_KINDS[raw], 0,
                                 GAME_CATALOG[raw].title, GAME_CATALOG[raw].subtitle};
        }
        switch (static_cast<uint8_t>(raw - GAME_CATALOG_COUNT)) {
            case 0:  return LauncherEntry{EntryKind::Scores,   0, "Scores",   "best & worst"};
            case 1:  return LauncherEntry{EntryKind::Settings, 0, "Settings", "device prefs"};
            case 2:  return LauncherEntry{EntryKind::WiFi,     0, "Wi-Fi",    "network & time"};
            default: return LauncherEntry{EntryKind::About,    0, "About",    "company info"};
        }
    }

    LauncherEntry launcherEntry(uint8_t filteredIndex) {
        uint8_t fi = 0;
        for (uint8_t raw = 0; raw < GAME_COUNT_TOTAL; ++raw) {
            if (!board_.gameVisible(GAME_CATALOG[raw].id)) continue;
            if (fi == filteredIndex) return allEntry(raw);
            ++fi;
        }
        // Settings/WiFi/About always at end
        return allEntry(GAME_COUNT_TOTAL + (filteredIndex - fi));
    }

    void handleLauncherTouch(const TouchPoint& touch) {
        if (!touch.justPressed) {
            return;
        }
        const int16_t lW = static_cast<int16_t>(board_.display().width());
        const int16_t lH = static_cast<int16_t>(board_.display().height());
        const Rect gearBtn = launcherGearRect(board_.layoutMode(), lW);
        if (gearBtn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            openSettings();
            return;
        }
        const uint8_t pageSize = launcherPageSize();
        const uint8_t pages = max<uint8_t>(1, (launcherEntryCount() + pageSize - 1) / pageSize);
        const int16_t pY = static_cast<int16_t>(lH - 28);
        if (Rect{8, pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) && launcherPage_ > 0) {
            --launcherPage_;
            launcherDirty_ = true;
            return;
        }
        if (Rect{static_cast<int16_t>(lW - 82), pY, 74, 24}.contains(touch.x, touch.y, TOUCH_HIT_SLOP) && launcherPage_ + 1 < pages) {
            ++launcherPage_;
            launcherDirty_ = true;
            return;
        }
        const uint8_t start = launcherPage_ * pageSize;
        const uint8_t count = launcherEntryCount();
        for (uint8_t slot = 0; slot < pageSize; ++slot) {
            const uint8_t index = start + slot;
            if (index >= count) {
                break;
            }
            if (launcherTileRect(slot, board_.layoutMode()).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                launch(launcherEntry(index));
                return;
            }
        }
    }

    void launch(const LauncherEntry& entry) {
        launchKind(entry.kind);
    }

    void launchKind(EntryKind kind) {
        switch (kind) {
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
            case EntryKind::Settings:
                activeGame_ = &settings_;
                break;
            case EntryKind::WiFi:
                activeGame_ = &wifi_;
                break;
            case EntryKind::ObjectAdd:
                activeGame_ = &objectAdd_;
                break;
            case EntryKind::FingerCount:
                activeGame_ = &fingerCount_;
                break;
            case EntryKind::Sequence:
                activeGame_ = &sequence_;
                break;
            case EntryKind::NumberLine:
                activeGame_ = &numberLine_;
                break;
            case EntryKind::States:
                activeGame_ = &states_;
                break;
            case EntryKind::Scores:
                activeGame_ = &scores_;
                break;
            case EntryKind::Flag:
                activeGame_ = &flag_;
                break;
            case EntryKind::About:
                activeGame_ = &about_;
                break;
        }
        view_ = View::Game;
        // Every game is authored against the fixed 320x240 landscape canvas
        // (SCREEN_WIDTH/SCREEN_HEIGHT), so the Tall/Wide menu setting
        // deliberately does not apply here.
        board_.setDisplayRotation(effectiveRotation(true));
        activeGame_->begin(*this);
        activeGame_->render(*this);
        activeGame_->clearDirty();
    }

    void drawLauncherIcon(TFT_eSPI& tft, const LauncherEntry& entry, const Rect& r,
                          uint16_t fill, int16_t cx, int16_t cy) {
        (void)r;
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
            case EntryKind::Settings:
                Ui::drawGearIcon(tft, Rect{static_cast<int16_t>(cx - 12), static_cast<int16_t>(cy - 12), 24, 24});
                break;
            case EntryKind::WiFi:
                tft.drawCircle(cx, cy + 6, 12, TFT_WHITE);
                tft.drawCircle(cx, cy + 6, 7, TFT_WHITE);
                tft.fillCircle(cx, cy + 8, 2, TFT_WHITE);
                tft.fillRect(cx - 13, cy + 7, 26, 14, fill);
                break;
            case EntryKind::ObjectAdd:
                tft.fillCircle(cx - 10, cy - 4, 7, TFT_WHITE);
                tft.fillCircle(cx + 2, cy - 4, 7, Ui::rgb(108, 232, 148));
                tft.fillCircle(cx - 4, cy + 10, 7, Ui::rgb(255, 246, 178));
                tft.setTextColor(TFT_WHITE, fill);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("+", cx + 14, cy + 4, 2);
                break;
            case EntryKind::FingerCount:
                for (uint8_t f = 0; f < 5; ++f) {
                    tft.fillRoundRect(cx - 14 + f * 6, cy - 14, 5, 16, 2,
                        f < 3 ? TFT_WHITE : Ui::rgb(108, 232, 148));
                }
                tft.fillRoundRect(cx - 14, cy + 4, 28, 8, 2, TFT_WHITE);
                break;
            case EntryKind::Sequence:
                tft.setTextColor(TFT_WHITE, fill);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("Mon", cx - 4, cy - 8, 1);
                tft.drawString("Tue", cx - 4, cy + 2, 1);
                tft.drawString("Wed", cx - 4, cy + 12, 1);
                break;
            case EntryKind::NumberLine:
                tft.drawFastHLine(cx - 17, cy + 4, 34, TFT_WHITE);
                for (uint8_t t = 0; t < 5; ++t) {
                    tft.drawFastVLine(cx - 17 + t * 8, cy, 5, TFT_WHITE);
                }
                tft.fillCircle(cx + 7, cy - 4, 5, Ui::rgb(255, 246, 178));
                break;
            case EntryKind::Flag:
                tft.drawFastVLine(cx - 12, cy - 15, 30, TFT_WHITE);
                tft.fillRect(cx - 11, cy - 14, 24, 15, Ui::rgb(230, 90, 90));
                tft.fillRect(cx - 11, cy - 14, 24, 5, Ui::rgb(255, 246, 178));
                tft.drawRect(cx - 11, cy - 14, 24, 15, TFT_WHITE);
                break;
            case EntryKind::States:
                tft.fillRoundRect(cx - 16, cy - 11, 32, 22, 3, Ui::rgb(60, 90, 170));
                tft.fillRect(cx - 16, cy - 11, 14, 10, Ui::rgb(232, 232, 242));
                tft.drawRoundRect(cx - 16, cy - 11, 32, 22, 3, TFT_WHITE);
                break;
            case EntryKind::Scores:
                for (uint8_t b = 0; b < 3; ++b) {
                    const int16_t h = static_cast<int16_t>(8 + b * 7);
                    tft.fillRect(static_cast<int16_t>(cx - 14 + b * 10),
                                 static_cast<int16_t>(cy + 12 - h), 8, h,
                                 b == 2 ? Ui::rgb(255, 226, 90) : TFT_WHITE);
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
        const int16_t lW = static_cast<int16_t>(tft.width());
        const int16_t lH = static_cast<int16_t>(tft.height());
        const Board::LayoutMode mode = board_.layoutMode();
        const bool tall = (mode == Board::LayoutMode::Vertical);
        const int16_t headerH = launcherHeaderHeight(mode);
        const Rect gearBtn = launcherGearRect(mode, lW);

        Ui::clear(tft);
        tft.fillRect(0, 0, lW, headerH, Ui::surface());
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.setTextDatum(ML_DATUM);

        if (tall) {
            /* Portrait gives the title its own full-width line at font 4 -- it
             * only collided with the clock before because both shared one row.
             * The clock now sits on the second line with the gear beneath it. */
            tft.setTextDatum(MC_DATUM);
            tft.drawString("GoodTime Kids!", static_cast<int16_t>(lW / 2), 17, 4);
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString("(C) GoodTime Micro", 8, 40, 1);
            tft.setTextColor(Ui::text(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString(Clock::timeText(), 8, 60, 2);
            {
                const int16_t bx = static_cast<int16_t>(8 + tft.textWidth(Clock::timeText(), 2) + 10);
                Ui::drawSyncBadge(tft, bx, 60, Clock::synced(), Ui::surface());
                Ui::drawWifiBadge(tft, static_cast<int16_t>(bx + 22), 60, Ui::surface());
            }
            // Thin rule under the title to separate it from the tiles.
            tft.drawFastHLine(8, 30, static_cast<int16_t>(lW - 16), Ui::shade(Ui::surface(), 150));
        } else {
            /* Two rows rather than one. A font-4 title is ~190px wide, so the
             * clock, both badges and the gear could not share its line without
             * crowding. Branding now occupies the left across two lines and the
             * status items form a tidy block on the right. */
            tft.drawString("GoodTime Kids!", 10, 16, 4);
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.drawString("(C) GoodTime Micro", 10, 38, 1);

            tft.setTextColor(Ui::text(), Ui::surface());
            tft.setTextDatum(MR_DATUM);
            tft.drawString(Clock::timeText(), static_cast<int16_t>(lW - 40), 14, 1);
            Ui::drawSyncBadge(tft, static_cast<int16_t>(lW - 68), 34, Clock::synced(), Ui::surface());
            Ui::drawWifiBadge(tft, static_cast<int16_t>(lW - 46), 34, Ui::surface());
            // Hairline separating the status block from the branding.
            tft.drawFastVLine(static_cast<int16_t>(lW - 88), 8, 32, Ui::outline());
        }
        // Launcher header is Ui::surface() -- near-white on the light theme,
        // where a white gear was invisible. Use the theme's text colour.
        Ui::drawGearIcon(tft, gearBtn, Ui::text());

        const uint8_t pageSize = launcherPageSize();
        const uint8_t start = launcherPage_ * pageSize;
        const uint8_t total = launcherEntryCount();
        for (uint8_t slot = 0; slot < pageSize; ++slot) {
            const uint8_t index = start + slot;
            if (index >= total) {
                break;
            }
            const LauncherEntry entry = launcherEntry(index);
            const Rect r = launcherTileRect(slot, mode);
            const uint16_t fill = slot % 3 == 0 ? Ui::rgb(36, 132, 204) : (slot % 3 == 1 ? Ui::rgb(45, 154, 96) : Ui::rgb(222, 83, 83));
            Ui::drawButton(tft, r, "", fill, TFT_DARKGREY, TFT_WHITE);

            if (tall) {
                // Square tile: icon on top, text centred underneath.
                drawLauncherIcon(tft, entry, r, fill,
                                 static_cast<int16_t>(r.x + r.w / 2),
                                 static_cast<int16_t>(r.y + 30));
                tft.setTextColor(TFT_WHITE, fill);
                tft.setTextDatum(MC_DATUM);
                const int16_t cxT = static_cast<int16_t>(r.x + r.w / 2);
                String titleText = entry.title;
                while (titleText.length() > 2 && tft.textWidth(titleText, 2) > r.w - 8) {
                    titleText.remove(titleText.length() - 1);
                }
                tft.drawString(titleText, cxT, static_cast<int16_t>(r.y + 68), 2);
                tft.setTextColor(Ui::rgb(235, 245, 255), fill);
                String subText = entry.subtitle;
                while (subText.length() > 2 && tft.textWidth(subText, 1) > r.w - 8) {
                    subText.remove(subText.length() - 1);
                }
                tft.drawString(subText, cxT, static_cast<int16_t>(r.y + 87), 1);
            } else {
                drawLauncherIcon(tft, entry, r, fill,
                                 static_cast<int16_t>(r.x + 24),
                                 static_cast<int16_t>(r.y + 22));
                tft.setTextColor(TFT_WHITE, fill);
                tft.setTextDatum(ML_DATUM);
                String titleText = entry.title;
                while (titleText.length() > 2 && tft.textWidth(titleText, 2) > r.w - 54) {
                    titleText.remove(titleText.length() - 1);
                }
                tft.drawString(titleText, r.x + 48, r.y + 17, 2);
                tft.setTextColor(Ui::rgb(235, 245, 255), fill);
                String subText = entry.subtitle;
                while (subText.length() > 2 && tft.textWidth(subText, 1) > r.w - 54) {
                    subText.remove(subText.length() - 1);
                }
                tft.drawString(subText, r.x + 48, r.y + 34, 1);
            }
        }

        const uint8_t pages = max<uint8_t>(1, (total + pageSize - 1) / pageSize);
        if (pages > 1) {
            const int16_t pY = static_cast<int16_t>(lH - 28);
            Ui::drawPagerButton(tft, Rect{8, pY, 74, 24}, "Prev", launcherPage_ > 0);
            Ui::drawPagerButton(tft, Rect{static_cast<int16_t>(lW - 82), pY, 74, 24}, "Next", launcherPage_ + 1 < pages);
            tft.setTextColor(Ui::text(), Ui::bg());
            tft.setTextDatum(MC_DATUM);
            tft.drawString(String(launcherPage_ + 1) + "/" + pages, lW / 2, static_cast<int16_t>(pY + 12), 2);
        }
        tft.setTextDatum(TL_DATUM);
    }

    /* Every paddle hit advances the rally: the ball and paddles change colour
     * and the ball speeds up, so a long rally is visibly faster and hotter. */
    void onScreenSaverHit() {
        static const uint16_t RALLY_COLORS[6] = {
            Ui::rgb(80, 180, 255),   // blue
            Ui::rgb(80, 240, 160),   // green
            Ui::rgb(255, 226, 90),   // yellow
            Ui::rgb(255, 160, 60),   // orange
            Ui::rgb(255, 96, 96),    // red
            Ui::rgb(220, 120, 255),  // violet
        };
        if (ssav_hits_ < 255) ++ssav_hits_;
        ssav_color_ = RALLY_COLORS[ssav_hits_ % 6];

        // Mirror the rally colour on the case LED, full brightness.
        static const uint8_t RALLY_RGB[6][3] = {
            {  0, 140, 255}, {  0, 255, 140}, {255, 210,   0},
            {255, 110,   0}, {255,   0,  40}, {200,   0, 255},
        };
        const uint8_t* c = RALLY_RGB[ssav_hits_ % 6];
        board_.setRgbColor(c[0], c[1], c[2]);
    }

    void renderScreenSaver() {
        TFT_eSPI& tft = board_.display();
        
        if (!ssav_initialized_) {
            tft.fillScreen(TFT_BLACK);
            // Ball starts centre, random direction
            ssav_bx_  = SCREEN_WIDTH  / 2.0f;
            ssav_by_  = SCREEN_HEIGHT / 2.0f;
            ssav_bvx_ = (random(2) ? 2.8f : -2.8f);
            ssav_bvy_ = 1.6f + random(100) * 0.02f;
            // Paddles start centred
            ssav_ly_  = SCREEN_HEIGHT / 2.0f;
            ssav_ry_  = SCREEN_HEIGHT / 2.0f;
            ssav_hits_ = 0;
            ssav_color_ = Ui::rgb(80, 180, 255);
            ssav_initialized_ = true;
        }
        
        constexpr int16_t PAD_H = 40;  // paddle height
        constexpr int16_t PAD_W = 6;
        constexpr int16_t BALL  = 6;
        constexpr int16_t LX    = 10; // left paddle x centre
        constexpr int16_t RX    = SCREEN_WIDTH - 10; // right paddle x centre
        
        // Erase previous positions
        tft.fillRect(LX - PAD_W/2, static_cast<int16_t>(ssav_ly_ - PAD_H/2 - 2), PAD_W, PAD_H + 4, TFT_BLACK);
        tft.fillRect(RX - PAD_W/2, static_cast<int16_t>(ssav_ry_ - PAD_H/2 - 2), PAD_W, PAD_H + 4, TFT_BLACK);
        tft.fillRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL*2, BALL*2, TFT_BLACK);
        
        // Move ball
        ssav_bx_ += ssav_bvx_;
        ssav_by_ += ssav_bvy_;
        
        // Bounce top/bottom
        if (ssav_by_ < BALL) { ssav_by_ = BALL; ssav_bvy_ = fabsf(ssav_bvy_); }
        if (ssav_by_ > SCREEN_HEIGHT - BALL) { ssav_by_ = SCREEN_HEIGHT - BALL; ssav_bvy_ = -fabsf(ssav_bvy_); }
        
        // Only the paddle the ball is heading toward tracks it. The idle one
        // mirrors vertically, so the two always sweep in opposite directions
        // instead of moving in lockstep.
        const float padSpeed = 3.2f;
        const bool ballGoingLeft = ssav_bvx_ < 0.0f;
        const float mirrorY = static_cast<float>(SCREEN_HEIGHT) - ssav_by_;
        const float lTargetY = ballGoingLeft ? ssav_by_ : mirrorY;
        const float rTargetY = ballGoingLeft ? mirrorY  : ssav_by_;
        if (ssav_ly_ < lTargetY - padSpeed) ssav_ly_ += padSpeed;
        else if (ssav_ly_ > lTargetY + padSpeed) ssav_ly_ -= padSpeed;
        if (ssav_ry_ < rTargetY - padSpeed) ssav_ry_ += padSpeed;
        else if (ssav_ry_ > rTargetY + padSpeed) ssav_ry_ -= padSpeed;
        // Clamp paddles
        if (ssav_ly_ < PAD_H/2) ssav_ly_ = PAD_H/2;
        if (ssav_ly_ > SCREEN_HEIGHT - PAD_H/2) ssav_ly_ = SCREEN_HEIGHT - PAD_H/2;
        if (ssav_ry_ < PAD_H/2) ssav_ry_ = PAD_H/2;
        if (ssav_ry_ > SCREEN_HEIGHT - PAD_H/2) ssav_ry_ = SCREEN_HEIGHT - PAD_H/2;
        
        // Bounce off left paddle
        if (ssav_bvx_ < 0 && ssav_bx_ <= LX + PAD_W/2 + BALL) {
            if (ssav_by_ >= ssav_ly_ - PAD_H/2 && ssav_by_ <= ssav_ly_ + PAD_H/2) {
                ssav_bvx_ = fabsf(ssav_bvx_) * 1.10f;
                if (ssav_bvx_ > 9.0f) ssav_bvx_ = 9.0f;
                ssav_bvy_ += (ssav_by_ - ssav_ly_) * 0.06f;
                ssav_bx_ = LX + PAD_W/2 + BALL + 1;
                onScreenSaverHit();
            }
        }
        // Bounce off right paddle
        if (ssav_bvx_ > 0 && ssav_bx_ >= RX - PAD_W/2 - BALL) {
            if (ssav_by_ >= ssav_ry_ - PAD_H/2 && ssav_by_ <= ssav_ry_ + PAD_H/2) {
                ssav_bvx_ = -fabsf(ssav_bvx_) * 1.10f;
                if (ssav_bvx_ < -9.0f) ssav_bvx_ = -9.0f;
                ssav_bvy_ += (ssav_by_ - ssav_ry_) * 0.06f;
                ssav_bx_ = RX - PAD_W/2 - BALL - 1;
                onScreenSaverHit();
            }
        }
        // Reset if somehow past edge
        if (ssav_bx_ < 0 || ssav_bx_ > SCREEN_WIDTH) {
            ssav_bx_ = SCREEN_WIDTH / 2.0f;
            ssav_by_ = SCREEN_HEIGHT / 2.0f;
            ssav_bvx_ = ssav_bvx_ > 0 ? 2.8f : -2.8f;
            ssav_bvy_ = 1.6f;
            ssav_hits_ = 0;
            ssav_color_ = Ui::rgb(80, 180, 255);
        }
        
        /* Company mark, laid out for whichever orientation the saver is in.
         * Drawn once per frame under the ball, so it never flickers. */
        {
            const int16_t sw = static_cast<int16_t>(tft.width());
            const int16_t sh = static_cast<int16_t>(tft.height());
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(Ui::rgb(120, 128, 150), TFT_BLACK);
            tft.drawString("GoodTime Micro", sw / 2, static_cast<int16_t>(sh / 2 - 10), 4);
            tft.setTextColor(Ui::rgb(70, 76, 92), TFT_BLACK);
            tft.drawString("(C) GoodTime Micro Company",
                           sw / 2, static_cast<int16_t>(sh / 2 + 14), 1);
            tft.setTextDatum(TL_DATUM);
        }

        // Draw court centre line
        for (int16_t y = 0; y < SCREEN_HEIGHT; y += 14) {
            tft.fillRect(SCREEN_WIDTH/2 - 1, y, 2, 8, Ui::rgb(40,40,40));
        }
        // Draw paddles
        tft.fillRoundRect(LX - PAD_W/2, static_cast<int16_t>(ssav_ly_ - PAD_H/2), PAD_W, PAD_H, 3, ssav_color_);
        tft.fillRoundRect(RX - PAD_W/2, static_cast<int16_t>(ssav_ry_ - PAD_H/2), PAD_W, PAD_H, 3, ssav_color_);
        // Ball takes the rally colour too, with a white core so it stays visible.
        tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL*2, BALL*2, 2, ssav_color_);
        tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL/2), static_cast<int16_t>(ssav_by_ - BALL/2), BALL, BALL, 1, TFT_WHITE);

        // Rally counter, so the speed-up is legible as progress.
        tft.setTextColor(Ui::rgb(70, 70, 76), TFT_BLACK);
        tft.setTextDatum(TC_DATUM);
        tft.drawString(String(ssav_hits_), SCREEN_WIDTH / 2, 6, 2);
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
    SettingsGame settings_;
    WifiGame wifi_;
    ObjectAddGame objectAdd_;
    FingerCountGame fingerCount_;
    SequenceGame sequence_;
    StatesGame states_;
    ProfileGame profile_;
    ScoresGame scores_;
    NumberLineGame numberLine_;
    FlagGame flag_;
    AboutGame about_;
    Game* activeGame_ = nullptr;
    View view_ = View::Launcher;
    // Pong screen saver state
    float ssav_bx_ = 160.0f, ssav_by_ = 120.0f;
    float ssav_bvx_ = 2.8f,  ssav_bvy_ = 1.6f;
    float ssav_ly_  = 120.0f, ssav_ry_  = 120.0f;
    bool  ssav_initialized_ = false;
    View  ssavPrevView_ = View::Launcher;
    bool  swallowTouch_ = false;
    uint8_t ssav_hits_ = 0;      // rally length: drives colour and speed
    uint16_t ssav_color_ = 0;
    uint32_t ssav_lastFrameMs_ = 0;
    uint8_t launcherPage_ = 0;
    uint32_t lastClockMinute_ = 0;
    uint32_t lastActivityMs_ = 0;
    uint32_t screenSaverStartMs_ = 0;
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
