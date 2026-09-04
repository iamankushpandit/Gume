#include "SettingsGame.h"

#include "engine/NearbyPlay.h"
#include "hal/Board.h"

/* The tab bodies -- Device, Power and Sound -- and the geometry of their rows.
 *
 * Every row here is a Rect accessor plus a renderer that draws into it, and
 * the touch handler in SettingsGame.cpp reads the same accessor. That is the
 * whole reason the geometry lives beside the drawing rather than in the
 * handler: a control the player can see and cannot press is the failure this
 * arrangement exists to make impossible.
 *
 * Admin lives in SettingsPin.cpp with the pad it opens. */

/* Device tab: four rows of 30px, then the brightness slider.
 *
 * Network shares a row with the NTP resync cadence: both are clock-related
 * global settings, and keeping the row compact leaves the brightness slider
 * where the existing muscle memory expects it. Reset keeps a slot of its own
 * on the last row rather than sitting beside a harmless toggle -- it is the
 * one destructive control here.
 *
 * Nearby is deliberately next to Beacon's row rather than beside it: it does
 * nothing unless Beacon is on, and reading downwards is the order you have to
 * turn them on in. */
/* ---- the shared grid -------------------------------------------------
 *
 * Every control on every tab sits on one grid, so a taller or wider panel
 * moves them all together rather than one tab at a time. The arithmetic is
 * chosen to reproduce the original 320x240 numbers exactly: at that size the
 * pitch works out to 34 and the columns to 144 wide at x = 8 and x = 164,
 * which is where they have always been.
 *
 * On a bigger panel the rows spread and the buttons grow with them, capped so
 * a very tall screen does not turn four controls into four slabs. Larger
 * targets are the point on this board -- it is here for players who want a
 * plainer screen and an easier thing to hit. */
namespace {
constexpr int16_t SETTINGS_MARGIN = 8;
constexpr int16_t SETTINGS_GAP = 12;
constexpr int16_t SETTINGS_TOP = 58;
constexpr int16_t SETTINGS_BOTTOM_RESERVE = 36;   // the brightness bar
constexpr int16_t SETTINGS_MIN_PITCH = 34;
constexpr int16_t SETTINGS_MAX_ROW_H = 44;
}  // namespace

int16_t SettingsGame::gridRowPitch() const {
    const int16_t avail = static_cast<int16_t>(
        panelH_ - SETTINGS_BOTTOM_RESERVE - SETTINGS_MARGIN - SETTINGS_TOP);
    const int16_t pitch = static_cast<int16_t>(avail / 4);
    return pitch < SETTINGS_MIN_PITCH ? SETTINGS_MIN_PITCH : pitch;
}

int16_t SettingsGame::gridRowHeight() const {
    const int16_t h = static_cast<int16_t>(gridRowPitch() - 4);
    return h > SETTINGS_MAX_ROW_H ? SETTINGS_MAX_ROW_H : h;
}

Rect SettingsGame::gridCell(uint8_t row, uint8_t col) const {
    const int16_t colW = static_cast<int16_t>(
        (panelW_ - SETTINGS_MARGIN - SETTINGS_GAP - SETTINGS_GAP) / 2);
    const int16_t x = col == 0
        ? SETTINGS_MARGIN
        : static_cast<int16_t>(SETTINGS_MARGIN + colW + SETTINGS_GAP);
    return Rect{x, static_cast<int16_t>(SETTINGS_TOP + row * gridRowPitch()),
                colW, gridRowHeight()};
}

Rect SettingsGame::gridWide(uint8_t row) const {
    return Rect{SETTINGS_MARGIN,
                static_cast<int16_t>(SETTINGS_TOP + row * gridRowPitch()),
                static_cast<int16_t>(panelW_ - SETTINGS_MARGIN * 2),
                gridRowHeight()};
}

Rect SettingsGame::themeRect()   const { return gridCell(0, 0); }
Rect SettingsGame::layoutRect()  const { return gridCell(0, 1); }
Rect SettingsGame::lightRect()   const { return gridCell(1, 0); }
Rect SettingsGame::bleRect()     const { return gridCell(1, 1); }
Rect SettingsGame::wifiRect()    const { return gridCell(2, 0); }
Rect SettingsGame::ntpSyncRect() const { return gridCell(2, 1); }
Rect SettingsGame::nearbyRect()  const { return gridCell(3, 0); }
Rect SettingsGame::resetRect()   const { return gridCell(3, 1); }

/* The brightness bar is pinned to the bottom rather than following the grid:
 * it is the only continuous control here and it reads as a footer. */
Rect SettingsGame::brightRect() const {
    return Rect{SETTINGS_MARGIN,
                static_cast<int16_t>(panelH_ - SETTINGS_BOTTOM_RESERVE),
                static_cast<int16_t>(panelW_ - SETTINGS_MARGIN * 2), 32};
}

/* Sound tab.
 *
 * Mute is a full-width row of its own because it is the control most people
 * come to this tab for and it should not have to be found. Below it the
 * volume slider, then the two test buttons side by side.
 *
 * The vertical stack is measured against 240px: tab baseline at 52, mute row
 * 58-88, the level readout on its own line at 96, the slider 104-136, the test
 * buttons 148-178, and two lines of explanation at 190 and 206 -- which leaves
 * 34px of margin at the bottom. There is room for one more row here and not
 * two. */
Rect SettingsGame::muteRect()      const { return gridWide(0); }
/* Takes the grid's position but keeps its designed height.
 *
 * A button gets easier to hit as it grows; a slider does not -- what matters
 * is the length of its travel, which is already the full width. Letting it
 * take the grid's row height made the rect 46px tall, and drawSlider centres
 * the track inside the rect, so the track sank while the "Volume" caption
 * stayed pinned above the rect's top edge and was left floating in the gap.
 * The brightness bar has always been a fixed 32 for the same reason. */
Rect SettingsGame::volumeRect()    const {
    const Rect r = gridWide(1);
    return Rect{r.x, r.y, r.w, 32};
}
Rect SettingsGame::testCueRect()   const { return gridCell(2, 0); }
Rect SettingsGame::testVoiceRect() const { return gridCell(2, 1); }

/* Power tab: four full-width rows, so the labels have room to say what the
 * setting actually does rather than abbreviating to fit half a screen.
 *
 * Wake lock sits with them because it is the last thing in the idle sequence:
 * saver, sleep, then what it takes to get back. */
Rect SettingsGame::idleActionRect() const { return gridWide(0); }
Rect SettingsGame::idleAfterRect()  const { return gridWide(1); }
Rect SettingsGame::sleepAfterRect() const { return gridWide(2); }
Rect SettingsGame::wakeLockRect()   const { return gridWide(3); }

bool SettingsGame::sleepRowActive(Board& board) const {
    return board.idleAction() == Board::IdleAction::SaverThenSleep;
}

void SettingsGame::cycleScreenSaver(Board& board) {
    const uint16_t current = board.screenSaverSeconds();
    const uint16_t next = current < 60 ? 60 : (current < 120 ? 120 : (current < 300 ? 300 : 30));
    board.setScreenSaverSeconds(next);
}

void SettingsGame::cycleSleepSeconds(Board& board) {
    const uint16_t current = board.sleepSeconds();
    const uint16_t next = current < 30 ? 30 : (current < 60 ? 60 : (current < 120 ? 120 :
                          (current < 300 ? 300 : 15)));
    board.setSleepSeconds(next);
}

void SettingsGame::cycleIdleAction(Board& board) {
    switch (board.idleAction()) {
        case Board::IdleAction::SaverThenSleep:
            board.setIdleAction(Board::IdleAction::SleepOnly); break;
        case Board::IdleAction::SleepOnly:
            board.setIdleAction(Board::IdleAction::SaverOnly); break;
        default:
            board.setIdleAction(Board::IdleAction::SaverThenSleep); break;
    }
}

void SettingsGame::cycleNtpResyncHours(Board& board) {
    const uint8_t current = board.ntpResyncHours();
    const uint8_t next = current >= Board::NTP_RESYNC_MAX_HOURS
        ? Board::NTP_RESYNC_MIN_HOURS
        : static_cast<uint8_t>(current + 1);
    board.setNtpResyncHours(next);
}

void SettingsGame::renderDeviceTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const bool admin = isAdmin(board);

    char label[28];
    snprintf(label, sizeof(label), "Theme: %s",
             board.themeMode() == Board::ThemeMode::Dark ? "Dark" : "Light");
    Ui::drawButton(tft, themeRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    snprintf(label, sizeof(label), "Menu: %s",
             board.layoutMode() == Board::LayoutMode::Horizontal ? "Horizontal" : "Vertical");
    Ui::drawButton(tft, layoutRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    snprintf(label, sizeof(label), "Light: %s", board.rgbEnabled() ? "On" : "Off");
    Ui::drawButton(tft, lightRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    snprintf(label, sizeof(label), "Beacon: %s", board.bleBeaconEnabled() ? "On" : "Off");
    Ui::drawButton(tft, bleRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    Ui::drawButton(tft, wifiRect(), "Network", admin ? Ui::rgb(36, 132, 204) : Ui::rgb(80, 80, 80),
                   Ui::outline(), admin ? TFT_WHITE : Ui::muted(), false, 2);
    snprintf(label, sizeof(label), "Sync: %uh", static_cast<unsigned>(board.ntpResyncHours()));
    Ui::drawButton(tft, ntpSyncRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);
    /* Greyed, not hidden, when the beacon is off: the control is what tells
     * you the feature exists and what it depends on. */
    const bool beaconOn = board.bleBeaconEnabled();
    snprintf(label, sizeof(label), "Nearby: %s",
             !beaconOn ? "needs Beacon" : (NearbyPlay::enabled() ? "On" : "Off"));
    const bool nearbyTextEnabled = admin && beaconOn;
    Ui::drawButton(tft, nearbyRect(), label,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), nearbyTextEnabled ? Ui::text() : Ui::muted(), false, 2);
    Ui::drawButton(tft, resetRect(),
                   confirmReset_ ? "Tap to ERASE" : "Reset device",
                   admin ? (confirmReset_ ? Ui::rgb(220, 40, 40) : Ui::rgb(120, 58, 58)) : Ui::rgb(80, 80, 80),
                   Ui::outline(), admin ? TFT_WHITE : Ui::muted(), false, 2);

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    if (!admin) {
        tft.drawString("Settings locked. Only Admin can change.", 8,
                       static_cast<int16_t>(brightRect().y - 10), 1);
    } else {
        tft.drawString(confirmReset_ ? "Erases scores, names, Wi-Fi and settings"
                                 : "Brightness", 8,
                       static_cast<int16_t>(brightRect().y - 10), 1);
        tft.setTextDatum(TR_DATUM);
        snprintf(label, sizeof(label), "%u%%", board.brightness());
        tft.drawString(label, panelW_ - 8, static_cast<int16_t>(brightRect().y - 10), 1);
        tft.setTextDatum(TL_DATUM);
        Ui::drawSlider(tft, brightRect(), board.brightness(), Board::BRIGHTNESS_MIN);
    }
}

void SettingsGame::renderPowerTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const bool admin = isAdmin(board);

    const Board::IdleAction action = board.idleAction();
    const uint16_t idleSecs  = board.screenSaverSeconds();
    const uint16_t sleepSecs = board.sleepSeconds();

    const char* actionName =
        action == Board::IdleAction::SaverThenSleep ? "Saver then sleep" :
        action == Board::IdleAction::SleepOnly      ? "Sleep only"       : "Saver only";

    char buf[52];
    snprintf(buf, sizeof(buf), "When idle: %s", actionName);
    Ui::drawButton(tft, idleActionRect(), buf,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);

    if (idleSecs >= 60 && idleSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Idle after: %um", idleSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Idle after: %us", idleSecs);
    }
    Ui::drawButton(tft, idleAfterRect(), buf,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(), admin ? Ui::text() : Ui::muted(), false, 2);

    /* The sleep delay only means anything when the saver hands over to sleep.
     * The other two policies say so on the button rather than leaving a live
     * control that quietly does nothing. */
    const bool sleepLive = sleepRowActive(board) && admin;
    if (!sleepLive) {
        snprintf(buf, sizeof(buf), "Sleep after: %s",
                 action == Board::IdleAction::SleepOnly ? "(at idle)" : "--");
    } else if (sleepSecs >= 60 && sleepSecs % 60 == 0) {
        snprintf(buf, sizeof(buf), "Sleep after: %um", sleepSecs / 60);
    } else {
        snprintf(buf, sizeof(buf), "Sleep after: %us", sleepSecs);
    }
    Ui::drawButton(tft, sleepAfterRect(), buf,
                   sleepLive ? Ui::panel() : Ui::surface(), Ui::outline(),
                   sleepLive ? Ui::text() : Ui::muted(), false, 2);

    /* Always live: it guards the saver as well as sleep, so it means
     * something under all three idle policies. */
    snprintf(buf, sizeof(buf), "Hold to unlock: %s",
             board.wakeLockEnabled() ? "On" : "Off");
    Ui::drawButton(tft, wakeLockRect(), buf,
                   admin ? Ui::panel() : Ui::surface(), Ui::outline(),
                   admin ? Ui::text() : Ui::muted(), false, 2);

    /* Say the resulting behaviour in plain words, composed from the live
     * values -- three settings that interact are hard to hold in your head. */
    char explain[64];
    if (!admin) {
        snprintf(explain, sizeof(explain), "Settings locked. Only Admin can change.");
    } else {
        switch (action) {
            case Board::IdleAction::SleepOnly:
                snprintf(explain, sizeof(explain), "Screen off at %us. No screen saver.", idleSecs);
                break;
            case Board::IdleAction::SaverOnly:
                snprintf(explain, sizeof(explain), "Saver at %us. Screen never turns off.", idleSecs);
                break;
            default:
                snprintf(explain, sizeof(explain), "Saver at %us, screen off at %us.",
                         idleSecs, static_cast<uint16_t>(idleSecs + sleepSecs));
                break;
        }
    }
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    const int16_t powerFootY =
        static_cast<int16_t>(wakeLockRect().y + wakeLockRect().h + 6);
    tft.drawString(explain, 8, powerFootY, 1);
    if (admin) {
        /* Say what a touch actually does, because the row above changes it.
         * A stray press in a bag is the case this exists for. */
        tft.drawString(board.wakeLockEnabled()
                           ? "A touch lights the screen; hold to go back."
                           : "Any touch goes straight back to what you were doing.",
                       8, static_cast<int16_t>(powerFootY + 16), 1);
    }
}

/* Sound.
 *
 * Three states, not two, and the difference matters to whoever is holding it:
 * this board cannot make a sound at all; it can and is muted; it can and is
 * not. Collapsing the first into the second would tell an owner their console
 * is muted and leave them looking for the switch that would fix it.
 *
 * The level is shown as the number the codec is actually set to, and the
 * slider's travel ends at AUDIO_VOLUME_MAX. Relabelling that ceiling as 100%
 * would make the control read better and lie -- see the note on drawSlider's
 * maxPct in Ui.h. */
void SettingsGame::renderSoundTab(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const bool admin = isAdmin(board);
    const bool present = Board::hasSound();
    const bool on = present && board.soundEnabled();
    /* Live means "this control will do something if you press it": the board
     * has a speaker, the sound is not muted, and you are the admin. */
    const bool live = on && admin;

    char label[40];
    if (!present) {
        snprintf(label, sizeof(label), "Sound: not on this board");
    } else {
        snprintf(label, sizeof(label), "Sound: %s", on ? "On" : "Muted");
    }
    Ui::drawButton(tft, muteRect(), label,
                   (admin && present) ? Ui::panel() : Ui::surface(), Ui::outline(),
                   (admin && present) ? Ui::text() : Ui::muted(), false, 2);

    /* The footnotes sit under the test buttons, so they move with them. They
     * were at a fixed 190/206, which was under the buttons at 240 tall and
     * straight through them on a taller panel. */
    const int16_t soundFootY =
        static_cast<int16_t>(testCueRect().y + testCueRect().h + 12);

    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Volume", 8, static_cast<int16_t>(volumeRect().y - 12), 1);
    tft.setTextDatum(TR_DATUM);
    if (!present) {
        snprintf(label, sizeof(label), "--");
    } else if (!on) {
        /* The stored level is still shown while muted, greyed. It is what
         * unmuting will come back to, and hiding it makes the switch look
         * like it also reset the volume. */
        snprintf(label, sizeof(label), "%u%% (muted)", board.volume());
    } else {
        snprintf(label, sizeof(label), "%u%%", board.volume());
    }
    tft.drawString(label, panelW_ - 8, static_cast<int16_t>(volumeRect().y - 12), 1);
    tft.setTextDatum(TL_DATUM);

    Ui::drawSlider(tft, volumeRect(), present ? board.volume() : 0, 0,
                   Board::AUDIO_VOLUME_MAX);

    Ui::drawButton(tft, testCueRect(), "Test sound",
                   live ? Ui::rgb(36, 132, 204) : Ui::rgb(80, 80, 80),
                   Ui::outline(), live ? TFT_WHITE : Ui::muted(), false, 2);
    Ui::drawButton(tft, testVoiceRect(), "Say hello",
                   live ? Ui::rgb(36, 132, 204) : Ui::rgb(80, 80, 80),
                   Ui::outline(), live ? TFT_WHITE : Ui::muted(), false, 2);

    /* Two lines, and both are measured to fit 320px at font 1 -- the longest
     * of them is the not-admin one. Keep any replacement under about 52
     * characters: TFT_eSPI drops characters off the right edge silently. */
    tft.setTextColor(Ui::muted(), Ui::bg());
    if (!admin) {
        tft.drawString("Settings locked. Only Admin can change.", 8, soundFootY, 1);
    } else if (!present) {
        tft.drawString("This board has no speaker or audio codec.", 8, soundFootY, 1);
        tft.drawString("The case LED still flashes for right and wrong.", 8,
                       static_cast<int16_t>(soundFootY + 16), 1);
    } else if (!on) {
        tft.drawString("Muted: no beeps, no cues, no startup voice.", 8, soundFootY, 1);
        tft.drawString("The case LED still flashes for right and wrong.", 8,
                       static_cast<int16_t>(soundFootY + 16), 1);
    } else {
        tft.drawString("Volume is capped for young ears.", 8, soundFootY, 1);
        tft.drawString("Every sound is made by the device, not a file.", 8,
                       static_cast<int16_t>(soundFootY + 16), 1);
    }
    tft.setTextDatum(TL_DATUM);
}
