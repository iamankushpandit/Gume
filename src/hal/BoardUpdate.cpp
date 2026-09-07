#include "Board.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "AppVersion.h"
#include "UpdateChannel.h"
#include "Watchdog.h"

/* Update availability -- the notice, not an update. The whole rationale, and
 * the rules about what may and may not be read out of the response, are in
 * include/UpdateChannel.h. Read that before changing anything here. */

namespace {

/* NVS keys. Fifteen characters is the hard cap, and these are global settings
 * rather than profile-scoped: which firmware the board is running is a fact
 * about the board, and every player should be told the same thing. */
constexpr const char* KEY_ENABLED = "updChk";
constexpr const char* KEY_LATEST = "updLatest";
constexpr const char* KEY_CHECKED = "updChkAt";
constexpr const char* KEY_NOTIFIED = "updNotAt";
constexpr const char* KEY_NOTIFIED_VER = "updNotVer";

/* Once a day. The NTP resync cadence is a different question with a different
 * default (6h), and borrowing it would have made this fire four times as often
 * as anybody wants for a thing that changes a few times a year. */
constexpr uint32_t CHECK_INTERVAL_S = 24UL * 60UL * 60UL;

/* Do not hammer a failing network. A check that fails -- captive portal, DNS
 * down, Pages having a bad day -- waits this long before trying again, rather
 * than retrying on every frame for the rest of the day. */
constexpr uint32_t RETRY_INTERVAL_MS = 30UL * 60UL * 1000UL;

/* The manifest is a few hundred bytes and is read into this in one go. A fixed
 * buffer rather than http.getString(): this runs on the loop task, and the
 * memory rule in CLAUDE.md is about heap churn, not about how often the code
 * happens to run. A manifest larger than this is malformed. */
constexpr size_t MANIFEST_CAP = 768;

/* Pull the leading integer off `s`, leaving `s` on the first character that is
 * not a digit. Returns 0 for an empty or non-numeric field, which is the right
 * answer for a malformed version: it sorts oldest, so it cannot raise a
 * notice. */
uint32_t takeNumber(const char*& s) {
    uint32_t n = 0;
    while (*s >= '0' && *s <= '9') {
        n = n * 10 + static_cast<uint32_t>(*s - '0');
        ++s;
    }
    return n;
}

}   // namespace

/* Compare two version strings of the form "a.b.c" with an optional "-SUFFIX".
 *
 * Returns >0 when `a` is newer, <0 when `b` is newer, 0 when they are the same
 * release.
 *
 * The suffix is what makes this more than three integer comparisons, and
 * getting it backwards would be actively harmful. `dev` carries a -SNAPSHOT
 * between releases by design, so a board flashed from `dev` runs
 * 5.7.0-SNAPSHOT while the newest published release is 5.6.0. Those compare
 * the wrong way round unless a snapshot sorts BEFORE the release of the same
 * number and after the release below it. Get it wrong and the console spends
 * the whole development cycle telling its owner to downgrade. */
int Board::compareVersions(const char* a, const char* b) {
    if (a == nullptr) a = "";
    if (b == nullptr) b = "";

    const char* pa = a;
    const char* pb = b;
    for (int i = 0; i < 3; ++i) {
        const uint32_t na = takeNumber(pa);
        const uint32_t nb = takeNumber(pb);
        if (na != nb) return na < nb ? -1 : 1;
        if (*pa == '.') ++pa;
        if (*pb == '.') ++pb;
    }

    /* Same numbers. A pre-release of that number is older than the release. */
    const bool aPre = (strchr(a, '-') != nullptr);
    const bool bPre = (strchr(b, '-') != nullptr);
    if (aPre == bPre) return 0;
    return aPre ? -1 : 1;
}

void Board::loadUpdateState() {
    if (updateStateLoaded_) return;
    updateStateLoaded_ = true;
    updateCheckEnabled_ = prefs_.getBool(KEY_ENABLED, true);
    lastUpdateCheckEpoch_ =
        static_cast<time_t>(prefs_.getULong(KEY_CHECKED, 0UL));
    const size_t n = prefs_.getString(KEY_LATEST, latestVersion_,
                                      sizeof(latestVersion_));
    if (n == 0) latestVersion_[0] = '\0';
}

bool Board::updateCheckEnabled() {
    loadUpdateState();
    return updateCheckEnabled_;
}

void Board::setUpdateCheckEnabled(bool on) {
    loadUpdateState();
    if (updateCheckEnabled_ == on) return;
    updateCheckEnabled_ = on;                 // mirror and NVS together
    prefs_.putBool(KEY_ENABLED, on);
    if (!on) {
        /* Switching the check off drops what it had learned. Leaving a stale
         * "5.9.0 is available" on the About page after the owner has said stop
         * looking would be answering a question they withdrew. */
        latestVersion_[0] = '\0';
        lastUpdateCheckEpoch_ = 0;
        prefs_.remove(KEY_LATEST);
        prefs_.remove(KEY_CHECKED);
        prefs_.remove(KEY_NOTIFIED);
        prefs_.remove(KEY_NOTIFIED_VER);
    }
}

const char* Board::latestKnownVersion() {
    loadUpdateState();
    return latestVersion_;
}

time_t Board::lastUpdateCheckEpoch() {
    loadUpdateState();
    return lastUpdateCheckEpoch_;
}

bool Board::updateAvailable() {
    loadUpdateState();
    if (latestVersion_[0] == '\0') return false;
    return compareVersions(latestVersion_, BRAINO_VERSION) > 0;
}

/* Read the manifest and pick this board's line out of it.
 *
 * Format, deliberately line-oriented rather than JSON -- the same reasoning
 * that makes the timezone lookup ask ip-api for /line/: no parser, no
 * allocation, and a malformed field costs a field rather than the document.
 *
 *     # comment
 *     * 5.7.0                 <- fallback: any board with no line of its own
 *     E32R32P 5.7.0           <- keyed by BOARD_NAME, exactly as compiled in
 *
 * A board's own line wins over the fallback wherever both are present, because
 * a board added in 5.4.0 has no 5.2.0 build and must not be told one exists.
 * The keys are BOARD_NAME rather than platformio's section ids so the firmware
 * compares against something it already knows about itself, instead of
 * carrying a second name for the same board. */
bool Board::fetchUpdateManifest() {
    if (WiFi.status() != WL_CONNECTED) return false;
    const Watchdog::Pause wdtPause;

    WiFiClientSecure client;
    /* Deliberately unverified. The response cannot choose a destination, cannot
     * start an install and cannot write anything -- its entire authority is
     * over one version string that gets displayed. Pinning a CA root here would
     * buy nothing against that threat model, and would silently brick the check
     * on every device in the field the day the root rotates, years from now.
     * See include/UpdateChannel.h. */
    client.setInsecure();

    HTTPClient http;
    http.setTimeout(6000);
    http.setConnectTimeout(6000);
    /* Follow the redirect Pages issues; the destination is still the
     * compiled-in host. */
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    if (!http.begin(client, BRAINO_UPDATE_MANIFEST_URL)) {
        logNetworkActivity("HTTP update begin fail");
        return false;
    }
    logNetworkActivity("HTTP update GET");

    const int code = http.GET();
    if (code != 200) {
        Serial.printf("[update] manifest fetch failed, http %d\n", code);
        logNetworkActivity("HTTP update %d", code);
        http.end();
        return false;
    }

    static char body[MANIFEST_CAP];
    size_t used = 0;
    {
        /* One blocking read against the client's own timeout, rather than a
         * poll loop with a delay() in it. Stream::readBytes already waits for
         * as much as it was asked for or until the timeout, so the sleep was
         * doing nothing the transport was not already doing -- and a delay()
         * on a path the loop task reaches is exactly what
         * tools/check_frame_rules.py refuses, correctly. The whole fetch is
         * still seconds long and still sits inside the Watchdog::Pause above;
         * this removes a busy-wait, not the blocking. */
        WiFiClient* stream = http.getStreamPtr();
        stream->setTimeout(4000);
        const int declared = http.getSize();
        size_t want = sizeof(body) - 1;
        if (declared > 0 && static_cast<size_t>(declared) < want) {
            want = static_cast<size_t>(declared);
        }
        used = stream->readBytes(body, want);
    }
    body[used] = '\0';
    http.end();

    if (used == 0) {
        logNetworkActivity("HTTP update empty");
        return false;
    }

    char fallback[VERSION_STR_CAP] = {0};
    char mine[VERSION_STR_CAP] = {0};

    char* line = body;
    while (line != nullptr && *line != '\0') {
        char* next = strpbrk(line, "\r\n");
        if (next != nullptr) {
            *next = '\0';
            ++next;
            while (*next == '\r' || *next == '\n') ++next;
        }

        while (*line == ' ' || *line == '\t') ++line;
        if (*line != '\0' && *line != '#') {
            char* sep = line;
            while (*sep != '\0' && *sep != ' ' && *sep != '\t') ++sep;
            if (*sep != '\0') {
                *sep = '\0';
                char* value = sep + 1;
                while (*value == ' ' || *value == '\t') ++value;
                /* Trim trailing blanks, so a manifest edited by hand cannot
                 * produce a version string with a space on the end that then
                 * fails to compare equal to itself. */
                size_t vlen = strlen(value);
                while (vlen > 0 && (value[vlen - 1] == ' ' ||
                                    value[vlen - 1] == '\t')) {
                    value[--vlen] = '\0';
                }
                if (vlen > 0 && vlen < VERSION_STR_CAP) {
                    if (strcmp(line, "*") == 0) {
                        strncpy(fallback, value, sizeof(fallback) - 1);
                    } else if (strcmp(line, BOARD_NAME) == 0) {
                        strncpy(mine, value, sizeof(mine) - 1);
                    }
                }
            }
        }
        line = next;
    }

    const char* picked = (mine[0] != '\0') ? mine : fallback;
    if (picked[0] == '\0') {
        Serial.println("[update] manifest had no entry for this board");
        logNetworkActivity("HTTP update no entry");
        return false;
    }

    if (strcmp(picked, latestVersion_) != 0) {
        strncpy(latestVersion_, picked, sizeof(latestVersion_) - 1);
        latestVersion_[sizeof(latestVersion_) - 1] = '\0';
        prefs_.putString(KEY_LATEST, latestVersion_);
    }
    lastUpdateCheckEpoch_ = time(nullptr);
    prefs_.putULong(KEY_CHECKED,
                    static_cast<uint32_t>(lastUpdateCheckEpoch_));

    Serial.printf("[update] latest for %s is %s (running %s)%s\n",
                  BOARD_NAME, latestVersion_, BRAINO_VERSION,
                  updateAvailable() ? " -- newer available" : "");
    logNetworkActivity("HTTP update ok %s", latestVersion_);
    return true;
}

/* Is a banner owed?
 *
 * An available update is a standing condition, not an event, and the two want
 * opposite things from a notification. A poke is shown once because it happened
 * once. An update is still available tomorrow, and an owner who was not the one
 * holding the device when it appeared has to find out somehow -- so it repeats,
 * daily, until the device is updated or the check is switched off.
 *
 * Both halves of the gate are persisted, and both matter. Without the
 * timestamp, a console power-cycled four times in an evening would announce the
 * same update four times. Without the version, a device sitting on 5.7.0 that
 * has already had its daily banner would stay silent through the release of
 * 5.8.0 until the following day. */
bool Board::updateNoticeDue() {
    if (!updateAvailable()) return false;

    char notified[VERSION_STR_CAP] = {0};
    prefs_.getString(KEY_NOTIFIED_VER, notified, sizeof(notified));
    if (strcmp(notified, latestVersion_) != 0) return true;

    const time_t now = time(nullptr);
    const time_t last = static_cast<time_t>(prefs_.getULong(KEY_NOTIFIED, 0UL));
    if (now <= 0 || last == 0) return true;
    return (now - last) >= static_cast<time_t>(CHECK_INTERVAL_S);
}

void Board::markUpdateNoticeShown() {
    prefs_.putString(KEY_NOTIFIED_VER, latestVersion_);
    const time_t now = time(nullptr);
    prefs_.putULong(KEY_NOTIFIED,
                    now > 0 ? static_cast<uint32_t>(now) : 0UL);
}

/* Once per frame from the runtime. Nearly every call returns on the first or
 * second test.
 *
 * The check rides the clock rather than owning a schedule of its own: it needs
 * Wi-Fi up, and it needs a synced clock, because the once-a-day gate is kept in
 * epoch seconds so that it survives a reboot. A device that is switched on for
 * ten minutes a day would otherwise check on every single power-up, which is
 * the one behaviour that would make this feel like telemetry. */
void Board::tickUpdateCheck() {
    loadUpdateState();
    if (!updateCheckEnabled_) return;
    if (!timeSynced()) return;
    if (WiFi.status() != WL_CONNECTED) return;

    const time_t now = time(nullptr);
    if (now <= 0) return;
    if (lastUpdateCheckEpoch_ != 0 &&
        now - lastUpdateCheckEpoch_ < static_cast<time_t>(CHECK_INTERVAL_S)) {
        return;
    }

    const uint32_t nowMs = millis();
    if (updateAttemptMs_ != 0 && nowMs - updateAttemptMs_ < RETRY_INTERVAL_MS) {
        return;
    }
    updateAttemptMs_ = nowMs;

    fetchUpdateManifest();
}
