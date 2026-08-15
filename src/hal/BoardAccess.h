#pragma once

#include "Board.h"

class BoardDisplayAccess {
public:
    explicit BoardDisplayAccess(Board& board) : board_(board) {}

    TFT_eSPI& raw() { return board_.display(); }
    Board::ThemeMode themeMode() { return board_.themeMode(); }
    void setThemeMode(Board::ThemeMode mode) { board_.setThemeMode(mode); }
    Board::LayoutMode layoutMode() { return board_.layoutMode(); }
    void setLayoutMode(Board::LayoutMode mode) { board_.setLayoutMode(mode); }
    uint8_t rotation() const { return board_.displayRotation(); }
    void setRotation(uint8_t rotation) { board_.setDisplayRotation(rotation); }
    uint8_t brightness() { return board_.brightness(); }
    void setBrightness(uint8_t percent) { board_.setBrightness(percent); }
    void applyBrightness() { board_.applyBrightness(); }
    void sleep() { board_.displaySleep(); }
    void wake() { board_.displayWake(); }
    bool asleep() const { return board_.displayAsleep(); }
    Board::DisplaySleepTelemetry sleepTelemetry() const { return board_.displaySleepTelemetry(); }
    bool drawBmp(const char* path, int16_t x, int16_t y, int16_t maxW, int16_t maxH) {
        return board_.drawBmp(path, x, y, maxW, maxH);
    }

private:
    Board& board_;
};

class BoardTouchAccess {
public:
    explicit BoardTouchAccess(Board& board) : board_(board) {}

    bool hasCalibration() const { return board_.hasTouchCalibration(); }
    void calibrate() { board_.runTouchCalibration(); }
    TouchPoint poll() { return board_.pollTouch(); }
    TouchPoint last() const { return board_.touch(); }

private:
    Board& board_;
};

class BoardStorageAccess {
public:
    explicit BoardStorageAccess(Board& board) : board_(board) {}

    uint8_t activeProfile() { return board_.activeProfile(); }
    void setActiveProfile(uint8_t index) { board_.setActiveProfile(index); }
    uint8_t kidCount() { return board_.kidCount(); }
    void removeKid(uint8_t index) { board_.removeKid(index); }
    bool isGuest() { return board_.isGuest(); }
    Board::StorageTelemetry storageTelemetry() { return board_.storageTelemetry(); }
    void factoryReset() { board_.factoryReset(); }
    uint32_t getScore(const char* key, uint32_t fallback = 0) {
        return board_.getScore(key, fallback);
    }
    void setScore(const char* key, uint32_t value) { board_.setScore(key, value); }
    bool saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) {
        return board_.saveBestScore(key, value, lowerIsBetter);
    }
    uint32_t worstScore(const char* key, uint32_t fallback = 0) {
        return board_.worstScore(key, fallback);
    }
    bool hasScore(const char* key) { return board_.hasScore(key); }
    uint32_t scoreFor(uint8_t profileIndex, const char* key, uint32_t fallback = 0) {
        return board_.scoreFor(profileIndex, key, fallback);
    }
    bool hasScoreFor(uint8_t profileIndex, const char* key) {
        return board_.hasScoreFor(profileIndex, key);
    }
    void loadBlob(const char* key, void* dst, size_t len) { board_.loadBlob(key, dst, len); }
    void saveBlob(const char* key, const void* src, size_t len) {
        board_.saveBlob(key, src, len);
    }
    bool gameVisible(uint8_t catalogIndex, bool fallback = true) {
        return board_.gameVisible(catalogIndex, fallback);
    }
    void setGameVisible(uint8_t catalogIndex, bool visible) {
        board_.setGameVisible(catalogIndex, visible);
    }
    bool gameVisibleFor(uint8_t catalogIndex, uint8_t profileIndex, bool fallback = true) {
        return board_.gameVisibleFor(catalogIndex, profileIndex, fallback);
    }
    void setGameVisibleFor(uint8_t catalogIndex, uint8_t profileIndex, bool visible) {
        board_.setGameVisibleFor(catalogIndex, profileIndex, visible);
    }

private:
    Board& board_;
};

class BoardPowerAccess {
public:
    explicit BoardPowerAccess(Board& board) : board_(board) {}

    Board::PowerState source() { return board_.getPowerSource(); }
    Board::BatteryTelemetry batteryTelemetry() { return board_.readBatteryTelemetry(); }
    float batteryVoltage() { return board_.getBatteryVoltage(); }
    int8_t batteryPercent() { return board_.getBatteryPercent(); }
    bool batteryPresent() { return board_.isBatteryPresent(); }
    Board::ChargingState chargingState() { return board_.getChargingState(); }
    uint16_t screenSaverSeconds() { return board_.screenSaverSeconds(); }
    void setScreenSaverSeconds(uint16_t seconds) { board_.setScreenSaverSeconds(seconds); }
    Board::IdleAction idleAction() { return board_.idleAction(); }
    void setIdleAction(Board::IdleAction action) { board_.setIdleAction(action); }
    uint16_t sleepSeconds() { return board_.sleepSeconds(); }
    void setSleepSeconds(uint16_t seconds) { board_.setSleepSeconds(seconds); }
    void displaySleep() { board_.displaySleep(); }
    void displayWake() { board_.displayWake(); }
    bool displayAsleep() const { return board_.displayAsleep(); }
    Board::DisplaySleepTelemetry displaySleepTelemetry() const {
        return board_.displaySleepTelemetry();
    }

private:
    Board& board_;
};

class BoardNetworkAccess {
public:
    explicit BoardNetworkAccess(Board& board) : board_(board) {}

    void clearWifiCredentials() { board_.clearWifiCredentials(); }
    bool hasWifiCredentials() { return board_.hasWifiCredentials(); }
    bool isWifiConnected() { return board_.isWifiConnected(); }
    bool ntpEnabled() { return board_.ntpEnabled(); }
    void setNtpEnabled(bool enabled) { board_.setNtpEnabled(enabled); }
    void beginTimeSync() { board_.beginTimeSync(); }
    void syncTimeNow() { board_.syncTimeNow(); }
    void applyTimeConfig() { board_.applyTimeConfig(); }
    bool ntpUdpProbe(const char* host) { return board_.ntpUdpProbe(host); }
    void tickTimeSync() { board_.tickTimeSync(); }
    bool timeSynced() const { return board_.timeSynced(); }
    uint32_t lastTimeSyncMs() const { return board_.lastTimeSyncMs(); }
    time_t lastTimeSyncEpoch() const { return board_.lastTimeSyncEpoch(); }
    int16_t tzOffsetMinutes() { return board_.tzOffsetMinutes(); }
    void setTzOffsetMinutes(int16_t minutes) { board_.setTzOffsetMinutes(minutes); }
    bool tzZoneChosen() { return board_.tzZoneChosen(); }
    uint8_t tzZoneIndex() { return board_.tzZoneIndex(); }
    void setTzZoneIndex(uint8_t index) { board_.setTzZoneIndex(index); }
    bool detectTimezone() { return board_.detectTimezone(); }
    bool tzAutoDetected() const { return board_.tzAutoDetected(); }
    bool bleBeaconEnabled() { return board_.bleBeaconEnabled(); }
    void setBleBeaconEnabled(bool on) { board_.setBleBeaconEnabled(on); }
    uint8_t networkActivityCount() const { return board_.networkActivityCount(); }
    Board::NetworkActivity networkActivity(uint8_t newestFirstIndex) const {
        return board_.networkActivity(newestFirstIndex);
    }

private:
    Board& board_;
};

class BoardFeedbackAccess {
public:
    explicit BoardFeedbackAccess(Board& board) : board_(board) {}

    void beepOk() { board_.beepOk(); }
    void beepError() { board_.beepError(); }
    void setRgb(bool red, bool green, bool blue) { board_.setRgb(red, green, blue); }
    void setRgbColor(uint8_t r, uint8_t g, uint8_t b) { board_.setRgbColor(r, g, b); }
    void pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms) {
        board_.pulseRgb(r, g, b, ms);
    }
    void tickRgb() { board_.tickRgb(); }
    void setRgbEnabled(bool on) { board_.setRgbEnabled(on); }
    bool rgbEnabled() { return board_.rgbEnabled(); }

private:
    Board& board_;
};

inline BoardDisplayAccess Board::displayAccess() { return BoardDisplayAccess(*this); }
inline BoardTouchAccess Board::touchAccess() { return BoardTouchAccess(*this); }
inline BoardStorageAccess Board::storageAccess() { return BoardStorageAccess(*this); }
inline BoardPowerAccess Board::powerAccess() { return BoardPowerAccess(*this); }
inline BoardNetworkAccess Board::networkAccess() { return BoardNetworkAccess(*this); }
inline BoardFeedbackAccess Board::feedbackAccess() { return BoardFeedbackAccess(*this); }
