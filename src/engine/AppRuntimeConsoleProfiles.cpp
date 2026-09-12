#include "AppRuntime.h"

#include "engine/AppRegistry.h"
#include "engine/ConsoleText.h"

using namespace ConsoleText;

/* PLAYERS OVER THE CONSOLE: CREATE, READ, UPDATE, DELETE -- AND THEIR GAMES.
 *
 *   profiles                          ok count="3" active="1" admin="0" p0="..." ...
 *   profile-add "Sam"                 ok profile="3" existed="0"
 *   profile-rename <slot> "Sam"       ok profile="3" name="Sam"
 *   profile-remove <slot>             ok removed="3" count="3"
 *   games <slot>                      ok profile="1" hidden="chess,maze"
 *   game <slot|all> <game-id> on|off  ok game="chess" visible="0" profiles="3"
 *
 * All of it is behind the PIN (APP_CAP_PROFILES), including the two reads:
 * `profiles` is the one command that prints player names, which is player
 * data, so it is gated like a change. A slot is the 0-based index `profiles`
 * reports; Guest is not a slot and cannot be renamed, removed or edited.
 *
 * The same refusals the Profiles screen makes, plus two of its own:
 *   - the admin profile cannot be removed (ProfileApp never offers it);
 *   - the ACTIVE player cannot be removed. From the screen that cannot come
 *     up -- you are looking at the list, not playing -- but from a cable it
 *     would pull a profile out from under a running game. Switch first.
 *   - two players cannot share a name (case-insensitive), so the tool can
 *     look players up by name and re-running it is harmless.
 *
 * `game all ...` is the classroom case: one game on or off for every player
 * at once. Guest always sees every game -- that is Board's rule, not ours.
 *
 * After any change, refreshAfterProfileChange(): the Profiles screen caches
 * which slot a menu is open on, so if it is up it is started over rather
 * than repainted; anything else just repaints. */
namespace {

bool parseSlot(const char* s, uint8_t count, uint8_t& out) {
    long v = 0;
    if (!parseRange(s, 0, static_cast<long>(count) - 1, v)) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

constexpr size_t NAME_BUF = Board::PROFILE_NAME_MAX + 8;   // "Player N" fallback fits

/* The slot of the player called `name` (case-insensitive), skipping `except`;
 * 0xFF if nobody is. Compared in a stack buffer and never printed. */
uint8_t slotNamed(Board& board, const char* name, uint8_t except = 0xFF) {
    char stored[NAME_BUF];
    for (uint8_t i = 0; i < board.playerCount(); ++i) {
        if (i == except) continue;
        board.copyProfileName(i, stored, sizeof(stored));
        if (strcasecmp(stored, name) == 0) return i;
    }
    return 0xFF;
}

const AppDefinition* playableById(const char* id) {
    for (uint8_t i = 0; i < playableAppCount(); ++i) {
        const AppDefinition& app = playableAppAt(i);
        if (strcmp(app.id(), id) == 0) return &app;
    }
    return nullptr;
}

}  // namespace

void BrainoApp::refreshAfterProfileChange() {
    if (activeGame_ == &games_.profile) {
        relaunchActiveGame();
    } else {
        repaintAfterConsoleChange();
    }
}

void BrainoApp::cmdProfiles(int, char**, Print& out) {
    const uint8_t n = board_.playerCount();
    const uint8_t active = board_.activeProfile();
    const uint8_t admin = board_.adminProfileIndex();
    // Casts matter: Print::print(uint8_t) would send the byte, not the number.
    out.printf("ok count=\"%u\" active=\"", (unsigned)n);
    if (active == Board::GUEST_INDEX) out.print("guest"); else out.print((unsigned)active);
    out.print("\" admin=\"");
    if (admin == Board::GUEST_INDEX) out.print("none"); else out.print((unsigned)admin);
    out.print('"');
    char name[NAME_BUF];
    for (uint8_t i = 0; i < n; ++i) {
        board_.copyProfileName(i, name, sizeof(name));
        // A stored name could predate the no-quote rule; never break the line.
        for (char* c = name; *c; ++c) if (*c == '"') *c = '\'';
        out.printf(" p%u=\"%s\"", (unsigned)i, name);
    }
    out.println();
}

void BrainoApp::cmdProfileAdd(int, char** argv, Print& out) {
    const char* name = argv[0];
    if (!printableName(name, Board::PROFILE_NAME_MAX)) {
        out.printf("err range name is 1-%u printable characters, no quotes\n",
                   (unsigned)Board::PROFILE_NAME_MAX);
        return;
    }
    // An existing name is not an error, so the tool can be re-run safely.
    const uint8_t existing = slotNamed(board_, name);
    if (existing != 0xFF) {
        out.printf("ok profile=\"%u\" existed=\"1\"\n", (unsigned)existing);
        return;
    }
    const uint8_t idx = board_.addPlayer(name);
    if (idx == 0xFF) { out.println("err full no free profile slot"); return; }
    refreshAfterProfileChange();
    out.printf("ok profile=\"%u\" existed=\"0\"\n", (unsigned)idx);
}

void BrainoApp::cmdProfileRename(int, char** argv, Print& out) {
    uint8_t slot = 0;
    if (!parseSlot(argv[0], board_.playerCount(), slot)) {
        out.println("err range no such slot -- profiles lists them");
        return;
    }
    const char* name = argv[1];
    if (!printableName(name, Board::PROFILE_NAME_MAX)) {
        out.printf("err range name is 1-%u printable characters, no quotes\n",
                   (unsigned)Board::PROFILE_NAME_MAX);
        return;
    }
    if (slotNamed(board_, name, slot) != 0xFF) {
        out.println("err exists another player already has that name");
        return;
    }
    board_.setProfileName(slot, name);
    refreshAfterProfileChange();
    out.printf("ok profile=\"%u\" name=\"%s\"\n", (unsigned)slot, name);
}

void BrainoApp::cmdProfileRemove(int, char** argv, Print& out) {
    uint8_t slot = 0;
    if (!parseSlot(argv[0], board_.playerCount(), slot)) {
        out.println("err range no such slot -- profiles lists them");
        return;
    }
    if (board_.isAdminProfile(slot)) {
        out.println("err refused the admin profile cannot be removed");
        return;
    }
    if (board_.activeProfile() == slot) {
        out.println("err refused that player is active -- switch profile first");
        return;
    }
    board_.removePlayer(slot);
    refreshAfterProfileChange();
    out.printf("ok removed=\"%u\" count=\"%u\"\n", (unsigned)slot,
               (unsigned)board_.playerCount());
}

void BrainoApp::cmdGames(int, char** argv, Print& out) {
    uint8_t slot = 0;
    if (!parseSlot(argv[0], board_.playerCount(), slot)) {
        out.println("err range no such slot -- profiles lists them");
        return;
    }
    out.printf("ok profile=\"%u\" hidden=\"", (unsigned)slot);
    bool first = true;
    for (uint8_t i = 0; i < playableAppCount(); ++i) {
        const AppDefinition& app = playableAppAt(i);
        if (!board_.gameVisibleFor(app.launcherIndex(), slot, app.defaultVisible())) {
            if (!first) out.print(',');
            out.print(app.id());
            first = false;
        }
    }
    out.println('"');
}

void BrainoApp::cmdGame(int, char** argv, Print& out) {
    const uint8_t n = board_.playerCount();
    const bool all = strcasecmp(argv[0], "all") == 0;
    uint8_t slot = 0;
    if (!all && !parseSlot(argv[0], n, slot)) {
        out.println("err range no such slot -- profiles lists them, or use all");
        return;
    }
    const AppDefinition* app = playableById(argv[1]);
    if (!app) { out.println("err range unknown game id"); return; }
    bool on = false;
    if (!parseOnOff(argv[2], on)) { out.println("err usage game <slot|all> <game-id> on|off"); return; }
    /* Visibility is a fixed-width mask per player and the catalogue is
     * longer, so the last games cannot be hidden at all. Say so rather than
     * answer ok for a change that did not happen. */
    if (app->launcherIndex() >= Board::gameVisibilityLimit()) {
        out.printf("err limit this game cannot be hidden yet: visibility holds %u games\n",
                   (unsigned)Board::gameVisibilityLimit());
        return;
    }
    const uint8_t from = all ? 0 : slot;
    const uint8_t to = all ? n : static_cast<uint8_t>(slot + 1);
    for (uint8_t p = from; p < to; ++p) {
        board_.setGameVisibleFor(app->launcherIndex(), p, on);
    }
    refreshAfterProfileChange();
    out.printf("ok game=\"%s\" visible=\"%d\" profiles=\"%u\"\n", app->id(), (int)on,
               (unsigned)(to - from));
}
