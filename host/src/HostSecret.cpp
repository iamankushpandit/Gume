// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostSecret.h. */

#include "HostSecret.h"

#include <mbedtls/sha256.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>

namespace {

using Bytes = std::vector<uint8_t>;

Bytes sha256(const Bytes& in) {
    Bytes out(32);
    mbedtls_sha256(in.data(), in.size(), out.data(), 0);
    return out;
}

Bytes hmac(const Bytes& key, const Bytes& msg) {
    constexpr size_t BLOCK = 64;
    Bytes k = key.size() > BLOCK ? sha256(key) : key;
    k.resize(BLOCK, 0);
    Bytes inner(BLOCK), outer(BLOCK);
    for (size_t i = 0; i < BLOCK; ++i) {
        inner[i] = static_cast<uint8_t>(k[i] ^ 0x36);
        outer[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    }
    inner.insert(inner.end(), msg.begin(), msg.end());
    const Bytes innerHash = sha256(inner);
    outer.insert(outer.end(), innerHash.begin(), innerHash.end());
    return sha256(outer);
}

Bytes text(const char* s) { return Bytes(s, s + std::strlen(s)); }

bool meaningful(const Bytes& b) {
    if (b.size() < 4) return false;
    bool allZero = true, allOnes = true;
    for (uint8_t v : b) {
        allZero = allZero && (v == 0 || v == '0');
        allOnes = allOnes && v == 0xFF;
    }
    return !allZero && !allOnes;
}

Bytes readFile(const char* path, size_t limit) {
    Bytes out;
    if (FILE* f = std::fopen(path, "rb")) {
        out.resize(limit);
        out.resize(std::fread(out.data(), 1, limit, f));
        std::fclose(f);
    }
    return out;
}

/* The value after "<label>" and a colon on some line of a text file, with
 * the surrounding space trimmed. */
Bytes labelled(const char* path, const char* label) {
    const Bytes all = readFile(path, 16384);
    const std::string s(all.begin(), all.end());
    size_t at = 0;
    while ((at = s.find(label, at)) != std::string::npos) {
        const size_t colon = s.find(':', at);
        const size_t end = s.find('\n', at);
        if (colon != std::string::npos && (end == std::string::npos || colon < end)) {
            size_t a = colon + 1;
            size_t b = end == std::string::npos ? s.size() : end;
            while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
            const Bytes v(s.begin() + a, s.begin() + b);
            if (meaningful(v)) return v;
        }
        at += std::strlen(label);
    }
    return Bytes();
}

/* What the key is made from: the first source this processor answers. The
 * value itself is never printed -- only where it came from. */
Bytes deviceMaterial(const char*& source) {
    struct Probe {
        const char* path;
        const char* label;   // nullptr: the file's raw bytes
        size_t bytes;
    };
    static const Probe probes[] = {
        {"/sys/class/sunxi_info/sys_info", "sunxi_chipid", 0},
        {"/sys/class/sunxi_info/sys_info", "sunxi_serial", 0},
        {"/sys/bus/nvmem/devices/sunxi-sid0/nvmem", nullptr, 16},
        {"/sys/firmware/devicetree/base/serial-number", nullptr, 64},
        {"/proc/cpuinfo", "Serial", 0},
    };
    for (const Probe& p : probes) {
        Bytes v = p.label ? labelled(p.path, p.label) : readFile(p.path, p.bytes);
        while (!v.empty() && (v.back() == 0 || v.back() == '\n')) v.pop_back();
        if (meaningful(v)) {
            source = p.path;
            return v;
        }
    }
#if !GUME_HOST_STRICT_SECRETS
    Bytes id = readFile("/etc/machine-id", 64);
    if (meaningful(id)) {
        source = "/etc/machine-id (not bound to hardware)";
        return id;
    }
#endif
    source = nullptr;
    return Bytes();
}

struct Keys {
    bool ready = false;
    Bytes enc;
    Bytes mac;
};

const Keys& keys() {
    static std::once_flag once;
    static Keys k;
    std::call_once(once, [] {
        const char* source = nullptr;
        const Bytes material = deviceMaterial(source);
        if (material.empty()) {
            std::printf("[secrets] no processor serial readable: the Wi-Fi network will not "
                        "be saved to the card\n");
        } else {
            const Bytes master = hmac(material, text("braino-host-secrets-v1"));
            k.enc = hmac(master, text("enc"));
            k.mac = hmac(master, text("mac"));
            k.ready = true;
            std::printf("[secrets] sealing with a key from %s\n", source);
        }
        std::fflush(stdout);
    });
    return k;
}

constexpr size_t NONCE = 16;
constexpr size_t TAG = 16;

Bytes header(const Bytes& nonce, const std::string& name) {
    Bytes h = nonce;
    h.insert(h.end(), name.begin(), name.end());
    h.push_back(0);
    return h;
}

void crypt(const Keys& k, const Bytes& head, const uint8_t* in, size_t n, uint8_t* out) {
    for (size_t block = 0; block * 32 < n; ++block) {
        Bytes msg = head;
        for (int s = 24; s >= 0; s -= 8) msg.push_back(static_cast<uint8_t>(block >> s));
        const Bytes stream = hmac(k.enc, msg);
        for (size_t i = 0; i < 32 && block * 32 + i < n; ++i) {
            out[block * 32 + i] = static_cast<uint8_t>(in[block * 32 + i] ^ stream[i]);
        }
    }
}

Bytes tagOf(const Keys& k, const Bytes& head, const uint8_t* ct, size_t n) {
    Bytes msg = head;
    msg.insert(msg.end(), ct, ct + n);
    Bytes t = hmac(k.mac, msg);
    t.resize(TAG);
    return t;
}

}   // namespace

namespace HostSecret {

bool available() { return keys().ready; }

bool seal(const std::string& name, const std::vector<uint8_t>& plain, std::vector<uint8_t>& out) {
    const Keys& k = keys();
    if (!k.ready) return false;
    Bytes nonce(NONCE);
    std::random_device rd;
    for (auto& b : nonce) b = static_cast<uint8_t>(rd());
    const Bytes head = header(nonce, name);
    out = nonce;
    out.resize(NONCE + plain.size());
    crypt(k, head, plain.data(), plain.size(), out.data() + NONCE);
    const Bytes t = tagOf(k, head, out.data() + NONCE, plain.size());
    out.insert(out.end(), t.begin(), t.end());
    return true;
}

bool open(const std::string& name, const std::vector<uint8_t>& sealed, std::vector<uint8_t>& out) {
    const Keys& k = keys();
    if (!k.ready || sealed.size() < NONCE + TAG) return false;
    const Bytes nonce(sealed.begin(), sealed.begin() + NONCE);
    const size_t n = sealed.size() - NONCE - TAG;
    const Bytes head = header(nonce, name);
    const Bytes want = tagOf(k, head, sealed.data() + NONCE, n);
    uint8_t diff = 0;   // compared in full, whatever the first wrong byte
    for (size_t i = 0; i < TAG; ++i) diff |= static_cast<uint8_t>(want[i] ^ sealed[NONCE + n + i]);
    if (diff != 0) return false;
    out.resize(n);
    crypt(k, head, sealed.data() + NONCE, n, out.data());
    return true;
}

}   // namespace HostSecret
