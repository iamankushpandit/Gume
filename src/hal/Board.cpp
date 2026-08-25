#include "Board.h"

#include "BleBeacon.h"
#include "ui/Ui.h"

Board::Board() : sdSpi_(VSPI) {}

void Board::begin() {
    Serial.begin(115200);
    delay(100);

    pinMode(PIN_BAT_ADC, INPUT);

    pinMode(PIN_TFT_BACKLIGHT, OUTPUT);
    digitalWrite(PIN_TFT_BACKLIGHT, HIGH);

    pinMode(PIN_RGB_R, OUTPUT);
    pinMode(PIN_RGB_G, OUTPUT);
    pinMode(PIN_RGB_B, OUTPUT);
    setRgb(false, false, false);

    pinMode(PIN_SPEAKER, OUTPUT);
    digitalWrite(PIN_SPEAKER, LOW);

    pinMode(PIN_TOUCH_MOSI, OUTPUT);
    pinMode(PIN_TOUCH_MISO, INPUT);
    pinMode(PIN_TOUCH_SCLK, OUTPUT);
    pinMode(PIN_TOUCH_CS, OUTPUT);
    pinMode(PIN_TOUCH_IRQ, INPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    digitalWrite(PIN_TOUCH_SCLK, LOW);

    tft_.init();
    tft_.setRotation(CYD_SCREEN_ROTATION);
    displayRotation_ = CYD_SCREEN_ROTATION;
    tft_.setTextWrap(false, false);
    tft_.fillScreen(Ui::bg());

    prefs_.begin("cydkids", false);
    migrateStorageSchema();
    logStorageUsage("boot");
    applyBrightness();
    loadTouchCalibration();
    mountSd();
    BleBeacon::begin(bleBeaconEnabled());
}

bool Board::sdReady() const {
    return sdMounted_;
}

bool Board::mountSd() {
    sdSpi_.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    sdMounted_ = SD.begin(PIN_SD_CS, sdSpi_, 16000000);
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
    if (index >= MAX_KIDS) return;
    char key[10];
    snprintf(key, sizeof(key), "pname%u", index);
    prefs_.putString(key, name.substring(0, PROFILE_NAME_MAX));
}

uint8_t Board::addKid(const char* name) {
    const uint8_t n = kidCount();
    if (n >= MAX_KIDS) return 0xFF;
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

void Board::removeKid(uint8_t index) {
    const uint8_t n = kidCount();
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
    } else if (oldActive != GUEST_INDEX && oldActive >= kidCount()) {
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
