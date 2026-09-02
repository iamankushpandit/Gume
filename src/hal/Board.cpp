#include "Board.h"

#if GUME_TOUCH_CAPACITIVE
#include <Wire.h>
#endif

#include "BleBeacon.h"
#include "ui/Ui.h"

Board::Board() : sdSpi_(VSPI) {}

void Board::begin() {
    Serial.begin(115200);
    delay(100);

    /* Every peripheral below is optional in the profile. A board that does not
     * wire one sets PIN_NONE and skips it here -- that is what makes a port a
     * header rather than a patch. Touch and the panel are not optional. */
    if (BOARD.hasBatterySense()) {
        pinMode(BOARD.battery.adcPin, INPUT);
    }

    if (BOARD.hasBacklightControl()) {
        pinMode(BOARD.panel.backlightPin, OUTPUT);
        digitalWrite(BOARD.panel.backlightPin, BOARD.panel.backlightActiveHigh ? HIGH : LOW);
    }

    if (BOARD.hasRgbLed()) {
        if (BOARD.rgb.r != PIN_NONE) pinMode(BOARD.rgb.r, OUTPUT);
        if (BOARD.rgb.g != PIN_NONE) pinMode(BOARD.rgb.g, OUTPUT);
        if (BOARD.rgb.b != PIN_NONE) pinMode(BOARD.rgb.b, OUTPUT);
        setRgb(false, false, false);
    }

    if (BOARD.hasSpeaker()) {
        pinMode(BOARD.audio.speakerPin, OUTPUT);
        digitalWrite(BOARD.audio.speakerPin, LOW);
    }

    /* Guarded by the preprocessor, not `if constexpr`: in a non-template
     * function both arms of an `if constexpr` are still compiled, so the
     * capacitive arm alone dragged Wire into every resistive build. */
#if GUME_TOUCH_CAPACITIVE
    {
        pinMode(BOARD.touch.irq, INPUT_PULLUP);
        /* ASSERT reset here and RELEASE it after the panel comes up.
         *
         * The controller must be brought out of reset before the first
         * transaction, or a chip that is present answers nothing and the pins
         * take the blame. It needs a hold low, then a settle before it
         * acknowledges its own address -- but neither has to be a delay().
         * tft_.init() runs a full panel init sequence between the two halves,
         * which is tens of milliseconds of real work, and the NVS and profile
         * setup below covers the settle before the first pollTouch(). Blocking
         * boot for 320ms to wait for something the boot was going to do anyway
         * would be waste, and check_frame_rules.py is right to refuse it. */
        if (BOARD.touch.reset != PIN_NONE) {
            pinMode(BOARD.touch.reset, OUTPUT);
            digitalWrite(BOARD.touch.reset, LOW);
        }
    }
#else
    {
        pinMode(BOARD.touch.mosi, OUTPUT);
        pinMode(BOARD.touch.miso, INPUT);
        pinMode(BOARD.touch.sclk, OUTPUT);
        pinMode(BOARD.touch.cs, OUTPUT);
        pinMode(BOARD.touch.irq, INPUT);
        digitalWrite(BOARD.touch.cs, HIGH);
        digitalWrite(BOARD.touch.sclk, LOW);
    }
#endif

    tft_.init();
    tft_.setRotation(BOARD.panel.landscapeRotation);
    displayRotation_ = BOARD.panel.landscapeRotation;
    tft_.setTextWrap(false, false);
    tft_.fillScreen(Ui::bg());

#if GUME_TOUCH_CAPACITIVE
    /* Release the touch controller. The panel init above was its reset pulse;
     * everything below is its settling time, and the first pollTouch() does
     * not happen until the loop starts. */
    if (BOARD.touch.reset != PIN_NONE) {
        digitalWrite(BOARD.touch.reset, HIGH);
    }
    Wire.begin(BOARD.touch.sda, BOARD.touch.scl, BOARD.touch.i2cHz);
#endif

    prefs_.begin("cydkids", false);
    migrateStorageSchema();
    logStorageUsage("boot");
    loadNtpEnabled();
    loadNtpResyncHours();
    applyBrightness();
    loadTouchCalibration();
    beginAudio();
    mountSd();
    BleBeacon::begin(bleBeaconEnabled());
}

bool Board::sdReady() const {
    return sdMounted_;
}

bool Board::mountSd() {
    if (!BOARD.hasSdSlot()) {
        sdMounted_ = false;
        return false;
    }
    sdSpi_.begin(BOARD.sd.sclk, BOARD.sd.miso, BOARD.sd.mosi, BOARD.sd.cs);
    sdMounted_ = SD.begin(BOARD.sd.cs, sdSpi_, BOARD.sd.spiHz);
    setRgb(!sdMounted_, sdMounted_, false);
    Serial.printf("SD mount: %s\n", sdMounted_ ? "ok" : "failed");
    return sdMounted_;
}

String Board::profileName(uint8_t index) {
    if (index == GUEST_INDEX) return String("Guest");
    char key[10];
    snprintf(key, sizeof(key), "pname%u", index);
    const String stored = prefs_.isKey(key) ? prefs_.getString(key, "") : String();
    if (stored.length() > 0) return stored;
    return String("Player ") + static_cast<int>(index + 1);
}

void Board::setProfileName(uint8_t index, const String& name) {
    if (index >= MAX_PLAYERS) return;
    char key[10];
    snprintf(key, sizeof(key), "pname%u", index);
    prefs_.putString(key, name.substring(0, PROFILE_NAME_MAX));
}

uint8_t Board::addPlayer(const char* name) {
    const uint8_t n = playerCount();
    if (n >= MAX_PLAYERS) return 0xFF;
    prefs_.putUChar("kids", static_cast<uint8_t>(n + 1));

    /* Truncation is the same PROFILE_NAME_MAX the String path applied via
     * substring(); doing it in a stack buffer just means no temporary. */
    char stored[PROFILE_NAME_MAX + 1];
    if (name == nullptr || name[0] == '\0') {
        snprintf(stored, sizeof(stored), "Player %u", static_cast<unsigned>(n + 1));
    } else {
        snprintf(stored, sizeof(stored), "%s", name);
    }
    char key[10];
    snprintf(key, sizeof(key), "pname%u", n);
    prefs_.putString(key, stored);
    return n;
}

void Board::removePlayer(uint8_t index) {
    const uint8_t n = playerCount();
    if (index >= n) return;

    const uint8_t oldActive = activeProfile();
    const uint8_t oldAdmin = adminProfileIndex();

    for (uint8_t i = index; i + 1 < n; ++i) {
        moveProfileSlot(static_cast<uint8_t>(i + 1), i);
    }
    clearProfileSlot(static_cast<uint8_t>(n - 1));
    prefs_.putUChar("kids", static_cast<uint8_t>(n - 1));

    visibilityCached_ = false;

    if (oldActive == index) {
        setActiveProfile(GUEST_INDEX);
    } else if (oldActive > index && oldActive < n) {
        setActiveProfile(static_cast<uint8_t>(oldActive - 1));
    } else if (oldActive != GUEST_INDEX && oldActive >= playerCount()) {
        setActiveProfile(GUEST_INDEX);
    }

    if (oldAdmin == index) {
        setAdminProfileIndex(GUEST_INDEX);
    } else if (oldAdmin > index && oldAdmin < n) {
        setAdminProfileIndex(static_cast<uint8_t>(oldAdmin - 1));
    }

    logStorageUsage("profile delete");
}

Board::ThemeMode Board::themeMode() {
    if (!themeCached_) {
        cachedTheme_ = prefs_.getUChar("themeMode", static_cast<uint8_t>(ThemeMode::Dark));
        themeCached_ = true;
    }
    return cachedTheme_ == static_cast<uint8_t>(ThemeMode::Light) ? ThemeMode::Light
                                                                 : ThemeMode::Dark;
}

void Board::setThemeMode(ThemeMode mode) {
    prefs_.putUChar("themeMode", static_cast<uint8_t>(mode));
    cachedTheme_ = static_cast<uint8_t>(mode);
    themeCached_ = true;
}

Board::LayoutMode Board::layoutMode() {
    if (!layoutCached_) {
        cachedLayout_ = prefs_.getUChar("layoutMode", static_cast<uint8_t>(LayoutMode::Horizontal));
        layoutCached_ = true;
    }
    return cachedLayout_ == static_cast<uint8_t>(LayoutMode::Vertical) ? LayoutMode::Vertical
                                                                      : LayoutMode::Horizontal;
}

void Board::setLayoutMode(LayoutMode mode) {
    prefs_.putUChar("layoutMode", static_cast<uint8_t>(mode));
    cachedLayout_ = static_cast<uint8_t>(mode);
    layoutCached_ = true;
}

uint16_t Board::screenSaverSeconds() {
    if (!idleCached_) {
        const uint16_t stored = prefs_.getUShort("idleSecs", 300);
        cachedIdleSecs_ = stored < 15 ? 15 : stored;
        idleCached_ = true;
    }
    return cachedIdleSecs_;
}

void Board::setScreenSaverSeconds(uint16_t seconds) {
    const uint16_t clamped = seconds < 15 ? 15 : seconds;
    prefs_.putUShort("idleSecs", clamped);
    cachedIdleSecs_ = clamped;
    idleCached_ = true;
}

Board::IdleAction Board::idleAction() {
    if (!idleActionCached_) {
        cachedIdleAction_ = prefs_.getUChar("idleAct",
                                static_cast<uint8_t>(IdleAction::SaverThenSleep));
        if (cachedIdleAction_ > static_cast<uint8_t>(IdleAction::SaverOnly)) {
            cachedIdleAction_ = static_cast<uint8_t>(IdleAction::SaverThenSleep);
        }
        idleActionCached_ = true;
    }
    return static_cast<IdleAction>(cachedIdleAction_);
}

void Board::setIdleAction(IdleAction action) {
    prefs_.putUChar("idleAct", static_cast<uint8_t>(action));
    cachedIdleAction_ = static_cast<uint8_t>(action);
    idleActionCached_ = true;
}

uint16_t Board::sleepSeconds() {
    if (!sleepSecsCached_) {
        const uint16_t stored = prefs_.getUShort("sleepSecs", 60);
        cachedSleepSecs_ = stored < 5 ? 5 : stored;
        sleepSecsCached_ = true;
    }
    return cachedSleepSecs_;
}

void Board::setSleepSeconds(uint16_t seconds) {
    const uint16_t clamped = seconds < 5 ? 5 : seconds;
    prefs_.putUShort("sleepSecs", clamped);
    cachedSleepSecs_ = clamped;
    sleepSecsCached_ = true;
}

/* Mirrored in RAM: the loop asks on every touch that reaches a sleeping or
 * screen-saving device, and Preferences is flash-backed. Defaults to on --
 * the guard is the point of the feature, and an owner who wants the old
 * single-touch behaviour can say so in Settings. */
bool Board::wakeLockEnabled() {
    if (!wakeLockCached_) {
        cachedWakeLock_ = prefs_.getBool("wakeLock", true);
        wakeLockCached_ = true;
    }
    return cachedWakeLock_;
}

void Board::setWakeLockEnabled(bool enabled) {
    prefs_.putBool("wakeLock", enabled);
    cachedWakeLock_ = enabled;
    wakeLockCached_ = true;
}

bool Board::gameVisible(uint8_t catalogIndex, bool fallback) {
    if (isGuest()) return true;
    return gameVisibleFor(catalogIndex, activeProfile(), fallback);
}

void Board::setGameVisible(uint8_t catalogIndex, bool visible) {
    if (isGuest()) return;
    setGameVisibleFor(catalogIndex, activeProfile(), visible);
}

void Board::visibilityKey(char* out, size_t cap, uint8_t profileIndex, uint8_t catalogIndex) {
    snprintf(out, cap, "p%u_gv%u", profileIndex, catalogIndex);
}

void Board::loadVisibility(uint8_t profileIndex, bool fallback) {
    uint32_t mask = 0;
    char key[16];
    for (uint8_t i = 0; i < VISIBILITY_BITS; ++i) {
        visibilityKey(key, sizeof(key), profileIndex, i);
        if (prefs_.getBool(key, fallback)) {
            mask |= (1UL << i);
        }
    }
    visibilityMask_ = mask;
    visibilityProfile_ = profileIndex;
    visibilityCached_ = true;
}

bool Board::gameVisibleFor(uint8_t catalogIndex, uint8_t profileIndex, bool fallback) {
    if (profileIndex == GUEST_INDEX) return true;
    if (catalogIndex >= VISIBILITY_BITS) return fallback;
    if (!visibilityCached_ || visibilityProfile_ != profileIndex) {
        loadVisibility(profileIndex, fallback);
    }
    return ((visibilityMask_ >> catalogIndex) & 1UL) != 0;
}

void Board::setGameVisibleFor(uint8_t catalogIndex, uint8_t profileIndex, bool visible) {
    if (profileIndex == GUEST_INDEX || catalogIndex >= VISIBILITY_BITS) return;
    char key[16];
    visibilityKey(key, sizeof(key), profileIndex, catalogIndex);
    prefs_.putBool(key, visible);
    if (visibilityCached_ && visibilityProfile_ == profileIndex) {
        if (visible) {
            visibilityMask_ |= (1UL << catalogIndex);
        } else {
            visibilityMask_ &= ~(1UL << catalogIndex);
        }
    }
}
