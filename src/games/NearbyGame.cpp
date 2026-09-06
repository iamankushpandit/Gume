#include "NearbyGame.h"

#include <string.h>

#include "engine/NearbyPlay.h"
#include "hal/BleBeacon.h"
#include "hal/Board.h"
#include "ui/Keypad.h"

namespace {
constexpr int16_t TOGGLE_H = 30;
constexpr int16_t TOGGLE_TOP = TOP_BAR_HEIGHT + 6;

/* Signal strength as a word rather than a number. A dBm figure invites a player
 * to compare two meaningless negatives; "Near" and "Far" say the thing the
 * number was standing in for. */
const char* proximityText(int8_t rssi) {
    if (rssi >= -55) return "Near";
    if (rssi >= -70) return "Close";
    return "Far";
}

/* Writes "1234" or "1234 pts" into a caller-owned buffer.
 *
 * This returned a String until the ratchet caught it. Two peers on screen is
 * four of these per rebuild, each one a heap block made and freed while the
 * scanner keeps the list churning -- exactly the fragmentation the memory rule
 * in CLAUDE.md is about, and exactly what RowList itself was rewritten to
 * avoid. The buffer belongs to the caller because RowList copies out of it
 * immediately. */
void scoreText(char* out, size_t cap, uint32_t value, const char* unit) {
    if (unit != nullptr && unit[0] != '\0') {
        snprintf(out, cap, "%lu %s", static_cast<unsigned long>(value), unit);
    } else {
        snprintf(out, cap, "%lu", static_cast<unsigned long>(value));
    }
}

/* Longest realistic value is a 10-digit score, a space and a short unit. */
constexpr size_t SCORE_TEXT_CAP = 24;

/* How long a peer's chip reads "Poked" after the press. Comfortably shorter
 * than the poke's own time on air, so the label going back to "Poke" never
 * suggests the poke has stopped when it has not. */
constexpr uint32_t POKED_LABEL_MS = 4000;
}

/* The draft buffer and the storage it lands in must agree, or a name typed to
 * the on-screen limit would be silently truncated on save. */
static_assert(NearbyGame::draftCapacity() == Board::PEER_NAME_MAX,
              "peer name draft buffer must match Board::PEER_NAME_MAX");

const char* NearbyGame::title() const {
    return "Nearby";
}

void NearbyGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_DEVICE_STATUS, "read radio status");
    rows_.clear();
    rowsStale_ = true;
    lastPeerGeneration_ = NearbyPlay::peerGeneration();
    /* Seeded, not left at zero: the periodic refresh below is what keeps the
     * "Listening" row and the proximity words current while nothing arrives,
     * and a zero here would leave it never firing. */
    lastRefreshMs_ = millis();
    scrollOffset_ = 0;
    scrolling_ = false;
    phase_ = Phase::List;
    markFullDirty();
}

/* Nothing to release: the scan is owned by engine/NearbyPlay and deliberately
 * keeps running when this screen closes -- a notification about somebody
 * beating your score is worth having while you are playing, not only while you
 * are staring at the list. rows_ is a flat member that never allocated. */
void NearbyGame::end(GameHost& host) {
    (void)host;
    rows_.clear();
    rowsStale_ = true;
    scrolling_ = false;
    pokeTargetCount_ = 0;
    pokedId_[0] = '\0';
    phase_ = Phase::List;
    clearDraft();
}

Rect NearbyGame::toggleRect(int16_t screenW) const {
    return Rect{8, TOGGLE_TOP, static_cast<int16_t>(screenW - 16), TOGGLE_H};
}

Rect NearbyGame::contentRect(int16_t screenW, int16_t screenH) const {
    const int16_t top = static_cast<int16_t>(TOGGLE_TOP + TOGGLE_H + 6);
    return Rect{0, top, screenW, static_cast<int16_t>(screenH - top)};
}

void NearbyGame::rebuildRows(GameHost& host) {
    Board& board = host.board();
    rows_.clear();
    pokeTargetCount_ = 0;
    /* Read once per rebuild rather than per peer: it is the actor that
     * matters, not the peer, and this decides whether Name chips exist at
     * all. */
    const bool isAdmin = board.isAdminProfile(board.activeProfile());

    /* Three states, said out loud. An empty list under a switch that reads
     * "On" would look identical whether the radio was off, the feature was
     * off, or nobody was there. */
    if (!board.bleBeaconEnabled()) {
        rows_.addSection("Radio");
        rows_.addRow("Beacon", "Off", Ui::muted());
        rows_.addRow("Turn on in", "Settings > Beacon", Ui::muted());
        rows_.addRow("Then", "Settings > Nearby", Ui::muted());
        rowsStale_ = false;
        return;
    }

    if (!NearbyPlay::enabled()) {
        rows_.addSection("Nearby");
        rows_.addRow("Sharing", "Off", Ui::muted());
        rows_.addRow("Shares", "Game open + best score");
        rows_.addRow("Never shares", "Names, profiles", Ui::success());
        rows_.addRow("You appear as", BleBeacon::configured().deviceId);
        rowsStale_ = false;
        return;
    }

    rows_.addSection("You");
    rows_.addRow("Your tag", BleBeacon::configured().deviceId);
    rows_.addRow("Listening", NearbyPlay::active() ? "Yes" : "Radio starting",
                 NearbyPlay::active() ? Ui::success() : Ui::warning());

    const uint8_t peers = NearbyPlay::peerCount();
    if (peers == 0) {
        rows_.addSection("Players");
        rows_.addRow("Found", "Nobody yet", Ui::muted());
        rows_.addRow("Range", "A few metres", Ui::muted());
        rowsStale_ = false;
        return;
    }

    for (uint8_t i = 0; i < peers; ++i) {
        const NearbyPlay::PeerView peer = NearbyPlay::peerAt(board, i);
        if (peer.deviceId[0] == '\0') {
            continue;
        }
        /* The section rule starts a fixed 54px in, so a heading longer than
         * about six characters gets struck through. The tag alone is the
         * heading; everything else about the peer is a row.
         *
         * A named peer shows its name there instead, which is the whole point
         * of naming one -- but the tag stays visible as its own row, because
         * the tag is what actually travels and what the other device calls
         * itself. Replacing it entirely would leave nobody able to work out
         * which console "RAVI" is when the label turns out to be on the wrong
         * one. PEER_NAME_MAX is 10 against that ~6 character rule, so a long
         * name is struck through by the rule -- accepted: a truncated name is
         * worse than a decorated one. */
        const char* label = board.peerName(peer.deviceId);
        rows_.addSection(label != nullptr ? label : peer.deviceId);
        if (label != nullptr) {
            rows_.addRow("Tag", peer.deviceId, Ui::muted());
        }
        rows_.addRow("Distance", proximityText(peer.rssi), Ui::muted());

        /* An idle peer gets the same treatment as a playing one, minus the
         * score rows. This used to `continue` here, which skipped everything
         * below -- including the Poke chip. A console sitting at its launcher
         * is exactly the peer you most want to poke, and it was the only kind
         * that could not be poked. Whatever is added after this point must
         * stay reachable from BOTH paths. */
        if (peer.gameTitle == nullptr) {
            rows_.addRow("Playing", peer.sharing ? "Choosing a game" : "Not sharing",
                         Ui::muted());
        } else {
            rows_.addRow("Playing", peer.gameTitle);

            char theirs[SCORE_TEXT_CAP];
            scoreText(theirs, sizeof(theirs), peer.theirScore, peer.unit);
            rows_.addRow("Their best", theirs, peer.beatsYou ? Ui::warning() : 0);

            char yours[SCORE_TEXT_CAP];
            if (peer.haveOwnScore) {
                scoreText(yours, sizeof(yours), peer.yourScore, peer.unit);
            }
            rows_.addRow("Your best",
                         peer.haveOwnScore ? yours : "Not played yet",
                         peer.haveOwnScore ? 0 : Ui::muted());
            if (peer.beatsYou) {
                rows_.addRow("", "They are ahead of you", Ui::warning());
            }
        }

        /* One chip per peer, carrying the index into pokeTargets_ rather than
         * the peer's position in the live table -- see the comment on that
         * member. Chips stop being added once the array is full; eight peers
         * is already more than the scanner tracks. */
        if (pokeTargetCount_ < MAX_POKE_TARGETS) {
            const bool justPoked = pokedId_[0] != '\0' &&
                strncmp(pokedId_, peer.deviceId, sizeof(pokedId_)) == 0 &&
                millis() - pokedAtMs_ < POKED_LABEL_MS;
            char chipLabel[RowList::LABEL_MAX];
            snprintf(chipLabel, sizeof(chipLabel), justPoked ? "Poked %s" : "Poke %s",
                     peer.deviceId);
            snprintf(pokeTargets_[pokeTargetCount_], sizeof(pokeTargets_[0]), "%s",
                     peer.deviceId);
            rows_.addAction(chipLabel, static_cast<int8_t>(pokeTargetCount_));
            /* Naming is admin-only, so a player who cannot use the chip is not
             * shown one. The greyed-out-but-live mistake has been made in
             * Settings already; absent is cleaner than decorative. */
            if (isAdmin) {
                char nameLabel[RowList::LABEL_MAX];
                snprintf(nameLabel, sizeof(nameLabel), "%s %s",
                         label != nullptr ? "Rename" : "Name", peer.deviceId);
                rows_.addAction(nameLabel,
                                static_cast<int8_t>(NAME_ACTION_BASE + pokeTargetCount_));
            }
            ++pokeTargetCount_;
        }
    }

    rowsStale_ = false;
}

void NearbyGame::clearDraft() {
    draft_[0] = '\0';
    draftLen_ = 0;
}

Rect NearbyGame::nameCancelRect(int16_t screenW, int16_t screenH) const {
    return Rect{static_cast<int16_t>((screenW - 52) / 2),
                static_cast<int16_t>(screenH - 30), 52, 22};
}

/* The naming phase. The keyboard's grid, hit testing and drawing all come from
 * ui/Keypad; what lives here is the only part that is about peers -- what OK
 * means, and the fact that an empty name forgets the device rather than
 * storing a blank one. */
void NearbyGame::updateName(GameHost& host, const TouchPoint& touch) {
    Board& board = host.board();
    const int16_t W = static_cast<int16_t>(host.display().width());
    const int16_t H = static_cast<int16_t>(host.display().height());

    if (!touch.justPressed) {
        return;
    }
    if (nameCancelRect(W, H).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        phase_ = Phase::List;
        clearDraft();
        rowsStale_ = true;
        markFullDirty();
        return;
    }

    const char ch = Ui::Keypad::hit(touch.x, touch.y, W, H,
                                    Ui::Keypad::FOOTER_BUTTON);
    if (ch == 0) {
        return;
    }
    if (ch == Ui::Keypad::BACKSPACE) {
        if (draftLen_ > 0) {
            draft_[--draftLen_] = '\0';
        }
    } else if (ch == Ui::Keypad::ACCEPT) {
        /* An empty draft CLEARS the label, which is why OK is not gated on
         * there being text: without that, a mistyped name would be permanent.
         */
        if (!board.setPeerName(namingId_, draft_)) {
            board.beepError();
            return;
        }
        board.beepOk();
        phase_ = Phase::List;
        clearDraft();
        rowsStale_ = true;
        markFullDirty();
        return;
    } else if (draftLen_ < DRAFT_MAX) {
        draft_[draftLen_++] = ch;
        draft_[draftLen_] = '\0';
    }
    markDirty();
}

void NearbyGame::renderName(GameHost& host) {
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    if (needsFullRender()) {
        Ui::clear(tft);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Ui::muted(), Ui::bg());
        char heading[32];
        snprintf(heading, sizeof(heading), "Name for %s", namingId_);
        tft.drawString(heading, static_cast<int16_t>(W / 2), 8, 2);
        Ui::drawButton(tft, nameCancelRect(W, H), "Cancel", Ui::panel(),
                       Ui::outline(), Ui::text(), false, 1);
        Ui::Keypad::draw(tft, W, H, Ui::Keypad::FOOTER_BUTTON);
    }

    /* Only the field repaints per keystroke. The keyboard is 39 buttons and
     * redrawing it for every letter is most of a frame budget spent on pixels
     * that did not change. */
    const int16_t fieldW = static_cast<int16_t>(min<int16_t>(240, W - 40));
    const Rect field{static_cast<int16_t>((W - fieldW) / 2), 28, fieldW, 30};
    tft.fillRoundRect(field.x, field.y, field.w, field.h, 4, Ui::surface());
    tft.drawRoundRect(field.x, field.y, field.w, field.h, 4, Ui::outline());
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.setTextDatum(MC_DATUM);
    /* An empty draft says what OK will do, rather than showing nothing and
     * leaving "will this clear it, or refuse?" to be discovered. */
    tft.drawString(draftLen_ > 0 ? draft_ : "(no name)",
                   static_cast<int16_t>(W / 2),
                   static_cast<int16_t>(field.y + field.h / 2), 4);
    tft.setTextDatum(TL_DATUM);
}

void NearbyGame::update(GameHost& host, const TouchPoint& touch) {
    if (phase_ == Phase::Name) {
        updateName(host, touch);
        return;
    }

    const uint32_t now = millis();
    const bool peersChanged = NearbyPlay::peerGeneration() != lastPeerGeneration_;
    if (peersChanged || now - lastRefreshMs_ >= REFRESH_MS) {
        lastPeerGeneration_ = NearbyPlay::peerGeneration();
        lastRefreshMs_ = now;
        rowsStale_ = true;
        /* Repaint on the clock only when there is something on screen that the
         * clock can change -- which means a peer, whose score and state are
         * re-read on the tick. With no peers every row rebuildRows() produces
         * is fixed text: the beacon is off, sharing is off, or nobody is here.
         * Those are the three states this screen spends almost all its life in,
         * and the default one is the first, so the old unconditional tick
         * wiped and redrew the whole content panel once a second, forever, for
         * text that cannot change. The rebuild still runs; only the repaint is
         * withheld. */
        if (peersChanged || NearbyPlay::peerCount() > 0) {
            markDirty();
        }
    }

    /* Retire the "Poked" label once it has had its moment. Done here rather
     * than left to the one-second tick so the chip cannot sit reading "Poked"
     * on a screen with no peers left to refresh it. */
    if (pokedId_[0] != '\0' && now - pokedAtMs_ >= POKED_LABEL_MS) {
        pokedId_[0] = '\0';
        rowsStale_ = true;
        markDirty();
    }

    Board& board = host.board();
    const int16_t W = static_cast<int16_t>(host.display().width());
    const int16_t H = static_cast<int16_t>(host.display().height());
    const Rect content = contentRect(W, H);

    if (touch.justReleased) {
        scrolling_ = false;
    }

    if (touch.justPressed) {
        if (Rect{0, 0, 42, TOP_BAR_HEIGHT}.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            host.goHome();
            return;
        }

        if (toggleRect(W).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            /* The beacon is the master switch, so this control cannot turn
             * itself on past it. Saying no out loud beats a button that
             * silently does nothing. */
            if (!board.bleBeaconEnabled()) {
                board.beepError();
                return;
            }
            if (!host.requireCapability(APP_CAP_DEVICE_SETTINGS, "change nearby sharing")) {
                return;
            }
            NearbyPlay::setEnabled(board, !NearbyPlay::enabled());
            board.beepOk();
            scrollOffset_ = 0;
            rowsStale_ = true;
            markFullDirty();
            return;
        }

        if (content.contains(touch.x, touch.y)) {
            const int8_t action = rows_.actionAt(touch.x, touch.y);
            if (action >= NAME_ACTION_BASE &&
                action < static_cast<int8_t>(NAME_ACTION_BASE + pokeTargetCount_)) {
                /* Gated HERE, not merely by withholding the chip: a chip is a
                 * drawing decision and enforces nothing on its own, which is
                 * exactly how every greyed-out Settings row stayed live. */
                if (!host.requireCapability(APP_CAP_DEVICE_SETTINGS,
                                            "name a nearby device")) {
                    return;
                }
                const int8_t which = static_cast<int8_t>(action - NAME_ACTION_BASE);
                snprintf(namingId_, sizeof(namingId_), "%s", pokeTargets_[which]);
                const char* existing = board.peerName(namingId_);
                snprintf(draft_, sizeof(draft_), "%s", existing != nullptr ? existing : "");
                draftLen_ = static_cast<uint8_t>(strlen(draft_));
                phase_ = Phase::Name;
                markFullDirty();
                return;
            }
            if (action >= 0 && action < static_cast<int8_t>(pokeTargetCount_)) {
                /* NearbyPlay::poke() re-derives the gate itself, so a chip
                 * pressed just as the radio went down is refused there rather
                 * than here. Saying no out loud beats a button that silently
                 * does nothing -- the same rule the sharing toggle follows. */
                if (NearbyPlay::poke(board, pokeTargets_[action])) {
                    snprintf(pokedId_, sizeof(pokedId_), "%s", pokeTargets_[action]);
                    pokedAtMs_ = millis();
                    rowsStale_ = true;
                    markDirty();
                } else {
                    board.beepError();
                }
                return;
            }
        }

        if (content.contains(touch.x, touch.y) && rows_.totalHeight() > content.h) {
            scrolling_ = true;
            scrollAnchorY_ = touch.y;
            scrollStartOffset_ = scrollOffset_;
        }
    } else if (touch.down && scrolling_) {
        scrollOffset_ = static_cast<int16_t>(scrollStartOffset_ - (touch.y - scrollAnchorY_));
        rows_.clampScroll(scrollOffset_, content.h);
        markDirty();
    }
}

void NearbyGame::renderStatic(GameHost& host) {
    if (phase_ == Phase::Name) {
        return;   // renderName() paints the whole phase, chrome included
    }
    Ui::clear(host.display());
    Ui::drawTopBar(host.board(), title());
    drawnToggle_ = false;
}

void NearbyGame::renderDynamic(GameHost& host) {
    if (phase_ == Phase::Name) {
        renderName(host);
        return;
    }
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    const bool radioOn = board.bleBeaconEnabled();
    const bool sharing = NearbyPlay::enabled();
    if (!drawnToggle_ || radioOn != drawnRadio_ || sharing != drawnSharing_) {
        char label[32];
        if (!radioOn) {
            snprintf(label, sizeof(label), "Needs Beacon in Settings");
        } else {
            snprintf(label, sizeof(label), "Sharing: %s", sharing ? "On" : "Off");
        }
        const uint16_t fill = !radioOn ? Ui::panel()
                                       : (sharing ? Ui::success() : Ui::panel());
        Ui::drawButton(tft, toggleRect(W), label, fill, Ui::outline(),
                       radioOn ? Ui::text() : Ui::muted());
        drawnRadio_ = radioOn;
        drawnSharing_ = sharing;
        drawnToggle_ = true;
    }

    const Rect cr = contentRect(W, H);
    if (rowsStale_) {
        rebuildRows(host);
    }
    rows_.clampScroll(scrollOffset_, cr.h);
    /* RowList::draw() opens by filling the whole content rect, so it erases
     * itself -- a peer leaving and the list getting shorter needs nothing from
     * us. That also makes it the expensive part of this screen, which is why
     * update() is careful about when it asks for a repaint at all. */
    rows_.draw(tft, cr, scrollOffset_);
}
