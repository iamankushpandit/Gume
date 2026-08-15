#include "Board.h"

#include <nvs.h>
#include <string.h>

namespace {
constexpr size_t STORAGE_KEY_CAP = 16;
constexpr const char* APP_NVS_NAMESPACE = "cydkids";
constexpr const char* WATCHDOG_NVS_NAMESPACE = "cydwdt";
constexpr size_t PROFILE_MOVE_BATCH_CAP = 24;
constexpr size_t PROFILE_MOVE_BLOB_CAP = 512;
constexpr size_t PROFILE_MOVE_STRING_CAP = 64;

struct ProfileNvsEntry {
    char key[STORAGE_KEY_CAP] = {0};
    nvs_type_t type = NVS_TYPE_ANY;
};

ProfileNvsEntry profileMoveBatch[PROFILE_MOVE_BATCH_CAP];
uint8_t profileMoveBlob[PROFILE_MOVE_BLOB_CAP];
char profileMoveString[PROFILE_MOVE_STRING_CAP];

bool buildProfilePrefix(char* out, size_t cap, uint8_t profileIndex) {
    return snprintf(out, cap, "p%u_", profileIndex) < static_cast<int>(cap);
}

bool buildProfileNameKey(char* out, size_t cap, uint8_t profileIndex) {
    return snprintf(out, cap, "pname%u", profileIndex) < static_cast<int>(cap);
}

void defaultProfileName(char* out, size_t cap, uint8_t profileIndex) {
    if (cap == 0) {
        return;
    }
    constexpr const char* DEFAULT_NAMES[] = {
        "Player 1", "Player 2", "Player 3", "Player 4", "Player 5"
    };
    const char* name = profileIndex < (sizeof(DEFAULT_NAMES) / sizeof(DEFAULT_NAMES[0]))
        ? DEFAULT_NAMES[profileIndex]
        : DEFAULT_NAMES[0];
    strncpy(out, name, cap);
    out[cap - 1] = '\0';
}

void readProfileNameForMove(uint8_t profileIndex, char* out, size_t cap) {
    if (cap == 0) {
        return;
    }
    defaultProfileName(out, cap, profileIndex);

    char key[STORAGE_KEY_CAP];
    if (!buildProfileNameKey(key, sizeof(key), profileIndex)) {
        return;
    }

    nvs_handle_t handle = 0;
    if (nvs_open(APP_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    size_t len = cap;
    if (nvs_get_str(handle, key, out, &len) != ESP_OK || out[0] == '\0') {
        defaultProfileName(out, cap, profileIndex);
    }
    nvs_close(handle);
}

void writeProfileNameForMove(uint8_t profileIndex, const char* name) {
    char key[STORAGE_KEY_CAP];
    if (!buildProfileNameKey(key, sizeof(key), profileIndex)) {
        return;
    }

    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (openErr != ESP_OK) {
        Serial.printf("[storage] cannot write moved profile name %u (%d)\n",
                      profileIndex, static_cast<int>(openErr));
        return;
    }
    const esp_err_t writeErr = nvs_set_str(handle, key, name);
    if (writeErr != ESP_OK) {
        Serial.printf("[storage] failed to write moved profile name %s (%d)\n",
                      key, static_cast<int>(writeErr));
    }
    nvs_commit(handle);
    nvs_close(handle);
}

size_t collectProfileEntries(uint8_t profileIndex, ProfileNvsEntry* entries, size_t cap) {
    if (cap == 0) {
        return 0;
    }

    char prefix[4];
    if (!buildProfilePrefix(prefix, sizeof(prefix), profileIndex)) {
        return 0;
    }
    const size_t prefixLen = strlen(prefix);
    size_t count = 0;

    nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, APP_NVS_NAMESPACE, NVS_TYPE_ANY);
    while (it != nullptr) {
        nvs_entry_info_t info{};
        nvs_entry_info(it, &info);
        if (strncmp(info.key, prefix, prefixLen) == 0) {
            strncpy(entries[count].key, info.key, sizeof(entries[count].key));
            entries[count].key[sizeof(entries[count].key) - 1] = '\0';
            entries[count].type = info.type;
            ++count;
            if (count == cap) {
                nvs_release_iterator(it);
                break;
            }
        }
        it = nvs_entry_next(it);
    }
    return count;
}

bool buildMovedProfileKey(char* out, size_t cap, uint8_t fromSlot, uint8_t toSlot,
                          const char* sourceKey) {
    char fromPrefix[4];
    char toPrefix[4];
    if (!buildProfilePrefix(fromPrefix, sizeof(fromPrefix), fromSlot) ||
        !buildProfilePrefix(toPrefix, sizeof(toPrefix), toSlot)) {
        return false;
    }
    const size_t fromLen = strlen(fromPrefix);
    if (strncmp(sourceKey, fromPrefix, fromLen) != 0) {
        return false;
    }
    return snprintf(out, cap, "%s%s", toPrefix, sourceKey + fromLen) < static_cast<int>(cap);
}

esp_err_t copyNvsEntry(nvs_handle_t handle, const char* sourceKey, const char* destKey,
                       nvs_type_t type) {
    switch (type) {
        case NVS_TYPE_U8: {
            uint8_t value = 0;
            esp_err_t err = nvs_get_u8(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_u8(handle, destKey, value) : err;
        }
        case NVS_TYPE_I8: {
            int8_t value = 0;
            esp_err_t err = nvs_get_i8(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_i8(handle, destKey, value) : err;
        }
        case NVS_TYPE_U16: {
            uint16_t value = 0;
            esp_err_t err = nvs_get_u16(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_u16(handle, destKey, value) : err;
        }
        case NVS_TYPE_I16: {
            int16_t value = 0;
            esp_err_t err = nvs_get_i16(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_i16(handle, destKey, value) : err;
        }
        case NVS_TYPE_U32: {
            uint32_t value = 0;
            esp_err_t err = nvs_get_u32(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_u32(handle, destKey, value) : err;
        }
        case NVS_TYPE_I32: {
            int32_t value = 0;
            esp_err_t err = nvs_get_i32(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_i32(handle, destKey, value) : err;
        }
        case NVS_TYPE_U64: {
            uint64_t value = 0;
            esp_err_t err = nvs_get_u64(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_u64(handle, destKey, value) : err;
        }
        case NVS_TYPE_I64: {
            int64_t value = 0;
            esp_err_t err = nvs_get_i64(handle, sourceKey, &value);
            return err == ESP_OK ? nvs_set_i64(handle, destKey, value) : err;
        }
        case NVS_TYPE_STR: {
            size_t len = 0;
            esp_err_t err = nvs_get_str(handle, sourceKey, nullptr, &len);
            if (err != ESP_OK) {
                return err;
            }
            if (len == 0 || len > sizeof(profileMoveString)) {
                Serial.printf("[storage] skipped profile string %s (%u bytes)\n",
                              sourceKey, static_cast<unsigned>(len));
                return ESP_ERR_NVS_VALUE_TOO_LONG;
            }
            err = nvs_get_str(handle, sourceKey, profileMoveString, &len);
            return err == ESP_OK ? nvs_set_str(handle, destKey, profileMoveString) : err;
        }
        case NVS_TYPE_BLOB: {
            size_t len = 0;
            esp_err_t err = nvs_get_blob(handle, sourceKey, nullptr, &len);
            if (err != ESP_OK) {
                return err;
            }
            if (len == 0 || len > sizeof(profileMoveBlob)) {
                Serial.printf("[storage] skipped profile blob %s (%u bytes)\n",
                              sourceKey, static_cast<unsigned>(len));
                return ESP_ERR_NVS_VALUE_TOO_LONG;
            }
            err = nvs_get_blob(handle, sourceKey, profileMoveBlob, &len);
            return err == ESP_OK ? nvs_set_blob(handle, destKey, profileMoveBlob, len) : err;
        }
        default:
            return ESP_ERR_NVS_TYPE_MISMATCH;
    }
}

void eraseProfilePrefixEntries(nvs_handle_t handle, uint8_t profileIndex) {
    while (true) {
        const size_t count = collectProfileEntries(profileIndex, profileMoveBatch,
                                                   PROFILE_MOVE_BATCH_CAP);
        if (count == 0) {
            return;
        }
        bool removedAny = false;
        for (size_t i = 0; i < count; ++i) {
            const esp_err_t err = nvs_erase_key(handle, profileMoveBatch[i].key);
            if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
                removedAny = true;
            } else {
                Serial.printf("[storage] failed to erase %s (%d)\n",
                              profileMoveBatch[i].key, static_cast<int>(err));
            }
        }
        nvs_commit(handle);
        if (!removedAny) {
            return;
        }
    }
}

void transferProfilePrefixEntries(nvs_handle_t handle, uint8_t fromSlot, uint8_t toSlot) {
    while (true) {
        const size_t count = collectProfileEntries(fromSlot, profileMoveBatch,
                                                   PROFILE_MOVE_BATCH_CAP);
        if (count == 0) {
            return;
        }

        bool removedAny = false;
        for (size_t i = 0; i < count; ++i) {
            char destKey[STORAGE_KEY_CAP];
            if (!buildMovedProfileKey(destKey, sizeof(destKey), fromSlot, toSlot,
                                      profileMoveBatch[i].key)) {
                Serial.printf("[storage] cannot move profile key %s\n", profileMoveBatch[i].key);
            } else {
                const esp_err_t copyErr = copyNvsEntry(handle, profileMoveBatch[i].key,
                                                       destKey, profileMoveBatch[i].type);
                if (copyErr != ESP_OK) {
                    Serial.printf("[storage] dropped %s while moving profile %u -> %u (%d)\n",
                                  profileMoveBatch[i].key, fromSlot, toSlot,
                                  static_cast<int>(copyErr));
                }
            }

            const esp_err_t eraseErr = nvs_erase_key(handle, profileMoveBatch[i].key);
            if (eraseErr == ESP_OK || eraseErr == ESP_ERR_NVS_NOT_FOUND) {
                removedAny = true;
            } else {
                Serial.printf("[storage] failed to erase moved key %s (%d)\n",
                              profileMoveBatch[i].key, static_cast<int>(eraseErr));
            }
        }
        nvs_commit(handle);
        if (!removedAny) {
            return;
        }
    }
}

bool namespaceEntryCount(const char* name, uint32_t& outEntries) {
    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(name, NVS_READONLY, &handle);
    if (openErr != ESP_OK) {
        outEntries = 0;
        return false;
    }

    size_t used = 0;
    const bool ok = nvs_get_used_entry_count(handle, &used) == ESP_OK;
    nvs_close(handle);
    outEntries = ok ? static_cast<uint32_t>(used) : 0;
    return ok;
}
}

Board::StorageTelemetry Board::storageTelemetry() {
    StorageTelemetry telemetry{};

    nvs_stats_t stats{};
    if (nvs_get_stats(NVS_DEFAULT_PART_NAME, &stats) == ESP_OK) {
        telemetry.available = true;
        telemetry.usedEntries = static_cast<uint32_t>(stats.used_entries);
        telemetry.freeEntries = static_cast<uint32_t>(stats.free_entries);
        telemetry.totalEntries = static_cast<uint32_t>(stats.total_entries);
        telemetry.namespaceCount = static_cast<uint32_t>(stats.namespace_count);
    }

    telemetry.appNamespaceAvailable = namespaceEntryCount(APP_NVS_NAMESPACE,
                                                          telemetry.appEntries);
    telemetry.watchdogNamespaceAvailable = namespaceEntryCount(WATCHDOG_NVS_NAMESPACE,
                                                               telemetry.watchdogEntries);
    return telemetry;
}

void Board::logStorageUsage(const char* context) {
    const StorageTelemetry telemetry = storageTelemetry();
    if (!telemetry.available) {
        Serial.printf("[storage] %s: NVS stats unavailable\n", context ? context : "status");
        return;
    }

    const uint32_t pct = telemetry.totalEntries == 0
        ? 0
        : ((telemetry.usedEntries * 100UL) + telemetry.totalEntries / 2UL) /
              telemetry.totalEntries;
    Serial.printf("[storage] %s: NVS %lu/%lu entries (%lu%%), free %lu, namespaces %lu, "
                  "%s=%lu, %s=%lu\n",
                  context ? context : "status",
                  static_cast<unsigned long>(telemetry.usedEntries),
                  static_cast<unsigned long>(telemetry.totalEntries),
                  static_cast<unsigned long>(pct),
                  static_cast<unsigned long>(telemetry.freeEntries),
                  static_cast<unsigned long>(telemetry.namespaceCount),
                  APP_NVS_NAMESPACE,
                  static_cast<unsigned long>(telemetry.appEntries),
                  WATCHDOG_NVS_NAMESPACE,
                  static_cast<unsigned long>(telemetry.watchdogEntries));
    if (pct >= STORAGE_CRITICAL_PERCENT) {
        Serial.printf("[storage] critical: NVS usage is above %u%%\n",
                      static_cast<unsigned>(STORAGE_CRITICAL_PERCENT));
    } else if (pct >= STORAGE_WARN_PERCENT) {
        Serial.printf("[storage] warning: NVS usage is above %u%%\n",
                      static_cast<unsigned>(STORAGE_WARN_PERCENT));
    }
}

void Board::clearProfileSlot(uint8_t slot) {
    if (slot >= MAX_KIDS) {
        return;
    }

    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (openErr != ESP_OK) {
        Serial.printf("[storage] cannot clear profile %u (%d)\n",
                      slot, static_cast<int>(openErr));
        return;
    }

    eraseProfilePrefixEntries(handle, slot);
    char nameKey[STORAGE_KEY_CAP];
    if (buildProfileNameKey(nameKey, sizeof(nameKey), slot)) {
        const esp_err_t err = nvs_erase_key(handle, nameKey);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
            Serial.printf("[storage] failed to erase profile name %s (%d)\n",
                          nameKey, static_cast<int>(err));
        }
    }
    nvs_commit(handle);
    nvs_close(handle);
    visibilityCached_ = false;
}

void Board::moveProfileSlot(uint8_t fromSlot, uint8_t toSlot) {
    if (fromSlot >= MAX_KIDS || toSlot >= MAX_KIDS || fromSlot == toSlot) {
        return;
    }

    char name[PROFILE_NAME_MAX + 1];
    readProfileNameForMove(fromSlot, name, sizeof(name));
    clearProfileSlot(toSlot);

    nvs_handle_t handle = 0;
    const esp_err_t openErr = nvs_open(APP_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (openErr != ESP_OK) {
        Serial.printf("[storage] cannot move profile %u -> %u (%d)\n",
                      fromSlot, toSlot, static_cast<int>(openErr));
        return;
    }
    transferProfilePrefixEntries(handle, fromSlot, toSlot);
    nvs_close(handle);

    writeProfileNameForMove(toSlot, name);
    clearProfileSlot(fromSlot);
    visibilityCached_ = false;
}
