// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* HostWifiOs on Android (host/src/HostWifiOs.h): the radio belongs to
 * Android, so the firmware's Wi-Fi is answered by asking Android's own
 * WifiManager, over JNI -- the same contract the desktop build answers by
 * asking NetworkManager or wpa_supplicant. It replaces host/src/HostWifiOs.cpp
 * for this build.
 *
 * Nothing here originates a request: link() reads what Android already
 * knows, scan() asks the radio to look when the firmware's Wi-Fi app asks,
 * and join() hands Android the network the owner typed in that app. Traffic
 * still leaves only by the firmware's own three flows, through the same
 * sockets every host build uses (HostNet.cpp).
 *
 * Android 9 tells an app the name of the network it is on, and what a scan
 * found, only if it holds the location permission and location is on. Braino
 * never asks where the device is -- the permission is what Android puts in
 * front of Wi-Fi names, and nothing more. build.ps1 -Install grants it.
 * Without it the link is still up and the clock still syncs; the name reads
 * empty and a scan finds nothing.
 *
 * Threads: the link is refreshed once a second on its own attached thread,
 * because the firmware asks status() every frame and must never wait on the
 * JVM. scan() runs on the caller's thread (the firmware's, which shows
 * "Scanning..." for it, as on a board); join() runs on a thread of its own. */

#include "HostAndroid.h"
#include "HostWifiOs.h"

#include <android_native_app_glue.h>
#include <jni.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mutex stateMutex;
HostWifiOs::Link current;
HostWifiOs::JoinState joining = HostWifiOs::JoinState::Idle;
std::string target;
std::once_flag started;

/* ------------------------------------------------------------ JNI */

JavaVM* vm() {
    android_app* app = HostAndroid::app();
    return app && app->activity ? app->activity->vm : nullptr;
}

/* The calling thread's env, attaching it if it is not yet. A thread that
 * attaches here stays attached: the poller and the firmware's thread live as
 * long as the process. */
JNIEnv* env() {
    JavaVM* v = vm();
    if (!v) return nullptr;
    JNIEnv* e = nullptr;
    if (v->GetEnv(reinterpret_cast<void**>(&e), JNI_VERSION_1_6) == JNI_OK) return e;
    if (v->AttachCurrentThread(&e, nullptr) != JNI_OK) return nullptr;
    return e;
}

/* A Java exception -- a SecurityException without the permission, above all
 * -- is cleared and reported as failure, never left pending. */
bool failed(JNIEnv* e) {
    if (!e->ExceptionCheck()) return false;
    e->ExceptionClear();
    return true;
}

std::string str(JNIEnv* e, jstring s) {
    if (!s) return std::string();
    const char* c = e->GetStringUTFChars(s, nullptr);
    std::string out = c ? c : "";
    if (c) e->ReleaseStringUTFChars(s, c);
    return out;
}

/* Android's WifiManager, a global reference, found once. */
jobject wifiManager(JNIEnv* e) {
    static jobject wm = nullptr;
    if (wm) return wm;
    android_app* app = HostAndroid::app();
    if (!app || !app->activity) return nullptr;
    jobject activity = app->activity->clazz;
    jclass ctxClass = e->FindClass("android/content/Context");
    jmethodID getApp = e->GetMethodID(ctxClass, "getApplicationContext", "()Landroid/content/Context;");
    jobject appCtx = e->CallObjectMethod(activity, getApp);
    if (failed(e) || !appCtx) return nullptr;
    jmethodID getService = e->GetMethodID(ctxClass, "getSystemService",
                                          "(Ljava/lang/String;)Ljava/lang/Object;");
    jstring name = e->NewStringUTF("wifi");
    jobject local = e->CallObjectMethod(appCtx, getService, name);
    e->DeleteLocalRef(name);
    e->DeleteLocalRef(appCtx);
    if (failed(e) || !local) return nullptr;
    wm = e->NewGlobalRef(local);
    e->DeleteLocalRef(local);
    return wm;
}

int channelOf(int mhz) {
    if (mhz >= 2412 && mhz <= 2484) return mhz == 2484 ? 14 : (mhz - 2407) / 5;
    if (mhz >= 5000 && mhz <= 5900) return (mhz - 5000) / 5;
    return 0;
}

/* WifiInfo.getSSID() quotes a name it can show and answers "<unknown ssid>"
 * for one it may not. */
std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    if (s == "<unknown ssid>") return std::string();
    return s;
}

/* The link as Android has it now. IPv4 addresses come back as an int whose
 * bytes, little-endian, are already in network order -- what in_addr holds. */
HostWifiOs::Link readLink(JNIEnv* e) {
    HostWifiOs::Link l;
    jobject wm = wifiManager(e);
    if (!wm) return l;
    l.radio = true;   // a Wi-Fi manager exists: the radio is there, on or off
    jclass wmClass = e->GetObjectClass(wm);
    jobject info = e->CallObjectMethod(
        wm, e->GetMethodID(wmClass, "getConnectionInfo", "()Landroid/net/wifi/WifiInfo;"));
    if (!failed(e) && info) {
        jclass ic = e->GetObjectClass(info);
        const int netId = e->CallIntMethod(info, e->GetMethodID(ic, "getNetworkId", "()I"));
        const int ip = e->CallIntMethod(info, e->GetMethodID(ic, "getIpAddress", "()I"));
        l.rssi = e->CallIntMethod(info, e->GetMethodID(ic, "getRssi", "()I"));
        l.channel = channelOf(e->CallIntMethod(info, e->GetMethodID(ic, "getFrequency", "()I")));
        auto* ssid = static_cast<jstring>(
            e->CallObjectMethod(info, e->GetMethodID(ic, "getSSID", "()Ljava/lang/String;")));
        l.ssid = unquote(str(e, ssid));
        auto* bssid = static_cast<jstring>(
            e->CallObjectMethod(info, e->GetMethodID(ic, "getBSSID", "()Ljava/lang/String;")));
        l.bssid = str(e, bssid);
        if (l.bssid == "02:00:00:00:00:00") l.bssid.clear();   // hidden from this app
        if (failed(e)) l = HostWifiOs::Link{true};
        l.associated = netId != -1 || ip != 0;
        l.ip = static_cast<uint32_t>(ip);
        l.hasAddress = ip != 0;
        if (ssid) e->DeleteLocalRef(ssid);
        if (bssid) e->DeleteLocalRef(bssid);
        e->DeleteLocalRef(ic);
        e->DeleteLocalRef(info);
    }
    jobject dhcp = e->CallObjectMethod(
        wm, e->GetMethodID(wmClass, "getDhcpInfo", "()Landroid/net/DhcpInfo;"));
    if (!failed(e) && dhcp) {
        jclass dc = e->GetObjectClass(dhcp);
        l.gateway = static_cast<uint32_t>(e->GetIntField(dhcp, e->GetFieldID(dc, "gateway", "I")));
        l.netmask = static_cast<uint32_t>(e->GetIntField(dhcp, e->GetFieldID(dc, "netmask", "I")));
        l.dns = static_cast<uint32_t>(e->GetIntField(dhcp, e->GetFieldID(dc, "dns1", "I")));
        failed(e);
        e->DeleteLocalRef(dc);
        e->DeleteLocalRef(dhcp);
    }
    e->DeleteLocalRef(wmClass);
    return l;
}

void pollLink() {
    JNIEnv* e = env();
    if (!e) return;
    bool announced = false;
    for (;;) {
        const HostWifiOs::Link l = readLink(e);
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            current = l;
        }
        if (!announced) {
            std::printf("[wifi] host radio: Android's%s\n",
                        l.radio ? (l.ssid.empty() && l.associated
                                       ? " (connected; the name needs the location permission)"
                                       : "")
                                : " -- not available");
            announced = true;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/* -------------------------------------------------------------- join */

/* An existing configuration for `ssid`, or -1. Android keeps the networks
 * the device has joined before -- the Portal's own setup among them -- and an
 * app cannot overwrite one another app saved, but it may enable it. */
int configuredId(JNIEnv* e, jobject wm, const std::string& ssid) {
    jclass wmClass = e->GetObjectClass(wm);
    jobject list = e->CallObjectMethod(
        wm, e->GetMethodID(wmClass, "getConfiguredNetworks", "()Ljava/util/List;"));
    e->DeleteLocalRef(wmClass);
    if (failed(e) || !list) return -1;
    jclass listClass = e->GetObjectClass(list);
    const int n = e->CallIntMethod(list, e->GetMethodID(listClass, "size", "()I"));
    jmethodID get = e->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    int found = -1;
    const std::string quoted = "\"" + ssid + "\"";
    for (int i = 0; i < n && found < 0; ++i) {
        jobject conf = e->CallObjectMethod(list, get, i);
        if (!conf) continue;
        jclass cc = e->GetObjectClass(conf);
        auto* s = static_cast<jstring>(
            e->GetObjectField(conf, e->GetFieldID(cc, "SSID", "Ljava/lang/String;")));
        if (str(e, s) == quoted) found = e->GetIntField(conf, e->GetFieldID(cc, "networkId", "I"));
        if (s) e->DeleteLocalRef(s);
        e->DeleteLocalRef(cc);
        e->DeleteLocalRef(conf);
    }
    failed(e);
    e->DeleteLocalRef(listClass);
    e->DeleteLocalRef(list);
    return found;
}

void runJoin(std::string ssid, std::string password) {
    JNIEnv* e = env();
    jobject wm = e ? wifiManager(e) : nullptr;
    auto finish = [&](HostWifiOs::JoinState s) {
        std::lock_guard<std::mutex> lock(stateMutex);
        if (target == ssid) joining = s;
    };
    if (!wm) return finish(HostWifiOs::JoinState::Failed);

    jclass confClass = e->FindClass("android/net/wifi/WifiConfiguration");
    jobject conf = e->NewObject(confClass, e->GetMethodID(confClass, "<init>", "()V"));
    jstring quotedSsid = e->NewStringUTF(("\"" + ssid + "\"").c_str());
    e->SetObjectField(conf, e->GetFieldID(confClass, "SSID", "Ljava/lang/String;"), quotedSsid);
    if (!password.empty()) {
        jstring psk = e->NewStringUTF(("\"" + password + "\"").c_str());
        e->SetObjectField(conf, e->GetFieldID(confClass, "preSharedKey", "Ljava/lang/String;"), psk);
        e->DeleteLocalRef(psk);
    } else {
        jobject keyMgmt = e->GetObjectField(
            conf, e->GetFieldID(confClass, "allowedKeyManagement", "Ljava/util/BitSet;"));
        jclass bits = e->GetObjectClass(keyMgmt);
        e->CallVoidMethod(keyMgmt, e->GetMethodID(bits, "set", "(I)V"), 0);   // KeyMgmt.NONE
        e->DeleteLocalRef(bits);
        e->DeleteLocalRef(keyMgmt);
    }
    jclass wmClass = e->GetObjectClass(wm);
    int id = e->CallIntMethod(wm, e->GetMethodID(wmClass, "addNetwork",
                                                 "(Landroid/net/wifi/WifiConfiguration;)I"), conf);
    if (failed(e)) id = -1;
    if (id == -1) id = configuredId(e, wm, ssid);
    e->DeleteLocalRef(quotedSsid);
    e->DeleteLocalRef(conf);
    e->DeleteLocalRef(confClass);
    if (id == -1) {
        e->DeleteLocalRef(wmClass);
        std::printf("[wifi] Android would not take the network\n");
        return finish(HostWifiOs::JoinState::Failed);
    }
    e->CallBooleanMethod(wm, e->GetMethodID(wmClass, "enableNetwork", "(IZ)Z"), id, JNI_TRUE);
    e->CallBooleanMethod(wm, e->GetMethodID(wmClass, "reconnect", "()Z"));
    failed(e);
    e->DeleteLocalRef(wmClass);

    /* Android associates and asks DHCP on its own; watch the link for the
     * named network with an address, as the desktop build watches its
     * manager. Twenty seconds is what a board's join is given. */
    for (int i = 0; i < 40; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            if (target != ssid) return;   // asked for another meanwhile
            if (current.ssid == ssid && current.hasAddress) {
                joining = HostWifiOs::JoinState::Idle;
                std::printf("[wifi] joined through Android\n");
                return;
            }
        }
    }
    finish(HostWifiOs::JoinState::Failed);
}

}   // namespace

namespace HostWifiOs {

void begin() {
    std::call_once(started, [] { std::thread(pollLink).detach(); });
}

Link link() {
    begin();
    std::lock_guard<std::mutex> lock(stateMutex);
    return current;
}

JoinState joinState() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return joining;
}

std::string joinTarget() {
    std::lock_guard<std::mutex> lock(stateMutex);
    return target;
}

void join(const std::string& ssid, const std::string& password) {
    begin();
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        target = ssid;
        if (current.ssid == ssid && current.hasAddress) {
            joining = JoinState::Idle;   // already on it: nothing to do
            return;
        }
        joining = JoinState::Joining;
    }
    std::thread(runJoin, ssid, password).detach();
}

int scan(ScanEntry* out, int capacity) {
    JNIEnv* e = env();
    jobject wm = e ? wifiManager(e) : nullptr;
    if (!wm || capacity <= 0) return 0;
    jclass wmClass = e->GetObjectClass(wm);
    e->CallBooleanMethod(wm, e->GetMethodID(wmClass, "startScan", "()Z"));
    failed(e);
    /* A board's scan takes about four seconds and the firmware shows
     * "Scanning..." for it; Android's radio is quicker, and three seconds
     * is enough for its results to land. Under the five Android allows a
     * window before it calls the app unresponsive. */
    std::this_thread::sleep_for(std::chrono::seconds(3));
    jobject list = e->CallObjectMethod(
        wm, e->GetMethodID(wmClass, "getScanResults", "()Ljava/util/List;"));
    e->DeleteLocalRef(wmClass);
    if (failed(e) || !list) return 0;

    std::vector<ScanEntry> found;
    jclass listClass = e->GetObjectClass(list);
    const int n = e->CallIntMethod(list, e->GetMethodID(listClass, "size", "()I"));
    jmethodID get = e->GetMethodID(listClass, "get", "(I)Ljava/lang/Object;");
    for (int i = 0; i < n; ++i) {
        jobject r = e->CallObjectMethod(list, get, i);
        if (!r) continue;
        jclass rc = e->GetObjectClass(r);
        ScanEntry s;
        auto* ssid = static_cast<jstring>(e->GetObjectField(r, e->GetFieldID(rc, "SSID", "Ljava/lang/String;")));
        auto* bssid = static_cast<jstring>(e->GetObjectField(r, e->GetFieldID(rc, "BSSID", "Ljava/lang/String;")));
        auto* caps = static_cast<jstring>(e->GetObjectField(r, e->GetFieldID(rc, "capabilities", "Ljava/lang/String;")));
        s.ssid = str(e, ssid);
        s.bssid = str(e, bssid);
        const std::string c = str(e, caps);
        s.secured = c.find("WEP") != std::string::npos || c.find("PSK") != std::string::npos ||
                    c.find("EAP") != std::string::npos || c.find("SAE") != std::string::npos;
        s.rssi = e->GetIntField(r, e->GetFieldID(rc, "level", "I"));
        s.channel = channelOf(e->GetIntField(r, e->GetFieldID(rc, "frequency", "I")));
        for (jobject o : {static_cast<jobject>(ssid), static_cast<jobject>(bssid), static_cast<jobject>(caps)}) {
            if (o) e->DeleteLocalRef(o);
        }
        e->DeleteLocalRef(rc);
        e->DeleteLocalRef(r);
        if (failed(e) || s.ssid.empty()) continue;   // a hidden network has no name to offer
        /* One row a network, at its strongest access point. */
        auto same = std::find_if(found.begin(), found.end(),
                                 [&](const ScanEntry& f) { return f.ssid == s.ssid; });
        if (same == found.end()) {
            found.push_back(s);
        } else if (s.rssi > same->rssi) {
            *same = s;
        }
    }
    e->DeleteLocalRef(listClass);
    e->DeleteLocalRef(list);
    std::sort(found.begin(), found.end(),
              [](const ScanEntry& a, const ScanEntry& b) { return a.rssi > b.rssi; });
    const int count = std::min(capacity, static_cast<int>(found.size()));
    for (int i = 0; i < count; ++i) out[i] = found[static_cast<size_t>(i)];
    return count;
}

const char* backendName() { return "Android"; }

/* Android keeps its own radio power: the firmware's sleep has nothing to
 * take down here, and taking the interface down would be taking Android's. */
void sleepRadio(bool) {}

}   // namespace HostWifiOs
