// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "ChessGame.h"

#include <string.h>

#include "ChessInternal.h"

/* Chess on two consoles: the lobby that starts it and the poll that plays it.
 * Everything on the air is the nearby service's two-player turn -- see
 * src/games/CLAUDE.md. */

namespace {

/* How often the lobby looks at the room, and a waiting console at whoever it
 * asked. Reading the peer table walks a small array under a lock; a second is
 * as fresh as a list of consoles needs to be. */
constexpr uint32_t ROOM_REFRESH_MS = 1000;

/* What a console's row in the lobby says about it. The first that fits wins,
 * top to bottom: an invitation to us is the one thing a player is being
 * asked, so it outranks whatever else that console is doing. */
enum class RowKind : uint8_t {
    Invites,    // asking this console for a game of chess: press to accept
    Asking,     // asking somebody else: busy, nothing to press
    Playing,    // in a game with a console we can also hear: likewise
    PlayBusy,   // in a game of some kind, with whom we cannot tell: can be asked
    Play,       // doing nothing we know of: can be asked
};

RowKind rowKind(const NearbySeat& seat) {
    if (seat.inviting && seat.forThisGame) return RowKind::Invites;
    if (seat.asking) return RowKind::Asking;
    if (seat.inGame && seat.partner[0] != 0) return RowKind::Playing;
    if (seat.inGame) return RowKind::PlayBusy;
    return RowKind::Play;
}

bool pressable(RowKind kind) {
    return kind != RowKind::Asking && kind != RowKind::Playing;
}

/* Would the two be drawn as the same row? Everything a row shows is here, so
 * the lobby repaints when a console starts or stops asking, joins or leaves
 * a game or is given a name -- not only when one arrives or goes. */
bool sameRow(const NearbySeat& a, const NearbySeat& b) {
    return rowKind(a) == rowKind(b) && strcmp(a.deviceId, b.deviceId) == 0 &&
           strcmp(a.name, b.name) == 0 && strcmp(a.partner, b.partner) == 0 &&
           strcmp(a.partnerName, b.partnerName) == 0;
}

/* A row that is news, not a control, and drawn as one: flat, muted, with no
 * outline and no shadow. A fill and an outline of one rectangle is what the
 * d-pad's selector stops on, and there is nothing here to press. */
void drawNewsRow(Ui::Renderer& tft, const Rect& r, const char* label) {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, Ui::cornerRadius(), Ui::surface());
    tft.setTextColor(Ui::muted(), Ui::surface());
    tft.setTextDatum(MC_DATUM);
    tft.drawString(Ui::fitted(tft, label, static_cast<int16_t>(r.w - 12), 2),
                   static_cast<int16_t>(r.x + r.w / 2),
                   static_cast<int16_t>(r.y + r.h / 2), 2);
    tft.setTextDatum(TL_DATUM);
}

}   // namespace

void ChessGame::renderLobby(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* The two chips. A label above a value, with a small triangle saying a
     * tap cycles it -- the same shape Go uses, so a player who has met one
     * knows what the other does. They qualify the computer row below them. */
    char label[40];
    static const char* const SIDES[3] = {"White", "Black", "Random"};
    const char* const values[2] = {
        level_ == Ch::Level::Easy ? "Easy" : "Medium",
        SIDES[static_cast<uint8_t>(sideChoice_)],
    };
    static const char* const CHIPS[2] = {"Level", "You play"};
    for (uint8_t i = 0; i < 2; ++i) {
        const Rect r = lobbyChipRect(host, i);
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::surface());
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(CHIPS[i], static_cast<int16_t>(r.x + 7),
                       static_cast<int16_t>(r.y + 3), 1);
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(values[i], static_cast<int16_t>(r.x + 7),
                       static_cast<int16_t>(r.y + 12), 2);
        tft.fillTriangle(static_cast<int16_t>(r.x + r.w - 16),
                         static_cast<int16_t>(r.y + 11),
                         static_cast<int16_t>(r.x + r.w - 8),
                         static_cast<int16_t>(r.y + 11),
                         static_cast<int16_t>(r.x + r.w - 12),
                         static_cast<int16_t>(r.y + 17), Ui::muted());
    }
    tft.setTextColor(Ui::text(), Ui::bg());

    /* The player's own unfinished game, when the lobby is up in front of it,
     * takes the row for its kind and says so. The other kind is not offered
     * while it is: starting one would write over the game being held, and a
     * game is only ever given up with End game. */
    const uint8_t rows = lobbyRowCount(host);
    if (heldMode_ == Mode::Computer) {
        drawNewsRow(tft, lobbyRowRect(host, 0), "Pass and play");
    } else {
        Ui::drawButton(tft, lobbyRowRect(host, 0),
                       heldMode_ == Mode::Local ? "Continue your game" : "Pass and play",
                       Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    }
    if (rows > 1 && heldMode_ == Mode::Local) {
        drawNewsRow(tft, lobbyRowRect(host, 1), "Play the computer");
    } else if (rows > 1) {
        Ui::drawButton(tft, lobbyRowRect(host, 1),
                       heldMode_ == Mode::Computer ? "Continue vs computer"
                                                   : "Play the computer",
                       Ui::panel(), Ui::outline(), Ui::text(), false, 2);
    }

    uint8_t row = 2;
    for (uint8_t i = 0; i < seatCount_ && row < rows; ++i, ++row) {
        /* One row a console, saying what it is doing: a peer offering us a
         * game reads differently from one merely present, and one busy with
         * somebody else has to say that too, or the only sign of it is an
         * invitation that never gets answered. */
        /* The owner's own label for that console when they have given it
         * one. Naming a peer is the whole point of being able to name one, so
         * it has to change what every screen calls it, not just the Nearby
         * app's list. */
        const NearbySeat& seat = seats_[i];
        const char* who = seat.name[0] != 0 ? seat.name : seat.deviceId;
        const char* partner = seat.partnerName[0] != 0 ? seat.partnerName
                                                       : seat.partner;
        const RowKind kind = rowKind(seat);
        switch (kind) {
            case RowKind::Invites:
                snprintf(label, sizeof(label), "%s invites you", who);
                break;
            case RowKind::Asking:
                snprintf(label, sizeof(label), "%s is asking %s", who, partner);
                break;
            case RowKind::Playing:
                snprintf(label, sizeof(label), "%s is playing %s", who, partner);
                break;
            case RowKind::PlayBusy:
                snprintf(label, sizeof(label), "Play %s (in a game)", who);
                break;
            default:
                snprintf(label, sizeof(label), "Play %s", who);
                break;
        }
        const Rect r = lobbyRowRect(host, row);
        if (!pressable(kind)) {
            drawNewsRow(tft, r, label);
            continue;
        }
        const uint16_t fill = kind == RowKind::Invites ? Ui::success() : Ui::panel();
        Ui::drawButton(tft, r, label, fill, Ui::outline(),
                       kind == RowKind::Invites ? Ui::onFill(fill) : Ui::text(),
                       false, 2);
    }

    tft.setTextDatum(BC_DATUM);
    /* The empty case names who can fix it. Switching the radio on is an
     * admin job, so a player who reads "turn Beacon on" and cannot find the
     * switch has been sent to a door they have no key for. Playing, once it is
     * on, needs no admin at all. A low battery outranks both: a console that
     * dies mid-game cannot tell anyone, so this is the moment to say it. */
    const bool low = seatCount_ > 0 && host.batteryLow();
    tft.setTextColor(low ? Ui::warning() : Ui::muted(), Ui::bg());
    const char* note = low             ? "Battery low: a nearby game may not finish."
                       : seatCount_ > 0 ? "Moves travel by Bluetooth. Anyone near hears them."
                                        : "An administrator can switch Beacon and Nearby on.";
    tft.drawString(note, static_cast<int16_t>(tft.width() / 2),
                   static_cast<int16_t>(tft.height() - 6), 1);
    tft.setTextDatum(TL_DATUM);
}

void ChessGame::updateLobby(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ > ROOM_REFRESH_MS) {
        seatsAtMs_ = now;
        /* Consoles asking us first, then everybody else, each in the order
         * the service gives them. A panel in landscape has room for two
         * consoles, and an invitation from a third must not be the row that
         * did not fit: it stands until it is answered, so it has to be
         * somewhere a player can answer it. */
        NearbySeat fresh[6];
        uint8_t count = 0;
        const uint8_t n = host.nearbySeatCount();
        for (uint8_t pass = 0; pass < 2; ++pass) {
            for (uint8_t i = 0; i < n && count < 6; ++i) {
                NearbySeat seat;
                if (!host.nearbySeatAt(i, seat)) continue;
                /* An invitation to some other game is not one to accept here:
                 * answering a Sea Battle invitation from this lobby leaves
                 * two consoles playing different games at each other. Its row
                 * reads "Play A4F2" instead -- see rowKind(). */
                const bool asksUs = rowKind(seat) == RowKind::Invites;
                if (asksUs != (pass == 0)) continue;
                fresh[count++] = seat;
            }
        }

        /* Compare the rows, not just how many there are. A peer that starts
         * inviting us does not change the count -- it changes what its row
         * says, from "Play A4F2" to "A4F2 invites you" -- so a count-only test
         * left an invitation sitting on the air with nothing on screen to
         * accept it, until some unrelated console wandered in or out of
         * range. The same goes for a console starting or finishing a game
         * with somebody else.
         *
         * Only the rows the panel has room for are compared, and whether
         * there is anybody at all, which the footnote says: a console that
         * did not fit must not clear the screen each time it does something. */
        const uint8_t rows = lobbyRowCount(host);
        const uint8_t room = rows > 2 ? static_cast<uint8_t>(rows - 2) : 0;
        const uint8_t shown = count < room ? count : room;
        bool changed = shown != (seatCount_ < room ? seatCount_ : room) ||
                       (count == 0) != (seatCount_ == 0);
        for (uint8_t i = 0; !changed && i < shown; ++i) {
            changed = !sameRow(fresh[i], seats_[i]);
        }
        for (uint8_t i = 0; i < count; ++i) seats_[i] = fresh[i];
        seatCount_ = count;
        if (changed) markFullDirty();
    }

    if (!touch.justPressed) return;

    /* The chips cycle. Neither is admin-gated and neither transmits: they are
     * settings for a game against this console. They are saved with the
     * lobby -- unless the player's own game is being held behind it, which a
     * save of the lobby would write over. */
    if (lobbyChipRect(host, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        level_ = level_ == Ch::Level::Easy ? Ch::Level::Medium : Ch::Level::Easy;
        host.playSound(Sound::Tap);
        if (heldMode_ == Mode::Lobby) saveGame(host);
        markFullDirty();
        return;
    }
    if (lobbyChipRect(host, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        sideChoice_ = static_cast<Side>((static_cast<uint8_t>(sideChoice_) + 1) % 3);
        host.playSound(Sound::Tap);
        if (heldMode_ == Mode::Lobby) saveGame(host);
        markFullDirty();
        return;
    }

    /* The two rows for a game on this console alone. With the player's own
     * game held behind the lobby, its row brings it back exactly as it was
     * left -- or leaves the lobby up, if it has gone from the save since --
     * and the other row is not a button: see renderLobby(). */
    const uint8_t rows = lobbyRowCount(host);
    if (lobbyRowRect(host, 0).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (heldMode_ == Mode::Computer) return;
        host.playSound(Sound::Select);
        if (heldMode_ == Mode::Local) {
            openPlayerSave(host, true);
            markFullDirty();
        } else {
            startLocal();
        }
        return;
    }
    if (rows > 1 &&
        lobbyRowRect(host, 1).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (heldMode_ == Mode::Local) return;
        host.playSound(Sound::Select);
        if (heldMode_ == Mode::Computer) {
            openPlayerSave(host, true);
        } else {
            startComputer();
        }
        markFullDirty();
        return;
    }
    /* Only the rows that were drawn. The ones that did not fit used to be
     * tested all the same, and a press on the footnote under the last row
     * invited a console that was not on the screen. */
    uint8_t row = 2;
    for (uint8_t i = 0; i < seatCount_ && row < rows; ++i, ++row) {
        if (!lobbyRowRect(host, row).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        const RowKind kind = rowKind(seats_[i]);
        /* A console busy with somebody else: its row is news, not a button. */
        if (!pressable(kind)) return;
        if (kind == RowKind::Invites) {
            /* The list is up to a second old and an invitation can be taken
             * back inside that second. Accepting one that has gone would sit
             * this console in a game alone, so it is asked for again now. */
            NearbySeat seat;
            if (inviterNow(host, seats_[i].deviceId, seat)) {
                acceptInvite(host, seat);
            } else {
                host.beepError();
                seatsAtMs_ = 0;   // look at the room again at once
            }
        } else {
            inviteSeat(host, seats_[i]);
        }
        return;
    }
}

bool ChessGame::inviterNow(AppContext& host, const char* deviceId,
                           NearbySeat& out) const {
    /* Every seat is looked at, rather than the service's one "an invitation
     * for us": with two consoles asking at once, that one could be the other
     * console's, or an invitation to a different game. */
    const uint8_t n = host.nearbySeatCount();
    for (uint8_t i = 0; i < n; ++i) {
        if (!host.nearbySeatAt(i, out)) continue;
        if (!out.inviting || !out.forThisGame) continue;
        if (deviceId == nullptr || strcmp(out.deviceId, deviceId) == 0) return true;
    }
    return false;
}

/* Accepting: they chose the session, and which of us moves first came with
 * the invitation -- the service flipped for it. We answer by publishing ply 0
 * in their session, which is the whole handshake; there is no second round
 * trip to go missing. From this moment the game is the console's, and is
 * saved as that, so a console switched off before the first move still comes
 * back to it. */
void ChessGame::acceptInvite(AppContext& host, const NearbySeat& seat) {
    host.playSound(Sound::Select);
    startRemote(seat, seat.session, seat.weMoveFirst);
    saveGame(host);
}

/* Inviting: our session id comes from the service, which starts from the
 * clock rather than a counter so two consoles inviting each other at once are
 * unlikely to pick the same number, and steps past any session this console
 * still holds a turn for -- an old game's last move must not read as this
 * one's. Which of us is White is NOT ours to decide -- nearbyInvite() tosses
 * for it and tells us.
 *
 * The invitation then stands. Nothing here sends it again, times it out or
 * gives up on it: it is on the air until the other console answers, or the
 * player takes it back with the button, or leaves the screen. */
void ChessGame::inviteSeat(AppContext& host, const NearbySeat& seat) {
    const uint8_t session = host.nearbyFreshSession();
    bool weMoveFirst = false;
    if (!host.nearbyInvite(seat.deviceId, session, weMoveFirst)) {
        host.beepError();
        return;
    }
    host.playSound(Sound::Select);
    startRemote(seat, session, weMoveFirst);
    /* Back to Waiting: startRemote() sets up the game, but nobody has
     * accepted yet and the board must not take a move until somebody has. */
    mode_ = Mode::Waiting;
    markFullDirty();
}

/* Two consoles that ask each other in the same moment would each wait for
 * the other for ever: an invitation no longer runs out, so neither question
 * would ever leave the air to let the other be answered. One has to give
 * way, and both have to agree which without exchanging anything -- so it is
 * the one with the greater tag. It takes its own invitation back and accepts
 * theirs, exactly as a press on their row in the lobby would; the one with
 * the smaller tag does nothing and is answered. No clock decides it. */
void ChessGame::answerCrossedInvite(AppContext& host) {
    const uint32_t now = millis();
    if (now - seatsAtMs_ <= ROOM_REFRESH_MS) return;
    seatsAtMs_ = now;
    NearbySeat seat;
    if (!inviterNow(host, opponent_, seat)) return;
    if (strcmp(host.nearbySelfId(), opponent_) <= 0) return;
    host.nearbyStop();
    acceptInvite(host, seat);
}

/* Take the opponent's move, if there is one we should act on.
 *
 * Four tests, and every one of them is load-bearing:
 *
 *   from this peer      -- another console's game must not leak into ours
 *   in this session     -- nor a previous game between the same two consoles
 *   the ply we expect   -- an advertisement repeats, so the same move arrives
 *                          many times; acting once is what makes it a move
 *                          rather than a stutter
 *   legal here          -- and this is the one that matters for safety. A
 *                          move is only applied if it is legal in OUR
 *                          position, so a confused or hostile advertiser
 *                          cannot force the board into a state that is not
 *                          reachable by playing chess.
 */
void ChessGame::pollOpponent(AppContext& host) {
    if (mode_ != Mode::Remote && mode_ != Mode::Waiting) return;
    if (opponent_[0] == 0) return;
    /* A finished board takes nothing more. A late "I am stopping" must not
     * relabel a checkmate as a game somebody walked away from, nor answer a
     * game this console had already stopped itself. */
    if (gameOver()) return;

    NearbyTurn turn;
    if (!host.nearbyTurnFrom(opponent_, session_, turn)) return;

    if (mode_ == Mode::Waiting) {
        /* They answered. Their first advertisement in our session is the
         * acceptance -- no separate message, because a separate message could
         * be the one that goes missing. Nothing is re-derived here: the seat,
         * the session and the side were all settled when the invitation went
         * out, and this only promotes the mode. */
        mode_ = Mode::Remote;
        /* And it is the console's game from here, saved as one before a
         * move has been made, so a console switched off now comes back to
         * the game it has just agreed to play. */
        saveGame(host);
        markFullDirty();
    }

    /* They stopped the game. This is tested BEFORE the whose-turn check on
     * purpose: a player gives up when they are stuck, which is usually while
     * they are waiting for us, and a declaration that only arrived on their
     * own turn would be one that mostly never arrived. The reserved encoding
     * belongs to the nearby service, not here -- this only reads the flag.
     *
     * It is also tested before the exact-ply test, and taken one ply late. A
     * turn is one state on the air: an ending sent straight after a move we
     * never heard has replaced that move for good, and it arrives numbered
     * one past the ply we are waiting for. Moves alternate, so one unheard
     * move is the most there can be. */
    if (turn.ended) {
        const uint8_t ahead = static_cast<uint8_t>((turn.ply - theirPly_) & 0x7F);
        if (ahead != 1 && ahead != 2) return;
        theirPly_ = turn.ply;
        declareEnd(host, false);
        host.playSound(Sound::GameOver);
        return;
    }

    const uint8_t expected = static_cast<uint8_t>((theirPly_ + 1) & 0x7F);
    if (turn.ply != expected) return;

    if (ourTurn()) return;              // not their move to make

    uint8_t legal[MAX_MOVES];
    const uint8_t n = legalMoves(pos_, turn.from, legal);
    bool ok = false;
    for (uint8_t i = 0; i < n; ++i) {
        if (legal[i] == turn.to) { ok = true; break; }
    }
    if (!ok) return;

    /* Their ply is recorded BEFORE the move is played, because playMove()
     * republishes and the ack it sends has to be the one we have just
     * accepted. */
    theirPly_ = turn.ply;
    /* publish == false: this move is theirs. Answering it with one of our own
     * ply numbers would tell them we had moved when we had not. */
    playMove(host, turn.from, turn.to, false);
}

/* ---- leaving a game that is over ---------------------------------------------
 *
 * Always back to the lobby, whichever way the game was being played: a new
 * game across consoles needs a new session and a fresh invitation. What it
 * must not do is take this console's last word off the air before the other
 * console has it.
 *
 * This is also the one place a game against a nearby console is finished
 * with: it is over, the player has read how, and pressed New game. Until
 * then it is the console's and is kept. */
void ChessGame::backToLobby(AppContext& host) {
    const Mode from = mode_;
    if (from == Mode::Waiting) {
        /* The invitation is taken back. Nothing was ever said in its
         * session, so there is no last word to leave behind. */
        host.nearbyStop();
    } else if (from == Mode::Remote) {
        /* Is the turn on the air news the other console needs? Our ending
         * is, and so is the move that finished the game if it was ours --
         * the side left to move is the one that did not make it. Their
         * ending or their last move leaves nothing owed. */
        const bool lastWordOurs = status_ == Status::Ended
                                      ? endedByUs_
                                      : pos_.whiteToMove != remoteIsWhite_;
        if (lastWordOurs) {
            memcpy(farewellPeer_, opponent_, sizeof(farewellPeer_));
            farewellPeer_[sizeof(farewellPeer_) - 1] = 0;
            farewellSession_ = session_;
            farewellPly_ = ourPly_;
            farewellUntilMs_ = millis() + FAREWELL_MS;
            if (farewellUntilMs_ == 0) farewellUntilMs_ = 1;   // 0 means none owed
        } else {
            host.nearbyStop();
        }
        host.clearConsoleBlob(CHESS_SAVE_KEY);
    }
    mode_ = Mode::Lobby;
    opponent_[0] = 0;
    opponentName_[0] = 0;
    session_ = 0;
    seatCount_ = 0;
    seatsAtMs_ = 0;
    newGame();
    if (from == Mode::Remote || from == Mode::Waiting) {
        /* Neither of those wrote anything of the player's. Whatever they had
         * of their own before -- a game against the computer, say, set aside
         * to answer an invitation -- is still in their save, and the lobby
         * offers it again rather than writing a blank board over it. */
        openPlayerSave(host, false);
    } else {
        /* Their own game, and it is over: the lobby takes its place. */
        heldMode_ = Mode::Lobby;
        saveGame(host);
    }
}

void ChessGame::tickFarewell(AppContext& host) {
    if (farewellUntilMs_ == 0) return;
    NearbyTurn turn;
    const bool heard =
        host.nearbyTurnFrom(farewellPeer_, farewellSession_, turn) &&
        turn.ack == farewellPly_;
    /* Not for ever: a console that has left the room, or gone on to another
     * game, will not acknowledge anything. The service takes the turn down
     * only if it is still this game's, so a word already replaced by another
     * game's is left alone. */
    if (!heard && static_cast<int32_t>(millis() - farewellUntilMs_) < 0) return;
    farewellUntilMs_ = 0;
    host.nearbyStop();
}

/* ---- the other console going quiet -----------------------------------------
 *
 * NearbyWatch decides what quiet means; this is what Chess does about it.
 * Only a live remote game is watched: not the lobby, not an invitation still
 * unanswered (that has its own "Asking..." and its own End), and not a game
 * the rules have already finished, where nothing is being waited for. */
bool ChessGame::updatePause(AppContext& host, const TouchPoint& touch) {
    if (mode_ != Mode::Remote || gameOver()) {
        if (watch_.paused()) {
            watch_.reset();
            pausePainted_ = false;
            markFullDirty();
        }
        return false;
    }
    const NearbyWatch::State before = watch_.state();
    if (watch_.tick(host, opponent_, millis())) {
        if (watch_.state() != before) statusStale_ = true;   // the status line names it
        markDirty();
    }
    if (watch_.resumed()) {
        /* They are back. The board under the card is still right; only the
         * card has to go, and the status line goes back to whose move it is. */
        host.playSound(Sound::Pop);
        pausePainted_ = false;
        statusStale_ = true;
        markFullDirty();
        return false;
    }
    if (!watch_.cardShown()) return false;
    switch (watch_.press(boardRect(host), touch, false)) {
        case NearbyWatch::Press::Wait:
            watch_.dismiss();
            pausePainted_ = false;
            host.playSound(Sound::Tap);
            markFullDirty();
            break;
        case NearbyWatch::Press::End:
            /* The same ending End game sends, so a console that does come
             * back hears it and goes to its lobby rather than to a board that
             * nobody is playing any more. */
            declareEnd(host, true);
            host.playSound(Sound::GameOver);
            pausePainted_ = false;
            markFullDirty();
            break;
        default:
            break;
    }
    return touch.justPressed;
}
