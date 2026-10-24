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

/* The calls every screen makes on the runtime -- storage, sound, feedback,
 * capabilities -- most of them a straight hand-off to Board. Kept apart from
 * AppRuntime.cpp, which is the loop and the screen transitions; nothing here
 * decides anything about which screen is up. */

Board& BrainoApp::board() {
    return board_;
}

bool BrainoApp::hasCapability(uint32_t capability) const {
    return activeApp_ != nullptr && activeApp_->hasCapability(capability);
}

bool BrainoApp::requireCapability(uint32_t capability, const char* action) {
    if (hasCapability(capability)) {
        return true;
    }
    Serial.printf("[auth] denied '%s' for app '%s' (need 0x%08lx, has 0x%08lx)\n",
                  action != nullptr ? action : "?",
                  activeApp_ != nullptr ? activeApp_->id() : "none",
                  static_cast<unsigned long>(capability),
                  static_cast<unsigned long>(activeApp_ != nullptr ? activeApp_->capabilities : 0));
    board_.beepError();
    return false;
}

ContentLoader& BrainoApp::content() {
    return content_;
}

uint32_t BrainoApp::getScore(const char* key, uint32_t fallback) {
    return board_.getScore(key, fallback);
}

void BrainoApp::setScore(const char* key, uint32_t value) {
    board_.setScore(key, value);
}

bool BrainoApp::saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) {
    const bool improved = board_.saveBestScore(key, value, lowerIsBetter);
    if (improved) {
        /* Peers should see the number that is true now, not the one that was
         * current when the screen opened. */
        NearbyPlay::refreshScore(board_);
    }
    return improved;
}

void BrainoApp::loadBlob(const char* key, void* dst, size_t len) {
    board_.loadBlob(key, dst, len);
}

void BrainoApp::saveBlob(const char* key, const void* src, size_t len) {
    board_.saveBlob(key, src, len);
}

void BrainoApp::beepOk() {
    board_.beepOk();
}

void BrainoApp::beepError() {
    board_.beepError();
}

void BrainoApp::playSound(Sound cue) {
    board_.playSound(cue);
}

bool BrainoApp::batteryLow() {
    /* isBatteryLow() reads the snapshot the sampling task publishes -- no
     * conversion on this path. A board with no battery sense is never low. */
    return BOARD.hasBatterySense() && board_.isBatteryLow();
}

void BrainoApp::pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
    board_.pulseRgb(r, g, b, ms);
}

void BrainoApp::drawTopBar(const char* title) {
    Ui::drawTopBar(board_, title);
}
