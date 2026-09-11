#include "SeaBattleGame.h"

#include <esp_system.h>   // esp_random(), for laying out a fleet
#include <string.h>

#include "engine/AppRegistry.h"

namespace {

/* Score is null: sinking a fleet is not a number. The field list is positional
 * and tools/app_registry_parser.py reads it that way, so keep comments out
 * from between the fields. */
constexpr AppMetadata SEA_BATTLE_METADATA = {
    "seabattle",
    "Sea Battle",
    nullptr,
    "sink the fleet",
    "Sea Battle",
    "Two players. Hunt the hidden ships.",
    nullptr,
    LauncherIcon::SeaBattle,
    33,
    true,
};

/* One four, one three and two twos: eleven cells hidden in sixty-four. Dense
 * enough that random guessing finds something, sparse enough that hunting is
 * worth doing -- the classic ten-by-ten ratio is seventeen in a hundred, and
 * this is deliberately a little more generous because the players are young. */
constexpr uint8_t SHIP_LEN[SeaBattleGame::SHIP_COUNT] = {4, 3, 2, 2};

constexpr int16_t TOP_BAR_H = 30;
constexpr int16_t MARGIN = 3;
constexpr int16_t GAP = 3;
constexpr int16_t ACTION_H = 26;
constexpr int16_t STATUS_H = 24;
constexpr int16_t TALLY_H = 12;
constexpr int16_t LABEL_H = 11;

/* Sea, and the two things that can be on it. Fixed rather than themed, like
 * the chess board's squares: these are the colours of a thing being depicted,
 * not of the interface around it, and a hit has to read as red in all nine
 * themes. */
uint16_t seaColour() { return Ui::rgb(30, 62, 104); }
uint16_t seaGrid() { return Ui::rgb(58, 96, 142); }
uint16_t missColour() { return Ui::rgb(150, 168, 190); }
uint16_t hitColour() { return Ui::rgb(214, 66, 54); }
uint16_t shipColour() { return Ui::rgb(120, 128, 140); }

}   // namespace

const AppMetadata& seaBattleAppMetadata() {
    return SEA_BATTLE_METADATA;
}

const char* SeaBattleGame::title() const {
    return seaBattleAppMetadata().screenTitle != nullptr
        ? seaBattleAppMetadata().screenTitle
        : seaBattleAppMetadata().title;
}

// ---------------------------------------------------------------- rules

/* Lay a fleet at random.
 *
 * Longest ship first, which matters: a four needs a run of four free squares
 * and gets steadily harder to place as the sea fills up, while a two will
 * almost always find room. Placing the awkward ones while the sea is empty is
 * what keeps the retry loop short.
 *
 * Ships may touch. The variant that forbids it is a harder game and a longer
 * placement, and neither is what this is for.
 *
 * The loop is bounded rather than open: eleven cells in sixty-four is loose
 * enough that a failure is vanishingly unlikely, but an unbounded search on a
 * device with a twelve-second watchdog is a hang waiting for a bad seed. On
 * giving up it starts the whole fleet again, which cannot loop forever because
 * each attempt is independent.
 */
void SeaBattleGame::placeFleet(Side& side) {
    for (uint8_t attempt = 0; attempt < 8; ++attempt) {
        memset(side.shipAt, NO_SHIP, sizeof(side.shipAt));
        bool ok = true;
        for (uint8_t ship = 0; ship < SHIP_COUNT && ok; ++ship) {
            const uint8_t len = SHIP_LEN[ship];
            bool placed = false;
            for (uint16_t tries = 0; tries < 200 && !placed; ++tries) {
                const uint32_t r = esp_random();
                const bool horizontal = (r & 1u) != 0;
                const uint8_t span = static_cast<uint8_t>(GRID - len + 1);
                const uint8_t col = static_cast<uint8_t>(
                    (r >> 1) % (horizontal ? span : GRID));
                const uint8_t row = static_cast<uint8_t>(
                    (r >> 9) % (horizontal ? GRID : span));

                bool free = true;
                for (uint8_t i = 0; i < len && free; ++i) {
                    const uint8_t c = horizontal ? static_cast<uint8_t>(col + i) : col;
                    const uint8_t w = horizontal ? row : static_cast<uint8_t>(row + i);
                    free = side.shipAt[cellOf(c, w)] == NO_SHIP;
                }
                if (!free) continue;

                for (uint8_t i = 0; i < len; ++i) {
                    const uint8_t c = horizontal ? static_cast<uint8_t>(col + i) : col;
                    const uint8_t w = horizontal ? row : static_cast<uint8_t>(row + i);
                    side.shipAt[cellOf(c, w)] = ship;
                }
                placed = true;
            }
            ok = placed;
        }
        if (ok) return;
    }
}

bool SeaBattleGame::shipSunk(const Side& side, uint8_t cell) {
    const uint8_t ship = side.shipAt[cell];
    if (ship == NO_SHIP) return false;
    for (uint8_t c = 0; c < CELLS; ++c) {
        if (side.shipAt[c] == ship && !side.incoming[c]) return false;
    }
    return true;
}

/* Fire at `target`. The damage is recorded on the side being fired AT, which
 * is the only side that knows whether anything was there -- and in a remote
 * game is the only side that has the information at all. */
SeaBattleGame::Shot SeaBattleGame::resolve(Side& target, uint8_t cell) {
    if (target.incoming[cell]) {
        /* Already fired at. Cannot happen from our own board -- a marked
         * square is not tappable -- but a peer's advertisement is not ours to
         * trust, and answering "miss" is the harmless reading. */
        return Shot::Miss;
    }
    target.incoming[cell] = true;
    if (target.shipAt[cell] == NO_SHIP) return Shot::Miss;

    ++target.hitsTaken;
    if (!shipSunk(target, cell)) return Shot::Hit;
    ++target.sunkAgainstUs;
    return Shot::Sunk;
}

// --------------------------------------------------------------- layout
//
// Every rect below is derived from the board so the panel cannot overlap it.
// The canvas is a fixed 320x240 in landscape -- see the class comment for why
// this game does not follow the owner's orientation.

Rect SeaBattleGame::boardRect() {
    const int16_t top = TOP_BAR_H + MARGIN;
    int16_t side = static_cast<int16_t>(GAME_CANVAS_HEIGHT - top - MARGIN);
    side = static_cast<int16_t>((side / GRID) * GRID);
    return Rect{MARGIN, top, side, side};
}

Rect SeaBattleGame::cellRect(uint8_t cell) {
    const Rect b = boardRect();
    const int16_t s = static_cast<int16_t>(b.w / GRID);
    return Rect{static_cast<int16_t>(b.x + colOf(cell) * s),
                static_cast<int16_t>(b.y + rowOf(cell) * s), s, s};
}

Rect SeaBattleGame::panelRect() {
    const Rect b = boardRect();
    const int16_t x = static_cast<int16_t>(b.x + b.w + GAP);
    return Rect{x, b.y, static_cast<int16_t>(GAME_CANVAS_WIDTH - x - MARGIN), b.h};
}

/* Our own sea, small, in the column beside the board. Square and centred: a
 * grid that is not square reads as a different game rather than as the same
 * one seen from further away. */
Rect SeaBattleGame::miniRect() {
    const Rect p = panelRect();
    int16_t s = p.w;
    s = static_cast<int16_t>((s / GRID) * GRID);
    return Rect{static_cast<int16_t>(p.x + (p.w - s) / 2),
                static_cast<int16_t>(p.y + LABEL_H), s, s};
}

Rect SeaBattleGame::miniCellRect(uint8_t cell) {
    const Rect m = miniRect();
    const int16_t s = static_cast<int16_t>(m.w / GRID);
    return Rect{static_cast<int16_t>(m.x + colOf(cell) * s),
                static_cast<int16_t>(m.y + rowOf(cell) * s), s, s};
}

Rect SeaBattleGame::statusRect() {
    const Rect p = panelRect();
    const Rect m = miniRect();
    return Rect{p.x, static_cast<int16_t>(m.y + m.h + GAP + 2), p.w, STATUS_H};
}

Rect SeaBattleGame::tallyRect() {
    const Rect s = statusRect();
    return Rect{s.x, static_cast<int16_t>(s.y + s.h), s.w, TALLY_H};
}

Rect SeaBattleGame::actionRect() {
    const Rect p = panelRect();
    return Rect{p.x, static_cast<int16_t>(p.y + p.h - ACTION_H), p.w, ACTION_H};
}

/* Ready sits above Shuffle while placing, and nothing is drawn there at any
 * other time -- so the two never need to share a row and neither has to shrink
 * to make room for a button that is usually absent. */
Rect SeaBattleGame::readyRect() {
    const Rect a = actionRect();
    return Rect{a.x, static_cast<int16_t>(a.y - ACTION_H - GAP), a.w, ACTION_H};
}

uint8_t SeaBattleGame::cellAt(int16_t x, int16_t y) {
    const Rect b = boardRect();
    if (!b.contains(x, y, 0)) return NO_CELL;
    const int16_t s = static_cast<int16_t>(b.w / GRID);
    const int16_t col = static_cast<int16_t>((x - b.x) / s);
    const int16_t row = static_cast<int16_t>((y - b.y) / s);
    if (col < 0 || col >= GRID || row < 0 || row >= GRID) return NO_CELL;
    return cellOf(static_cast<uint8_t>(col), static_cast<uint8_t>(row));
}

// --------------------------------------------------------------- screen

void SeaBattleGame::newGame() {
    for (Side& s : side_) {
        memset(s.shipAt, NO_SHIP, sizeof(s.shipAt));
        memset(s.incoming, 0, sizeof(s.incoming));
        for (Mark& m : s.shot) m = Mark::Unknown;
        s.hitsScored = 0;
        s.hitsTaken = 0;
        s.sunkAgainstUs = 0;
    }
    placeFleet(side_[0]);
    placeFleet(side_[1]);
    placed_[0] = placed_[1] = false;
    turnPlayer_ = 0;
    phase_ = Phase::Placing;
    won_ = lost_ = ended_ = false;
    endedByUs_ = false;
    lastShot_ = Shot::None;
    pendingCell_ = NO_CELL;
    owedReply_ = Shot::None;
    ourPly_ = 0;
    theirPly_ = 0;
    ourShotCell_ = 0;
    ourReplyCode_ = 0;
    confirmUntilMs_ = 0;
    dirtyCount_ = 0;
    fullPaint_ = true;
    panelStale_ = true;
    markFullDirty();
}

void SeaBattleGame::begin(AppContext& host) {
    newGame();
    mode_ = Mode::Lobby;
    seatCount_ = 0;
    seatsAtMs_ = 0;
    opponent_[0] = 0;
    opponentName_[0] = 0;
    session_ = 0;
    restoreGame(host);
    markFullDirty();
}

void SeaBattleGame::end(AppContext& host) {
    if (mode_ == Mode::Local || mode_ == Mode::Remote) saveGame(host);
    host.nearbyStop();
}

void SeaBattleGame::startLocal() {
    mode_ = Mode::Local;
    opponent_[0] = 0;
    opponentName_[0] = 0;
    newGame();
}

void SeaBattleGame::startRemote(const NearbySeat& seat, uint8_t session,
                                bool weFireFirst) {
    strncpy(opponent_, seat.deviceId, sizeof(opponent_) - 1);
    opponent_[sizeof(opponent_) - 1] = 0;
    strncpy(opponentName_, seat.name, sizeof(opponentName_) - 1);
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = static_cast<uint8_t>(session & 0x3F);
    remoteFiresFirst_ = weFireFirst;
    mode_ = Mode::Remote;
    newGame();
}

/* In a remote game the plies alternate strictly, so whose turn it is falls out
 * of how many shots each side has taken and who was given the first one. */
bool SeaBattleGame::ourTurn() const {
    if (mode_ != Mode::Remote) return true;
    if (remoteFiresFirst_) return ourPly_ == theirPly_;
    return ourPly_ < theirPly_;
}

void SeaBattleGame::markCell(uint8_t cell) {
    if (cell == NO_CELL) return;
    for (uint8_t i = 0; i < dirtyCount_; ++i) {
        if (dirty_[i] == cell) return;
    }
    if (dirtyCount_ < sizeof(dirty_)) dirty_[dirtyCount_++] = cell;
}

/* Fire at one square of the enemy sea.
 *
 * The two modes part company here and nowhere else. Locally the other fleet is
 * in memory, so the shot resolves immediately. Remotely only the opponent
 * knows what is there, so the square is marked Pending and the answer arrives
 * on their next turn -- which is also when we tell them about theirs. One
 * round trip carries both, because a separate answer message is the one that
 * would go missing.
 */
void SeaBattleGame::fireAt(AppContext& host, uint8_t cell) {
    Side& us = side_[viewer()];
    if (us.shot[cell] != Mark::Unknown) return;   // already tried there

    if (mode_ == Mode::Remote) {
        us.shot[cell] = Mark::Pending;
        pendingCell_ = cell;
        ourShotCell_ = cell;
        ourReplyCode_ = static_cast<uint8_t>(owedReply_);
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        host.nearbyPublish(session_, ourPly_, ourShotCell_, ourReplyCode_,
                           theirPly_);
        owedReply_ = Shot::None;
        markCell(cell);
        host.playSound(Sound::Whoosh);
        panelStale_ = true;
        markDirty();
        saveGame(host);
        return;
    }

    Side& them = side_[1 - viewer()];
    const Shot result = resolve(them, cell);
    us.shot[cell] = result == Shot::Miss ? Mark::Miss : Mark::Hit;
    if (result != Shot::Miss) ++us.hitsScored;
    lastShot_ = result;
    markCell(cell);
    host.playSound(result == Shot::Sunk  ? Sound::LevelUp
                   : result == Shot::Hit ? Sound::Coin
                                         : Sound::Tap);

    if (us.hitsScored >= FLEET_CELLS) {
        won_ = true;
        host.playSound(Sound::Victory);
    } else {
        /* Hand over. The curtain is what makes two players on one console
         * possible at all: without it the next player sees the sea they are
         * about to be asked to guess. */
        turnPlayer_ = static_cast<uint8_t>(1 - turnPlayer_);
        phase_ = Phase::Passing;
        fullPaint_ = true;
    }
    panelStale_ = true;
    markDirty();
    saveGame(host);
}

void SeaBattleGame::declareEnd(AppContext& host, bool byUs) {
    ended_ = true;
    endedByUs_ = byUs;
    confirmUntilMs_ = 0;
    if (mode_ == Mode::Remote && byUs) {
        ourPly_ = static_cast<uint8_t>((ourPly_ + 1) & 0x7F);
        host.nearbyEnd(session_, ourPly_, theirPly_);
    }
    panelStale_ = true;
    fullPaint_ = true;
    saveGame(host);
    markFullDirty();
}

// ------------------------------------------------------------- persistence

void SeaBattleGame::saveGame(AppContext& host) const {
    Saved out{};
    out.magic = SAVE_MAGIC;
    out.version = SAVE_VERSION;
    out.mode = static_cast<uint8_t>(mode_);
    out.phase = static_cast<uint8_t>(phase_);
    out.turnPlayer = turnPlayer_;
    out.placed[0] = placed_[0] ? 1 : 0;
    out.placed[1] = placed_[1] ? 1 : 0;
    out.won = won_ ? 1 : 0;
    out.lost = lost_ ? 1 : 0;
    out.ended = ended_ ? 1 : 0;
    out.endedByUs = endedByUs_ ? 1 : 0;
    out.lastShot = static_cast<uint8_t>(lastShot_);
    out.pendingCell = pendingCell_;
    out.owedReply = static_cast<uint8_t>(owedReply_);
    out.remoteFiresFirst = remoteFiresFirst_ ? 1 : 0;
    memcpy(out.opponent, opponent_, sizeof(out.opponent));
    memcpy(out.opponentName, opponentName_, sizeof(out.opponentName));
    out.session = session_;
    out.ourPly = ourPly_;
    out.theirPly = theirPly_;
    out.ourShotCell = ourShotCell_;
    out.ourReplyCode = ourReplyCode_;
    for (uint8_t s = 0; s < 2; ++s) {
        memcpy(out.shipAt[s], side_[s].shipAt, CELLS);
        for (uint8_t c = 0; c < CELLS; ++c) {
            out.shot[s][c] = static_cast<uint8_t>(side_[s].shot[c]);
            out.incoming[s][c] = side_[s].incoming[c] ? 1 : 0;
        }
        out.hitsScored[s] = side_[s].hitsScored;
        out.hitsTaken[s] = side_[s].hitsTaken;
        out.sunkAgainstUs[s] = side_[s].sunkAgainstUs;
    }
    host.saveBlob("game", &out, sizeof(out));
}

bool SeaBattleGame::restoreGame(AppContext& host) {
    Saved in{};
    host.loadBlob("game", &in, sizeof(in));
    if (in.magic != SAVE_MAGIC || in.version != SAVE_VERSION) return false;

    const Mode m = static_cast<Mode>(in.mode);
    if (m != Mode::Local && m != Mode::Remote) return false;
    /* A finished game is restored as nothing, so the screen opens at the lobby
     * rather than at a result somebody has already read. */
    if (in.won || in.lost || in.ended) return false;
    if (in.turnPlayer > 1) return false;

    mode_ = m;
    phase_ = static_cast<Phase>(in.phase);
    turnPlayer_ = in.turnPlayer;
    placed_[0] = in.placed[0] != 0;
    placed_[1] = in.placed[1] != 0;
    won_ = lost_ = ended_ = false;
    endedByUs_ = in.endedByUs != 0;
    lastShot_ = static_cast<Shot>(in.lastShot);
    pendingCell_ = in.pendingCell;
    owedReply_ = static_cast<Shot>(in.owedReply);
    remoteFiresFirst_ = in.remoteFiresFirst != 0;
    memcpy(opponent_, in.opponent, sizeof(opponent_));
    opponent_[sizeof(opponent_) - 1] = 0;
    memcpy(opponentName_, in.opponentName, sizeof(opponentName_));
    opponentName_[sizeof(opponentName_) - 1] = 0;
    session_ = in.session;
    ourPly_ = in.ourPly;
    theirPly_ = in.theirPly;
    ourShotCell_ = in.ourShotCell;
    ourReplyCode_ = in.ourReplyCode;
    for (uint8_t s = 0; s < 2; ++s) {
        memcpy(side_[s].shipAt, in.shipAt[s], CELLS);
        for (uint8_t c = 0; c < CELLS; ++c) {
            side_[s].shot[c] = static_cast<Mark>(in.shot[s][c]);
            side_[s].incoming[c] = in.incoming[s][c] != 0;
        }
        side_[s].hitsScored = in.hitsScored[s];
        side_[s].hitsTaken = in.hitsTaken[s];
        side_[s].sunkAgainstUs = in.sunkAgainstUs[s];
    }
    return true;
}

// -------------------------------------------------------------- the lobby

Rect SeaBattleGame::lobbyRowRect(uint8_t row) const {
    const int16_t top = TOP_BAR_H + 8;
    const int16_t h = 30;
    return Rect{10, static_cast<int16_t>(top + row * (h + 6)),
                static_cast<int16_t>(GAME_CANVAS_WIDTH - 20), h};
}

void SeaBattleGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    Ui::drawButton(tft, lobbyRowRect(0), "Pass and play", Ui::panel(),
                   Ui::outline(), Ui::text(), false, 2);

    char label[32];
    uint8_t row = 1;
    for (uint8_t i = 0; i < seatCount_ && row < 5; ++i, ++row) {
        const char* who = seats_[i].name[0] != 0 ? seats_[i].name
                                                 : seats_[i].deviceId;
        if (seats_[i].inviting) {
            snprintf(label, sizeof(label), "%s invites you", who);
        } else {
            snprintf(label, sizeof(label), "Play %s", who);
        }
        Ui::drawButton(tft, lobbyRowRect(row), label,
                       seats_[i].inviting ? Ui::success() : Ui::panel(),
                       Ui::outline(), Ui::text(), false, 2);
    }

    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    const char* note = seatCount_ > 0
        ? "Shots travel by Bluetooth. Ships never do."
        : "Nobody nearby. An adult can switch Beacon and Nearby on.";
    tft.drawString(note, GAME_CANVAS_WIDTH / 2,
                   static_cast<int16_t>(GAME_CANVAS_HEIGHT - 6), 1);
    tft.setTextDatum(TL_DATUM);
}

void SeaBattleGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ > 1000) {
        seatsAtMs_ = now;
        NearbySeat fresh[6];
        uint8_t count = 0;
        const uint8_t n = host.nearbySeatCount();
        for (uint8_t i = 0; i < n && count < 6; ++i) {
            NearbySeat seat;
            if (host.nearbySeatAt(i, seat)) {
                /* An invitation to some other game is not one to accept here:
                 * answering it would leave two consoles playing different
                 * games at each other. The row reads "Play A4F2" instead. */
                seat.inviting = seat.inviting && seat.forThisGame;
                fresh[count++] = seat;
            }
        }
        /* Compare the rows and not just how many: a peer that starts inviting
         * changes what its row says without changing the count. */
        bool changed = count != seatCount_;
        for (uint8_t i = 0; !changed && i < count; ++i) {
            changed = fresh[i].inviting != seats_[i].inviting ||
                      strcmp(fresh[i].deviceId, seats_[i].deviceId) != 0;
        }
        for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
        seatCount_ = count;
        if (changed) markFullDirty();
    }

    if (!touch.justPressed) return;

    if (lobbyRowRect(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.playSound(Sound::Select);
        startLocal();
        return;
    }
    for (uint8_t i = 0; i < seatCount_; ++i) {
        if (!lobbyRowRect(static_cast<uint8_t>(i + 1))
                 .contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        if (seats_[i].inviting) {
            host.playSound(Sound::Select);
            startRemote(seats_[i], seats_[i].session, seats_[i].weMoveFirst);
        } else {
            const uint8_t session = static_cast<uint8_t>((millis() >> 3) & 0x3F);
            bool weFireFirst = false;
            if (host.nearbyInvite(seats_[i].deviceId, session, weFireFirst)) {
                host.playSound(Sound::Select);
                startRemote(seats_[i], session, weFireFirst);
                mode_ = Mode::Waiting;
                markFullDirty();
            } else {
                host.beepError();
            }
        }
        return;
    }
}

/* Take the opponent's turn, if there is one we should act on.
 *
 * The same four tests Chess uses -- from this peer, in this session, the ply
 * we expect, and legal here -- with "legal" meaning a square inside the grid
 * that has not already been fired at. A peer's advertisement is a broadcast
 * and is not ours to trust.
 *
 * One thing is particular to this game: a turn carries TWO facts, their shot
 * and the answer to ours. The answer is applied first, because it belongs to
 * the shot we made before theirs.
 */
void SeaBattleGame::pollOpponent(AppContext& host) {
    if (mode_ != Mode::Remote && mode_ != Mode::Waiting) return;
    if (opponent_[0] == 0) return;

    NearbyTurn turn;
    if (!host.nearbyTurnFrom(opponent_, session_, turn)) return;

    if (mode_ == Mode::Waiting) {
        mode_ = Mode::Remote;
        markFullDirty();
    }

    const uint8_t expected = static_cast<uint8_t>((theirPly_ + 1) & 0x7F);
    if (turn.ply != expected) return;

    if (turn.ended) {
        theirPly_ = turn.ply;
        declareEnd(host, false);
        host.playSound(Sound::GameOver);
        return;
    }

    /* Not yet ready to be shot at. The turn stays on the air -- it is state,
     * not a message -- so it will still be here when this fleet is placed.
     * Without this the opponent's opening shot would be dropped by whichever
     * console took longer over Shuffle. */
    if (phase_ == Phase::Placing) return;
    if (turn.from >= CELLS) return;

    theirPly_ = turn.ply;

    // Their answer to our shot, which we have been showing as Pending.
    if (pendingCell_ != NO_CELL) {
        const Shot answer = static_cast<Shot>(turn.to);
        Side& us = side_[0];
        if (answer == Shot::Hit || answer == Shot::Sunk) {
            us.shot[pendingCell_] = Mark::Hit;
            ++us.hitsScored;
        } else {
            us.shot[pendingCell_] = Mark::Miss;
        }
        lastShot_ = answer;
        markCell(pendingCell_);
        pendingCell_ = NO_CELL;
        if (us.hitsScored >= FLEET_CELLS) {
            won_ = true;
            host.playSound(Sound::Victory);
            panelStale_ = true;
            markDirty();
            saveGame(host);
            return;
        }
        host.playSound(answer == Shot::Sunk  ? Sound::LevelUp
                       : answer == Shot::Hit ? Sound::Coin
                                             : Sound::Reveal);
    }

    // Their shot at us, and the answer we will owe them on our next turn.
    owedReply_ = resolve(side_[0], turn.from);
    if (side_[0].hitsTaken >= FLEET_CELLS) {
        lost_ = true;
        host.playSound(Sound::GameOver);
    }
    panelStale_ = true;
    markDirty();
    saveGame(host);
}

// ---------------------------------------------------------------- update

void SeaBattleGame::update(AppContext& host, const TouchPoint& touch) {
    if (mode_ == Mode::Lobby) {
        updateLobby(host, touch);
        return;
    }

    pollOpponent(host);

    /* Republish every frame. Unchanged values do not touch the radio, and a
     * turn must stay on the air until it is replaced -- from ply 0, which is
     * how an accepted invitation is answered. */
    if (mode_ == Mode::Remote && !ended_) {
        host.nearbyPublish(session_, ourPly_, ourShotCell_, ourReplyCode_,
                           theirPly_);
    }

    if (confirmUntilMs_ != 0 && millis() > confirmUntilMs_) {
        confirmUntilMs_ = 0;
        panelStale_ = true;
        markDirty();
    }

    if (!touch.justPressed) return;

    // The curtain: one tap, anywhere, and nothing else on screen.
    if (phase_ == Phase::Passing) {
        phase_ = placed_[0] && placed_[1] ? Phase::Firing : Phase::Placing;
        fullPaint_ = true;
        host.playSound(Sound::Tap);
        markFullDirty();
        return;
    }

    if (phase_ == Phase::Placing) {
        if (readyRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.playSound(Sound::Select);
            placed_[viewer()] = true;
            if (mode_ == Mode::Remote) {
                phase_ = Phase::Firing;
            } else if (placed_[0] && placed_[1]) {
                turnPlayer_ = 0;
                phase_ = Phase::Passing;
            } else {
                turnPlayer_ = static_cast<uint8_t>(1 - turnPlayer_);
                phase_ = Phase::Passing;
            }
            fullPaint_ = true;
            markFullDirty();
            return;
        }
        if (actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.playSound(Sound::Whoosh);
            placeFleet(side_[viewer()]);
            fullPaint_ = true;
            markFullDirty();
        }
        return;
    }

    // Firing.
    if (actionRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (gameOver()) {
            host.playSound(Sound::Select);
            if (mode_ == Mode::Remote) host.nearbyStop();
            mode_ = Mode::Lobby;
            opponent_[0] = 0;
            opponentName_[0] = 0;
            session_ = 0;
            seatCount_ = 0;
            seatsAtMs_ = 0;
            newGame();
            saveGame(host);
            markFullDirty();
        } else if (confirmUntilMs_ != 0) {
            declareEnd(host, true);
            host.playSound(Sound::GameOver);
        } else {
            confirmUntilMs_ = millis() + CONFIRM_MS;
            host.playSound(Sound::Tap);
            panelStale_ = true;
            markDirty();
        }
        return;
    }

    if (gameOver()) return;
    if (mode_ == Mode::Remote && !ourTurn()) return;
    if (mode_ == Mode::Remote && pendingCell_ != NO_CELL) return;

    const uint8_t cell = cellAt(touch.x, touch.y);
    if (cell == NO_CELL) return;
    fireAt(host, cell);
}

// ---------------------------------------------------------------- render

/* One cell of the enemy sea: what we have fired at and what we found.
 *
 * An unknown square is plain sea. That is the whole game -- the grid is a
 * record of guesses, not a map -- so there is deliberately nothing here that
 * reads the enemy fleet. Getting that wrong would not look like a bug, it
 * would look like a game that is no fun. */
void SeaBattleGame::drawCell(AppContext& host, uint8_t cell) const {
    Ui::Renderer& tft = host.display();
    const Rect r = cellRect(cell);
    const Side& us = side_[viewer()];

    if (phase_ == Phase::Placing) {
        // Our own sea, full size, while the fleet is being laid out.
        const bool ship = us.shipAt[cell] != NO_SHIP;
        tft.fillRect(r.x, r.y, r.w, r.h, ship ? shipColour() : seaColour());
        tft.drawRect(r.x, r.y, r.w, r.h, seaGrid());
        return;
    }

    tft.fillRect(r.x, r.y, r.w, r.h, seaColour());
    tft.drawRect(r.x, r.y, r.w, r.h, seaGrid());
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    const int16_t cy = static_cast<int16_t>(r.y + r.h / 2);
    switch (us.shot[cell]) {
        case Mark::Miss:
            tft.fillCircle(cx, cy, static_cast<int16_t>(r.w / 6), missColour());
            break;
        case Mark::Hit:
            tft.fillCircle(cx, cy, static_cast<int16_t>(r.w / 3), hitColour());
            break;
        case Mark::Pending:
            // Fired, answer not back yet: a ring, so it reads as "waiting".
            tft.drawCircle(cx, cy, static_cast<int16_t>(r.w / 3), Ui::warning());
            break;
        case Mark::Unknown:
            break;
    }
}

void SeaBattleGame::drawBoard(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    for (uint8_t c = 0; c < CELLS; ++c) drawCell(host, c);
    const Rect b = boardRect();
    tft.drawRect(b.x, b.y, b.w, b.h, Ui::outline());
}

/* Our own sea, small. Ships we still have, ships that have been hit, and the
 * water they have wasted a shot on. Always visible, because being shot at is
 * half of what is happening and a screen you have to switch to is a screen
 * nobody switches to. */
void SeaBattleGame::drawMini(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect m = miniRect();
    const Side& us = side_[viewer()];

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString("Your sea", m.x, static_cast<int16_t>(m.y - LABEL_H), 1);

    for (uint8_t c = 0; c < CELLS; ++c) {
        const Rect r = miniCellRect(c);
        const bool ship = us.shipAt[c] != NO_SHIP;
        uint16_t fill = seaColour();
        if (ship && us.incoming[c]) {
            fill = hitColour();
        } else if (ship) {
            fill = shipColour();
        } else if (us.incoming[c]) {
            fill = missColour();
        }
        tft.fillRect(r.x, r.y, r.w, r.h, fill);
    }
    tft.drawRect(m.x, m.y, m.w, m.h, Ui::outline());
}

void SeaBattleGame::drawStatus(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect r = statusRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());

    char top[24];
    char bot[24];
    top[0] = 0;
    bot[0] = 0;
    uint16_t colour = Ui::text();

    if (mode_ == Mode::Waiting) {
        snprintf(top, sizeof(top), "Asking %s", opponentLabel());
        snprintf(bot, sizeof(bot), "waiting...");
        colour = Ui::muted();
    } else if (won_) {
        snprintf(top, sizeof(top), "You win!");
        snprintf(bot, sizeof(bot), "fleet sunk");
        colour = Ui::success();
    } else if (lost_) {
        snprintf(top, sizeof(top), "Fleet lost");
        snprintf(bot, sizeof(bot), "%s wins", opponentLabel());
        colour = Ui::warning();
    } else if (ended_) {
        snprintf(top, sizeof(top), "Game ended");
        snprintf(bot, sizeof(bot),
                 mode_ == Mode::Remote
                     ? (endedByUs_ ? "you stopped it" : "they stopped it")
                     : "no winner");
        colour = Ui::muted();
    } else if (phase_ == Phase::Placing) {
        snprintf(top, sizeof(top), "Your fleet");
        snprintf(bot, sizeof(bot), "shuffle or ready");
    } else if (mode_ == Mode::Remote && pendingCell_ != NO_CELL) {
        snprintf(top, sizeof(top), "Fired");
        snprintf(bot, sizeof(bot), "waiting...");
        colour = Ui::muted();
    } else if (mode_ == Mode::Remote && !ourTurn()) {
        snprintf(top, sizeof(top), "%s", opponentLabel());
        snprintf(bot, sizeof(bot), "is aiming");
        colour = Ui::muted();
    } else {
        /* What just happened beats whose turn it is: a child who has just
         * scored a hit wants to be told so, and the turn is obvious from the
         * board being tappable. */
        switch (lastShot_) {
            case Shot::Sunk:
                snprintf(top, sizeof(top), "Sunk!");
                snprintf(bot, sizeof(bot), "fire again");
                colour = Ui::success();
                break;
            case Shot::Hit:
                snprintf(top, sizeof(top), "Hit!");
                snprintf(bot, sizeof(bot), "fire again");
                colour = Ui::success();
                break;
            case Shot::Miss:
                snprintf(top, sizeof(top), "Miss");
                snprintf(bot, sizeof(bot), "fire again");
                break;
            default:
                snprintf(top, sizeof(top), mode_ == Mode::Remote ? "Your shot"
                                                                 : "Player %u");
                if (mode_ != Mode::Remote) {
                    snprintf(top, sizeof(top), "Player %u",
                             static_cast<unsigned>(turnPlayer_ + 1));
                }
                snprintf(bot, sizeof(bot), "tap the sea");
                break;
        }
    }

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(colour, Ui::bg());
    tft.drawString(top, r.x, r.y, 2);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(bot, r.x, static_cast<int16_t>(r.y + 14), 1);
}

void SeaBattleGame::drawTally(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    const Rect r = tallyRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    if (phase_ == Phase::Placing) return;

    char line[24];
    snprintf(line, sizeof(line), "Hits %u/%u  lost %u",
             static_cast<unsigned>(side_[viewer()].hitsScored),
             static_cast<unsigned>(FLEET_CELLS),
             static_cast<unsigned>(side_[viewer()].hitsTaken));
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(line, r.x, r.y, 1);
}

void SeaBattleGame::drawButtons(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    if (phase_ == Phase::Placing) {
        Ui::drawButton(tft, readyRect(), "Ready", Ui::success(), Ui::outline(),
                       Ui::bg(), false, 2);
        Ui::drawButton(tft, actionRect(), "Shuffle", Ui::panel(), Ui::outline(),
                       Ui::text(), false, 2);
        return;
    }
    const bool over = gameOver();
    const bool confirming = !over && confirmUntilMs_ != 0;
    Ui::drawButton(tft, actionRect(),
                   over ? "New game" : (confirming ? "Sure?" : "End game"),
                   confirming ? Ui::warning() : Ui::panel(), Ui::outline(),
                   confirming ? Ui::bg() : Ui::text(), false, 2);
}

/* The curtain. Nothing of either sea on it, which is the entire point: this
 * screen exists so that one player can hand the console to the other without
 * handing over the answers. */
void SeaBattleGame::renderPass(AppContext& host) const {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    char line[32];
    snprintf(line, sizeof(line), "Pass to Player %u",
             static_cast<unsigned>(turnPlayer_ + 1));

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(line, GAME_CANVAS_WIDTH / 2, 108, 4);

    const char* sub = !placed_[turnPlayer_] ? "then place your fleet"
                      : lastShot_ == Shot::Sunk ? "a ship went down!"
                      : lastShot_ == Shot::Hit  ? "that was a hit"
                      : lastShot_ == Shot::Miss ? "that was a miss"
                                                : "then take your shot";
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(sub, GAME_CANVAS_WIDTH / 2, 140, 2);
    tft.drawString("Tap when ready", GAME_CANVAS_WIDTH / 2, 180, 2);
    tft.setTextDatum(TL_DATUM);
}

void SeaBattleGame::renderStatic(AppContext& host) {
    if (mode_ == Mode::Lobby) {
        renderLobby(host);
        return;
    }
    if (phase_ == Phase::Passing) {
        renderPass(host);
        dirtyCount_ = 0;
        return;
    }
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());
    drawBoard(host);
    drawMini(host);
    drawStatus(host);
    drawTally(host);
    drawButtons(host);
    dirtyCount_ = 0;
    fullPaint_ = false;
    panelStale_ = false;
}

void SeaBattleGame::renderDynamic(AppContext& host) {
    if (mode_ == Mode::Lobby) {
        renderLobby(host);
        return;
    }
    if (phase_ == Phase::Passing || fullPaint_) {
        renderStatic(host);
        return;
    }
    for (uint8_t i = 0; i < dirtyCount_; ++i) drawCell(host, dirty_[i]);
    dirtyCount_ = 0;
    if (panelStale_) {
        drawMini(host);
        drawStatus(host);
        drawTally(host);
        drawButtons(host);
        panelStale_ = false;
    }
}
