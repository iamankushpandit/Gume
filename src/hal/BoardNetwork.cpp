#include "Board.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <stdarg.h>
#include <sys/time.h>
#include <time.h>
#include "Watchdog.h"

namespace {
constexpr const char* DEFAULT_NTP_SERVER = "pool.ntp.org";
constexpr uint32_t TIME_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t TIME_SYNC_TIMEOUT_MS = 45000;
constexpr uint32_t TIME_RETRY_MS = 5UL * 60UL * 1000UL;
constexpr const char* NTP_ENABLED_KEY = "ntpOn";
constexpr const char* NTP_RESYNC_HOURS_KEY = "ntpSyncHrs";

bool clockLooksValid() {
    struct tm t;
    return getLocalTime(&t, 0) && t.tm_year > (2020 - 1900);
}

struct TzZone { const char* name; const char* posix; };
const TzZone TZ_ZONES[] = {
    {"UTC", "UTC0"},
    {"US Eastern", "EST5EDT,M3.2.0,M11.1.0"},
    {"US Central", "CST6CDT,M3.2.0,M11.1.0"},
    {"US Mountain", "MST7MDT,M3.2.0,M11.1.0"},
    {"US Arizona", "MST7"},
    {"US Pacific", "PST8PDT,M3.2.0,M11.1.0"},
    {"US Alaska", "AKST9AKDT,M3.2.0,M11.1.0"},
    {"Hawaii", "HST10"},
    {"Brazil (Sao Paulo)", "<-03>3"},
    {"UK / Ireland", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Central Europe", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"East Europe", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"India", "IST-5:30"},
    {"China", "CST-8"},
    {"Japan / Korea", "JST-9"},
    {"Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"New Zealand", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
};
constexpr uint8_t TZ_ZONE_COUNT = sizeof(TZ_ZONES) / sizeof(TZ_ZONES[0]);
}

void Board::logNetworkActivity(const char* fmt, ...) {
    NetworkActivity& entry = networkActivity_[networkActivityNext_];
    entry.atMs = millis();
    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.detail, sizeof(entry.detail), fmt, args);
    va_end(args);

    networkActivityNext_ = static_cast<uint8_t>((networkActivityNext_ + 1) % NETWORK_ACTIVITY_CAP);
    if (networkActivityUsed_ < NETWORK_ACTIVITY_CAP) {
        networkActivityUsed_++;
    }
}

uint8_t Board::networkActivityCount() const {
    return networkActivityUsed_;
}

Board::NetworkActivity Board::networkActivity(uint8_t newestFirstIndex) const {
    NetworkActivity empty;
    if (newestFirstIndex >= networkActivityUsed_) {
        return empty;
    }

    int16_t index = static_cast<int16_t>(networkActivityNext_) - 1 - newestFirstIndex;
    while (index < 0) {
        index += NETWORK_ACTIVITY_CAP;
    }
    return networkActivity_[index];
}

void Board::noteTimeSyncSuccess() {
    lastTimeSyncMs_ = millis();
    lastTimeSyncEpoch_ = time(nullptr);
}

uint32_t Board::lastTimeSyncMs() const {
    return lastTimeSyncMs_;
}

time_t Board::lastTimeSyncEpoch() const {
    return lastTimeSyncEpoch_;
}

void Board::loadWifiCache() {
    if (wifiCacheLoaded_) return;
    wifiSsidCache_ = prefs_.isKey("wifiSsid") ? prefs_.getString("wifiSsid", "") : String();
    wifiPassCache_ = prefs_.isKey("wifiPass") ? prefs_.getString("wifiPass", "") : String();
    wifiCacheLoaded_ = true;
}

String Board::wifiSsid() {
    loadWifiCache();
    return wifiSsidCache_;
}

String Board::wifiPassword() {
    loadWifiCache();
    return wifiPassCache_;
}

void Board::setWifiCredentials(const String& ssid, const String& password) {
    const size_t nS = prefs_.putString("wifiSsid", ssid);
    const size_t nP = prefs_.putString("wifiPass", password);
    wifiSsidCache_ = ssid;
    wifiPassCache_ = password;
    wifiCacheLoaded_ = true;
    Serial.printf("[wifi] saved '%s' -> ssid %u bytes, pass %u bytes\n",
                  ssid.c_str(), (unsigned)nS, (unsigned)nP);
}

void Board::clearWifiCredentials() {
    prefs_.remove("wifiSsid");
    prefs_.remove("wifiPass");
    wifiSsidCache_ = String();
    wifiPassCache_ = String();
    wifiCacheLoaded_ = true;
}

bool Board::hasWifiCredentials() {
    loadWifiCache();
    return wifiSsidCache_.length() > 0;
}

void Board::loadNtpEnabled() {
    cachedNtpEnabled_ = prefs_.getBool(NTP_ENABLED_KEY, true);
    ntpEnabledCached_ = true;
}

bool Board::ntpEnabled() {
    if (!ntpEnabledCached_) {
        loadNtpEnabled();
    }
    return cachedNtpEnabled_;
}

void Board::setNtpEnabled(bool enabled) {
    prefs_.putBool(NTP_ENABLED_KEY, enabled);
    cachedNtpEnabled_ = enabled;
    ntpEnabledCached_ = true;
}

uint8_t Board::clampNtpResyncHours(uint8_t hours) {
    if (hours < NTP_RESYNC_MIN_HOURS) return NTP_RESYNC_MIN_HOURS;
    if (hours > NTP_RESYNC_MAX_HOURS) return NTP_RESYNC_MAX_HOURS;
    return hours;
}

void Board::loadNtpResyncHours() {
    const uint8_t stored = prefs_.getUChar(NTP_RESYNC_HOURS_KEY, NTP_RESYNC_DEFAULT_HOURS);
    cachedNtpResyncHours_ = clampNtpResyncHours(stored);
    ntpResyncCached_ = true;
}

uint8_t Board::ntpResyncHours() {
    if (!ntpResyncCached_) {
        loadNtpResyncHours();
    }
    return cachedNtpResyncHours_;
}

void Board::setNtpResyncHours(uint8_t hours) {
    const uint8_t clamped = clampNtpResyncHours(hours);
    prefs_.putUChar(NTP_RESYNC_HOURS_KEY, clamped);
    cachedNtpResyncHours_ = clamped;
    ntpResyncCached_ = true;
}

uint32_t Board::ntpResyncIntervalMs() const {
    return static_cast<uint32_t>(cachedNtpResyncHours_) * 60UL * 60UL * 1000UL;
}

String Board::ntpServer() {
    String server = prefs_.getString("ntpServer", DEFAULT_NTP_SERVER);
    if (server.length() == 0) {
        server = DEFAULT_NTP_SERVER;
    }
    return server;
}

void Board::setNtpServer(const String& server) {
    const String value = server.length() > 0 ? server : String(DEFAULT_NTP_SERVER);
    prefs_.putString("ntpServer", value);
}

bool Board::isWifiConnected() {
    return WiFi.status() == WL_CONNECTED;
}

uint8_t Board::tzZoneCount() { return TZ_ZONE_COUNT; }

const char* Board::tzZonePosix(uint8_t index) {
    if (index >= TZ_ZONE_COUNT) return "UTC0";
    return TZ_ZONES[index].posix;
}

const char* Board::tzZoneName(uint8_t index) {
    if (index >= TZ_ZONE_COUNT) return "?";
    return TZ_ZONES[index].name;
}

bool Board::tzZoneChosen() {
    return prefs_.getUChar("tzZone", 0xFF) != 0xFF;
}

uint8_t Board::tzZoneIndex() {
    const uint8_t i = prefs_.getUChar("tzZone", 0);
    return i < TZ_ZONE_COUNT ? i : 0;
}

void Board::setTzZoneIndex(uint8_t index) {
    if (index >= TZ_ZONE_COUNT) index = 0;
    prefs_.putUChar("tzZone", index);
    tzAutoDetected_ = false;
    if (WiFi.status() == WL_CONNECTED) {
        applyTimeConfig();
        lastResyncMs_ = millis();
    } else {
        setenv("TZ", TZ_ZONES[index].posix, 1);
        tzset();
    }
}

int16_t Board::tzOffsetMinutes() {
    return static_cast<int16_t>(prefs_.getShort("tzOffMin", 0));
}

void Board::setTzOffsetMinutes(int16_t minutes) {
    if (minutes < -12 * 60) minutes = -12 * 60;
    if (minutes > 14 * 60) minutes = 14 * 60;
    prefs_.putShort("tzOffMin", minutes);
    if (WiFi.status() == WL_CONNECTED) {
        applyTimeConfig();
        lastResyncMs_ = millis();
    }
}

bool Board::detectTimezone() {
    if (WiFi.status() != WL_CONNECTED) return false;
    const Watchdog::Pause wdtPause;

    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin("http://ip-api.com/line/?fields=offset")) {
        Serial.println("[time] tz lookup: http.begin failed");
        logNetworkActivity("HTTP ip-api.com begin fail");
        return false;
    }
    Serial.println("[time] tz lookup: querying ip-api.com");
    logNetworkActivity("HTTP ip-api.com GET");

    const int code = http.GET();
    if (code != 200) {
        Serial.printf("[time] tz lookup failed, http %d\n", code);
        logNetworkActivity("HTTP ip-api.com %d", code);
        http.end();
        return false;
    }

    const String body = http.getString();
    http.end();

    const long seconds = body.toInt();
    if (seconds == 0 && body.indexOf('0') < 0) return false;
    if (seconds < -12 * 3600L || seconds > 14 * 3600L) return false;

    const int16_t minutes = static_cast<int16_t>(seconds / 60);
    Serial.printf("[time] detected UTC%+d:%02d from public IP\n",
                  minutes / 60, abs(minutes % 60));
    logNetworkActivity("HTTP ip-api.com ok tz=%+d", minutes / 60);
    prefs_.putShort("tzOffMin", minutes);
    tzAutoDetected_ = true;
    applyTimeConfig();
    return true;
}

bool Board::timeSynced() const {
    return timeSyncState_ == TimeSyncState::Synced;
}

void Board::beginTimeSync() {
    if (!ntpEnabled() || !hasWifiCredentials()) return;
    if (timeSyncState_ == TimeSyncState::Connecting ||
        timeSyncState_ == TimeSyncState::Syncing) return;

    lastSyncAttemptMs_ = millis();

    if (WiFi.status() == WL_CONNECTED) {
        applyTimeConfig();
        if (!ntpUdpProbe(ntpServer().c_str())) ntpUdpProbe("time.google.com");
        timeSyncStartedMs_ = millis();
        timeSyncState_ = TimeSyncState::Syncing;
        Serial.println("[time] already connected -> syncing");
        return;
    }

    const uint32_t now = millis();
    if (lastWifiBeginMs_ != 0 && now - lastWifiBeginMs_ < 30000UL) {
        timeSyncState_ = TimeSyncState::Connecting;
        timeSyncStartedMs_ = now;
        return;
    }

    WiFi.mode(WIFI_STA);
    const String ssid = wifiSsid();
    const String pass = wifiPassword();
    Serial.printf("[time] connecting to %s\n", ssid.c_str());
    logNetworkActivity("WiFi begin %s", ssid.c_str());
    WiFi.begin(ssid.c_str(), pass.c_str());
    lastWifiBeginMs_ = now;
    timeSyncStartedMs_ = now;
    timeSyncState_ = TimeSyncState::Connecting;
}

/* Program lwIP's SNTP client, at most once per real configuration change.
 *
 * Two things here are load-bearing, and getting either wrong is invisible for
 * hours and then fatal.
 *
 * First, the server name must outlive this call. lwIP's sntp_setservername()
 * keeps the pointer rather than copying the string, and the daemon re-reads it
 * every time it resolves the host -- on its own poll cadence, long after we
 * have returned. It used to be handed ntpServer().c_str(), the buffer of a
 * String temporary destroyed at the end of that same statement, so the daemon
 * was left pointing into freed heap. That survives for as long as the block
 * happens to go untouched, which is exactly why the device ran fine for
 * minutes and died after hours, once Wi-Fi buffers had reused the memory. The
 * name now lives in ntpServerName_, a member of the singleton board.
 *
 * Second, this is idempotent. It used to be re-issued on every automatic
 * resync, and each call stops and restarts the daemon. That is not what keeps
 * the clock honest -- once started, SNTP re-polls by itself -- it was pure
 * churn whenever the device stayed idle. */
void Board::applyTimeConfig() {
    /* Copied straight out of the temporary rather than held in a named local:
     * the point of this whole function is that nothing lwIP keeps may live in
     * a String. */
    char wanted[NTP_NAME_CAP];
    snprintf(wanted, sizeof(wanted), "%s", ntpServer().c_str());

    const uint8_t z = prefs_.getUChar("tzZone", 0xFF);

    char tzSpec[TZ_SPEC_CAP];
    if (z != 0xFF && z < tzZoneCount()) {
        snprintf(tzSpec, sizeof(tzSpec), "%s", tzZonePosix(z));
    } else {
        /* The fixed-offset case, written as a POSIX zone so that one code path
         * programs the daemon and one comparison decides whether it changed.
         * POSIX TZ counts hours *west* of Greenwich, the opposite sign to the
         * offset we store. */
        const long west = -(static_cast<long>(tzOffsetMinutes()) * 60L);
        const long absMin = (west < 0 ? -west : west) / 60L;
        snprintf(tzSpec, sizeof(tzSpec), "GMT%c%ld:%02ld",
                 west < 0 ? '-' : '+', absMin / 60L, absMin % 60L);
    }

    if (sntpConfigured_ && strcmp(tzSpec, appliedTz_) == 0 &&
        strcmp(wanted, ntpServerName_) == 0) {
        return;
    }

    snprintf(ntpServerName_, sizeof(ntpServerName_), "%s", wanted);
    snprintf(appliedTz_, sizeof(appliedTz_), "%s", tzSpec);
    sntpConfigured_ = true;

    /* ntpServerName_ and appliedTz_ are board members and the two fallbacks
     * are string literals, so every pointer handed over here outlives the
     * daemon that keeps it. Do not inline a String temporary back in. */
    configTzTime(appliedTz_, ntpServerName_, "time.google.com", "time.cloudflare.com");
    Serial.printf("[time] configTzTime %s server=%s\n", appliedTz_, ntpServerName_);
    logNetworkActivity("SNTP cfg %s + fallbacks", ntpServerName_);
}

bool Board::ntpUdpProbe(const char* host) {
    if (WiFi.status() != WL_CONNECTED) return false;
    const Watchdog::Pause wdtPause;

    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) {
        Serial.printf("[time] DNS FAILED for %s\n", host);
        logNetworkActivity("DNS fail %s", host);
        return false;
    }
    Serial.printf("[time] %s -> %s\n", host, ip.toString().c_str());
    logNetworkActivity("DNS %s -> %s", host, ip.toString().c_str());

    WiFiUDP udp;
    if (!udp.begin(2390)) {
        Serial.println("[time] udp begin failed");
        logNetworkActivity("NTP UDP open fail");
        return false;
    }

    uint8_t pkt[48] = {0};
    pkt[0] = 0b11100011;
    udp.beginPacket(ip, 123);
    udp.write(pkt, 48);
    udp.endPacket();

    const uint32_t t0 = millis();
    while (millis() - t0 < 4000) {
        if (udp.parsePacket() >= 48) {
            udp.read(pkt, 48);
            udp.stop();
            const uint32_t ntpSecs = (static_cast<uint32_t>(pkt[40]) << 24) |
                                     (static_cast<uint32_t>(pkt[41]) << 16) |
                                     (static_cast<uint32_t>(pkt[42]) << 8) |
                                      static_cast<uint32_t>(pkt[43]);
            if (ntpSecs == 0) {
                Serial.println("[time] UDP reply had a zero timestamp");
                return false;
            }
            const uint32_t unixSecs = ntpSecs - 2208988800UL;
            struct timeval tv;
            tv.tv_sec = static_cast<time_t>(unixSecs);
            tv.tv_usec = 0;
            settimeofday(&tv, nullptr);
            Serial.printf("[time] UDP NTP OK, clock set from %s (unix %u)\n",
                          host, (unsigned)unixSecs);
            noteTimeSyncSuccess();
            logNetworkActivity("NTP UDP ok %s", host);
            return true;
        }
        delay(20);
    }
    udp.stop();
    Serial.printf("[time] no UDP reply from %s - port 123 blocked?\n", host);
    logNetworkActivity("NTP UDP no reply %s", host);
    return false;
}

void Board::syncTimeNow() {
    timeSyncState_ = TimeSyncState::Idle;
    lastSyncAttemptMs_ = 0;
    lastResyncMs_ = 0;
    Serial.println("[time] manual sync requested");

    if (!hasWifiCredentials()) {
        Serial.println("[time] manual sync: no saved network");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[time] manual sync: not connected, associating");
        beginTimeSync();
        return;
    }

    if (!tzZoneChosen()) detectTimezone();
    applyTimeConfig();
    if (!ntpUdpProbe(ntpServer().c_str())) {
        ntpUdpProbe("time.google.com");
    }
    timeSyncStartedMs_ = millis();
    timeSyncState_ = TimeSyncState::Syncing;
}

void Board::tickTimeSync() {
    const uint32_t now = millis();

    switch (timeSyncState_) {
        case TimeSyncState::Idle:
            if (ntpEnabled() && hasWifiCredentials() &&
                (lastSyncAttemptMs_ == 0 || now - lastSyncAttemptMs_ >= TIME_RETRY_MS)) {
                beginTimeSync();
            }
            break;

        case TimeSyncState::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("[time] wifi up, ip=%s rssi=%d\n",
                              WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
                if (prefs_.getUChar("tzZone", 0xFF) == 0xFF) detectTimezone();
                applyTimeConfig();
                if (!ntpUdpProbe(ntpServer().c_str())) {
                    ntpUdpProbe("time.google.com");
                }
                timeSyncStartedMs_ = now;
                timeSyncState_ = TimeSyncState::Syncing;
            } else if (now - timeSyncStartedMs_ > TIME_CONNECT_TIMEOUT_MS) {
                Serial.printf("[time] connect timeout, status=%d\n", (int)WiFi.status());
                timeSyncState_ = TimeSyncState::Idle;
            }
            break;

        case TimeSyncState::Syncing:
            if (clockLooksValid()) {
                struct tm t;
                getLocalTime(&t, 0);
                Serial.printf("[time] SYNCED %04d-%02d-%02d %02d:%02d:%02d (%s)\n",
                              t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                              t.tm_hour, t.tm_min, t.tm_sec,
                              tzZoneChosen() ? tzZoneName(tzZoneIndex()) : "auto");
                noteTimeSyncSuccess();
                logNetworkActivity("SNTP sync ok");
                timeSyncState_ = TimeSyncState::Synced;
                lastResyncMs_ = now;
            } else if (now - timeSyncStartedMs_ > TIME_SYNC_TIMEOUT_MS) {
                Serial.println("[time] sntp timeout, trying direct UDP");
                if (ntpUdpProbe("time.google.com") || ntpUdpProbe("time.cloudflare.com")) {
                    timeSyncState_ = TimeSyncState::Synced;
                    lastResyncMs_ = now;
                } else {
                    timeSyncState_ = TimeSyncState::Idle;
                }
            }
            break;

        case TimeSyncState::Synced:
            if (!ntpEnabled() || !hasWifiCredentials()) {
                break;
            }
            if (now - lastResyncMs_ >= ntpResyncIntervalMs()) {
                lastResyncMs_ = now;
                if (WiFi.status() == WL_CONNECTED) {
                    applyTimeConfig();
                } else {
                    timeSyncState_ = TimeSyncState::Idle;
                    lastSyncAttemptMs_ = 0;
                }
            }
            break;
    }
}
