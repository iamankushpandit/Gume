// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* WiFi, WiFiUDP and HTTPClient for the host. See host/shim/WiFi.h for what
 * these promise -- above all, that they only answer the firmware and never
 * send anything it did not ask for. */

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <esp_wifi.h>

#include <arpa/inet.h>
#include <curl/curl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "HostWifiOs.h"

WiFiClass WiFi;

namespace {

constexpr int MAX_SCAN = 32;
HostWifiOs::ScanEntry scanResults[MAX_SCAN];
int scanCount = 0;

}   // namespace

/* ------------------------------------------------------------- WiFi */

wl_status_t WiFiClass::begin(const char* ssid, const char* password) {
    HostWifiOs::join(ssid ? ssid : "", password ? password : "");
    return status();
}

/* While the firmware has asked for a particular network, status answers for
 * THAT network: WifiApp saves the credentials it typed only once status()
 * says connected, so reporting some other network the computer happens to be
 * on would save the wrong password against the right name. Before it has
 * asked, whatever the computer is connected to counts -- which is what lets a
 * console whose saved network is already up sync its clock at once. */
wl_status_t WiFiClass::status() {
    const HostWifiOs::Link link = HostWifiOs::link();
    if (!link.radio) return WL_NO_SHIELD;
    const bool up = link.associated && link.hasAddress;
    const std::string target = HostWifiOs::joinTarget();
    if (target.empty()) return up ? WL_CONNECTED : WL_DISCONNECTED;
    if (up && link.ssid == target) return WL_CONNECTED;
    switch (HostWifiOs::joinState()) {
        case HostWifiOs::JoinState::Failed: return WL_CONNECT_FAILED;
        case HostWifiOs::JoinState::NoSuchNetwork: return WL_NO_SSID_AVAIL;
        default: return WL_DISCONNECTED;
    }
}

int16_t WiFiClass::scanNetworks(bool, bool, bool, uint32_t) {
    scanCount = HostWifiOs::scan(scanResults, MAX_SCAN);
    return static_cast<int16_t>(scanCount);
}

void WiFiClass::scanDelete() { scanCount = 0; }

String WiFiClass::SSID() { return String(HostWifiOs::link().ssid); }

String WiFiClass::SSID(int i) {
    return (i >= 0 && i < scanCount) ? String(scanResults[i].ssid) : String();
}

int32_t WiFiClass::RSSI() { return HostWifiOs::link().rssi; }

int32_t WiFiClass::RSSI(int i) { return (i >= 0 && i < scanCount) ? scanResults[i].rssi : 0; }

wifi_auth_mode_t WiFiClass::encryptionType(int i) {
    return (i >= 0 && i < scanCount && scanResults[i].secured) ? WIFI_AUTH_WPA2_PSK
                                                               : WIFI_AUTH_OPEN;
}

int32_t WiFiClass::channel() { return HostWifiOs::link().channel; }

String WiFiClass::BSSIDstr() { return String(HostWifiOs::link().bssid); }

IPAddress WiFiClass::localIP() { return IPAddress::fromRaw(HostWifiOs::link().ip); }
IPAddress WiFiClass::gatewayIP() { return IPAddress::fromRaw(HostWifiOs::link().gateway); }
IPAddress WiFiClass::subnetMask() { return IPAddress::fromRaw(HostWifiOs::link().netmask); }
IPAddress WiFiClass::dnsIP(uint8_t) { return IPAddress::fromRaw(HostWifiOs::link().dns); }

int WiFiClass::hostByName(const char* host, IPAddress& out) {
    if (!host || !*host) return 0;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    addrinfo* result = nullptr;
    if (::getaddrinfo(host, nullptr, &hints, &result) != 0 || !result) return 0;
    out = IPAddress::fromRaw(reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr);
    ::freeaddrinfo(result);
    return 1;
}

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t* out) {
    if (WiFi.status() != WL_CONNECTED) return ESP_FAIL;
    const HostWifiOs::Link link = HostWifiOs::link();
    std::memset(out, 0, sizeof(*out));
    std::strncpy(reinterpret_cast<char*>(out->ssid), link.ssid.c_str(), sizeof(out->ssid) - 1);
    unsigned b[6] = {0};
    if (std::sscanf(link.bssid.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4],
                    &b[5]) == 6) {
        for (int i = 0; i < 6; ++i) out->bssid[i] = static_cast<uint8_t>(b[i]);
    }
    out->primary = static_cast<uint8_t>(link.channel);
    out->rssi = static_cast<int8_t>(link.rssi);
    out->phy_11n = 1;   // the OS does not say; every radio a handheld ships with does
    out->phy_11g = 1;
    out->phy_11b = 1;
    return ESP_OK;
}

size_t WiFiClient::readBytes(char* out, size_t len) {
    const size_t n = std::min(len, data_.size() - at_);
    std::memcpy(out, data_.data() + at_, n);
    at_ += n;
    return n;
}

/* ------------------------------------------------------------- UDP */

uint8_t WiFiUDP::begin(uint16_t localPort) {
    stop();
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return 0;
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(localPort);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        /* The fixed port is taken (another copy running, or another program):
         * any port answers an NTP reply just as well. */
        local.sin_port = 0;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
            stop();
            return 0;
        }
    }
    return 1;
}

void WiFiUDP::stop() {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
    txLen_ = 0;
    rxLen_ = 0;
    rxAt_ = 0;
}

int WiFiUDP::beginPacket(IPAddress ip, uint16_t port) {
    destIp_ = ip.raw();
    destPort_ = port;
    txLen_ = 0;
    return fd_ >= 0 ? 1 : 0;
}

int WiFiUDP::beginPacket(const char* host, uint16_t port) {
    IPAddress ip;
    if (!WiFi.hostByName(host, ip)) return 0;
    return beginPacket(ip, port);
}

size_t WiFiUDP::write(const uint8_t* data, size_t len) {
    const size_t n = std::min(len, sizeof(tx_) - txLen_);
    std::memcpy(tx_ + txLen_, data, n);
    txLen_ += n;
    return n;
}

int WiFiUDP::endPacket() {
    if (fd_ < 0) return 0;
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = destIp_;
    dest.sin_port = htons(destPort_);
    const ssize_t sent = ::sendto(fd_, tx_, txLen_, 0, reinterpret_cast<sockaddr*>(&dest),
                                  sizeof(dest));
    txLen_ = 0;
    return sent >= 0 ? 1 : 0;
}

int WiFiUDP::parsePacket() {
    if (fd_ < 0) return 0;
    const ssize_t n = ::recvfrom(fd_, rx_, sizeof(rx_), MSG_DONTWAIT, nullptr, nullptr);
    if (n <= 0) return 0;
    rxLen_ = static_cast<size_t>(n);
    rxAt_ = 0;
    return static_cast<int>(n);
}

int WiFiUDP::read(uint8_t* out, size_t len) {
    const size_t n = std::min(len, rxLen_ - rxAt_);
    std::memcpy(out, rx_ + rxAt_, n);
    rxAt_ += n;
    return static_cast<int>(n);
}

/* ------------------------------------------------------------- HTTP */

namespace {

std::once_flag curlStarted;

size_t collect(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

}   // namespace

bool HTTPClient::begin(const char* url) {
    if (!url || !*url) return false;
    url_ = url;
    body_.clear();
    client_ = nullptr;
    return true;
}

bool HTTPClient::begin(WiFiClient& client, const char* url) {
    if (!begin(url)) return false;
    client_ = &client;
    return true;
}

void HTTPClient::end() {
    url_.clear();
    body_.clear();
}

int HTTPClient::GET() {
    if (url_.empty()) return HTTPC_ERROR_CONNECTION_REFUSED;
    std::call_once(curlStarted, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    CURL* curl = curl_easy_init();
    if (!curl) return HTTPC_ERROR_CONNECTION_REFUSED;

    body_.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs_));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(connectTimeoutMs_));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow_ == HTTPC_DISABLE_FOLLOW_REDIRECTS ? 0L : 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    /* Nothing but the request the firmware wrote: libcurl sends no
     * User-Agent unless told to, and is told nothing. Only http and https,
     * so a redirect cannot take the request anywhere stranger. */
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif

    const CURLcode rc = curl_easy_perform(curl);
    long code = 0;
    if (rc == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    getStreamPtr()->hostLoad(body_);
    if (rc == CURLE_OPERATION_TIMEDOUT) return HTTPC_ERROR_READ_TIMEOUT;
    if (rc != CURLE_OK) return HTTPC_ERROR_CONNECTION_REFUSED;
    return static_cast<int>(code);
}
