// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "GoDrawArt.h"

#include <string.h>

/* Go's side panel: whose turn, the captures, the info box (the magnifier on
 * 19x19) and the buttons. Each part repaints itself whole when what it shows
 * changed, behind its own stale flag in drawPanel(). */

using namespace GoArt;

void GoGame::drawTurn(Ui::Renderer& tft) const {
    const Rect r = turnRect();
    tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    /* The border is a fill under the face, not an outline over it: a fill
     * and an outline of one rectangle is what the d-pad's selector takes for
     * a control, and this box is nothing to press. */
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    tft.fillRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 5, Ui::surface());
    const uint8_t side = phase_ == Phase::Over ? state_.winner : state_.toMove;
    const int16_t sr = big() ? 6 : 8;
    drawStone(tft, r.x + 4 + sr, r.y + r.h / 2, sr, side == Go::EMPTY ? Go::BLACK : side, false);
    char line[16];
    if (phase_ == Phase::Over) {
        snprintf(line, sizeof(line), "%.8s wins", sideName(side));
    } else if (phase_ == Phase::Marking) {
        snprintf(line, sizeof(line), "Marking");
    } else if (mode_ == Mode::Waiting) {
        snprintf(line, sizeof(line), "Asking...");
    } else if (humanTurn() && mode_ != Mode::Local) {
        snprintf(line, sizeof(line), "Your turn");
    } else {
        snprintf(line, sizeof(line), "%.8s", sideName(side));
    }
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(Ui::text(), Ui::surface());
    tft.drawString(line, r.x + 8 + 2 * sr, r.y + r.h / 2, big() ? 1 : 2);
    if (big()) {
        /* The 19x19 panel has no captures block: the counts ride here. */
        char caps[12];
        snprintf(caps, sizeof(caps), "%u:%u", static_cast<unsigned>(state_.captured[Go::BLACK]),
                 static_cast<unsigned>(state_.captured[Go::WHITE]));
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(caps, r.x + r.w - 6, r.y + r.h / 2, 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void GoGame::drawCaptures(Ui::Renderer& tft) const {
    if (big()) return;
    const Rect r = capturesRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(Ui::muted(), Ui::bg());
    const uint8_t target = Go::captureTarget(state_.rules);
    char label[16];
    if (target != 0) {
        snprintf(label, sizeof(label), "Captured of %u", static_cast<unsigned>(target));
    } else {
        snprintf(label, sizeof(label), "Captured");
    }
    tft.drawString(label, r.x, r.y, 1);
    const int16_t cy = static_cast<int16_t>(r.y + 22);
    drawStone(tft, r.x + 10, cy, 8, Go::BLACK, false);
    drawStone(tft, r.x + 58, cy, 8, Go::WHITE, false);
    char n[8];
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    snprintf(n, sizeof(n), "%u", static_cast<unsigned>(state_.captured[Go::BLACK]));
    tft.drawString(n, r.x + 24, cy, 4);
    snprintf(n, sizeof(n), "%u", static_cast<unsigned>(state_.captured[Go::WHITE]));
    tft.drawString(n, r.x + 72, cy, 4);
    tft.setTextDatum(TL_DATUM);
}

void GoGame::drawInfo(Ui::Renderer& tft) const {
    if (big()) {
        drawMagnifier(tft);
        return;
    }
    const Rect r = infoRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    /* Border as a fill under the face, as in drawTurn(): not a control. */
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 6, Ui::outline());
    tft.fillRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 5, Ui::surface());
    tft.setTextDatum(TL_DATUM);
    if (ghost_ != Go::NO_POINT && phase_ == Phase::Play) {
        char at[8];
        coordinate(ghost_, at, sizeof(at));
        char line[24];
        const bool ok = Go::legal(state_, ghost_);
        const uint16_t taken = ok ? Go::captures(state_, ghost_) : 0;
        if (!ok) {
            snprintf(line, sizeof(line), "%s: not allowed", at);
        } else if (taken > 0) {
            snprintf(line, sizeof(line), "%s takes %u", at, static_cast<unsigned>(taken));
        } else {
            snprintf(line, sizeof(line), "%s", at);
        }
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(ok ? "Tap again to play" : "Try elsewhere", r.x + 6, r.y + 5, 1);
        tft.setTextColor(ok ? (taken > 0 ? Ui::success() : Ui::text()) : Ui::warning(),
                         Ui::surface());
        tft.drawString(line, r.x + 6, r.y + 16, 2);
        return;
    }
    /* The message, wrapped against the live panel at font 1 into at most
     * INFO_LINES lines and centred in the box. drawWrappedText() spaced its
     * lines 18px apart, so only the first fitted in this box and the rest of
     * the words went unseen. A message that would need a third line keeps
     * what fits of it on the second, cut as Ui::fitted() cuts and ending in
     * its mark, rather than losing words with no sign. Fixed buffers, no
     * String: this box repaints on every move. */
    const int16_t maxW = static_cast<int16_t>(r.w - 2 * INFO_PAD_X);
    char lines[INFO_LINES + 1][Ui::WRAP_LINE_MAX];
    const uint8_t need = Ui::wrapLines(tft, message_, maxW, 1, lines, INFO_LINES + 1);
    if (need == 0) return;
    if (need > INFO_LINES) {
        char* last = lines[INFO_LINES - 1];
        char rest[2 * Ui::WRAP_LINE_MAX];
        snprintf(rest, sizeof(rest), "%s %s", last, lines[INFO_LINES]);
        size_t len = strlen(rest);
        if (len > Ui::WRAP_LINE_MAX - 1) rest[len = Ui::WRAP_LINE_MAX - 1] = '\0';
        while (len > 2 && tft.textWidth(rest, 1) > maxW) rest[--len] = '\0';
        rest[len - 1] = '.';
        memcpy(last, rest, len + 1);
    }
    const uint8_t shown = need > INFO_LINES ? INFO_LINES : need;
    const int16_t block = static_cast<int16_t>((shown - 1) * INFO_LINE_H + INFO_FONT_H);
    int16_t y = static_cast<int16_t>(r.y + (r.h - block) / 2);
    tft.setTextColor(Ui::text(), Ui::surface());
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], r.x + INFO_PAD_X, y, 1);
        y = static_cast<int16_t>(y + INFO_LINE_H);
    }
}

void GoGame::drawMagnifier(Ui::Renderer& tft) const {
    /* The five-by-five around the ghost, at 18px a cell, so a point that is
     * ten pixels from its neighbours on the board is something a finger can
     * check before Place. With no ghost, the centre of the board. */
    const Rect r = infoRect();
    tft.fillRect(panelX(), r.y, panelW(), r.h, Ui::bg());
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());   // see drawTurn()
    tft.fillRoundRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 3, wood());
    const uint16_t centre = ghost_ != Go::NO_POINT ? ghost_ : Go::point(state_.n, 9, 9);
    const int16_t cr = Go::rowOf(state_.n, centre);
    const int16_t cc = Go::colOf(state_.n, centre);
    const int16_t half = MAG_CELLS / 2;
    for (int16_t dr = -half; dr <= half; ++dr) {
        for (int16_t dc = -half; dc <= half; ++dc) {
            const int16_t rr = static_cast<int16_t>(cr + dr);
            const int16_t col = static_cast<int16_t>(cc + dc);
            const int16_t x = static_cast<int16_t>(r.x + (dc + half) * MAG_CELL + MAG_CELL / 2);
            const int16_t y = static_cast<int16_t>(r.y + (dr + half) * MAG_CELL + MAG_CELL / 2);
            if (rr < 0 || col < 0 || rr >= state_.n || col >= state_.n) continue;
            /* Grid through this cell, stopping at the board's edge. */
            const int16_t x0 = col > 0 ? static_cast<int16_t>(x - MAG_CELL / 2) : x;
            const int16_t x1 = col + 1 < state_.n ? static_cast<int16_t>(x + MAG_CELL / 2) : x;
            const int16_t y0 = rr > 0 ? static_cast<int16_t>(y - MAG_CELL / 2) : y;
            const int16_t y1 = rr + 1 < state_.n ? static_cast<int16_t>(y + MAG_CELL / 2) : y;
            tft.drawFastHLine(x0, y, static_cast<int16_t>(x1 - x0 + 1), grid());
            tft.drawFastVLine(x, y0, static_cast<int16_t>(y1 - y0 + 1), grid());
            const uint16_t p = Go::point(state_.n, static_cast<uint8_t>(rr), static_cast<uint8_t>(col));
            if (isStar(p)) tft.fillCircle(x, y, 1, grid());
            if (state_.at[p] != Go::EMPTY) {
                drawStone(tft, x, y, MAG_CELL / 2 - 2, state_.at[p], false);
            } else if (p == ghost_) {
                tft.drawCircle(x, y, MAG_CELL / 2 - 2, hilite());
                tft.drawCircle(x, y, MAG_CELL / 2 - 3, hilite());
            }
        }
    }
    /* The coordinate, in the corner of the magnifier. */
    char at[8];
    coordinate(ghost_, at, sizeof(at));
    tft.setTextDatum(BL_DATUM);
    tft.setTextColor(Ui::text(), wood());
    tft.drawString(at, r.x + 3, r.y + r.h - 2, 2);
    tft.setTextDatum(TL_DATUM);
}

void GoGame::drawButtons(Ui::Renderer& tft, bool sure) const {
    const Rect rects[4] = {passRect(), undoRect(), actionRect(),
                           big() ? placeRect() : Rect{0, 0, 0, 0}};
    for (const Rect& r : rects) {
        if (r.w == 0) continue;
        tft.fillRect(r.x, r.y, r.w + Ui::BUTTON_SHADOW_DX, r.h + Ui::BUTTON_SHADOW_DY, Ui::bg());
    }
    const bool playing = phase_ == Phase::Play && mode_ != Mode::Waiting;
    const bool ours = playing && humanTurn();
    const uint8_t font = big() ? 1 : 2;
    if (phase_ == Phase::Marking) {
        if (mode_ == Mode::Remote) {
            /* No resuming across consoles: the slot is the way out. */
            Ui::drawButton(tft, passRect(), sure ? "Sure?" : "End game",
                           sure ? Ui::warning() : Ui::panel(), Ui::outline(),
                           sure ? Ui::bg() : Ui::text(), false, font);
        } else {
            Ui::drawPagerButton(tft, passRect(), "Resume", true);
        }
        Ui::drawPagerButton(tft, undoRect(), "Undo", false);
        if (big()) Ui::drawPagerButton(tft, placeRect(), "Place", false);
        /* "Agreed" means we have agreed and are waiting for somebody else,
         * which can only happen across consoles. Locally the next press is
         * the OTHER player's, so the button has to keep saying "Agree" --
         * it read "Agreed" while still wanting a press, which is the small
         * wrong behaviour the two encodings were hiding. */
        const bool waitingOthers =
            mode_ == Mode::Remote && (agreed_ & agreedBit(humanColour_)) != 0;
        Ui::drawButton(tft, actionRect(), waitingOthers ? "Agreed" : "Agree",
                       waitingOthers ? Ui::panel() : Ui::success(), Ui::outline(),
                       waitingOthers ? Ui::muted() : static_cast<uint16_t>(TFT_BLACK), false, font);
        return;
    }
    Ui::drawPagerButton(tft, passRect(), "Pass", ours && Go::legal(state_, Go::PASS));
    Ui::drawPagerButton(tft, undoRect(), "Undo",
                        ours && haveUndo_ && mode_ != Mode::Remote && mode_ != Mode::Waiting);
    if (big()) {
        const bool can = ours && ghost_ != Go::NO_POINT && Go::legal(state_, ghost_);
        if (can) {
            Ui::drawButton(tft, placeRect(), "Place", Ui::success(), Ui::outline(), TFT_BLACK, false,
                           font);
        } else {
            Ui::drawPagerButton(tft, placeRect(), "Place", false);
        }
    }
    const bool over = phase_ == Phase::Over;
    /* An invitation not yet answered is taken back, not ended: one press. */
    const char* action = over                      ? "New game"
                         : mode_ == Mode::Waiting ? "Cancel"
                         : sure                    ? "Sure?"
                                                   : "End game";
    Ui::drawButton(tft, actionRect(), action, sure ? Ui::warning() : Ui::panel(), Ui::outline(),
                   sure ? Ui::bg() : Ui::text(), false, font);
}

void GoGame::drawPanel(Ui::Renderer& tft) {
    if (turnStale_) {
        drawTurn(tft);
        turnStale_ = false;
    }
    if (capturesStale_) {
        drawCaptures(tft);
        capturesStale_ = false;
    }
    if (infoStale_) {
        drawInfo(tft);
        infoStale_ = false;
    }
    const bool sure = (phase_ == Phase::Play || phase_ == Phase::Marking) &&
                      millis() < confirmUntilMs_;
    if (buttonsStale_ || sure != confirmShown_) {
        drawButtons(tft, sure);
        confirmShown_ = sure;
        buttonsStale_ = false;
    }
}
