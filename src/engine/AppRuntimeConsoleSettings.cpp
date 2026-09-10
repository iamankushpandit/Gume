#include "AppRuntime.h"

#include "engine/ConsoleText.h"
#include "engine/NearbyPlay.h"

using namespace ConsoleText;

/* DEVICE SETTINGS OVER THE CONSOLE: ONE TABLE, READ AND UPDATE.
 *
 * Settings are not rows that come and go, so their CRUD is Read and Update:
 * `get` reports every key (or one, with the values it accepts) and `set`
 * changes one. Every row is a setting some screen already offers -- Settings
 * or Wi-Fi -- with the same choices that screen offers, and its setter is the
 * Board call that screen makes. A key the glass cannot reach does not belong
 * here either.
 *
 * Deliberately not settable: the update check (not declinable, by the privacy
 * statement), the NTP server (it is where a request goes), and anything
 * about the admin. Add a row only for something the owner can already change
 * on the device, and state its choices the way that screen does.
 *
 * Global device settings, not per-player: see the list in CLAUDE.md. The
 * per-player ones (game visibility) are in AppRuntimeConsoleProfiles.cpp. */
namespace {

constexpr uint16_t SAVER_CHOICES[] = {30, 60, 120, 300};      // SettingsPanels
constexpr uint16_t SLEEP_CHOICES[] = {15, 30, 60, 120, 300};  // SettingsPanels

struct Named { const char* name; uint8_t value; };
constexpr Named IDLE_NAMES[] = {
    {"saver-then-sleep", static_cast<uint8_t>(Board::IdleAction::SaverThenSleep)},
    {"sleep", static_cast<uint8_t>(Board::IdleAction::SleepOnly)},
    {"saver", static_cast<uint8_t>(Board::IdleAction::SaverOnly)},
};

template <size_t N>
bool oneOf(const char* s, const uint16_t (&choices)[N], uint16_t& out) {
    long v = 0;
    if (!parseRange(s, 0, 65535, v)) return false;
    for (uint16_t c : choices) {
        if (c == v) { out = c; return true; }
    }
    return false;
}

void onOff(char* out, size_t cap, bool on) { snprintf(out, cap, "%s", on ? "on" : "off"); }

void listThemes(Print& out) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(Board::ThemeMode::Count); ++i) {
        if (i) out.print('|');
        out.print(Ui::themeName(static_cast<Ui::Theme>(i)));
    }
}

void listZones(Print& out) {
    for (uint8_t i = 0; i < Board::tzZoneCount(); ++i) {
        if (i) out.print('|');
        out.print(Board::tzZoneName(i));
    }
}

void listVolume(Print& out) { out.printf("0-%u", (unsigned)Board::AUDIO_VOLUME_MAX); }

void listNtpHours(Print& out) {
    out.printf("%u-%u", (unsigned)Board::NTP_RESYNC_MIN_HOURS,
               (unsigned)Board::NTP_RESYNC_MAX_HOURS);
}

}  // namespace

/* The lambdas are defined inside a member function, so they may use
 * BrainoApp's private members through the reference they are handed. */
const BrainoApp::ConsoleSetting* BrainoApp::consoleSettings(size_t& count) {
    static const ConsoleSetting TABLE[] = {
        {"theme", nullptr, listThemes,
         [](BrainoApp& a, char* o, size_t n) {
             snprintf(o, n, "%s", Ui::themeName(static_cast<Ui::Theme>(a.board_.themeMode())));
         },
         [](BrainoApp& a, const char* v) -> const char* {
             for (uint8_t i = 0; i < static_cast<uint8_t>(Board::ThemeMode::Count); ++i) {
                 const Ui::Theme t = static_cast<Ui::Theme>(i);
                 if (strcasecmp(v, Ui::themeName(t)) == 0) {
                     a.board_.setThemeMode(static_cast<Board::ThemeMode>(i));
                     Ui::setTheme(t);
                     return nullptr;
                 }
             }
             return "range unknown theme";
         }},
        {"brightness", "25-100", nullptr,
         [](BrainoApp& a, char* o, size_t n) { snprintf(o, n, "%u", (unsigned)a.board_.brightness()); },
         [](BrainoApp& a, const char* v) -> const char* {
             long x = 0;
             if (!parseRange(v, Board::BRIGHTNESS_MIN, 100, x)) return "range brightness is 25-100";
             a.board_.setBrightness(static_cast<uint8_t>(x));
             return nullptr;
         }},
        {"layout", "horizontal|vertical", nullptr,
         [](BrainoApp& a, char* o, size_t n) {
             snprintf(o, n, "%s", a.board_.layoutMode() == Board::LayoutMode::Vertical
                                      ? "vertical" : "horizontal");
         },
         [](BrainoApp& a, const char* v) -> const char* {
             Board::LayoutMode m;
             if (strcasecmp(v, "horizontal") == 0) m = Board::LayoutMode::Horizontal;
             else if (strcasecmp(v, "vertical") == 0) m = Board::LayoutMode::Vertical;
             else return "range layout is horizontal|vertical";
             a.board_.setLayoutMode(m);
             // What launch() does: the screen that is up takes the new rotation.
             if (a.view_ == View::Game && a.activeGame_) {
                 a.applyRotation(a.rotationForActiveScreen());
             }
             return nullptr;
         }},
        {"sound", "on|off", nullptr,
         [](BrainoApp& a, char* o, size_t n) { onOff(o, n, a.board_.soundEnabled()); },
         [](BrainoApp& a, const char* v) -> const char* {
             bool on = false;
             if (!parseOnOff(v, on)) return "range sound is on|off";
             a.board_.setSoundEnabled(on);
             return nullptr;
         }},
        {"volume", nullptr, listVolume,
         [](BrainoApp& a, char* o, size_t n) { snprintf(o, n, "%u", (unsigned)a.board_.volume()); },
         [](BrainoApp& a, const char* v) -> const char* {
             long x = 0;
             if (!parseRange(v, 0, Board::AUDIO_VOLUME_MAX, x)) return "range volume is above this board's ceiling";
             a.board_.setVolume(static_cast<uint8_t>(x));
             return nullptr;
         }},
        {"saver", "30|60|120|300", nullptr,
         [](BrainoApp& a, char* o, size_t n) { snprintf(o, n, "%u", (unsigned)a.board_.screenSaverSeconds()); },
         [](BrainoApp& a, const char* v) -> const char* {
             uint16_t s = 0;
             if (!oneOf(v, SAVER_CHOICES, s)) return "range saver is 30|60|120|300 seconds";
             a.board_.setScreenSaverSeconds(s);
             return nullptr;
         }},
        {"sleep", "15|30|60|120|300", nullptr,
         [](BrainoApp& a, char* o, size_t n) { snprintf(o, n, "%u", (unsigned)a.board_.sleepSeconds()); },
         [](BrainoApp& a, const char* v) -> const char* {
             uint16_t s = 0;
             if (!oneOf(v, SLEEP_CHOICES, s)) return "range sleep is 15|30|60|120|300 seconds";
             a.board_.setSleepSeconds(s);
             return nullptr;
         }},
        {"idle", "saver-then-sleep|sleep|saver", nullptr,
         [](BrainoApp& a, char* o, size_t n) {
             const uint8_t cur = static_cast<uint8_t>(a.board_.idleAction());
             const char* name = "saver-then-sleep";
             for (const Named& e : IDLE_NAMES) if (e.value == cur) name = e.name;
             snprintf(o, n, "%s", name);
         },
         [](BrainoApp& a, const char* v) -> const char* {
             for (const Named& e : IDLE_NAMES) {
                 if (strcasecmp(v, e.name) == 0) {
                     a.board_.setIdleAction(static_cast<Board::IdleAction>(e.value));
                     return nullptr;
                 }
             }
             return "range idle is saver-then-sleep|sleep|saver";
         }},
        {"wakelock", "on|off", nullptr,
         [](BrainoApp& a, char* o, size_t n) { onOff(o, n, a.board_.wakeLockEnabled()); },
         [](BrainoApp& a, const char* v) -> const char* {
             bool on = false;
             if (!parseOnOff(v, on)) return "range wakelock is on|off";
             a.board_.setWakeLockEnabled(on);
             return nullptr;
         }},
        {"light", "on|off", nullptr,
         [](BrainoApp& a, char* o, size_t n) { onOff(o, n, a.board_.rgbEnabled()); },
         [](BrainoApp& a, const char* v) -> const char* {
             bool on = false;
             if (!parseOnOff(v, on)) return "range light is on|off";
             a.board_.setRgbEnabled(on);
             return nullptr;
         }},
        {"beacon", "on|off", nullptr,
         [](BrainoApp& a, char* o, size_t n) { onOff(o, n, a.board_.bleBeaconEnabled()); },
         [](BrainoApp& a, const char* v) -> const char* {
             bool on = false;
             if (!parseOnOff(v, on)) return "range beacon is on|off";
             a.board_.setBleBeaconEnabled(on);
             return nullptr;
         }},
        {"nearby", "on|off (needs the beacon)", nullptr,
         [](BrainoApp& a, char* o, size_t n) { onOff(o, n, a.board_.nearbyEnabled()); },
         [](BrainoApp& a, const char* v) -> const char* {
             bool on = false;
             if (!parseOnOff(v, on)) return "range nearby is on|off";
             // The same refusal Settings makes: Nearby rides on the beacon.
             if (on && !a.board_.bleBeaconEnabled()) return "refused the beacon is off";
             NearbyPlay::setEnabled(a.board_, on);
             return nullptr;
         }},
        {"ntp", "on|off", nullptr,
         [](BrainoApp& a, char* o, size_t n) { onOff(o, n, a.board_.ntpEnabled()); },
         [](BrainoApp& a, const char* v) -> const char* {
             bool on = false;
             if (!parseOnOff(v, on)) return "range ntp is on|off";
             a.board_.setNtpEnabled(on);
             return nullptr;
         }},
        {"ntp_hours", nullptr, listNtpHours,
         [](BrainoApp& a, char* o, size_t n) { snprintf(o, n, "%u", (unsigned)a.board_.ntpResyncHours()); },
         [](BrainoApp& a, const char* v) -> const char* {
             long x = 0;
             if (!parseRange(v, Board::NTP_RESYNC_MIN_HOURS, Board::NTP_RESYNC_MAX_HOURS, x)) {
                 return "range ntp_hours is 1-24";
             }
             a.board_.setNtpResyncHours(static_cast<uint8_t>(x));
             return nullptr;
         }},
        {"timezone", nullptr, listZones,
         [](BrainoApp& a, char* o, size_t n) {
             snprintf(o, n, "%s", a.board_.tzZoneChosen()
                                      ? Board::tzZoneName(a.board_.tzZoneIndex()) : "auto");
         },
         [](BrainoApp& a, const char* v) -> const char* {
             for (uint8_t i = 0; i < Board::tzZoneCount(); ++i) {
                 if (strcasecmp(v, Board::tzZoneName(i)) == 0) {
                     a.board_.setTzZoneIndex(i);
                     return nullptr;
                 }
             }
             return "range unknown zone -- get timezone lists them";
         }},
    };
    count = sizeof(TABLE) / sizeof(TABLE[0]);
    return TABLE;
}

namespace {
// A template so it need not name BrainoApp's private row type.
template <typename Row>
const Row* findSetting(const Row* table, size_t count, const char* key) {
    for (size_t i = 0; i < count; ++i) {
        if (strcasecmp(key, table[i].key) == 0) return &table[i];
    }
    return nullptr;
}
}  // namespace

/* `get` alone: every setting on one line, plus three facts that are not
 * settings but answer "is this board set up?" -- counts and flags only, never
 * a name. `get <key>`: its value and what `set` accepts. */
void BrainoApp::cmdGet(int argc, char** argv, Print& out) {
    size_t count = 0;
    const ConsoleSetting* table = consoleSettings(count);
    char value[40];
    if (argc == 1) {
        const ConsoleSetting* s = findSetting(table, count, argv[0]);
        if (!s) { out.println("err unknown setting -- get lists them"); return; }
        s->get(*this, value, sizeof(value));
        out.printf("ok key=\"%s\" value=\"%s\" values=\"", s->key, value);
        if (s->listValues) s->listValues(out); else out.print(s->values);
        out.println('"');
        return;
    }
    out.print("ok v=\"1\"");
    for (size_t i = 0; i < count; ++i) {
        table[i].get(*this, value, sizeof(value));
        out.printf(" %s=\"%s\"", table[i].key, value);
    }
    out.printf(" wifi=\"%d\" profiles=\"%u\" admin=\"%d\"\n",
               (int)board_.hasWifiCredentials(), (unsigned)board_.playerCount(),
               (int)(board_.adminProfileIndex() != Board::GUEST_INDEX));
}

void BrainoApp::cmdSet(int, char** argv, Print& out) {
    size_t count = 0;
    const ConsoleSetting* table = consoleSettings(count);
    const ConsoleSetting* s = findSetting(table, count, argv[0]);
    if (!s) { out.println("err unknown setting -- get lists them"); return; }
    if (const char* err = s->set(*this, argv[1])) {
        out.printf("err %s\n", err);
        return;
    }
    repaintAfterConsoleChange();
    char value[40];
    s->get(*this, value, sizeof(value));   // read back what actually stuck
    out.printf("ok %s=\"%s\"\n", s->key, value);
}
