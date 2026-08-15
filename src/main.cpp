#include <Arduino.h>
#include <esp_system.h>
#include "engine/Game.h"
#include "engine/GameCatalog.h"
#include "games/AboutGame.h"
#include "games/CountingGame.h"
#include "games/ColorMixGame.h"
#include "games/FractionGame.h"
#include "games/GreWordsGame.h"
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
#include "games/PercentCircleGame.h"
#include "games/SequenceGame.h"
#include "games/ProfileGame.h"
#include "games/ScoresGame.h"
#include "games/SettingsGame.h"
#include "games/ShapeColorGame.h"
#include "games/CinnamonGame.h"
#include "games/SlidingPuzzleGame.h"
#include "games/SortGame.h"
#include "games/StateFlagGame.h"
#include "games/StateMapGame.h"
#include "games/StatesGame.h"
#include "games/MicrokuGame.h"
#include "games/SystemInfoGame.h"
#include "games/TimeGame.h"
#include "games/TicTacToeGame.h"
#include "games/TraceGame.h"
#include "games/WifiGame.h"
#include "games/WhackAMoleGame.h"
#include "hal/BleBeacon.h"
#include "hal/Board.h"
#include "hal/Clock.h"
#include "hal/Watchdog.h"
#include "ui/Ui.h"

namespace {
// Portrait rotation. 0 puts the USB connector at the bottom edge;
// rotation 2 (the previous value) put it at the top.
constexpr uint8_t CYD_PORTRAIT_ROTATION = 0;

constexpr Rect HOME_BUTTON{0, 0, 44, TOP_BAR_HEIGHT};

Rect topBarSettingsRect(int16_t screenW) {
    return Rect{static_cast<int16_t>(screenW - 34), 3, 26, 24};
}

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

Rect launcherProfileRect(Board::LayoutMode mode, int16_t lW) {
    if (mode == Board::LayoutMode::Vertical) {
        return Rect{8, 34, static_cast<int16_t>(min<int16_t>(112, lW - 46)), 20};
    }
    /* Landscape puts the name on the byline row, to the right of the
     * copyright rather than across it -- at x=104 the old rect started 14px
     * before "(C) GoodTime Micro" ended. Runs up to the hairline at lW-110. */
    const int16_t x = 124;
    const int16_t rightLimit = static_cast<int16_t>(lW - 114);
    const int16_t w = static_cast<int16_t>(min<int16_t>(86, max<int16_t>(52, rightLimit - x)));
    return Rect{x, 30, w, 18};
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
    tft.drawString("Hello Board", 10, TOP_BAR_HEIGHT / 2, 4);
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
    Watchdog::begin();
    Watchdog::setContext("bringup");
    Clock::begin();
    if (!board.hasTouchCalibration()) {
        board.runTouchCalibration();
    }
    drawBringup();
}

void loop() {
    Watchdog::feed();
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
        // Straight after Serial comes up, so the crash report from the
        // previous run is the first thing in the log.
        Watchdog::begin();
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

    /* Target frame period. 20ms is 50Hz, comfortably above what the panel and
     * a child's finger can distinguish, and it leaves headroom for the frames
     * that legitimately cost more (a full repaint pushes ~150KB over SPI). */
    static constexpr uint32_t FRAME_BUDGET_MS = 20;
    /* Poll period while the panel is asleep. */
    static constexpr uint32_t SLEEP_POLL_MS = 100;

    void loop() {
        Watchdog::feed();
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
        
        /* Idle policy. screenSaverSeconds() is the delay from the last touch
         * to *something* happening; which something depends on idleAction().
         * SleepOnly skips the saver entirely, which is the setting that
         * actually conserves the battery. */
        const Board::IdleAction idlePolicy = board_.idleAction();
        if (view_ != View::ScreenSaver && view_ != View::Asleep) {
            const uint32_t idleMs = nowMs - lastActivityMs_;
            const uint32_t timeoutMs = static_cast<uint32_t>(board_.screenSaverSeconds()) * 1000UL;
            if (timeoutMs > 0 && idleMs > timeoutMs) {
                if (idlePolicy == Board::IdleAction::SleepOnly) {
                    enterSleep();
                } else {
                    enterScreenSaver();
                }
            }
        } else if (view_ == View::ScreenSaver &&
                   idlePolicy == Board::IdleAction::SaverThenSleep) {
            const uint32_t saverMs = nowMs - screenSaverStartMs_;
            if (saverMs > static_cast<uint32_t>(board_.sleepSeconds()) * 1000UL) {
                enterSleep();
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
        } else if (view_ == View::Asleep) {
            /* rawTouch, not touch: a swallowed event must still be able to
             * wake the panel, otherwise the first press after going to sleep
             * is silently eaten and the device looks dead. */
            if (rawTouch.justPressed || rawTouch.down) {
                wakeFromSleep();
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
            const Rect settingsButton = topBarSettingsRect(static_cast<int16_t>(board_.display().width()));
            if (touch.justPressed && settingsButton.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
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
        const uint32_t workMs = millis() - nowMs;
        Watchdog::recordFrameWork(workMs);
        /* Pace to a deadline, not a fixed nap.
         *
         * This used to be an unconditional delay(18), so the frame period was
         * work + 18ms and a heavy frame was punished twice -- once for being
         * slow, then again by a full extra sleep on top. Sleeping only the
         * remainder of the budget makes touch latency depend on how long the
         * work actually took.
         *
         * Always yield at least a tick: the idle task on this core has to run
         * or its own watchdog trips. */
        /* Asleep there is nothing to draw, so drop from 50Hz to 10Hz. That is
         * the point of the feature -- the backlight is off and the CPU should
         * not be spinning a render loop behind it. Well inside the watchdog's
         * 12s timeout. */
        const uint32_t budgetMs = (view_ == View::Asleep) ? SLEEP_POLL_MS
                                                          : FRAME_BUDGET_MS;
        if (workMs < budgetMs) {
            delay(budgetMs - workMs);
        } else {
            delay(1);
        }
    }

    /* Board::setDisplayRotation() clears lastTouch_. If a finger is still down
     * when that happens, the next poll sees down && !lastTouch_.down and
     * reports a fresh justPressed at the same coordinates -- a phantom tap on
     * whatever the new screen put there. Selecting a profile landed straight on
     * a launcher tile that way. Every rotation change goes through here so the
     * events are swallowed until the finger actually lifts. */
    void applyRotation(uint8_t rotation) {
        if (board_.displayRotation() != rotation) {
            board_.setDisplayRotation(rotation);
        }
        swallowTouch_ = true;
    }

    uint8_t effectiveRotation(bool landscape) {
        return landscape ? CYD_SCREEN_ROTATION : CYD_PORTRAIT_ROTATION;
    }

    /* Single funnel for "the active screen is being replaced". Every
     * transition goes through here so a screen's end() cannot be skipped by
     * one path and honoured by another. */
    void leaveActiveGame() {
        if (activeGame_ == nullptr) {
            return;
        }
        const char* leaving = Watchdog::context();
        activeGame_->end(*this);
        activeGame_ = nullptr;
        /* A screen that does not hand back what it borrowed is the only way
         * heap can march downwards on a device with no garbage collector, and
         * on a 27Hz UI it is invisible until something fails to allocate hours
         * later. Comparing free heap across the screen's whole lifetime turns
         * that into a line in the log the first time it happens. */
        Watchdog::noteScreenLeft(leaving, heapAtLaunch_);
    }

    /** Free heap immediately before the active screen's begin(). */
    uint32_t heapAtLaunch_ = 0;

    void goHome() override {
        Watchdog::setContext("Launcher");
        leaveActiveGame();
        view_ = View::Launcher;
        clampLauncherPage();
        // Rotate display based on chosen layout mode
        applyRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
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
        applyRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
        Watchdog::setContext("Profiles");
        view_ = View::Profiles;
        leaveActiveGame();
        activeGame_ = &profile_;
        heapAtLaunch_ = ESP.getFreeHeap();
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
        applyRotation(effectiveRotation(board_.layoutMode() != Board::LayoutMode::Vertical));
        Watchdog::setContext("ScreenSaver");
        view_ = View::ScreenSaver;
        screenSaverStartMs_ = millis();
        ssav_initialized_ = false;
    }
    
    /* Panel sleep. Distinct from the screen saver: the backlight is off and
     * nothing renders, so the only way out is a touch. Where we came from is
     * remembered so waking returns there rather than dumping the child on the
     * launcher -- and when we arrive here from the saver, the saver's own
     * previous view is what we want, not the saver itself. */
    void enterSleep() {
        if (view_ != View::ScreenSaver) {
            ssavPrevView_ = view_;
        }
        board_.setRgbColor(0, 0, 0);   // the case LED is battery too
        board_.displaySleep();
        Watchdog::setContext("Asleep");
        view_ = View::Asleep;
    }

    void wakeFromSleep() {
        /* displayWake() blocks ~120ms waiting for the panel to leave its
         * low-power state. That is deliberate and happens once per wake, but
         * an unannounced block looks exactly like a hang to the watchdog. */
        {
            Watchdog::Pause guard;
            board_.displayWake();
        }

        const bool backToGame = (ssavPrevView_ == View::Game && activeGame_ != nullptr);

        // Games always run landscape; only the launcher follows the layout
        // setting. applyRotation() also swallows the touch that woke us, so it
        // cannot press whatever happens to be under the finger.
        applyRotation(
            effectiveRotation(backToGame || board_.layoutMode() != Board::LayoutMode::Vertical));

        lastActivityMs_ = millis();

        if (backToGame) {
            Watchdog::setContext(kindTitle(activeKind_));
            view_ = View::Game;
            activeGame_->requestRender();
        } else if (ssavPrevView_ == View::Profiles) {
            Watchdog::setContext("Profiles");
            view_ = View::Profiles;
            profile_.requestRender();
        } else {
            Watchdog::setContext("Launcher");
            view_ = View::Launcher;
            clampLauncherPage();
            launcherDirty_ = true;
        }
    }

    void exitScreenSaver() {
        board_.setRgbColor(0, 0, 0);

        const bool backToGame = (ssavPrevView_ == View::Game && activeGame_ != nullptr);

        // Games always run landscape; only the launcher follows the layout
        // setting. applyRotation() also swallows the in-flight touch.
        applyRotation(
            effectiveRotation(backToGame || board_.layoutMode() != Board::LayoutMode::Vertical));

        lastActivityMs_ = millis();

        if (backToGame) {
            Watchdog::setContext(kindTitle(activeKind_));
            view_ = View::Game;
            activeGame_->requestRender();
        } else {
            Watchdog::setContext("Launcher");
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
        ScreenSaver,
        Asleep
    };

    enum class EntryKind : uint8_t {
        TicTacToe,
        Memory,
        Math,
        Multiplication,
        Time,
        WhackAMole,
        Cinnamon,
        Microku,
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
        Trace,
        StateFlag,
        StateMap,
        Percent,
        GreWords,
        Scores,
        Profiles,
        Flag,
        Settings,
        WiFi,
        About,
        SystemInfo
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
        uint8_t count = 6; // Scores, Settings, WiFi, Profiles, About, SystemInfo always shown
        for (uint8_t i = 0; i < GAME_COUNT_TOTAL; ++i) {
            if (board_.gameVisible(i)) ++count;
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
        EntryKind::Cinnamon,     EntryKind::Microku,        EntryKind::ShapeColor,
        EntryKind::Counting,  EntryKind::Money,         EntryKind::Fractions,
        EntryKind::Maze,      EntryKind::Sort,          EntryKind::ColorMix,
        EntryKind::SlidingPuzzle, EntryKind::OddOneOut, EntryKind::ObjectAdd,
        EntryKind::FingerCount,   EntryKind::Sequence,  EntryKind::NumberLine,
        EntryKind::Flag,          EntryKind::States,    EntryKind::Trace,
        EntryKind::StateFlag,     EntryKind::StateMap,   EntryKind::Percent,
        EntryKind::GreWords,
    };

    LauncherEntry allEntry(uint8_t raw) const {
        if (raw < GAME_CATALOG_COUNT) {
            return LauncherEntry{CATALOG_KINDS[raw], 0,
                                 GAME_CATALOG[raw].title, GAME_CATALOG[raw].subtitle};
        }
        switch (static_cast<uint8_t>(raw - GAME_CATALOG_COUNT)) {
            case 0:  return LauncherEntry{EntryKind::Scores,     0, "Scores",      "best & worst"};
            case 3:  return LauncherEntry{EntryKind::Profiles,   0, "Profiles",    "switch player"};
            case 1:  return LauncherEntry{EntryKind::Settings,   0, "Settings",    "device prefs"};
            case 2:  return LauncherEntry{EntryKind::WiFi,       0, "Wi-Fi",       "network & time"};
            case 4:  return LauncherEntry{EntryKind::SystemInfo, 0, "System Info", "device status"};
            case 5:  return LauncherEntry{EntryKind::About,      0, "About",       "company info"};
            default: return LauncherEntry{EntryKind::About,      0, "About",       "company info"};
        }
    }

    /* Human-readable name for a kind, taken from the same catalog the tiles
     * use, so the watchdog's crash context cannot drift from the launcher. */
    const char* kindTitle(EntryKind kind) const {
        for (uint8_t raw = 0; raw < GAME_CATALOG_COUNT; ++raw) {
            if (CATALOG_KINDS[raw] == kind) return GAME_CATALOG[raw].title;
        }
        switch (kind) {
            case EntryKind::Scores:     return "Scores";
            case EntryKind::Settings:   return "Settings";
            case EntryKind::WiFi:       return "Wi-Fi";
            case EntryKind::Profiles:   return "Profiles";
            case EntryKind::SystemInfo: return "System Info";
            case EntryKind::About:      return "About";
            default:                    return "Game";
        }
    }

    LauncherEntry launcherEntry(uint8_t filteredIndex) {
        uint8_t fi = 0;
        for (uint8_t raw = 0; raw < GAME_COUNT_TOTAL; ++raw) {
            if (!board_.gameVisible(raw)) continue;
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
        const Board::LayoutMode mode = board_.layoutMode();
        const Rect profileBtn = launcherProfileRect(mode, lW);
        if (profileBtn.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            openProfiles();
            return;
        }
        const Rect gearBtn = launcherGearRect(mode, lW);
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
            if (launcherTileRect(slot, mode).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
                launch(launcherEntry(index));
                return;
            }
        }
    }

    void launch(const LauncherEntry& entry) {
        launchKind(entry.kind);
    }

    void launchKind(EntryKind kind) {
        leaveActiveGame();
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
            case EntryKind::Cinnamon:
                activeGame_ = &cinnamon_;
                break;
            case EntryKind::Microku:
                activeGame_ = &microku_;
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
            case EntryKind::Trace:
                activeGame_ = &trace_;
                break;
            case EntryKind::StateFlag:
                activeGame_ = &stateFlag_;
                break;
            case EntryKind::StateMap:
                activeGame_ = &stateMap_;
                break;
            case EntryKind::Percent:
                activeGame_ = &percent_;
                break;
            case EntryKind::GreWords:
                activeGame_ = &greWords_;
                break;
            case EntryKind::Scores:
                activeGame_ = &scores_;
                break;
            case EntryKind::Profiles:
                openProfiles();
                return;
            case EntryKind::Flag:
                activeGame_ = &flag_;
                break;
            case EntryKind::SystemInfo:
                activeGame_ = &systemInfo_;
                break;
            case EntryKind::About:
                activeGame_ = &about_;
                break;
        }
        view_ = View::Game;
        activeKind_ = kind;
        // A crash report is far more useful when it names the screen.
        Watchdog::setContext(kindTitle(kind));
        const bool followsLayout = (kind == EntryKind::SystemInfo);
        applyRotation(effectiveRotation(!followsLayout ||
                                        board_.layoutMode() != Board::LayoutMode::Vertical));
        heapAtLaunch_ = ESP.getFreeHeap();
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
            case EntryKind::Cinnamon:
                tft.fillCircle(cx - 10, cy - 8, 7, Ui::rgb(255, 112, 112));
                tft.fillCircle(cx + 10, cy - 8, 7, Ui::rgb(94, 190, 255));
                tft.fillCircle(cx - 10, cy + 10, 7, Ui::rgb(108, 232, 148));
                tft.fillCircle(cx + 10, cy + 10, 7, Ui::rgb(255, 232, 94));
                break;
            case EntryKind::Microku:
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
            case EntryKind::Trace:
                tft.setTextColor(TFT_WHITE, fill);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("A", cx - 6, cy - 2, 4);
                for (int16_t d = 0; d < 12; d += 3) {
                    tft.fillCircle(cx + 12, cy - 10 + d, 1, Ui::rgb(108, 232, 148));
                }
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::StateFlag:
                tft.drawFastVLine(cx - 12, cy - 15, 30, TFT_WHITE);
                tft.fillRect(cx - 11, cy - 14, 24, 15, Ui::rgb(90, 130, 230));
                tft.fillRect(cx - 11, cy - 14, 24, 5, Ui::rgb(255, 246, 178));
                tft.drawRect(cx - 11, cy - 14, 24, 15, TFT_WHITE);
                tft.fillCircle(cx + 10, cy + 8, 4, Ui::rgb(255, 200, 0));
                break;
            case EntryKind::StateMap:
                tft.fillRoundRect(cx - 14, cy - 14, 28, 28, 2, Ui::bg());
                tft.drawRoundRect(cx - 14, cy - 14, 28, 28, 2, TFT_WHITE);
                tft.fillTriangle(cx - 6, cy - 8, cx + 10, cy - 2, cx - 2, cy + 10, Ui::rgb(36, 132, 204));
                tft.fillCircle(cx + 10, cy + 8, 4, Ui::rgb(255, 200, 0));
                break;
            case EntryKind::Percent:
                tft.fillCircle(cx, cy, 16, TFT_WHITE);
                // A quarter wedge, drawn as two triangles from the centre --
                // TFT_eSPI has no fillWedge, and drawArc is overkill at 32px.
                tft.fillTriangle(cx, cy, cx, cy - 16, cx + 16, cy - 16, Ui::rgb(255, 202, 84));
                tft.fillTriangle(cx, cy, cx + 16, cy - 16, cx + 16, cy, Ui::rgb(255, 202, 84));
                tft.drawCircle(cx, cy, 16, Ui::rgb(36, 132, 204));
                tft.setTextColor(Ui::rgb(36, 132, 204), fill);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("%", cx, cy, 1);
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::GreWords:
                tft.fillRoundRect(cx - 12, cy - 12, 20, 20, 3, Ui::rgb(160, 170, 176));
                tft.fillRoundRect(cx - 14, cy - 14, 20, 20, 3, TFT_WHITE);
                tft.setTextColor(Ui::rgb(36, 132, 204), TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.drawString("Aa", cx - 2, cy - 2, 2);
                tft.setTextDatum(TL_DATUM);
                break;
            case EntryKind::Profiles:
                tft.fillCircle(cx - 6, cy - 5, 6, TFT_WHITE);
                tft.fillCircle(cx - 6, cy + 9, 10, TFT_WHITE);
                tft.fillRect(cx - 18, cy + 10, 24, 8, fill);
                tft.fillCircle(cx + 9, cy - 3, 5, Ui::rgb(255, 226, 90));
                tft.fillCircle(cx + 9, cy + 9, 8, Ui::rgb(255, 226, 90));
                tft.fillRect(cx + 1, cy + 10, 18, 8, fill);
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
            case EntryKind::SystemInfo:
                tft.drawRoundRect(cx - 15, cy - 10, 30, 20, 2, TFT_WHITE);
                tft.fillTriangle(cx - 2, cy - 8, cx - 6, cy + 1, cx - 1, cy + 1, Ui::rgb(255, 226, 90));
                tft.fillTriangle(cx + 2, cy + 8, cx + 6, cy - 1, cx + 1, cy - 1, Ui::rgb(255, 226, 90));
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
        const Rect profileBtn = launcherProfileRect(mode, lW);

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
            /* The name, but no button chrome around it: the framed chip is
             * what overlapped the status badges on a 240px header. Plain text
             * in the same rect, which is still the touch target. */
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(Ui::text(), Ui::surface());
            tft.drawString(Ui::fitted(tft, board_.profileName(board_.activeProfile()),
                                      profileBtn.w, 2),
                           static_cast<int16_t>(profileBtn.x + 2),
                           static_cast<int16_t>(profileBtn.y + profileBtn.h / 2), 2);
            tft.setTextColor(Ui::text(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString(Clock::timeText(), 8, 60, 2);
            {
                const int16_t bx = static_cast<int16_t>(8 + tft.textWidth(Clock::timeText(), 2) + 10);
                Ui::drawSyncBadge(tft, bx, 60, Clock::synced(), Ui::surface());
                Ui::drawWifiBadge(tft, static_cast<int16_t>(bx + 22), 60, Ui::surface());
                Ui::drawBatteryBadge(tft, static_cast<int16_t>(bx + 46), 60, 
                                     board_.getBatteryPercent(), 
                                     board_.getPowerSource() == Board::PowerState::EXTERNAL_POWER, 
                                     Ui::surface());
                /* Portrait has room to simply extend the badge row: the gear
                 * sits at lW-32 on this line, and even the widest clock text
                 * leaves the beacon badge well short of it. */
                if (BleBeacon::active()) {
                    Ui::drawBleBadge(tft, static_cast<int16_t>(bx + 70), 60, Ui::surface());
                }
            }
            // Thin rule under the title to separate it from the tiles.
            tft.drawFastHLine(8, 30, static_cast<int16_t>(lW - 16), Ui::shade(Ui::surface(), 150));
        } else {
            /* Two rows rather than one. A font-4 title is ~190px wide, so the
             * clock, badges and the gear could not share its line without
             * crowding. Branding now occupies the left across two lines and the
             * status items form a tidy block on the right. */
            tft.drawString("GoodTime Kids!", 10, 16, 4);
            tft.setTextColor(Ui::muted(), Ui::surface());
            tft.drawString("(C) GoodTime Micro", 10, 38, 1);
            /* Name sits after the copyright on the byline row, at font 1 --
             * the gap between them and the hairline is about 86px. Drawn in
             * the text colour while the byline stays muted, so it reads as
             * live information rather than more small print. */
            tft.setTextDatum(ML_DATUM);
            tft.setTextColor(Ui::text(), Ui::surface());
            tft.drawString(Ui::fitted(tft, board_.profileName(board_.activeProfile()),
                                      profileBtn.w, 1),
                           static_cast<int16_t>(profileBtn.x),
                           static_cast<int16_t>(profileBtn.y + profileBtn.h / 2), 1);

            tft.setTextColor(Ui::text(), Ui::surface());
            tft.setTextDatum(MR_DATUM);
            tft.drawString(Clock::timeText(), static_cast<int16_t>(lW - 40), 14, 1);
            /* Landscape cannot extend the badge row: it runs from the hairline
             * at lW-110 to the gear at lW-30 with about 8px to spare. The
             * beacon badge goes on the clock's line instead, positioned off the
             * measured width of the clock string so a longer time format pushes
             * it left rather than under the text. */
            if (BleBeacon::active()) {
                const int16_t clockLeft =
                    static_cast<int16_t>(lW - 40 - tft.textWidth(Clock::timeText(), 1));
                Ui::drawBleBadge(tft, max<int16_t>(static_cast<int16_t>(lW - 104),
                                                   static_cast<int16_t>(clockLeft - 14)),
                                 14, Ui::surface());
            }
            Ui::drawSyncBadge(tft, static_cast<int16_t>(lW - 92), 34, Clock::synced(), Ui::surface());
            Ui::drawWifiBadge(tft, static_cast<int16_t>(lW - 68), 34, Ui::surface());
            Ui::drawBatteryBadge(tft, static_cast<int16_t>(lW - 44), 34, 
                                 board_.getBatteryPercent(), 
                                 board_.getPowerSource() == Board::PowerState::EXTERNAL_POWER, 
                                 Ui::surface());
            // Hairline separating the status block from the branding.
            // Move hairline slightly further left to accommodate the extra badge
            tft.drawFastVLine(static_cast<int16_t>(lW - 110), 8, 32, Ui::outline());
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

    /* Serves the ball again from centre with the base speed and a clean
     * rally. Used both when a rally reaches SSAV_MAX_HITS and as a fallback
     * if the ball ever somehow gets past a paddle. */
    void resetScreenSaverRally(int16_t effW, int16_t effH) {
        ssav_bx_ = effW / 2.0f;
        ssav_by_ = effH / 2.0f;
        ssav_bvx_ = (random(2) ? 2.8f : -2.8f);
        ssav_bvy_ = 1.6f;
        ssav_hits_ = 0;
        ssav_color_ = Ui::rgb(80, 180, 255);
    }

    void renderScreenSaver() {
        TFT_eSPI& tft = board_.display();

        // The saver rotates the physical display to match the chosen layout
        // (unlike regular games, which are pinned to the fixed 320x240
        // landscape canvas). It has to lay itself out against the *actual*
        // panel dimensions, not the SCREEN_WIDTH/SCREEN_HEIGHT landscape
        // constants -- using the fixed constants here was why the court
        // looked rotated 90 degrees from the real screen in vertical mode.
        const int16_t effW = static_cast<int16_t>(tft.width());
        const int16_t effH = static_cast<int16_t>(tft.height());

        if (!ssav_initialized_) {
            tft.fillScreen(TFT_BLACK);
            // Ball starts centre, random direction
            ssav_bx_  = effW / 2.0f;
            ssav_by_  = effH / 2.0f;
            ssav_bvx_ = (random(2) ? 2.8f : -2.8f);
            ssav_bvy_ = 1.6f + random(100) * 0.02f;
            // Paddles start centred
            ssav_ly_  = effH / 2.0f;
            ssav_ry_  = effH / 2.0f;
            ssav_hits_ = 0;
            ssav_color_ = Ui::rgb(80, 180, 255);
            ssav_textCy_ = -1;
            ssav_initialized_ = true;
        }

        constexpr int16_t PAD_H = 40;  // paddle height
        constexpr int16_t PAD_W = 6;
        constexpr int16_t BALL  = 6;
        constexpr int16_t LX    = 10; // left paddle x centre
        const int16_t RX = static_cast<int16_t>(effW - 10); // right paddle x centre

        // Erase previous positions
        tft.fillRect(LX - PAD_W/2, static_cast<int16_t>(ssav_ly_ - PAD_H/2 - 2), PAD_W, PAD_H + 4, TFT_BLACK);
        tft.fillRect(RX - PAD_W/2, static_cast<int16_t>(ssav_ry_ - PAD_H/2 - 2), PAD_W, PAD_H + 4, TFT_BLACK);
        tft.fillRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL*2, BALL*2, TFT_BLACK);

        // Move ball
        ssav_bx_ += ssav_bvx_;
        ssav_by_ += ssav_bvy_;

        // Bounce top/bottom
        if (ssav_by_ < BALL) { ssav_by_ = BALL; ssav_bvy_ = fabsf(ssav_bvy_); }
        if (ssav_by_ > effH - BALL) { ssav_by_ = effH - BALL; ssav_bvy_ = -fabsf(ssav_bvy_); }

        // Speed ramps off the hit count -- eased with sqrt so the very first
        // few touches already feel like a jump, then it keeps climbing
        // toward FAST_SPEED/FAST_PAD_SPEED as the rally approaches MAX_HITS.
        // Paddle speed rides the same curve so paddles visibly quicken too,
        // not just the ball. The vertical kick off a paddle is clamped below
        // BASE_PAD_SPEED so the paddles can always keep pace and the rally
        // reliably runs the full MAX_HITS before serving again.
        constexpr uint8_t MAX_HITS = 30;
        constexpr float BASE_SPEED = 3.0f;
        constexpr float FAST_SPEED = 22.0f;
        constexpr float BASE_PAD_SPEED = 3.2f;
        constexpr float FAST_PAD_SPEED = 7.5f;
        constexpr float MAX_KICK_BVY = 3.0f;
        const float easedT = sqrtf(static_cast<float>(min<uint8_t>(ssav_hits_, MAX_HITS)) / MAX_HITS);

        // Only the paddle the ball is currently heading toward moves at all.
        // The other one holds perfectly still -- it doesn't start sweeping
        // to meet the ball until the instant the ball bounces back toward
        // it (ballGoingLeft flips), giving it the whole crossing to get
        // into position, same as the paddle that just returned the serve.
        const float padSpeed = BASE_PAD_SPEED + (FAST_PAD_SPEED - BASE_PAD_SPEED) * easedT;
        const bool ballGoingLeft = ssav_bvx_ < 0.0f;
        if (ballGoingLeft) {
            if (ssav_ly_ < ssav_by_ - padSpeed) ssav_ly_ += padSpeed;
            else if (ssav_ly_ > ssav_by_ + padSpeed) ssav_ly_ -= padSpeed;
        } else {
            if (ssav_ry_ < ssav_by_ - padSpeed) ssav_ry_ += padSpeed;
            else if (ssav_ry_ > ssav_by_ + padSpeed) ssav_ry_ -= padSpeed;
        }
        // Clamp paddles
        if (ssav_ly_ < PAD_H/2) ssav_ly_ = PAD_H/2;
        if (ssav_ly_ > effH - PAD_H/2) ssav_ly_ = effH - PAD_H/2;
        if (ssav_ry_ < PAD_H/2) ssav_ry_ = PAD_H/2;
        if (ssav_ry_ > effH - PAD_H/2) ssav_ry_ = effH - PAD_H/2;

        // Bounce off left paddle
        if (ssav_bvx_ < 0 && ssav_bx_ <= LX + PAD_W/2 + BALL) {
            if (ssav_by_ >= ssav_ly_ - PAD_H/2 && ssav_by_ <= ssav_ly_ + PAD_H/2) {
                onScreenSaverHit();
                const float t = sqrtf(static_cast<float>(min<uint8_t>(ssav_hits_, MAX_HITS)) / MAX_HITS);
                ssav_bvx_ = BASE_SPEED + (FAST_SPEED - BASE_SPEED) * t;
                ssav_bvy_ = constrain(ssav_bvy_ + (ssav_by_ - ssav_ly_) * 0.05f, -MAX_KICK_BVY, MAX_KICK_BVY);
                ssav_bx_ = LX + PAD_W/2 + BALL + 1;
                if (ssav_hits_ >= MAX_HITS) resetScreenSaverRally(effW, effH);
            }
        }
        // Bounce off right paddle
        if (ssav_bvx_ > 0 && ssav_bx_ >= RX - PAD_W/2 - BALL) {
            if (ssav_by_ >= ssav_ry_ - PAD_H/2 && ssav_by_ <= ssav_ry_ + PAD_H/2) {
                onScreenSaverHit();
                const float t = sqrtf(static_cast<float>(min<uint8_t>(ssav_hits_, MAX_HITS)) / MAX_HITS);
                ssav_bvx_ = -(BASE_SPEED + (FAST_SPEED - BASE_SPEED) * t);
                ssav_bvy_ = constrain(ssav_bvy_ + (ssav_by_ - ssav_ry_) * 0.05f, -MAX_KICK_BVY, MAX_KICK_BVY);
                ssav_bx_ = RX - PAD_W/2 - BALL - 1;
                if (ssav_hits_ >= MAX_HITS) resetScreenSaverRally(effW, effH);
            }
        }
        // Reset if somehow past edge (paddles should always catch it now,
        // but this stays as a safety net)
        if (ssav_bx_ < 0 || ssav_bx_ > effW) {
            resetScreenSaverRally(effW, effH);
        }

        /* Company mark, laid out for whichever orientation the saver is in.
         * Drawn once per frame under the ball, so it never flickers. It
         * bobs slowly up and down so it isn't a static burn-in target. */
        {
            // Amplitude scales with the panel's actual usable height (which
            // flips with orientation), so the mark sweeps out toward the
            // top/bottom edges instead of a fixed, orientation-blind 10px.
            // TEXT_BAND_TOP/BOTTOM below is the mark's own footprint, kept
            // clear of the true edge.
            const float bobAmplitude = fmaxf(10.0f, effH / 2.0f - 40.0f);
            const float bob = sinf(static_cast<float>(millis()) * 0.00035f) * bobAmplitude;
            const int16_t cy = static_cast<int16_t>(effH / 2.0f + bob);

            // The mark moves a little every frame; drawString only clears
            // behind its own new position, so the old spot was left
            // un-erased and built up into a smeared trail. Erase the whole
            // band it can occupy before drawing the new frame.
            constexpr int16_t TEXT_BAND_TOP = 26;    // above centre line
            constexpr int16_t TEXT_BAND_BOTTOM = 22; // below centre line
            if (ssav_textCy_ >= 0) {
                tft.fillRect(0, static_cast<int16_t>(ssav_textCy_ - TEXT_BAND_TOP), effW,
                             TEXT_BAND_TOP + TEXT_BAND_BOTTOM, TFT_BLACK);
            }
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(Ui::rgb(120, 128, 150), TFT_BLACK);
            tft.drawString("GoodTime Micro", effW / 2, static_cast<int16_t>(cy - 10), 4);
            tft.setTextColor(Ui::rgb(70, 76, 92), TFT_BLACK);
            tft.drawString("(C) GoodTime Micro Company",
                           effW / 2, static_cast<int16_t>(cy + 14), 1);
            tft.setTextDatum(TL_DATUM);
            ssav_textCy_ = cy;
        }

        // Draw court centre line
        for (int16_t y = 0; y < effH; y += 14) {
            tft.fillRect(effW/2 - 1, y, 2, 8, Ui::rgb(40,40,40));
        }

        // Draw battery badge at the top middle over the centre line
        const int16_t batCx = effW / 2;
        const int16_t batCy = 14;
        tft.fillRect(static_cast<int16_t>(batCx - 12), static_cast<int16_t>(batCy - 6), 24, 12, TFT_BLACK);
        Ui::drawBatteryBadge(tft, batCx, batCy,
                             board_.getBatteryPercent(),
                             board_.getPowerSource() == Board::PowerState::EXTERNAL_POWER,
                             TFT_BLACK);

        // Draw paddles
        tft.fillRoundRect(LX - PAD_W/2, static_cast<int16_t>(ssav_ly_ - PAD_H/2), PAD_W, PAD_H, 3, ssav_color_);
        tft.fillRoundRect(RX - PAD_W/2, static_cast<int16_t>(ssav_ry_ - PAD_H/2), PAD_W, PAD_H, 3, ssav_color_);
        // Ball takes the rally colour too, with a white core so it stays visible.
        tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL), static_cast<int16_t>(ssav_by_ - BALL), BALL*2, BALL*2, 2, ssav_color_);
        tft.fillRoundRect(static_cast<int16_t>(ssav_bx_ - BALL/2), static_cast<int16_t>(ssav_by_ - BALL/2), BALL, BALL, 1, TFT_WHITE);
    }
    
    Board board_;
    ContentLoader content_;
    TicTacToeGame ticTacToe_;
    MemoryGame memory_;
    MathGame math_;
    MultiplicationGame multiplication_;
    TimeGame time_;
    WhackAMoleGame whackAMole_;
    CinnamonGame cinnamon_;
    MicrokuGame microku_;
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
    StateFlagGame stateFlag_;
    StateMapGame stateMap_;
    TraceGame trace_;
    PercentCircleGame percent_;
    GreWordsGame greWords_;
    ProfileGame profile_;
    ScoresGame scores_;
    NumberLineGame numberLine_;
    FlagGame flag_;
    AboutGame about_;
    SystemInfoGame systemInfo_;
    Game* activeGame_ = nullptr;
    EntryKind activeKind_ = EntryKind::About;   // labels the watchdog context
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
    int16_t ssav_textCy_ = -1;  // previous frame's company-mark centre y, -1 = not drawn yet
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
