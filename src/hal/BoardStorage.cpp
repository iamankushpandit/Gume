#include "Board.h"
#include "Watchdog.h"

#include <string.h>

namespace {
constexpr size_t STORAGE_KEY_CAP = 16;

enum class StorageValueKind : uint8_t {
    UInt,
    Blob,
};

struct StorageKeyMap {
    const char* legacy;
    const char* scoped;
    StorageValueKind kind;
    bool copyWorst;
};

constexpr StorageKeyMap STORAGE_KEYS[] = {
    {"tttX", "ttt_x", StorageValueKind::UInt, false},
    {"tttO", "ttt_o", StorageValueKind::UInt, false},
    {"tttDraw", "ttt_draw", StorageValueKind::UInt, false},
    {"memBest", "mem_best", StorageValueKind::UInt, true},
    {"mathBest", "math_best", StorageValueKind::UInt, false},
    {"mathTime", "math_time", StorageValueKind::UInt, false},
    {"multBest", "mult_best", StorageValueKind::UInt, false},
    {"timeBest", "time_best", StorageValueKind::UInt, true},
    {"whackBest", "whack_best", StorageValueKind::UInt, true},
    {"cinnamonBest", "cin_best", StorageValueKind::UInt, true},
    {"microkuBest", "mku_best", StorageValueKind::UInt, true},
    {"shapeBest", "shape_best", StorageValueKind::UInt, true},
    {"countBest", "count_best", StorageValueKind::UInt, true},
    {"moneyBest", "money_best", StorageValueKind::UInt, true},
    {"fracBest", "frac_best", StorageValueKind::UInt, true},
    {"mazeLevel", "maze_level", StorageValueKind::UInt, false},
    {"sortBest", "sort_best", StorageValueKind::UInt, true},
    {"mixBest", "mix_best", StorageValueKind::UInt, true},
    {"oddBest", "odd_best", StorageValueKind::UInt, true},
    {"geoTier", "flags_tier", StorageValueKind::UInt, false},
    {"flagSrs", "flags_srs", StorageValueKind::Blob, false},
    {"stateTier", "states_tier", StorageValueKind::UInt, false},
    {"stateBest", "states_best", StorageValueKind::UInt, true},
    {"stateSrs", "states_srs", StorageValueKind::Blob, false},
    {"sflagTier", "sflag_tier", StorageValueKind::UInt, false},
    {"sflagBest", "sflag_best", StorageValueKind::UInt, true},
    {"smapTier", "smap_tier", StorageValueKind::UInt, false},
    {"smapBest", "smap_best", StorageValueKind::UInt, true},
    {"pctBest", "pct_best", StorageValueKind::UInt, true},
    {"greBest", "gre_best", StorageValueKind::UInt, true},
    {"greProg", "gre_prog", StorageValueKind::Blob, false},
};

constexpr size_t STORAGE_KEY_COUNT = sizeof(STORAGE_KEYS) / sizeof(STORAGE_KEYS[0]);
constexpr size_t MIGRATION_BLOB_CAP = 512;

const StorageKeyMap* findStorageKey(const char* legacy) {
    for (size_t i = 0; i < STORAGE_KEY_COUNT; ++i) {
        if (strcmp(STORAGE_KEYS[i].legacy, legacy) == 0) {
            return &STORAGE_KEYS[i];
        }
    }
    return nullptr;
}

void translateStorageLeaf(char* out, size_t cap, const char* legacy) {
    if (const StorageKeyMap* mapping = findStorageKey(legacy)) {
        snprintf(out, cap, "%s", mapping->scoped);
        return;
    }
    if (strncmp(legacy, "mazeB", 5) == 0) {
        snprintf(out, cap, "maze_b%s", legacy + 5);
        return;
    }
    if (strncmp(legacy, "slideB", 6) == 0) {
        snprintf(out, cap, "slide_b%s", legacy + 6);
        return;
    }
    snprintf(out, cap, "%s", legacy);
}

bool keysDiffer(const char* legacy) {
    char translated[STORAGE_KEY_CAP];
    translateStorageLeaf(translated, sizeof(translated), legacy);
    return strcmp(translated, legacy) != 0;
}

void buildLegacyScopedKeyForProfile(char* out, size_t cap, uint8_t profileIndex, const char* key) {
    snprintf(out, cap, "p%u_%s", profileIndex, key);
}

void buildScopedKeyForProfile(char* out, size_t cap, uint8_t profileIndex, const char* key) {
    char translated[STORAGE_KEY_CAP];
    translateStorageLeaf(translated, sizeof(translated), key);
    snprintf(out, cap, "p%u_%s", profileIndex, translated);
}

bool appendKeySuffix(char* key, size_t cap, char suffix) {
    const size_t len = strnlen(key, cap);
    if (len + 1 >= cap) {
        return false;
    }
    key[len] = suffix;
    key[len + 1] = '\0';
    return true;
}

void migrateUIntKey(Preferences& prefs, const char* oldKey, const char* newKey, bool copyWorst) {
    if (!prefs.isKey(oldKey)) {
        return;
    }
    if (!prefs.isKey(newKey)) {
        prefs.putUInt(newKey, prefs.getUInt(oldKey, 0));
    }
    prefs.remove(oldKey);

    if (!copyWorst) {
        return;
    }

    char oldWorst[STORAGE_KEY_CAP];
    char newWorst[STORAGE_KEY_CAP];
    strncpy(oldWorst, oldKey, sizeof(oldWorst));
    oldWorst[sizeof(oldWorst) - 1] = '\0';
    strncpy(newWorst, newKey, sizeof(newWorst));
    newWorst[sizeof(newWorst) - 1] = '\0';
    if (!appendKeySuffix(oldWorst, sizeof(oldWorst), 'W') ||
        !appendKeySuffix(newWorst, sizeof(newWorst), 'W')) {
        return;
    }
    if (!prefs.isKey(oldWorst)) {
        return;
    }
    if (!prefs.isKey(newWorst)) {
        prefs.putUInt(newWorst, prefs.getUInt(oldWorst, 0));
    }
    prefs.remove(oldWorst);
}

void migrateBlobKey(Preferences& prefs, const char* oldKey, const char* newKey) {
    if (!prefs.isKey(oldKey)) {
        return;
    }

    const size_t len = prefs.getBytesLength(oldKey);
    if (len == 0 || len > MIGRATION_BLOB_CAP) {
        Serial.printf("[storage] skipped blob migration for %s (%u bytes)\n",
                      oldKey, static_cast<unsigned>(len));
        return;
    }

    uint8_t blob[MIGRATION_BLOB_CAP];
    memset(blob, 0, len);
    prefs.getBytes(oldKey, blob, len);
    if (!prefs.isKey(newKey)) {
        prefs.putBytes(newKey, blob, len);
    }
    prefs.remove(oldKey);
}

void migrateProfileKey(Preferences& prefs, uint8_t profileIndex, const char* legacy, StorageValueKind kind, bool copyWorst) {
    if (!keysDiffer(legacy)) {
        return;
    }

    char oldKey[STORAGE_KEY_CAP];
    char newKey[STORAGE_KEY_CAP];
    buildLegacyScopedKeyForProfile(oldKey, sizeof(oldKey), profileIndex, legacy);
    buildScopedKeyForProfile(newKey, sizeof(newKey), profileIndex, legacy);
    if (strcmp(oldKey, newKey) == 0) {
        return;
    }

    if (kind == StorageValueKind::Blob) {
        migrateBlobKey(prefs, oldKey, newKey);
    } else {
        migrateUIntKey(prefs, oldKey, newKey, copyWorst);
    }
}

void migrateDynamicScoreRange(Preferences& prefs, uint8_t profileIndex, const char* legacyPrefix,
                              const char* newPrefix, uint8_t start, uint8_t endInclusive, bool copyWorst) {
    char legacyLeaf[12];
    for (uint8_t i = start; i <= endInclusive; ++i) {
        snprintf(legacyLeaf, sizeof(legacyLeaf), "%s%u", legacyPrefix, i);
        char oldKey[STORAGE_KEY_CAP];
        char newKey[STORAGE_KEY_CAP];
        buildLegacyScopedKeyForProfile(oldKey, sizeof(oldKey), profileIndex, legacyLeaf);
        snprintf(newKey, sizeof(newKey), "p%u_%s%u", profileIndex, newPrefix, i);
        migrateUIntKey(prefs, oldKey, newKey, copyWorst);
    }
}
}

void Board::migrateStorageSchema() {
    const uint16_t stored = prefs_.getUShort("schema", 0);
    if (stored >= STORAGE_SCHEMA_VERSION) {
        return;
    }

    Serial.printf("[storage] migrating schema %u -> %u\n", stored, STORAGE_SCHEMA_VERSION);
    for (uint8_t profile = 0; profile < MAX_KIDS; ++profile) {
        for (size_t i = 0; i < STORAGE_KEY_COUNT; ++i) {
            migrateProfileKey(prefs_, profile, STORAGE_KEYS[i].legacy,
                              STORAGE_KEYS[i].kind, STORAGE_KEYS[i].copyWorst);
        }
        migrateDynamicScoreRange(prefs_, profile, "mazeB", "maze_b", 0, 99, true);
        migrateDynamicScoreRange(prefs_, profile, "slideB", "slide_b", 2, 3, true);
    }
    prefs_.putUShort("schema", STORAGE_SCHEMA_VERSION);
}

void Board::legacyScopedKeyForProfile(char* out, size_t cap, uint8_t profileIndex, const char* key) {
    snprintf(out, cap, "p%u_%s", profileIndex, key);
}

void Board::scopedKeyForProfile(char* out, size_t cap, uint8_t profileIndex, const char* key) {
    char translated[STORAGE_KEY_CAP];
    translateStorageLeaf(translated, sizeof(translated), key);
    snprintf(out, cap, "p%u_%s", profileIndex, translated);
}

void Board::scopedKey(char* out, size_t cap, const char* key) {
    scopedKeyForProfile(out, cap, activeProfile(), key);
}

uint8_t Board::activeProfile() {
    if (!profileCached_) {
        const uint8_t v = prefs_.getUChar("profile", GUEST_INDEX);
        cachedProfile_ = (v == GUEST_INDEX || v >= kidCount()) ? GUEST_INDEX : v;
        profileCached_ = true;
    }
    return cachedProfile_;
}

void Board::setActiveProfile(uint8_t index) {
    if (index != GUEST_INDEX && index >= kidCount()) index = GUEST_INDEX;
    prefs_.putUChar("profile", index);
    cachedProfile_ = index;
    profileCached_ = true;
    visibilityCached_ = false;
}

uint8_t Board::kidCount() {
    const uint8_t n = prefs_.getUChar("kids", 0);
    return n > MAX_KIDS ? MAX_KIDS : n;
}

uint8_t Board::adminProfileIndex() {
    if (!adminIdxCached_) {
        cachedAdminIdx_ = prefs_.getUChar("admin_idx", GUEST_INDEX);
        adminIdxCached_ = true;
    }
    return cachedAdminIdx_;
}

void Board::setAdminProfileIndex(uint8_t index) {
    if (index != GUEST_INDEX && index >= kidCount()) index = GUEST_INDEX;
    prefs_.putUChar("admin_idx", index);
    cachedAdminIdx_ = index;
    adminIdxCached_ = true;
}

uint16_t Board::adminPin() {
    if (!adminPinCached_) {
        cachedAdminPin_ = prefs_.getUShort("admin_pin", 0);
        adminPinCached_ = true;
    }
    return cachedAdminPin_;
}

void Board::setAdminPin(uint16_t pin) {
    if (pin > 9999) pin = 9999;
    prefs_.putUShort("admin_pin", pin);
    cachedAdminPin_ = pin;
    adminPinCached_ = true;
}

bool Board::verifyAdminPin(uint16_t pin) {
    return adminPin() == pin;
}

bool Board::isAdminProfile(uint8_t profileIndex) {
    return profileIndex != GUEST_INDEX && profileIndex == adminProfileIndex();
}

void Board::factoryReset() {
    Watchdog::pause();
    Serial.println("[reset] erasing all stored data");
    prefs_.clear();
    prefs_.end();
    ESP.restart();
}

uint32_t Board::getScore(const char* key, uint32_t fallback) {
    char scoped[STORAGE_KEY_CAP];
    scopedKey(scoped, sizeof(scoped), key);
    return prefs_.getUInt(scoped, fallback);
}

void Board::setScore(const char* key, uint32_t value) {
    if (isGuest()) return;
    char scoped[STORAGE_KEY_CAP];
    scopedKey(scoped, sizeof(scoped), key);
    prefs_.putUInt(scoped, value);
}

bool Board::saveBestScore(const char* key, uint32_t value, bool lowerIsBetter) {
    if (isGuest()) return false;
    char scoped[STORAGE_KEY_CAP];
    char scopedWorst[STORAGE_KEY_CAP];
    scopedKey(scoped, sizeof(scoped), key);
    strncpy(scopedWorst, scoped, sizeof(scopedWorst));
    scopedWorst[sizeof(scopedWorst) - 1] = '\0';
    if (!appendKeySuffix(scopedWorst, sizeof(scopedWorst), 'W')) {
        return false;
    }

    const uint32_t missing = lowerIsBetter ? UINT32_MAX : 0;
    const uint32_t current = prefs_.getUInt(scoped, missing);
    const bool shouldSave = lowerIsBetter ? value < current : value > current;
    if (shouldSave) {
        prefs_.putUInt(scoped, value);
    }

    const uint32_t worstMissing = lowerIsBetter ? 0 : UINT32_MAX;
    const uint32_t worstCurrent = prefs_.getUInt(scopedWorst, worstMissing);
    const bool saveWorst = lowerIsBetter ? value > worstCurrent : value < worstCurrent;
    if (saveWorst) {
        prefs_.putUInt(scopedWorst, value);
    }
    return shouldSave;
}

uint32_t Board::worstScore(const char* key, uint32_t fallback) {
    char scoped[STORAGE_KEY_CAP];
    char scopedWorst[STORAGE_KEY_CAP];
    scopedKey(scoped, sizeof(scoped), key);
    strncpy(scopedWorst, scoped, sizeof(scopedWorst));
    scopedWorst[sizeof(scopedWorst) - 1] = '\0';
    if (!appendKeySuffix(scopedWorst, sizeof(scopedWorst), 'W')) {
        return fallback;
    }
    return prefs_.getUInt(scopedWorst, fallback);
}

bool Board::hasScore(const char* key) {
    char scoped[STORAGE_KEY_CAP];
    scopedKey(scoped, sizeof(scoped), key);
    return prefs_.isKey(scoped);
}

uint32_t Board::scoreFor(uint8_t profileIndex, const char* key, uint32_t fallback) {
    if (profileIndex >= MAX_KIDS) return fallback;
    char scoped[STORAGE_KEY_CAP];
    scopedKeyForProfile(scoped, sizeof(scoped), profileIndex, key);
    return prefs_.getUInt(scoped, fallback);
}

bool Board::hasScoreFor(uint8_t profileIndex, const char* key) {
    if (profileIndex >= MAX_KIDS) return false;
    char scoped[STORAGE_KEY_CAP];
    scopedKeyForProfile(scoped, sizeof(scoped), profileIndex, key);
    return prefs_.isKey(scoped);
}

void Board::loadBlob(const char* key, void* dst, size_t len) {
    char scoped[STORAGE_KEY_CAP];
    scopedKey(scoped, sizeof(scoped), key);
    if (!prefs_.isKey(scoped)) return;
    if (prefs_.getBytesLength(scoped) != len) return;
    prefs_.getBytes(scoped, dst, len);
}

void Board::saveBlob(const char* key, const void* src, size_t len) {
    if (isGuest()) return;
    char scoped[STORAGE_KEY_CAP];
    scopedKey(scoped, sizeof(scoped), key);
    prefs_.putBytes(scoped, src, len);
}
