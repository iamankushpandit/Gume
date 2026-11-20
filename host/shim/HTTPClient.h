// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

#include <string>
#include "WiFi.h"
#include "WString.h"

/* A GET, over libcurl, for the firmware's two HTTP requests: the timezone
 * lookup and the update manifest. It adds nothing to them -- no
 * User-Agent, no header, no query string the firmware did not write -- which
 * matters because check_privacy.py's promise about the manifest request is
 * that it says nothing about the device. TLS is verified against the
 * system's certificates, which is stricter than the device's setInsecure(). */

#define HTTPC_ERROR_CONNECTION_REFUSED (-1)
#define HTTPC_ERROR_READ_TIMEOUT (-11)
#define HTTP_CODE_OK 200

typedef enum {
    HTTPC_DISABLE_FOLLOW_REDIRECTS,
    HTTPC_STRICT_FOLLOW_REDIRECTS,
    HTTPC_FORCE_FOLLOW_REDIRECTS,
} followRedirects_t;

class HTTPClient {
public:
    bool begin(const char* url);
    bool begin(const String& url) { return begin(url.c_str()); }
    bool begin(WiFiClient& client, const char* url);
    bool begin(WiFiClient& client, const String& url) { return begin(client, url.c_str()); }
    void end();
    void setTimeout(uint16_t ms) { timeoutMs_ = ms; }
    void setConnectTimeout(int32_t ms) { connectTimeoutMs_ = ms; }
    void setFollowRedirects(followRedirects_t mode) { follow_ = mode; }
    void setUserAgent(const String&) {}
    int GET();
    int getSize() { return static_cast<int>(body_.size()); }
    String getString() { return String(body_); }
    WiFiClient* getStreamPtr() { return client_ ? client_ : &own_; }

private:
    std::string url_;
    std::string body_;
    WiFiClient* client_ = nullptr;
    WiFiClient own_;
    uint32_t timeoutMs_ = 5000;
    int32_t connectTimeoutMs_ = 5000;
    followRedirects_t follow_ = HTTPC_DISABLE_FOLLOW_REDIRECTS;
};
