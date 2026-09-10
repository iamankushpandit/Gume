#include "AppRuntime.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "engine/AppCapabilities.h"
#include "engine/NearbyPlay.h"

/* THE SERIAL CONSOLE: ONE READER, ONE TABLE, ONE REPLY GRAMMAR.
 *
 * A bench of boards used to be configured by hand -- profiles, theme,
 * brightness, Nearby, Wi-Fi -- through a resistive touchscreen after every
 * flash. tools/configure_boards.py now does it from one config file, over the
 * line protocol `identify?` introduced. This is also the first piece of the
 * layered design being planned (docs/FRAMEWORK_PLAN.md on the platform-
 * separation branch): a shell that calls the same services the UI does, so
 * nothing is reachable only through the glass. So it is built to grow:
 *
 *   ONE READER   tickSerialQuery(): a fixed buffer, at most
 *                CONSOLE_BYTES_PER_FRAME bytes a loop, split into argv with
 *                "double quotes" grouping a value that has spaces in it.
 *   ONE TABLE    consoleTable(): name, usage, help, capability, handler.
 *                `help` is derived from it. A new command is a row, never
 *                another string match.
 *   ONE GRAMMAR  every command answers with exactly one line:
 *                  ok key="value" ...
 *                  err <code> <message>
 *                so every tool parses every command the same way.
 *
 * Commands:
 *
 *   identify           ok v="1" device=... board=... version=...   (alias identify?)
 *   settings           ok v="1" theme=... brightness=... ...       (alias settings?)
 *   help [command]     ok commands="..."  |  ok usage=... help=... gated=...
 *   unlock 1234        admin PIN; opens writes for UNLOCK_MS after the last
 *   lock
 *   theme Midnight     any Ui::themeName(), case-insensitive
 *   brightness 60      BRIGHTNESS_MIN..100
 *   beacon on|off
 *   nearby on|off      refused while the beacon is off
 *   wifi "name" "pw"   saved, then time sync starts;  wifi clear
 *   profile-add "Sam"  refused when full; an existing name is not an error
 *
 * Four rules, each the reason for code below:
 *
 *   - READS ARE OPEN, WRITES NEED THE ADMIN PIN. The gate is one function,
 *     needsUnlock(), keyed on the row's capability, so a new write command
 *     cannot forget it. CLAUDE.md: any route to admin powers needs the PIN.
 *     PIN_TRIES wrong guesses lock the console out for PIN_LOCKOUT_MS, and
 *     only exactly four digits are judged -- a PIN's digit count is not
 *     derivable from its value, which both on-screen pads had to learn.
 *   - THE SAME DOORS AS THE SCREENS. Each write is the Board setter the
 *     Settings/Wi-Fi/Profiles screens call, with the same refusals, and then
 *     whatever screen is showing the old value repaints.
 *   - NOTHING PERSONAL COMES BACK. `settings` is counts and flags, never a
 *     profile name or the network's name; a password is never echoed; the
 *     line buffer is wiped after every command. Serial logs get pasted.
 *   - NOT ACTIVITY. Nothing here touches lastActivityMs_, so configuring a
 *     bench does not keep screens awake or disturb what another agent is
 *     testing, and a sleeping board stays asleep (Board::setBrightness()).
 *
 * Serial only. A console over Wi-Fi or BLE would be a new outbound flow under
 * the closed privacy list, not an extension of this one.
 *
 * Deliberately absent: factory reset, removing profiles, reading scores.
 * Adding a command that changes the device or reads player data is a decision
 * for the maintainer, not a convenience.
 *
 * Cost: one Serial.available() a loop when idle. A command is rare and may
 * take a few milliseconds (an NVS write, a reply draining); it never runs per
 * frame. */
namespace {

constexpr size_t LINE_CAP = 128;             // `wifi "<32>" "<63>"` fits
constexpr int CONSOLE_BYTES_PER_FRAME = 64;
constexpr size_t MAX_ARGS = 4;
constexpr uint32_t UNLOCK_MS = 120000;
constexpr uint8_t PIN_TRIES = 3;
constexpr uint32_t PIN_LOCKOUT_MS = 30000;
constexpr size_t SSID_MAX = 32;              // 802.11
constexpr size_t PASS_MAX = 63;              // WPA2 passphrase

// Everything that changes the device. Reads (status, diagnostics) are open.
constexpr uint32_t WRITE_CAPS = APP_CAP_DEVICE_SETTINGS | APP_CAP_NETWORK |
                                APP_CAP_PROFILES | APP_CAP_SCORES |
                                APP_CAP_FACTORY_RESET;

char lineBuf[LINE_CAP];
size_t lineLen = 0;
bool lineOverlong = false;

bool unlocked = false;
uint32_t unlockedUntilMs = 0;
uint8_t badPins = 0;
uint32_t lockedOutUntilMs = 0;

bool needsUnlock(uint32_t capability) { return (capability & WRITE_CAPS) != 0; }

// millis() wraps every ~49 days; compare by signed difference.
bool before(uint32_t now, uint32_t deadline) {
    return static_cast<int32_t>(now - deadline) < 0;
}

/* Split in place on spaces; "double quotes" group a value with spaces in it.
 * No escapes -- a value cannot contain a quote, a fair price for a parser
 * this small. Returns the token count, or -1 on an unterminated quote or too
 * many tokens. */
int tokenize(char* s, char* argv[], size_t maxArgs) {
    size_t n = 0;
    while (*s) {
        while (*s == ' ') ++s;
        if (!*s) break;
        if (n == maxArgs) return -1;
        if (*s == '"') {
            ++s;
            argv[n++] = s;
            char* end = strchr(s, '"');
            if (!end) return -1;
            *end = '\0';
            s = end + 1;
        } else {
            argv[n++] = s;
            while (*s && *s != ' ') ++s;
            if (*s) *s++ = '\0';
        }
    }
    return static_cast<int>(n);
}

bool parseOnOff(const char* s, bool& out) {
    if (strcasecmp(s, "on") == 0 || strcmp(s, "1") == 0) { out = true; return true; }
    if (strcasecmp(s, "off") == 0 || strcmp(s, "0") == 0) { out = false; return true; }
    return false;
}

bool fourDigits(const char* s, uint16_t& out) {
    if (strlen(s) != 4) return false;
    uint16_t v = 0;
    for (int i = 0; i < 4; ++i) {
        if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
        v = static_cast<uint16_t>(v * 10 + (s[i] - '0'));
    }
    out = v;
    return true;
}

bool printableName(const char* s, size_t maxLen) {
    const size_t len = strlen(s);
    if (len == 0 || len > maxLen) return false;
    for (size_t i = 0; i < len; ++i) {
        if (s[i] < 0x20 || s[i] > 0x7E) return false;
    }
    return true;
}

}  // namespace

/* THE TABLE. Order is the order `help` lists them in. argsMin/argsMax count
 * the arguments after the command name, and are checked before a handler
 * runs, so no handler re-validates its argument count. */
const BrainoApp::ConsoleCommand* BrainoApp::consoleTable(size_t& count) {
    static const ConsoleCommand TABLE[] = {
        {"identify", "identify", "which board and firmware this is",
         APP_CAP_DEVICE_STATUS, 0, 0, &BrainoApp::cmdIdentify},
        {"settings", "settings", "device settings, as counts and flags",
         APP_CAP_DEVICE_STATUS, 0, 0, &BrainoApp::cmdSettings},
        {"help", "help [command]", "list commands, or describe one",
         APP_CAP_NONE, 0, 1, &BrainoApp::cmdHelp},
        {"unlock", "unlock <admin PIN>", "allow changes for two minutes",
         APP_CAP_NONE, 1, 1, &BrainoApp::cmdUnlock},
        {"lock", "lock", "stop allowing changes",
         APP_CAP_NONE, 0, 0, &BrainoApp::cmdLock},
        {"theme", "theme <name>", "set the theme",
         APP_CAP_DEVICE_SETTINGS, 1, 1, &BrainoApp::cmdTheme},
        {"brightness", "brightness <25-100>", "set the backlight",
         APP_CAP_DEVICE_SETTINGS, 1, 1, &BrainoApp::cmdBrightness},
        {"beacon", "beacon on|off", "the Bluetooth beacon",
         APP_CAP_DEVICE_SETTINGS, 1, 1, &BrainoApp::cmdBeacon},
        {"nearby", "nearby on|off", "Nearby play (needs the beacon)",
         APP_CAP_DEVICE_SETTINGS, 1, 1, &BrainoApp::cmdNearby},
        {"wifi", "wifi \"<name>\" \"<password>\" | wifi clear", "the Wi-Fi network",
         APP_CAP_NETWORK, 1, 2, &BrainoApp::cmdWifi},
        {"profile-add", "profile-add \"<name>\"", "add a player",
         APP_CAP_PROFILES, 1, 1, &BrainoApp::cmdProfileAdd},
    };
    count = sizeof(TABLE) / sizeof(TABLE[0]);
    return TABLE;
}

void BrainoApp::tickSerialQuery() {
    for (int budget = CONSOLE_BYTES_PER_FRAME; budget > 0 && Serial.available() > 0;
         --budget) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r' || c == '\n') {
            if (lineOverlong) {
                Serial.println("err toolong line over 127 characters");
            } else if (lineLen > 0) {
                lineBuf[lineLen] = '\0';
                runConsoleLine(lineBuf, Serial);
            }
            // Wipe every time: this buffer may just have held a password.
            memset(lineBuf, 0, sizeof(lineBuf));
            lineLen = 0;
            lineOverlong = false;
            continue;
        }
        if (lineLen < LINE_CAP - 1) {
            lineBuf[lineLen++] = static_cast<char>(c);
        } else {
            lineOverlong = true;
        }
    }
}

void BrainoApp::runConsoleLine(char* line, Print& out) {
    char* argv[MAX_ARGS] = {};
    const int argc = tokenize(line, argv, MAX_ARGS);
    if (argc < 0) { out.println("err syntax unterminated quote or too many arguments"); return; }
    if (argc == 0) return;

    // The two spellings identify_boards.py and configure_boards.py first used.
    const char* name = argv[0];
    if (strcmp(name, "identify?") == 0) name = "identify";
    if (strcmp(name, "settings?") == 0) name = "settings";

    size_t count = 0;
    const ConsoleCommand* table = consoleTable(count);
    for (size_t i = 0; i < count; ++i) {
        const ConsoleCommand& cmd = table[i];
        if (strcmp(name, cmd.name) != 0) continue;

        const int nargs = argc - 1;
        if (nargs < cmd.argsMin || nargs > cmd.argsMax) {
            out.printf("err usage %s\n", cmd.usage);
            return;
        }
        if (needsUnlock(cmd.capability)) {
            const uint32_t now = millis();
            if (unlocked && !before(now, unlockedUntilMs)) unlocked = false;
            if (!unlocked) { out.println("err locked unlock with the admin PIN first"); return; }
            unlockedUntilMs = now + UNLOCK_MS;   // each change keeps it open
        }
        (this->*cmd.run)(nargs, argv + 1, out);
        return;
    }
    out.println("err unknown command -- try help");
}

void BrainoApp::cmdIdentify(int, char**, Print& out) { replyIdentify(out); }

/* Counts and flags only. No profile names, no network name. */
void BrainoApp::cmdSettings(int, char**, Print& out) {
    out.printf("ok v=\"1\" theme=\"%s\" brightness=\"%u\" beacon=\"%d\" "
               "nearby=\"%d\" wifi=\"%d\" profiles=\"%u\" admin=\"%d\"\n",
               Ui::themeName(static_cast<Ui::Theme>(board_.themeMode())),
               (unsigned)board_.brightness(),
               (int)board_.bleBeaconEnabled(), (int)board_.nearbyEnabled(),
               (int)board_.hasWifiCredentials(), (unsigned)board_.playerCount(),
               (int)(board_.adminProfileIndex() != Board::GUEST_INDEX));
}

/* Derived from the table, never hand-written: a command cannot exist without
 * appearing here, or appear here without existing. */
void BrainoApp::cmdHelp(int argc, char** argv, Print& out) {
    size_t count = 0;
    const ConsoleCommand* table = consoleTable(count);
    if (argc == 1) {
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(argv[0], table[i].name) == 0) {
                out.printf("ok usage=\"%s\" help=\"%s\" gated=\"%d\"\n", table[i].usage,
                           table[i].help, (int)needsUnlock(table[i].capability));
                return;
            }
        }
        out.println("err unknown command -- try help");
        return;
    }
    out.print("ok commands=\"");
    for (size_t i = 0; i < count; ++i) {
        out.print(table[i].name);
        if (i + 1 < count) out.print(' ');
    }
    out.println('"');
}

void BrainoApp::cmdUnlock(int, char** argv, Print& out) {
    const uint32_t now = millis();
    if (before(now, lockedOutUntilMs)) {
        out.printf("err lockout try again in %lus\n",
                   (unsigned long)((lockedOutUntilMs - now + 999) / 1000));
        return;
    }
    /* No admin profile means nobody holds admin, so a PIN left in NVS must
     * not grant it here either. */
    if (board_.adminProfileIndex() == Board::GUEST_INDEX) {
        out.println("err noadmin this device has no admin profile");
        return;
    }
    uint16_t pin = 0;
    if (fourDigits(argv[0], pin) && board_.verifyAdminPin(pin)) {
        unlocked = true;
        unlockedUntilMs = now + UNLOCK_MS;
        badPins = 0;
        out.printf("ok unlocked=\"%lu\"\n", (unsigned long)(UNLOCK_MS / 1000));
        return;
    }
    unlocked = false;
    if (++badPins >= PIN_TRIES) {
        badPins = 0;
        lockedOutUntilMs = now + PIN_LOCKOUT_MS;
    }
    out.println("err pin wrong PIN");
}

void BrainoApp::cmdLock(int, char**, Print& out) {
    unlocked = false;
    out.println("ok locked=\"1\"");
}

void BrainoApp::cmdTheme(int, char** argv, Print& out) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(Board::ThemeMode::Count); ++i) {
        const Ui::Theme t = static_cast<Ui::Theme>(i);
        if (strcasecmp(argv[0], Ui::themeName(t)) == 0) {
            board_.setThemeMode(static_cast<Board::ThemeMode>(i));
            Ui::setTheme(t);
            repaintAfterConsoleChange();
            out.printf("ok theme=\"%s\"\n", Ui::themeName(t));
            return;
        }
    }
    out.println("err range unknown theme");
}

void BrainoApp::cmdBrightness(int, char** argv, Print& out) {
    char* end = nullptr;
    const long v = strtol(argv[0], &end, 10);
    if (!end || *end || v < Board::BRIGHTNESS_MIN || v > 100) {
        out.printf("err range brightness is %u..100\n", (unsigned)Board::BRIGHTNESS_MIN);
        return;
    }
    board_.setBrightness(static_cast<uint8_t>(v));
    repaintAfterConsoleChange();
    out.printf("ok brightness=\"%u\"\n", (unsigned)board_.brightness());
}

void BrainoApp::cmdBeacon(int, char** argv, Print& out) {
    bool on = false;
    if (!parseOnOff(argv[0], on)) { out.println("err usage beacon on|off"); return; }
    board_.setBleBeaconEnabled(on);
    repaintAfterConsoleChange();
    out.printf("ok beacon=\"%d\"\n", (int)board_.bleBeaconEnabled());
}

void BrainoApp::cmdNearby(int, char** argv, Print& out) {
    bool on = false;
    if (!parseOnOff(argv[0], on)) { out.println("err usage nearby on|off"); return; }
    // The same refusal Settings makes: Nearby rides on the beacon.
    if (on && !board_.bleBeaconEnabled()) {
        out.println("err refused the beacon is off");
        return;
    }
    NearbyPlay::setEnabled(board_, on);
    repaintAfterConsoleChange();
    out.printf("ok nearby=\"%d\"\n", (int)board_.nearbyEnabled());
}

void BrainoApp::cmdWifi(int argc, char** argv, Print& out) {
    if (argc == 1) {
        if (strcmp(argv[0], "clear") != 0) {
            out.println("err usage wifi \"<name>\" \"<password>\" | wifi clear");
            return;
        }
        board_.clearWifiCredentials();
        repaintAfterConsoleChange();
        out.println("ok wifi=\"0\"");
        return;
    }
    const size_t ls = strlen(argv[0]);
    const size_t lp = strlen(argv[1]);
    if (ls == 0 || ls > SSID_MAX || lp > PASS_MAX) {
        out.println("err range name 1..32 characters, password 0..63");
        return;
    }
    board_.setWifiCredentials(argv[0], argv[1]);
    board_.beginTimeSync();
    repaintAfterConsoleChange();
    out.println("ok wifi=\"1\"");   // never the name, never the password
}

void BrainoApp::cmdProfileAdd(int, char** argv, Print& out) {
    const char* name = argv[0];
    if (!printableName(name, Board::PROFILE_NAME_MAX)) {
        out.printf("err range name is 1..%u printable characters\n",
                   (unsigned)Board::PROFILE_NAME_MAX);
        return;
    }
    // Compared here and never printed, so the tool can be re-run safely.
    for (uint8_t i = 0; i < board_.playerCount(); ++i) {
        if (board_.profileName(i).equalsIgnoreCase(name)) {
            out.printf("ok profile=\"%u\" existed=\"1\"\n", (unsigned)i);
            return;
        }
    }
    const uint8_t idx = board_.addPlayer(name);
    if (idx == 0xFF) { out.println("err full no free profile slot"); return; }
    repaintAfterConsoleChange();
    out.printf("ok profile=\"%u\" existed=\"0\"\n", (unsigned)idx);
}

/* The panel is showing the value that just changed. A console command is
 * rare, so a full repaint is simply the right answer here. Only a live screen
 * repaints; the saver, sleep and the lock screen hand back through
 * resumeUnderlyingScreen(), which already repaints. */
void BrainoApp::repaintAfterConsoleChange() {
    if (view_ == View::Game && activeGame_) {
        activeGame_->requestRender();
    }
}
