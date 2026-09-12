#include "NearbyWatch.h"

#include <stdio.h>

#include "engine/NearbyPlay.h"

/* See NearbyWatch.h. The thresholds are the service's, not this file's:
 * NearbyPlay::PEER_QUIET_MS and the scanner's SIGHTING_TTL_MS decide when a
 * peer is Quiet and Gone; this only turns those into a card. */

namespace {

constexpr int16_t CARD_W = 196;
constexpr int16_t CARD_H = 100;    // title, seconds, one row of two buttons
constexpr int16_t EXTRA_H = 30;    // the third button, when there is one
constexpr int16_t PAD = 8;
constexpr int16_t BUTTON_H = 26;
constexpr int16_t TITLE_H = 18;
constexpr int16_t LINE_H = 14;

}   // namespace

void NearbyWatch::reset() {
    state_ = State::Present;
    dismissed_ = State::Present;
    who_ = 0;
    seconds_ = 0;
    resumed_ = false;
}

NearbyWatch::State NearbyWatch::stateFor(uint32_t silentMs) {
    if (silentMs == NearbyPlay::PEER_SILENT_UNKNOWN) return State::Gone;
    if (silentMs >= NearbyPlay::PEER_QUIET_MS) return State::Quiet;
    return State::Present;
}

bool NearbyWatch::tick(AppContext& host, const char* peerId, uint32_t now) {
    char ids[1][5];
    snprintf(ids[0], sizeof(ids[0]), "%s", peerId != nullptr ? peerId : "");
    return tickTable(host, ids, 1, 0xFF, 0, now);
}

bool NearbyWatch::tickTable(AppContext& host, const char (*ids)[5], uint8_t count,
                            uint8_t skip, uint8_t dropped, uint32_t now) {
    (void)now;
    /* The worst peer decides. Present beats nothing; Gone beats Quiet. A peer
     * no longer at the table is not waited for -- that is what dropping one
     * is for. */
    State worst = State::Present;
    uint32_t worstMs = 0;
    uint8_t worstWho = who_;
    for (uint8_t i = 0; i < count; ++i) {
        if (i == skip || (dropped & (1U << i)) != 0) continue;
        const uint32_t ms = host.nearbyPeerSilentMs(ids[i]);
        const State s = stateFor(ms);
        if (s > worst || (s == worst && s != State::Present && ms > worstMs)) {
            worst = s;
            worstMs = ms;
            worstWho = i;
        }
    }

    const State before = state_;
    const bool wasShown = cardShown();
    const uint16_t secondsBefore = seconds_;

    resumed_ = false;
    if (worst == State::Present) {
        if (before != State::Present) {
            resumed_ = true;
            dismissed_ = State::Present;   // the next silence is a new one
        }
        state_ = State::Present;
        seconds_ = 0;
        return resumed_;
    }

    state_ = worst;
    who_ = worstWho;
    seconds_ = worst == State::Gone ? 0 : static_cast<uint16_t>(worstMs / 1000);
    /* Report a change the screen would draw: the state, the card coming up,
     * or the seconds line while the card is up. A dismissed card counting in
     * the background is not a repaint. */
    return state_ != before || cardShown() != wasShown ||
           (cardShown() && seconds_ != secondsBefore);
}

// ---- the card ---------------------------------------------------------------

Rect NearbyWatch::cardRect(const Rect& area, bool hasExtra) {
    const int16_t h = static_cast<int16_t>(CARD_H + (hasExtra ? EXTRA_H : 0));
    const int16_t w = area.w - 8 < CARD_W ? static_cast<int16_t>(area.w - 8) : CARD_W;
    return Rect{static_cast<int16_t>(area.x + (area.w - w) / 2),
                static_cast<int16_t>(area.y + (area.h - h) / 2), w, h};
}

Rect NearbyWatch::secondsRect(const Rect& area, bool hasExtra) const {
    const Rect c = cardRect(area, hasExtra);
    return Rect{static_cast<int16_t>(c.x + PAD), static_cast<int16_t>(c.y + PAD + TITLE_H),
                static_cast<int16_t>(c.w - 2 * PAD), LINE_H};
}

Rect NearbyWatch::waitRect(const Rect& area, bool hasExtra) const {
    const Rect c = cardRect(area, hasExtra);
    const int16_t w = static_cast<int16_t>((c.w - 3 * PAD) / 2);
    return Rect{static_cast<int16_t>(c.x + PAD),
                static_cast<int16_t>(c.y + PAD + TITLE_H + LINE_H + PAD), w, BUTTON_H};
}

Rect NearbyWatch::endRect(const Rect& area, bool hasExtra) const {
    const Rect w = waitRect(area, hasExtra);
    return Rect{static_cast<int16_t>(w.x + w.w + PAD), w.y, w.w, w.h};
}

Rect NearbyWatch::extraRect(const Rect& area) const {
    const Rect c = cardRect(area, true);
    const Rect w = waitRect(area, true);
    return Rect{w.x, static_cast<int16_t>(w.y + BUTTON_H + 4),
                static_cast<int16_t>(c.w - 2 * PAD), BUTTON_H};
}

void NearbyWatch::draw(Ui::Renderer& tft, const Rect& area, const char* who,
                       const char* extra) const {
    const bool hasExtra = extra != nullptr;
    const Rect c = cardRect(area, hasExtra);
    tft.fillRoundRect(c.x + 2, c.y + Ui::BUTTON_SHADOW_DY, c.w, c.h, 8, Ui::shade(Ui::bg(), 60));
    tft.fillRoundRect(c.x, c.y, c.w, c.h, 8, Ui::surface());
    tft.drawRoundRect(c.x, c.y, c.w, c.h, 8,
                      state_ == State::Gone ? Ui::error() : Ui::warning());

    char line[40];
    snprintf(line, sizeof(line), "Waiting for %s", who != nullptr ? who : "");
    Ui::drawLabel(tft, Rect{static_cast<int16_t>(c.x + PAD), static_cast<int16_t>(c.y + PAD),
                            static_cast<int16_t>(c.w - 2 * PAD), TITLE_H},
                  Ui::fitted(tft, line, static_cast<int16_t>(c.w - 2 * PAD), 2), Ui::text(), 2,
                  Align::Center);
    drawSeconds(tft, area, hasExtra);

    Ui::drawButton(tft, waitRect(area, hasExtra), "Keep waiting", Ui::panel(), Ui::outline(),
                   Ui::text(), false, 2);
    Ui::drawButton(tft, endRect(area, hasExtra), "End game", Ui::panel(), Ui::outline(),
                   Ui::text(), false, 2);
    if (hasExtra) {
        Ui::drawButton(tft, extraRect(area), extra, Ui::panel(), Ui::outline(), Ui::text(),
                       false, 2);
    }
}

void NearbyWatch::drawSeconds(Ui::Renderer& tft, const Rect& area, bool hasExtra) const {
    /* The card's own colour behind it, since this line is the one thing on
     * the card that repaints on its own. */
    const Rect r = secondsRect(area, hasExtra);
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::surface());
    char line[40];
    if (state_ == State::Gone) {
        snprintf(line, sizeof(line), "Out of range, or switched off");
    } else {
        snprintf(line, sizeof(line), "Nothing heard for %us", static_cast<unsigned>(seconds_));
    }
    Ui::drawLabel(tft, r, line, state_ == State::Gone ? Ui::error() : Ui::muted(), 1,
                  Align::Center);
}

NearbyWatch::Press NearbyWatch::press(const Rect& area, const TouchPoint& touch,
                                      bool hasExtra) const {
    if (!touch.justPressed || !cardShown()) return Press::None;
    if (waitRect(area, hasExtra).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) return Press::Wait;
    if (endRect(area, hasExtra).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) return Press::End;
    if (hasExtra && extraRect(area).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        return Press::Extra;
    }
    /* Anywhere else on the card. The caller returns without touching the
     * board while cardShown(): a press through the card must not be a move. */
    return Press::None;
}
