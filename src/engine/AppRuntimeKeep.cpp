// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AppRuntime.h"

#include "engine/NearbyPlay.h"
#include "hal/Watchdog.h"

/* What the console keeps for a player between two visits to a game: the game
 * itself, until the console is switched off, and the level they have reached
 * in it, for good.
 *
 * Both live here rather than in the games because both are one rule for
 * forty games. A game says only whether it has something worth coming back
 * to (Game::keepsGame()) and what level its player is on
 * (AppContext::courseLevel()); who the player is, whether they are the one
 * who left the game, and where a byte goes in storage are not a game's
 * business, and a game that had to know would get it wrong forty ways. */

namespace {

/* ---- the level a player has reached in each game ------------------------
 *
 * One blob per player holds every game's level: COURSE_SLOTS entries of a
 * two-byte key and a level. One blob and not a stored number per game
 * because the 20 KB of storage is shared by five players and forty games --
 * a number each would be two hundred entries of the five hundred there are,
 * where this is ten per player.
 *
 * The key is a hash of the game's id and not its place in the launcher: a
 * game added in the middle of the list moves every index after it, and each
 * player's levels would slide onto the next game along. tools/check_catalog.py
 * refuses two ids with the same key. */
constexpr char COURSE_KEY[] = "course";

}   // namespace

uint16_t BrainoApp::courseKey(const char* id, uint8_t track) {
    /* The id takes the bits the track leaves. Fewer than eight would leave
     * the forty games too few keys to stay apart. */
    constexpr uint8_t ID_BITS = 16 - COURSE_TRACK_BITS;
    static_assert(ID_BITS >= 8, "the track must leave the game's id most of the key");
    constexpr uint32_t ID_MASK = (1UL << ID_BITS) - 1U;
    uint32_t h = 2166136261UL;                 // FNV-1a
    for (const char* p = id; *p != '\0'; ++p) {
        h ^= static_cast<uint8_t>(*p);
        h *= 16777619UL;
    }
    uint16_t key = static_cast<uint16_t>(((h >> 16) ^ (h & 0xFFFFU)) & ID_MASK);
    if (key == 0) {
        key = 1;                               // 0 is an empty entry
    }
    return static_cast<uint16_t>(key | (static_cast<uint16_t>(track) << ID_BITS));
}

/* The levels in memory are the active player's. Read once per change of
 * player, never per question: Preferences is flash-backed, and a getter that
 * reads it on a path a game calls each round is how a frame gets lost. */
void BrainoApp::syncCourse() {
    const uint8_t profile = board_.activeProfile();
    const uint8_t epoch = board_.playersEpoch();
    if (courseLoaded_ && profile == courseProfile_ && epoch == courseEpoch_) {
        return;
    }
    memset(course_, 0, sizeof(course_));
    board_.loadBlob(COURSE_KEY, course_, sizeof(course_));
    courseProfile_ = profile;
    courseEpoch_ = epoch;
    courseLoaded_ = true;
}

/* A game asks about itself; the console asks about any game by its id. Both
 * end in the same two functions below, so a level set from a cable is the
 * one the game reads, and not a second copy of it. */
uint8_t BrainoApp::courseLevel(uint8_t track) {
    if (activeApp_ == nullptr || !activeApp_->isCatalogApp()) {
        return 0;
    }
    return courseLevelOf(activeApp_->id(), track);
}

void BrainoApp::setCourseLevel(uint8_t level, uint8_t track) {
    if (activeApp_ == nullptr || !activeApp_->isCatalogApp()) {
        return;
    }
    storeCourseLevel(activeApp_->id(), track, level);
}

uint8_t BrainoApp::courseLevelOf(const char* id, uint8_t track) {
    if (track >= COURSE_TRACKS) {
        return 0;
    }
    syncCourse();
    const uint16_t key = courseKey(id, track);
    for (uint8_t i = 0; i < COURSE_SLOTS; ++i) {
        const uint8_t* entry = &course_[i * COURSE_ENTRY];
        if ((entry[0] | (entry[1] << 8)) == key) {
            return entry[2];
        }
    }
    return 0;
}

bool BrainoApp::storeCourseLevel(const char* id, uint8_t track, uint8_t level) {
    if (track >= COURSE_TRACKS) {
        return false;
    }
    syncCourse();
    const uint16_t key = courseKey(id, track);
    uint8_t* slot = nullptr;
    for (uint8_t i = 0; i < COURSE_SLOTS; ++i) {
        uint8_t* entry = &course_[i * COURSE_ENTRY];
        const uint16_t have = static_cast<uint16_t>(entry[0] | (entry[1] << 8));
        if (have == key) {
            slot = entry;
            break;
        }
        if (have == 0 && slot == nullptr) {
            slot = entry;       // first free one, if the key is not found
        }
    }
    if (slot == nullptr) {
        return false;           // every entry taken: the level lasts the visit
    }
    if ((slot[0] | (slot[1] << 8)) == key && slot[2] == level) {
        return true;            // nothing changed, so nothing is written
    }
    slot[0] = static_cast<uint8_t>(key & 0xFF);
    slot[1] = static_cast<uint8_t>(key >> 8);
    slot[2] = level;
    /* Dropped for Guest by Board::saveBlob(), like every write of Guest's;
     * the copy in memory still serves until another player takes over. */
    board_.saveBlob(COURSE_KEY, course_, sizeof(course_));
    return true;
}

/* ---- the game a player left ---------------------------------------------
 *
 * Asked as the screen is left, before end(): end() is where a game hands
 * things back, and the answer is about the game as it stood. */
void BrainoApp::noteGameLeft() {
    if (activeApp_ == nullptr || !activeApp_->isCatalogApp() || activeGame_ == &carryOn_) {
        return;
    }
    KeptGame& kept = kept_[activeApp_->launcherIndex()];
    kept.how = pressedInGame_ ? activeGame_->keepsGame() : Game::Keep::No;
    kept.profile = board_.activeProfile();
    kept.epoch = board_.playersEpoch();
    kept.leftAtMs = millis();
}

/* A kept game is its player's. Another player opening the same game gets a
 * new one -- and that begin() is what ends the kept one, since there is one
 * instance of each game. The epoch is for a player removed in between: the
 * players after them move down a slot, and a slot number alone would hand
 * the game to whoever now sits in it. */
Game::Keep BrainoApp::keptHow(const AppDefinition& app) {
    if (!app.isCatalogApp()) {
        return Game::Keep::No;
    }
    KeptGame& kept = kept_[app.launcherIndex()];
    if (kept.how != Game::Keep::No &&
        (kept.profile != board_.activeProfile() || kept.epoch != board_.playersEpoch())) {
        kept.how = Game::Keep::No;
    }
    return kept.how;
}

/* Straight back into a game that asked for no question. */
void BrainoApp::resumeKept(const AppDefinition& app) {
    KeptGame& kept = kept_[app.launcherIndex()];
    const uint32_t awayMs = millis() - kept.leftAtMs;
    kept.how = Game::Keep::No;
    enterApp(app, true, awayMs);
}

/* The one way onto a game's screen: begun, or taken up where it was left. */
void BrainoApp::enterApp(const AppDefinition& app, bool carryOn, uint32_t awayMs) {
    activeGame_ = &app.game(games_);
    view_ = View::Game;
    activeApp_ = &app;
    /* Only playable games are announced. Opening Settings or System Info is
     * nobody else's business, and a system screen has no score to share. */
    NearbyPlay::setActiveApp(board_, app.isCatalogApp() ? &app : nullptr);
    Watchdog::setContext(app.title());
    applyRotation(rotationForActiveScreen());
    heapAtLaunch_ = ESP.getFreeHeap();
    if (carryOn) {
        activeGame_->resume(*this, awayMs);
    } else {
        activeGame_->begin(*this);
    }
    /* A game taken up again has a press in it already, or it would not have
     * been kept: leaving it a second time keeps it a second time. */
    pressedInGame_ = carryOn;
    /* A SCREEN TRANSITION IS ALWAYS A WHOLE-SCREEN REPAINT.
     *
     * Every screen is a static instance reused for the life of the device, so
     * it arrives here carrying the dirty flags the LAST visit left behind --
     * and the last thing that visit did was clearDirty(). Without this, a
     * screen whose begin() happens to call only markDirty() renders without
     * its renderStatic(), which is where both Ui::clear() and the top bar
     * live: no chrome, and the previous screen still showing underneath it.
     *
     * It belongs here rather than in each begin(). Fourteen screens would each
     * have to remember, and the one that forgot would be found by eye on a
     * panel rather than by any build. Partial repaint is an optimisation
     * WITHIN a screen's lifetime; entering one is not the place to be greedy.
     * It matters twice over for a game taken up again: resume() draws
     * nothing, and every tracker of what is on the panel is a visit old. */
    activeGame_->requestRender();
    activeGame_->render(*this);
    activeGame_->clearDirty();
}

/* The question, in front of the game it is about. The game is the active
 * app already -- its title is in the top bar, its canvas and rotation are
 * the ones in force -- but it has not been touched: neither begin() nor
 * resume() runs until the player has answered. */
void BrainoApp::askCarryOn(const AppDefinition& app) {
    carryOn_.offer(app);
    activeGame_ = &carryOn_;
    view_ = View::Game;
    activeApp_ = &app;
    NearbyPlay::setActiveApp(board_, &app);
    Watchdog::setContext(app.title());
    applyRotation(rotationForActiveScreen());
    heapAtLaunch_ = ESP.getFreeHeap();
    activeGame_->begin(*this);
    pressedInGame_ = false;
    activeGame_->requestRender();
    activeGame_->render(*this);
    activeGame_->clearDirty();
}

/* Read after every update() of the question. Leaving it by Home, the gear or
 * the padlock answers nothing, and the game goes on waiting. */
void BrainoApp::settleCarryOn() {
    if (activeGame_ != &carryOn_ || activeApp_ == nullptr) {
        return;
    }
    const CarryOnScreen::Choice choice = carryOn_.take();
    if (choice == CarryOnScreen::Choice::None) {
        return;
    }
    const AppDefinition& app = *activeApp_;
    KeptGame& kept = kept_[app.launcherIndex()];
    const uint32_t awayMs = millis() - kept.leftAtMs;
    kept.how = Game::Keep::No;
    board_.playSound(Sound::Select);
    leaveActiveGame();      // the question is a screen like any other
    enterApp(app, choice == CarryOnScreen::Choice::CarryOn, awayMs);
}
