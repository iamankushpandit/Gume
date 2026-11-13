// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* NVS for the host: one key-value store, typed the way ESP-IDF types it,
 * behind both the nvs_* C API and the Preferences class. See host/shim/nvs.h.
 *
 * Two device behaviours are kept because firmware depends on them. A read
 * with the wrong type fails rather than converting -- Preferences turns that
 * failure into the caller's fallback -- and a key or namespace longer than 15
 * characters is refused, because a key that only works on the host is a
 * defect waiting for the device.
 *
 * The store lives in RAM and is written to <data dir>/nvs.txt, whole, after
 * every change. It is a few hundred entries; rewriting it is cheaper than
 * being clever, and a crash cannot leave it half-written because the new
 * copy is renamed over the old.
 *
 * The new copy is fsync'd BEFORE the rename, and the directory after it.
 * Without that, the rename can reach the card while the data does not: the
 * first RG40XXV run lost every profile that way, an nvs.txt of 0 bytes after
 * a power-off, because exFAT on an SD card holds writes back for seconds.
 * NVS on a board commits as it writes; this is what makes the file do the
 * same. */

#include <Preferences.h>
#include <nvs.h>

#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "HostPlatform.h"
#include "HostSecret.h"

namespace {

/* The values sealed before they reach the card (see HostSecret.h), and what
 * happens when no key can be made. The Wi-Fi name and password are then kept
 * in memory only -- re-entered after a restart rather than left readable. The
 * admin PIN is kept readable instead: an unsaved PIN falls back to the public
 * default, which is worse than one that can be read off the card. */
struct SecretKey {
    const char* name;
    bool dropWithoutKey;
};
constexpr SecretKey SECRET_KEYS[] = {
    {"wifiSsid", true},
    {"wifiPass", true},
    {"admin_pin", false},
};
constexpr const char* SEALED_TYPE = "se";   // in the type column of nvs.txt

const SecretKey* secretFor(const std::string& key) {
    for (const SecretKey& s : SECRET_KEYS) {
        if (key == s.name) return &s;
    }
    return nullptr;
}

constexpr size_t NAME_MAX_CHARS = 15;
constexpr size_t TOTAL_ENTRIES = 630;   // the device's default 20 KB partition, roughly

struct Entry {
    nvs_type_t type = NVS_TYPE_ANY;
    std::vector<uint8_t> data;
};

using Namespace = std::map<std::string, Entry>;
std::map<std::string, Namespace> store;
std::map<nvs_handle_t, std::pair<std::string, bool>> handles;   // ns, writable
nvs_handle_t nextHandle = 1;
bool loaded = false;
std::recursive_mutex storeMutex;

std::string filePath() { return HostPlatform::dataDir() + "/nvs.txt"; }

void save() {
    const std::string path = filePath();
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "w");
    if (!f) return;
    for (const auto& ns : store) {
        for (const auto& kv : ns.second) {
            if (const SecretKey* secret = secretFor(kv.first)) {
                /* Sealed with its type, under its namespaced name, so a value
                 * copied to another key does not open. */
                std::vector<uint8_t> plain;
                plain.push_back(static_cast<uint8_t>(kv.second.type));
                plain.insert(plain.end(), kv.second.data.begin(), kv.second.data.end());
                std::vector<uint8_t> sealed;
                if (HostSecret::seal(ns.first + "/" + kv.first, plain, sealed)) {
                    std::fprintf(f, "%s\t%s\t%s\t", ns.first.c_str(), kv.first.c_str(), SEALED_TYPE);
                    for (uint8_t b : sealed) std::fprintf(f, "%02x", b);
                    std::fputc('\n', f);
                    continue;
                }
                if (secret->dropWithoutKey) continue;   // memory only, this run
            }
            std::fprintf(f, "%s\t%s\t%02x\t", ns.first.c_str(), kv.first.c_str(),
                         static_cast<unsigned>(kv.second.type));
            for (uint8_t b : kv.second.data) std::fprintf(f, "%02x", b);
            std::fputc('\n', f);
        }
    }
    const bool written = std::fflush(f) == 0 && ::fsync(::fileno(f)) == 0;
    std::fclose(f);
    if (!written) return;   // keep the old copy rather than rename a short one over it
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return;
    const std::string dir = HostPlatform::dataDir();
    const int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
}

void zeroInPlace(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) return;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    static const char zeros[512] = {0};
    for (long left = size; left > 0; left -= static_cast<long>(sizeof(zeros))) {
        std::fwrite(zeros, 1, left < static_cast<long>(sizeof(zeros)) ? left : sizeof(zeros), f);
    }
    std::fflush(f);
    ::fsync(::fileno(f));
    std::fclose(f);
}

void load() {
    if (loaded) return;
    loaded = true;
    FILE* f = std::fopen(filePath().c_str(), "r");
    if (!f) return;
    char line[4096];
    bool readableSecret = false;      // any secret stored readable
    bool readableDroppable = false;   // ...one that must not be, key or no key
    while (std::fgets(line, sizeof(line), f)) {
        char* ns = std::strtok(line, "\t\n");
        char* key = std::strtok(nullptr, "\t\n");
        char* type = std::strtok(nullptr, "\t\n");
        char* hex = std::strtok(nullptr, "\t\n");
        if (!ns || !key || !type) continue;
        std::vector<uint8_t> bytes;
        for (size_t i = 0; hex && hex[i] && hex[i + 1]; i += 2) {
            const char pair[3] = {hex[i], hex[i + 1], 0};
            bytes.push_back(static_cast<uint8_t>(std::strtoul(pair, nullptr, 16)));
        }
        Entry e;
        if (std::strcmp(type, SEALED_TYPE) == 0) {
            std::vector<uint8_t> plain;
            std::string name(ns);
            name.append("/").append(key);
            if (!HostSecret::open(name, bytes, plain) || plain.empty()) {
                /* Another console's card, or a damaged value: forgotten, so
                 * the owner is asked again rather than shown nonsense. */
                std::printf("[secrets] a saved value could not be opened on this console; "
                            "it will need entering again\n");
                std::fflush(stdout);
                continue;
            }
            e.type = static_cast<nvs_type_t>(plain[0]);
            e.data.assign(plain.begin() + 1, plain.end());
        } else {
            e.type = static_cast<nvs_type_t>(std::strtoul(type, nullptr, 16));
            e.data = bytes;
            if (const SecretKey* secret = secretFor(key)) {
                readableSecret = true;
                readableDroppable = readableDroppable || secret->dropWithoutKey;
            }
        }
        store[ns][key] = e;
    }
    std::fclose(f);
    /* A file from before sealing, or from a run with no key: rewrite it now,
     * so a readable copy does not sit on the card until the next change --
     * and zero the old one in place first, because a rename only frees its
     * clusters and the bytes would stay on the card to be recovered. With no
     * key, the rewrite leaves the Wi-Fi network out (it stays in memory for
     * this run) and keeps the PIN readable, as SECRET_KEYS says. */
    if (HostSecret::available() ? readableSecret : readableDroppable) {
        zeroInPlace(filePath());
        save();
    }
}

Namespace* nsFor(nvs_handle_t h, bool forWrite, esp_err_t& err) {
    const auto it = handles.find(h);
    if (it == handles.end()) {
        err = ESP_ERR_NVS_INVALID_HANDLE;
        return nullptr;
    }
    if (forWrite && !it->second.second) {
        err = ESP_ERR_NVS_READ_ONLY;
        return nullptr;
    }
    err = ESP_OK;
    return &store[it->second.first];
}

esp_err_t getRaw(nvs_handle_t h, const char* key, nvs_type_t type, void* out, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    esp_err_t err;
    Namespace* ns = nsFor(h, false, err);
    if (!ns) return err;
    const auto it = ns->find(key ? key : "");
    if (it == ns->end()) return ESP_ERR_NVS_NOT_FOUND;
    if (it->second.type != type) return ESP_ERR_NVS_TYPE_MISMATCH;
    if (it->second.data.size() != size) return ESP_ERR_NVS_TYPE_MISMATCH;
    std::memcpy(out, it->second.data.data(), size);
    return ESP_OK;
}

esp_err_t setRaw(nvs_handle_t h, const char* key, nvs_type_t type, const void* data, size_t size) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    if (!key || std::strlen(key) > NAME_MAX_CHARS) return ESP_ERR_INVALID_ARG;
    esp_err_t err;
    Namespace* ns = nsFor(h, true, err);
    if (!ns) return err;
    Entry& e = (*ns)[key];
    e.type = type;
    const auto* bytes = static_cast<const uint8_t*>(data);
    e.data.assign(bytes, bytes + size);
    save();
    return ESP_OK;
}

esp_err_t getVar(nvs_handle_t h, const char* key, nvs_type_t type, void* out, size_t* len) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    esp_err_t err;
    Namespace* ns = nsFor(h, false, err);
    if (!ns) return err;
    const auto it = ns->find(key ? key : "");
    if (it == ns->end()) return ESP_ERR_NVS_NOT_FOUND;
    if (it->second.type != type) return ESP_ERR_NVS_TYPE_MISMATCH;
    const size_t need = it->second.data.size();
    if (!out) {
        *len = need;
        return ESP_OK;
    }
    if (*len < need) return ESP_ERR_NVS_INVALID_LENGTH;
    std::memcpy(out, it->second.data.data(), need);
    *len = need;
    return ESP_OK;
}

}   // namespace

/* ------------------------------------------------------------- nvs_* */

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    if (!ns || std::strlen(ns) > NAME_MAX_CHARS) return ESP_ERR_INVALID_ARG;
    load();
    if (mode == NVS_READONLY && store.find(ns) == store.end()) return ESP_ERR_NVS_NOT_FOUND;
    const nvs_handle_t h = nextHandle++;
    handles[h] = {ns, mode == NVS_READWRITE};
    *out = h;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    handles.erase(handle);
}

esp_err_t nvs_commit(nvs_handle_t) { return ESP_OK; }

esp_err_t nvs_erase_key(nvs_handle_t h, const char* key) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    esp_err_t err;
    Namespace* ns = nsFor(h, true, err);
    if (!ns) return err;
    if (ns->erase(key ? key : "") == 0) return ESP_ERR_NVS_NOT_FOUND;
    save();
    return ESP_OK;
}

esp_err_t nvs_erase_all(nvs_handle_t h) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    esp_err_t err;
    Namespace* ns = nsFor(h, true, err);
    if (!ns) return err;
    ns->clear();
    save();
    return ESP_OK;
}

#define HOST_NVS_SCALAR(suffix, ctype, code)                                          \
    esp_err_t nvs_get_##suffix(nvs_handle_t h, const char* key, ctype* out) {         \
        return getRaw(h, key, code, out, sizeof(ctype));                              \
    }                                                                                 \
    esp_err_t nvs_set_##suffix(nvs_handle_t h, const char* key, ctype value) {        \
        return setRaw(h, key, code, &value, sizeof(ctype));                           \
    }

HOST_NVS_SCALAR(u8, uint8_t, NVS_TYPE_U8)
HOST_NVS_SCALAR(i8, int8_t, NVS_TYPE_I8)
HOST_NVS_SCALAR(u16, uint16_t, NVS_TYPE_U16)
HOST_NVS_SCALAR(i16, int16_t, NVS_TYPE_I16)
HOST_NVS_SCALAR(u32, uint32_t, NVS_TYPE_U32)
HOST_NVS_SCALAR(i32, int32_t, NVS_TYPE_I32)
HOST_NVS_SCALAR(u64, uint64_t, NVS_TYPE_U64)
HOST_NVS_SCALAR(i64, int64_t, NVS_TYPE_I64)

#undef HOST_NVS_SCALAR

esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len) {
    return getVar(h, key, NVS_TYPE_STR, out, len);
}

esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value) {
    if (!value) return ESP_ERR_INVALID_ARG;
    return setRaw(h, key, NVS_TYPE_STR, value, std::strlen(value) + 1);
}

esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len) {
    return getVar(h, key, NVS_TYPE_BLOB, out, len);
}

esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len) {
    return setRaw(h, key, NVS_TYPE_BLOB, value, len);
}

struct HostNvsIterator {
    std::vector<nvs_entry_info_t> entries;
    size_t at = 0;
};

nvs_iterator_t nvs_entry_find(const char*, const char* ns, nvs_type_t type) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    load();
    auto* it = new HostNvsIterator();   // owned by the caller until release or exhaustion
    for (const auto& n : store) {
        if (ns && n.first != ns) continue;
        for (const auto& kv : n.second) {
            if (type != NVS_TYPE_ANY && kv.second.type != type) continue;
            nvs_entry_info_t info{};
            std::strncpy(info.namespace_name, n.first.c_str(), sizeof(info.namespace_name) - 1);
            std::strncpy(info.key, kv.first.c_str(), sizeof(info.key) - 1);
            info.type = kv.second.type;
            it->entries.push_back(info);
        }
    }
    if (it->entries.empty()) {
        delete it;
        return nullptr;
    }
    return it;
}

nvs_iterator_t nvs_entry_next(nvs_iterator_t it) {
    if (!it) return nullptr;
    if (++it->at >= it->entries.size()) {
        delete it;
        return nullptr;
    }
    return it;
}

void nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* out) {
    if (it && out) *out = it->entries[it->at];
}

void nvs_release_iterator(nvs_iterator_t it) { delete it; }

esp_err_t nvs_get_stats(const char*, nvs_stats_t* stats) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    load();
    size_t used = 0;
    for (const auto& n : store) used += n.second.size() + 1;   // +1: the namespace entry
    stats->used_entries = used;
    stats->total_entries = TOTAL_ENTRIES;
    stats->free_entries = used < TOTAL_ENTRIES ? TOTAL_ENTRIES - used : 0;
    stats->namespace_count = store.size();
    return ESP_OK;
}

esp_err_t nvs_get_used_entry_count(nvs_handle_t h, size_t* used) {
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    esp_err_t err;
    Namespace* ns = nsFor(h, false, err);
    if (!ns) return err;
    *used = ns->size();
    return ESP_OK;
}

/* ------------------------------------------------------------- Preferences */

bool Preferences::begin(const char* name, bool readOnly, const char*) {
    if (started_) return false;
    /* Preferences opens read-write even to read, so a namespace that does not
     * exist yet is created rather than refused. */
    if (nvs_open(name, NVS_READWRITE, &handle_) != ESP_OK) return false;
    readOnly_ = readOnly;
    started_ = true;
    return true;
}

void Preferences::end() {
    if (!started_) return;
    nvs_close(handle_);
    started_ = false;
}

bool Preferences::clear() { return started_ && !readOnly_ && nvs_erase_all(handle_) == ESP_OK; }

bool Preferences::remove(const char* key) {
    return started_ && !readOnly_ && nvs_erase_key(handle_, key) == ESP_OK;
}

bool Preferences::isKey(const char* key) { return getType(key) != PT_INVALID; }

PreferenceType Preferences::getType(const char* key) {
    if (!started_ || !key) return PT_INVALID;
    std::lock_guard<std::recursive_mutex> lock(storeMutex);
    esp_err_t err;
    Namespace* ns = nsFor(handle_, false, err);
    if (!ns) return PT_INVALID;
    const auto it = ns->find(key);
    if (it == ns->end()) return PT_INVALID;
    switch (it->second.type) {
        case NVS_TYPE_I8: return PT_I8;
        case NVS_TYPE_U8: return PT_U8;
        case NVS_TYPE_I16: return PT_I16;
        case NVS_TYPE_U16: return PT_U16;
        case NVS_TYPE_I32: return PT_I32;
        case NVS_TYPE_U32: return PT_U32;
        case NVS_TYPE_I64: return PT_I64;
        case NVS_TYPE_U64: return PT_U64;
        case NVS_TYPE_STR: return PT_STR;
        case NVS_TYPE_BLOB: return PT_BLOB;
        default: return PT_INVALID;
    }
}

size_t Preferences::freeEntries() {
    nvs_stats_t stats{};
    nvs_get_stats(nullptr, &stats);
    return stats.free_entries;
}

#define HOST_PREF_PAIR(Name, ctype, suffix)                                          \
    size_t Preferences::put##Name(const char* key, ctype value) {                    \
        if (!started_ || readOnly_) return 0;                                        \
        return nvs_set_##suffix(handle_, key, value) == ESP_OK ? sizeof(ctype) : 0;  \
    }                                                                                \
    ctype Preferences::get##Name(const char* key, ctype fallback) {                  \
        ctype value = fallback;                                                      \
        if (started_ && nvs_get_##suffix(handle_, key, &value) == ESP_OK) return value; \
        return fallback;                                                             \
    }

HOST_PREF_PAIR(Char, int8_t, i8)
HOST_PREF_PAIR(UChar, uint8_t, u8)
HOST_PREF_PAIR(Short, int16_t, i16)
HOST_PREF_PAIR(UShort, uint16_t, u16)
HOST_PREF_PAIR(Int, int32_t, i32)
HOST_PREF_PAIR(UInt, uint32_t, u32)
HOST_PREF_PAIR(Long, int32_t, i32)
HOST_PREF_PAIR(ULong, uint32_t, u32)
HOST_PREF_PAIR(Long64, int64_t, i64)
HOST_PREF_PAIR(ULong64, uint64_t, u64)

#undef HOST_PREF_PAIR

size_t Preferences::putBool(const char* key, bool value) {
    return putUChar(key, value ? 1 : 0);
}

bool Preferences::getBool(const char* key, bool fallback) {
    return getUChar(key, fallback ? 1 : 0) != 0;
}

size_t Preferences::putString(const char* key, const char* value) {
    if (!started_ || readOnly_ || !value) return 0;
    return nvs_set_str(handle_, key, value) == ESP_OK ? std::strlen(value) : 0;
}

String Preferences::getString(const char* key, const String& fallback) {
    if (!started_) return fallback;
    size_t len = 0;
    if (nvs_get_str(handle_, key, nullptr, &len) != ESP_OK || len == 0) return fallback;
    std::vector<char> buf(len);
    if (nvs_get_str(handle_, key, buf.data(), &len) != ESP_OK) return fallback;
    return String(buf.data());
}

size_t Preferences::getString(const char* key, char* out, size_t maxLen) {
    if (!started_ || !out) return 0;
    size_t len = maxLen;
    if (nvs_get_str(handle_, key, out, &len) != ESP_OK) return 0;
    return len;
}

size_t Preferences::putBytes(const char* key, const void* value, size_t len) {
    if (!started_ || readOnly_ || !value || len == 0) return 0;
    return nvs_set_blob(handle_, key, value, len) == ESP_OK ? len : 0;
}

size_t Preferences::getBytesLength(const char* key) {
    if (!started_) return 0;
    size_t len = 0;
    return nvs_get_blob(handle_, key, nullptr, &len) == ESP_OK ? len : 0;
}

size_t Preferences::getBytes(const char* key, void* out, size_t maxLen) {
    if (!started_ || !out) return 0;
    size_t len = getBytesLength(key);
    if (len == 0 || len > maxLen) return 0;
    return nvs_get_blob(handle_, key, out, &len) == ESP_OK ? len : 0;
}
