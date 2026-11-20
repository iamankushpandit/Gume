// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The ESP-IDF NVS C API over the host's key-value store. Preferences.h sits on
 * the same store, exactly as the Arduino Preferences class sits on NVS, so
 * BoardStorageMaintenance.cpp's profile moves -- which enumerate and copy raw
 * NVS entries -- run unchanged against what Board wrote through Preferences.
 *
 * Type codes are IDF's own, because the maintenance code switches on them. */

#include <cstddef>
#include <cstdint>
#include "esp_system.h"

typedef uint32_t nvs_handle_t;
typedef enum { NVS_READONLY, NVS_READWRITE } nvs_open_mode_t;

typedef enum {
    NVS_TYPE_U8 = 0x01,
    NVS_TYPE_I8 = 0x11,
    NVS_TYPE_U16 = 0x02,
    NVS_TYPE_I16 = 0x12,
    NVS_TYPE_U32 = 0x04,
    NVS_TYPE_I32 = 0x14,
    NVS_TYPE_U64 = 0x08,
    NVS_TYPE_I64 = 0x18,
    NVS_TYPE_STR = 0x21,
    NVS_TYPE_BLOB = 0x42,
    NVS_TYPE_ANY = 0xff,
} nvs_type_t;

#define NVS_DEFAULT_PART_NAME "nvs"
#define NVS_KEY_NAME_MAX_SIZE 16
#define NVS_NS_NAME_MAX_SIZE 16

#define ESP_ERR_NVS_BASE 0x1100
#define ESP_ERR_NVS_NOT_INITIALIZED (ESP_ERR_NVS_BASE + 0x01)
#define ESP_ERR_NVS_NOT_FOUND (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_TYPE_MISMATCH (ESP_ERR_NVS_BASE + 0x03)
#define ESP_ERR_NVS_READ_ONLY (ESP_ERR_NVS_BASE + 0x04)
#define ESP_ERR_NVS_INVALID_HANDLE (ESP_ERR_NVS_BASE + 0x07)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x0c)
#define ESP_ERR_NVS_VALUE_TOO_LONG (ESP_ERR_NVS_BASE + 0x0e)

typedef struct {
    char namespace_name[NVS_NS_NAME_MAX_SIZE];
    char key[NVS_KEY_NAME_MAX_SIZE];
    nvs_type_t type;
} nvs_entry_info_t;

struct HostNvsIterator;
typedef HostNvsIterator* nvs_iterator_t;

typedef struct {
    size_t used_entries;
    size_t free_entries;
    size_t total_entries;
    size_t namespace_count;
} nvs_stats_t;

esp_err_t nvs_open(const char* ns, nvs_open_mode_t mode, nvs_handle_t* out);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char* key);
esp_err_t nvs_erase_all(nvs_handle_t handle);

esp_err_t nvs_get_u8(nvs_handle_t h, const char* key, uint8_t* out);
esp_err_t nvs_get_i8(nvs_handle_t h, const char* key, int8_t* out);
esp_err_t nvs_get_u16(nvs_handle_t h, const char* key, uint16_t* out);
esp_err_t nvs_get_i16(nvs_handle_t h, const char* key, int16_t* out);
esp_err_t nvs_get_u32(nvs_handle_t h, const char* key, uint32_t* out);
esp_err_t nvs_get_i32(nvs_handle_t h, const char* key, int32_t* out);
esp_err_t nvs_get_u64(nvs_handle_t h, const char* key, uint64_t* out);
esp_err_t nvs_get_i64(nvs_handle_t h, const char* key, int64_t* out);
esp_err_t nvs_get_str(nvs_handle_t h, const char* key, char* out, size_t* len);
esp_err_t nvs_get_blob(nvs_handle_t h, const char* key, void* out, size_t* len);

esp_err_t nvs_set_u8(nvs_handle_t h, const char* key, uint8_t value);
esp_err_t nvs_set_i8(nvs_handle_t h, const char* key, int8_t value);
esp_err_t nvs_set_u16(nvs_handle_t h, const char* key, uint16_t value);
esp_err_t nvs_set_i16(nvs_handle_t h, const char* key, int16_t value);
esp_err_t nvs_set_u32(nvs_handle_t h, const char* key, uint32_t value);
esp_err_t nvs_set_i32(nvs_handle_t h, const char* key, int32_t value);
esp_err_t nvs_set_u64(nvs_handle_t h, const char* key, uint64_t value);
esp_err_t nvs_set_i64(nvs_handle_t h, const char* key, int64_t value);
esp_err_t nvs_set_str(nvs_handle_t h, const char* key, const char* value);
esp_err_t nvs_set_blob(nvs_handle_t h, const char* key, const void* value, size_t len);

nvs_iterator_t nvs_entry_find(const char* part, const char* ns, nvs_type_t type);
nvs_iterator_t nvs_entry_next(nvs_iterator_t it);
void nvs_entry_info(nvs_iterator_t it, nvs_entry_info_t* out);
void nvs_release_iterator(nvs_iterator_t it);

esp_err_t nvs_get_stats(const char* part, nvs_stats_t* stats);
esp_err_t nvs_get_used_entry_count(nvs_handle_t handle, size_t* used);
