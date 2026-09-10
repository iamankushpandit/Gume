#include "AppRuntime.h"

#include "engine/AppCapabilities.h"
#include "engine/ConsoleText.h"

using namespace ConsoleText;

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
 *   ONE TABLE    consoleTable(): name, usage, help, capability, argument
 *                counts, handler. `help` is derived from it. A new command is
 *                a row, never another string match. Settings have their own
 *                table behind `get`/`set` (AppRuntimeConsoleSettings.cpp).
 *   ONE GRAMMAR  every command answers with exactly one line:
 *                  ok key="value" ...
 *                  err <code> <message>
 *                so every tool parses every command the same way.
 *
 * The entities, and what each supports:
 *
 *   device    identify                          read
 *   settings  get [key] / set <key> <value>     read, update (every key)
 *   wifi      wifi "name" "pw" / wifi clear     create/update, delete;
 *                                               read is get's wifi flag
 *   profiles  profiles / profile-add / profile-rename / profile-remove
 *   games     games <slot> / game <slot|all> <id> on|off   (per player)
 *   session   unlock <PIN> / lock / help [command]
 *
 * Aliases kept because tools on dev already send them: `identify?`,
 * `settings` and `settings?` (the last two mean `get`).
 *
 * Four rules, each the reason for code below:
 *
 *   - READS OF THE DEVICE ARE OPEN; WRITES, AND READS OF PLAYER DATA, NEED
 *     THE ADMIN PIN. The gate is one function, needsUnlock(), keyed on the
 *     row's capability, so a new row cannot forget it. `profiles` lists
 *     names, so it carries APP_CAP_PROFILES and is gated like a write.
 *     CLAUDE.md: any route to admin powers needs the PIN. PIN_TRIES wrong
 *     guesses lock the console out for PIN_LOCKOUT_MS.
 *   - THE SAME DOORS AS THE SCREENS. Each change is the Board setter the
 *     Settings/Wi-Fi/Profiles screens call, with the same refusals, and then
 *     whatever screen is showing the old value repaints.
 *   - NOTHING PERSONAL COMES BACK UNASKED. `get` is settings only; a password
 *     is never echoed; the line buffer is wiped after every command. Serial
 *     logs get pasted into public issues.
 *   - NOT ACTIVITY. Nothing here touches lastActivityMs_, so configuring a
 *     bench does not keep screens awake or disturb what another agent is
 *     testing, and a sleeping board stays asleep (Board::setBrightness()).
 *
 * Serial only. A console over Wi-Fi or BLE would be a new outbound flow under
 * the closed privacy list, not an extension of this one.
 *
 * Deliberately absent: factory reset, reading or clearing scores, changing
 * the admin PIN or which profile is admin, peer labels. Each is a decision
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

// Changes the device, or reads player data. Device status reads are open.
constexpr uint32_t GATED_CAPS = APP_CAP_DEVICE_SETTINGS | APP_CAP_NETWORK |
                                APP_CAP_PROFILES | APP_CAP_SCORES |
                                APP_CAP_FACTORY_RESET;

char lineBuf[LINE_CAP];
size_t lineLen = 0;
bool lineOverlong = false;

bool unlocked = false;
uint32_t unlockedUntilMs = 0;
uint8_t badPins = 0;
uint32_t lockedOutUntilMs = 0;

bool needsUnlock(uint32_t capability) { return (capability & GATED_CAPS) != 0; }

struct Alias { const char* spelled; const char* means; };
constexpr Alias ALIASES[] = {
    {"identify?", "identify"},
    {"settings", "get"},
    {"settings?", "get"},
};

}  // namespace

/* THE TABLE. Order is the order `help` lists them in. argsMin/argsMax count
 * the arguments after the command name, and are checked before a handler
 * runs, so no handler re-validates its argument count. */
const BrainoApp::ConsoleCommand* BrainoApp::consoleTable(size_t& count) {
    static const ConsoleCommand TABLE[] = {
        {"identify", "identify", "which board and firmware this is",
         APP_CAP_DEVICE_STATUS, 0, 0, &BrainoApp::cmdIdentify},
        {"get", "get [key]", "device settings; with a key, its value and choices",
         APP_CAP_DEVICE_STATUS, 0, 1, &BrainoApp::cmdGet},
        {"set", "set <key> <value>", "change a device setting (keys: get)",
         APP_CAP_DEVICE_SETTINGS, 2, 2, &BrainoApp::cmdSet},
        {"wifi", "wifi \"<name>\" \"<password>\" | wifi clear", "the Wi-Fi network",
         APP_CAP_NETWORK, 1, 2, &BrainoApp::cmdWifi},
        {"profiles", "profiles", "list players by slot",
         APP_CAP_PROFILES, 0, 0, &BrainoApp::cmdProfiles},
        {"profile-add", "profile-add \"<name>\"", "add a player",
         APP_CAP_PROFILES, 1, 1, &BrainoApp::cmdProfileAdd},
        {"profile-rename", "profile-rename <slot> \"<name>\"", "rename a player",
         APP_CAP_PROFILES, 2, 2, &BrainoApp::cmdProfileRename},
        {"profile-remove", "profile-remove <slot>", "delete a player and their data",
         APP_CAP_PROFILES, 1, 1, &BrainoApp::cmdProfileRemove},
        {"games", "games <slot>", "the games one player has switched off",
         APP_CAP_PROFILES, 1, 1, &BrainoApp::cmdGames},
        {"game", "game <slot|all> <game-id> on|off", "show or hide a game for a player",
         APP_CAP_PROFILES, 3, 3, &BrainoApp::cmdGame},
        {"unlock", "unlock <admin PIN>", "allow changes for two minutes",
         APP_CAP_NONE, 1, 1, &BrainoApp::cmdUnlock},
        {"lock", "lock", "stop allowing changes",
         APP_CAP_NONE, 0, 0, &BrainoApp::cmdLock},
        {"help", "help [command]", "list commands, or describe one",
         APP_CAP_NONE, 0, 1, &BrainoApp::cmdHelp},
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

    const char* name = argv[0];
    for (const Alias& a : ALIASES) {
        if (strcmp(name, a.spelled) == 0) { name = a.means; break; }
    }

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
            unlockedUntilMs = now + UNLOCK_MS;   // each command keeps it open
        }
        (this->*cmd.run)(nargs, argv + 1, out);
        return;
    }
    out.println("err unknown command -- try help");
}

void BrainoApp::cmdIdentify(int, char**, Print& out) { replyIdentify(out); }

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

/* The panel is showing the value that just changed. A console command is
 * rare, so a full repaint is simply the right answer here. Only a live screen
 * repaints; the saver, sleep and the lock screen hand back through
 * resumeUnderlyingScreen(), which already repaints. */
void BrainoApp::repaintAfterConsoleChange() {
    if (view_ == View::Game && activeGame_) {
        activeGame_->requestRender();
    }
}
