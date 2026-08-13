#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include "BoardConfig.h"

struct TouchPoint {
    bool down = false;
    bool justPressed = false;
    bool justReleased = false;
    int16_t x = 0;
    int16_t y = 0;
    uint16_t pressure = 0;
};

class Board {
public:
    struct BatteryTelemetry {
        uint16_t rawAdc = 0;
        float adcVoltage = 0.0f;
        float batteryVoltage = 0.0f;
    };

    struct NetworkActivity {
        uint32_t atMs = 0;
        char detail[40] = {0};
    };

    struct TouchCalibration {
        uint32_t magic = 0;
        float ax = 0.0f;
        float bx = 0.0f;
        float cx = 0.0f;
        float ay = 0.0f;
        float by = 0.0f;
        float cy = 0.0f;
    };

    enum class ThemeMode : uint8_t {
        Dark = 0,
        Light = 1,
    };

    // TODO(HARDWARE-VALIDATION): Verify E32R28T-1 battery ADC calibration, divider ratio, battery-present thresholds, no-battery behavior, USB-power behavior, and charging-state detection using physical hardware.
    enum class PowerState {
        BATTERY,
        EXTERNAL_POWER,
        UNKNOWN
    };

    enum class ChargingState {
        UNKNOWN
    };

    enum class LayoutMode : uint8_t {
        Horizontal = 0,
        Vertical = 1,
    };

    Board();

    void begin();
    TFT_eSPI& display();

    PowerState getPowerSource();
    BatteryTelemetry readBatteryTelemetry();
    float getBatteryVoltage();
    int8_t getBatteryPercent();
    bool isBatteryPresent();
    ChargingState getChargingState();

    bool sdReady() const;
    bool mountSd();

    bool hasTouchCalibration() const;
    void runTouchCalibration();
    TouchPoint pollTouch();
    TouchPoint touch() const;

    void beepOk();
    void beepError();
    /* ---- Profiles -------------------------------------------------------
     * Scores, best/worst records and spaced-repetition data are stored per
     * child. Every key that goes through getScore/setScore/saveBestScore and
     * loadBlob/saveBlob is transparently prefixed with the active profile, so
     * all 22 games became per-profile without touching a single game file.
     * Device settings (theme, layout, Wi-Fi, brightness) stay global. */
    /* Up to five children plus a permanent Guest slot.
     *
     * Guest deliberately does NOT persist anything: every write through
     * setScore/saveBestScore/saveBlob is dropped while it is active. That is
     * what makes it a guest rather than a sixth child -- a visitor can play
     * without leaving results behind or disturbing anyone's records. */
    static constexpr uint8_t MAX_KIDS      = 5;
    static constexpr uint8_t GUEST_INDEX   = MAX_KIDS;   // 5
    static constexpr uint8_t PROFILE_SLOTS = MAX_KIDS + 1;
    static constexpr uint8_t PROFILE_NAME_MAX = 9;   // NAME_MAX is a POSIX macro

    uint8_t activeProfile();
    void setActiveProfile(uint8_t index);
    String profileName(uint8_t index);
    void setProfileName(uint8_t index, const String& name);

    /** How many child profiles exist (0..MAX_KIDS). Guest is always extra. */
    uint8_t kidCount();
    /** Append a child. Returns its index, or 0xFF when full. */
    uint8_t addKid(const String& name);
    /** Delete a child, shifting the ones after it down. */
    void removeKid(uint8_t index);
    bool isGuest() { return activeProfile() == GUEST_INDEX; }

    /** Wipe every stored setting, score and credential, then reboot. */
    void factoryReset();

    uint32_t getScore(const char* key, uint32_t fallback = 0);
    void setScore(const char* key, uint32_t value);
    bool saveBestScore(const char* key, uint32_t value, bool lowerIsBetter);
    /** The opposite extreme, recorded automatically alongside every best. */
    uint32_t worstScore(const char* key, uint32_t fallback = 0);
    bool hasScore(const char* key);

    /* Small binary blobs, used by engine/Progress for per-item mastery. */
    void loadBlob(const char* key, void* dst, size_t len);
    void saveBlob(const char* key, const void* src, size_t len);

    ThemeMode themeMode();
    void setThemeMode(ThemeMode mode);
    /* Backlight brightness as a percentage. The floor is deliberately well
     * above zero: at very low duty the panel is unreadable, and a child who
     * dragged it to the bottom would have no way to see the control to undo
     * it. The slider's full travel maps to BRIGHTNESS_MIN..100. */
    static constexpr uint8_t BRIGHTNESS_MIN = 25;
    uint8_t brightness();
    void setBrightness(uint8_t percent);
    void applyBrightness();

    LayoutMode layoutMode();
    void setLayoutMode(LayoutMode mode);
    uint16_t screenSaverSeconds();
    void setScreenSaverSeconds(uint16_t seconds);
    bool gameVisible(uint8_t catalogIndex, bool fallback = true);
    void setGameVisible(uint8_t catalogIndex, bool visible);
    bool gameVisibleFor(uint8_t catalogIndex, uint8_t profileIndex, bool fallback = true);
    void setGameVisibleFor(uint8_t catalogIndex, uint8_t profileIndex, bool visible);
    String wifiSsid();
    String wifiPassword();
    void setWifiCredentials(const String& ssid, const String& password);
    void clearWifiCredentials();
    bool hasWifiCredentials();
    bool ntpEnabled();
    void setNtpEnabled(bool enabled);
    String ntpServer();
    void setNtpServer(const String& server);
    bool isWifiConnected();

    /* Time sync. Wi-Fi is used for nothing but NTP: we connect, set the clock,
     * and then re-issue the sync every TIME_RESYNC_MS so drift is corrected.
     * Both calls are non-blocking; tickTimeSync() drives a small state machine
     * from the main loop. */
    void beginTimeSync();
    /** Force an immediate re-sync; ignores the 5 minute cadence. */
    void syncTimeNow();
    void applyTimeConfig();   // programs SNTP with the stored tz offset
    /* Queries an NTP server over raw UDP. Doubles as a diagnostic (it logs DNS
     * and reachability) and as a fallback: if lwIP's SNTP never answers we set
     * the clock from this reply directly. Returns true if the clock was set. */
    bool ntpUdpProbe(const char* host);
    void tickTimeSync();
    bool timeSynced() const;
    uint32_t lastTimeSyncMs() const;
    time_t lastTimeSyncEpoch() const;
    /* Time zone.
     *
     * Routers do not advertise a zone (DHCP option 100/101 exists but is
     * essentially never implemented), so we either look it up from the public
     * IP or let the user pick a named zone. Stored as minutes to support the
     * :30 and :45 zones. */
    int16_t tzOffsetMinutes();
    void setTzOffsetMinutes(int16_t minutes);
    /** False while the zone is still being auto-detected from the public IP. */
    bool tzZoneChosen();
    uint8_t tzZoneIndex();
    void setTzZoneIndex(uint8_t index);
    static uint8_t tzZoneCount();
    static const char* tzZoneName(uint8_t index);
    static const char* tzZonePosix(uint8_t index);
    /** Look the zone up from the public IP. Requires an active connection. */
    bool detectTimezone();
    bool tzAutoDetected() const { return tzAutoDetected_; }
    void setDisplayRotation(uint8_t rotation);
    uint8_t displayRotation() const;

    void setRgb(bool red, bool green, bool blue);

    /* RGB backlight LED (GPIO 4/16/17, common anode so the drive is inverted).
     * Driven by LEDC PWM for full-brightness colour mixing -- bright enough to
     * light a translucent case. setRgbColor() holds a colour; pulseRgb() shows
     * one for a moment and then fades out via tickRgb(), which the main loop
     * calls every frame. */
    void setRgbColor(uint8_t r, uint8_t g, uint8_t b);
    void pulseRgb(uint8_t r, uint8_t g, uint8_t b, uint16_t ms);
    void tickRgb();
    void setRgbEnabled(bool on);
    bool rgbEnabled();
    bool drawBmp(const char* path, int16_t x, int16_t y, int16_t maxW, int16_t maxH);
    uint8_t networkActivityCount() const;
    NetworkActivity networkActivity(uint8_t newestFirstIndex) const;

private:
    struct RawTouch {
        bool down = false;
        int16_t x = 0;
        int16_t y = 0;
        uint16_t pressure = 0;
    };

    uint16_t readTouchAdc(uint8_t command);
    RawTouch readRawTouch();
    bool waitForStableRaw(int16_t& rawX, int16_t& rawY);
    bool loadTouchCalibration();
    void saveTouchCalibration();
    bool mapTouch(const RawTouch& raw, int16_t& x, int16_t& y) const;
    void beep(uint16_t frequency, uint16_t ms);

    TFT_eSPI tft_;
    SPIClass sdSpi_;
    Preferences prefs_;
    TouchCalibration cal_;
    TouchPoint lastTouch_;
    bool sdMounted_ = false;
    uint8_t displayRotation_ = 1;

    enum class TimeSyncState : uint8_t { Idle, Connecting, Syncing, Synced };
    TimeSyncState timeSyncState_ = TimeSyncState::Idle;
    uint32_t timeSyncStartedMs_ = 0;
    uint32_t lastSyncAttemptMs_ = 0;
    uint32_t lastResyncMs_ = 0;
    uint32_t lastWifiBeginMs_ = 0;   // throttles our own WiFi.begin()
    bool tzAutoDetected_ = false;

    /* Credentials are cached in RAM. hasWifiCredentials() is polled every
     * loop by the time-sync state machine; hitting NVS at ~27Hz wasted
     * cycles and flooded the log with nvs_get_str NOT_FOUND errors. */
    String wifiSsidCache_;
    String wifiPassCache_;
    bool wifiCacheLoaded_ = false;
    void loadWifiCache();
    String scopedKey(const char* key);   // "p0_" + key, per active profile
    void logNetworkActivity(const char* fmt, ...);
    void noteTimeSyncSuccess();

    bool rgbReady_ = false;
    uint32_t rgbHoldUntilMs_ = 0;
    uint8_t rgbR_ = 0, rgbG_ = 0, rgbB_ = 0;
    uint32_t lastTimeSyncMs_ = 0;
    time_t lastTimeSyncEpoch_ = 0;
    static constexpr uint8_t NETWORK_ACTIVITY_CAP = 8;
    NetworkActivity networkActivity_[NETWORK_ACTIVITY_CAP]{};
    uint8_t networkActivityNext_ = 0;
    uint8_t networkActivityUsed_ = 0;
};
