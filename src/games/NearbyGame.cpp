#include "NearbyGame.h"

#include "engine/NearbyPlay.h"
#include "hal/BleBeacon.h"
#include "hal/Board.h"

namespace {
constexpr int16_t TOGGLE_H = 30;
constexpr int16_t TOGGLE_TOP = TOP_BAR_HEIGHT + 6;

/* Signal strength as a word rather than a number. A dBm figure invites a child
 * to compare two meaningless negatives; "Near" and "Far" say the thing the
 * number was standing in for. */
const char* proximityText(int8_t rssi) {
    if (rssi >= -55) return "Near";
    if (rssi >= -70) return "Close";
    return "Far";
}

String scoreText(uint32_t value, const char* unit) {
    String out(value);
    if (unit != nullptr && unit[0] != '\0') {
        out += ' ';
        out += unit;
    }
    return out;
}
}

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
         * heading; everything else about the peer is a row. */
        rows_.addSection(peer.deviceId);
        rows_.addRow("Distance", proximityText(peer.rssi), Ui::muted());
        if (peer.gameTitle == nullptr) {
            rows_.addRow("Playing", peer.sharing ? "Choosing a game" : "Not sharing",
                         Ui::muted());
            continue;
        }
        rows_.addRow("Playing", peer.gameTitle);
        rows_.addRow("Their best", scoreText(peer.theirScore, peer.unit),
                     peer.beatsYou ? Ui::warning() : 0);
        rows_.addRow("Your best",
                     peer.haveOwnScore ? scoreText(peer.yourScore, peer.unit)
                                       : String("Not played yet"),
                     peer.haveOwnScore ? 0 : Ui::muted());
        if (peer.beatsYou) {
            rows_.addRow("", "They are ahead of you", Ui::warning());
        }
    }

    rowsStale_ = false;
}

void NearbyGame::update(GameHost& host, const TouchPoint& touch) {
    const uint32_t now = millis();
    if (NearbyPlay::peerGeneration() != lastPeerGeneration_ ||
        now - lastRefreshMs_ >= REFRESH_MS) {
        lastPeerGeneration_ = NearbyPlay::peerGeneration();
        lastRefreshMs_ = now;
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

void NearbyGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t W = static_cast<int16_t>(tft.width());
    const int16_t H = static_cast<int16_t>(tft.height());

    if (needsFullRender()) {
        Ui::clear(tft);
        Ui::drawTopBar(board, title());
    }

    const bool radioOn = board.bleBeaconEnabled();
    const bool sharing = NearbyPlay::enabled();
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

    const Rect cr = contentRect(W, H);
    if (rowsStale_) {
        rebuildRows(host);
    }
    rows_.clampScroll(scrollOffset_, cr.h);
    rows_.draw(tft, cr, scrollOffset_);
}
