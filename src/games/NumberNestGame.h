// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include "engine/Game.h"
#include "games/NumberNestRules.h"
#include "ui/Ui.h"

struct AppMetadata;

const AppMetadata& numberNestAppMetadata();

/* NumberNest: five numbers, one of which is the sum -- or the product -- of
 * two of the others. Tap it. Five answers fill the nest, and the nest is then
 * the last group.
 *
 * This class is the thin half. What a nest is, how one is built, when a tap
 * counts, how a level is earned and which parts of the screen a change
 * touches are all in NumberNestRules.h, where a host test can reach them.
 * What is left here is the two things that need a device: turning a touch
 * into one of those events, and putting a Nest::View on the panel.
 *
 * DRAWING. The screen keeps the View it last painted and asks
 * Nest::changedRegions() what differs from the one it should show now. Each
 * region has one painter that covers its own box completely, so repainting it
 * is an overdraw with nothing to erase first. An idle frame is never rendered
 * at all -- update() marks the screen dirty only when Nest::tick() reports a
 * visible change or a tap did something -- and a wrong tap repaints one
 * circle and one line of text.
 *
 * The rules page is the one whole-screen change, and the only place this
 * screen asks for a full repaint after it has been entered. */
class NumberNestGame : public AppGame {
public:
    const char* title() const override;
    void begin(AppContext& host) override;
    void update(AppContext& host, const TouchPoint& touch) override;
    void renderStatic(AppContext& host) override;
    void renderDynamic(AppContext& host) override;

private:
    Nest::Session session_{};

    /* What is on the panel. Meaningless until paintedValid_, which a full
     * repaint clears: after a wipe every region has to be drawn whatever the
     * last View said. */
    Nest::View painted_{};
    bool paintedValid_ = false;

    bool showRules_ = false;
    /* This visit opened on the rules page and it has not been closed yet:
     * closing it is what records that this player has read it. Opening the
     * page from the Rules button records nothing. */
    bool rulesUnread_ = false;
    /* Whoever is playing keeps nothing -- a guest. Worked out in begin(),
     * the only way a screen can: by writing and reading back. */
    bool keepsNothing_ = false;
    /* Survives leaving the screen, deliberately: begin() does not clear it.
     * A guest's flag cannot be stored, and without this a guest would be
     * shown the rules on every visit. It is consulted only for a guest, so
     * it never stands in for a real player's own flag. */
    bool guestSawRules_ = false;

    /* The longest run of first-tap answers this visit, across both
     * operations, and the best such run ever saved for this player. */
    uint16_t run_ = 0;
    uint16_t best_ = 0;

    uint32_t freshSeed() const;
    void saveLevel(AppContext& host, Nest::Op op);
    void switchTo(AppContext& host, Nest::Op op, uint32_t now);
    void answer(AppContext& host, uint8_t index, uint32_t now);
    void closeRules(AppContext& host, uint32_t now);

    void paintRules(Ui::Renderer& tft) const;
    void paintModes(Ui::Renderer& tft, const Nest::View& v) const;
    void paintLevel(Ui::Renderer& tft, const Nest::View& v) const;
    void paintStatus(Ui::Renderer& tft, const Nest::View& v) const;
    void paintCaption(Ui::Renderer& tft, const Nest::View& v) const;
    void paintNumber(Ui::Renderer& tft, const Nest::View& v, uint8_t index, bool rimToo) const;
    void paintSlot(Ui::Renderer& tft, const Nest::View& v, uint8_t index, bool rimToo) const;
    void paintFooter(Ui::Renderer& tft, const Nest::View& v) const;
};
