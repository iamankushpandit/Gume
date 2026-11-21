// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* HostPlatform on Android: the panel is the NativeActivity's window, the
 * touch controller is the touchscreen, and the data directory is the app's
 * own private storage. It replaces host/src/HostPlatform.cpp (SDL) for this
 * build only; every other host file is the one the desktop compiles.
 *
 * The frame is the panel shim's RGB565 buffer at the window's own size, and
 * the window is given exactly that size and format, so a present is a row
 * copy -- no conversion, no scaling, nothing a GPU has to be asked for. That
 * is the same shape as the RG40XX V's DRM path (host/src/HostDrm.h), which
 * also draws the frame into a buffer it was handed and flips it.
 *
 * The layout follows host/src/HostPlatform.cpp's findPanel() exactly: the
 * largest whole UI scale that still leaves the firmware the 320x240 game
 * canvas. On the Portal+ (1920x1080) that is 4, so the firmware lays out on
 * 480x270 -- the launcher and the system screens use all of it, and a game
 * is its 320x240 canvas centred, as on every other panel larger than the
 * canvas. */

#include "HostAndroid.h"
#include "HostPlatform.h"
#include "BoardConfig.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>

#include <TFT_eSPI.h>

namespace {

constexpr uint32_t PRESENT_INTERVAL_MS = 16;

android_app* theApp = nullptr;
TFT_eSPI* panel = nullptr;
std::thread::id mainThread;

/* The panel the firmware lays out on, and the window's real pixels. */
int FRAME_W = GAME_CANVAS_WIDTH, FRAME_H = GAME_CANVAS_HEIGHT;
int PHYS_W = GAME_CANVAS_WIDTH, PHYS_H = GAME_CANVAS_HEIGHT;
int density = 1;

/* The window, as last configured. Android takes it away while the activity
 * is not visible (APP_CMD_TERM_WINDOW) and hands back a new one; until then
 * there is nowhere to present, and the firmware keeps running regardless. */
ANativeWindow* window = nullptr;
enum class Geometry { None, Frame, Image };
Geometry geometry = Geometry::None;

bool quit = false;

/* The finger the firmware sees: the first one down, followed until it lifts.
 * The FT6336U the panel shim emulates reports one contact, as the boards'
 * controllers do. */
bool touchDown = false;
int32_t touchId = -1;
float touchX = 0.0f, touchY = 0.0f;   // window pixels
bool backHeld = false;

float backlight = 1.0f;
uint32_t lastPresentMs = 0;
uint32_t presents = 0;

/* What was last put on the glass, so an unchanged frame is not presented
 * again -- the same test host/src/HostPlatform.cpp makes. */
struct Shown {
    uint32_t frameGen = 0;
    uint8_t lamp = 255;
    bool displayOn = true;
    bool asleep = false;
    bool operator==(const Shown& o) const {
        return frameGen == o.frameGen && lamp == o.lamp && displayOn == o.displayOn &&
               asleep == o.asleep;
    }
};
Shown lastShown;
bool shownOnce = false;

uint32_t nowMs() {
    using namespace std::chrono;
    return static_cast<uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

/* findPanel() from host/src/HostPlatform.cpp, minus BRAINO_PANEL: on a
 * device, the panel is the window and nothing overrides it. */
void findPanel(int w, int h) {
    if (h > w) std::swap(w, h);
    if (w < GAME_CANVAS_WIDTH) w = GAME_CANVAS_WIDTH;
    if (h < GAME_CANVAS_HEIGHT) h = GAME_CANVAS_HEIGHT;
    int d = std::min(w / GAME_CANVAS_WIDTH, h / GAME_CANVAS_HEIGHT);
    if (d < 1) d = 1;
    density = d;
    FRAME_W = w / d;
    FRAME_H = h / d;
    PHYS_W = FRAME_W * d;
    PHYS_H = FRAME_H * d;
    std::printf("[host] panel %dx%d (window), ui scale %d: laid out on %dx%d\n", w, h, density,
                FRAME_W, FRAME_H);
}

/* Window pixels to the frame the firmware lays out on. The compositor
 * stretches the buffer over the whole window, so the mapping is the ratio
 * of the two, divided by the UI scale. */
void toFrame(float wx, float wy, int16_t& fx, int16_t& fy) {
    const int ww = window ? ANativeWindow_getWidth(window) : PHYS_W;
    const int wh = window ? ANativeWindow_getHeight(window) : PHYS_H;
    int x = ww > 0 ? static_cast<int>(wx * PHYS_W / ww) / density : 0;
    int y = wh > 0 ? static_cast<int>(wy * PHYS_H / wh) / density : 0;
    x = std::max(0, std::min(FRAME_W - 1, x));
    y = std::max(0, std::min(FRAME_H - 1, y));
    fx = static_cast<int16_t>(x);
    fy = static_cast<int16_t>(y);
}

bool useGeometry(Geometry g, int w, int h) {
    if (!window) return false;
    if (geometry == g) return true;
    const int32_t format = g == Geometry::Frame ? WINDOW_FORMAT_RGB_565 : WINDOW_FORMAT_RGBX_8888;
    if (ANativeWindow_setBuffersGeometry(window, w, h, format) != 0) {
        std::fprintf(stderr, "[host] window geometry %dx%d refused\n", w, h);
        return false;
    }
    geometry = g;
    return true;
}

/* One row of RGB565, dimmed by the emulated backlight: what the PWM does to
 * the lamp on a board, done to the picture here because the window has no
 * lamp of its own to turn down. */
void copyRow(uint16_t* dst, const uint16_t* src, int n, uint8_t lamp) {
    if (lamp == 255) {
        std::memcpy(dst, src, static_cast<size_t>(n) * sizeof(uint16_t));
        return;
    }
    for (int i = 0; i < n; ++i) {
        const uint16_t p = src[i];
        const uint16_t r = static_cast<uint16_t>(((p >> 11) & 0x1F) * lamp / 255);
        const uint16_t g = static_cast<uint16_t>(((p >> 5) & 0x3F) * lamp / 255);
        const uint16_t b = static_cast<uint16_t>((p & 0x1F) * lamp / 255);
        dst[i] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }
}

void present(uint32_t now) {
    if (!panel || !window) return;
    float level = std::max(0.0f, std::min(1.0f, backlight));
    Shown shown;
    shown.frameGen = panel->frameGeneration();
    shown.lamp = static_cast<uint8_t>(level * 255.0f + 0.5f);
    shown.displayOn = panel->displayOn();
    shown.asleep = panel->asleep();
    /* Asleep: one black frame, then nothing until it wakes. */
    if (shownOnce && shown == lastShown) return;
    if (!useGeometry(Geometry::Frame, PHYS_W, PHYS_H)) return;

    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(window, &buf, nullptr) != 0) return;
    auto* dst = static_cast<uint16_t*>(buf.bits);
    const int w = std::min(PHYS_W, static_cast<int>(buf.width));
    const int h = std::min(PHYS_H, static_cast<int>(buf.height));
    if (!shown.displayOn) {
        for (int y = 0; y < h; ++y) std::memset(dst + y * buf.stride, 0, w * sizeof(uint16_t));
    } else {
        const uint16_t* src = panel->frame();
        for (int y = 0; y < h; ++y) copyRow(dst + y * buf.stride, src + y * PHYS_W, w, shown.lamp);
    }
    ANativeWindow_unlockAndPost(window);
    lastShown = shown;
    shownOnce = true;
    lastPresentMs = now;
    ++presents;
}

void onAppCmd(android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            window = app->window;
            geometry = Geometry::None;
            shownOnce = false;   // a new window: draw it whole
            break;
        case APP_CMD_TERM_WINDOW:
            window = nullptr;
            geometry = Geometry::None;
            touchDown = false;
            break;
        case APP_CMD_WINDOW_REDRAW_NEEDED:
        case APP_CMD_CONTENT_RECT_CHANGED:
            shownOnce = false;
            break;
        case APP_CMD_DESTROY:
            quit = true;
            break;
        default:
            break;
    }
}

int32_t onInput(android_app*, AInputEvent* event) {
    const int32_t type = AInputEvent_getType(event);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(event);
        const int32_t masked = action & AMOTION_EVENT_ACTION_MASK;
        const size_t index = static_cast<size_t>(
            (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >>
            AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        switch (masked) {
            case AMOTION_EVENT_ACTION_DOWN:
                touchDown = true;
                touchId = AMotionEvent_getPointerId(event, 0);
                touchX = AMotionEvent_getX(event, 0);
                touchY = AMotionEvent_getY(event, 0);
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                for (size_t i = 0; i < AMotionEvent_getPointerCount(event); ++i) {
                    if (AMotionEvent_getPointerId(event, i) == touchId) {
                        touchX = AMotionEvent_getX(event, i);
                        touchY = AMotionEvent_getY(event, i);
                    }
                }
                break;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                if (AMotionEvent_getPointerId(event, index) == touchId) touchDown = false;
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                touchDown = false;
                touchId = -1;
                break;
            default:
                break;
        }
        return 1;
    }
    if (type == AINPUT_EVENT_TYPE_KEY && AKeyEvent_getKeyCode(event) == AKEYCODE_BACK) {
        /* Back is the BOOT key, so it goes Home, as B does on a gamepad.
         * Consumed, so the system never finishes the activity on it. */
        backHeld = AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_UP;
        return 1;
    }
    return 0;
}

bool makeDir(const std::string& path) {
    for (size_t at = 1; at <= path.size(); ++at) {
        if (at == path.size() || path[at] == '/') {
            const std::string part = path.substr(0, at);
            if (::mkdir(part.c_str(), 0700) != 0 && errno != EEXIST) return false;
        }
    }
    return true;
}

}   // namespace

namespace HostAndroid {

void setApp(android_app* app) {
    theApp = app;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInput;
}

android_app* app() { return theApp; }

bool pump(int timeoutMs) {
    if (!theApp) return !quit;
    int events = 0;
    android_poll_source* source = nullptr;
    /* Drain whatever is waiting; wait only on the first poll. */
    int wait = timeoutMs;
    while (ALooper_pollOnce(wait, nullptr, &events, reinterpret_cast<void**>(&source)) >= 0) {
        if (source) source->process(theApp, source);
        if (theApp->destroyRequested) quit = true;
        wait = 0;
    }
    return !quit;
}

}   // namespace HostAndroid

namespace HostPlatform {

int panelWidth() { return FRAME_W; }
int panelHeight() { return FRAME_H; }
int panelScale() { return density; }

bool begin(int, char**) {
    mainThread = std::this_thread::get_id();
    if (!window) {
        std::fprintf(stderr, "[host] no window to draw in\n");
        return false;
    }
    findPanel(ANativeWindow_getWidth(window), ANativeWindow_getHeight(window));
    return true;
}

void shutdown() {
    window = nullptr;
    geometry = Geometry::None;
}

bool service() {
    HostAndroid::pump(0);
    const uint32_t now = nowMs();
    if (now - lastPresentMs >= PRESENT_INTERVAL_MS) present(now);
    return !quit;
}

bool quitRequested() { return quit; }

void outputSize(int& w, int& h) {
    w = window ? ANativeWindow_getWidth(window) : PHYS_W;
    h = window ? ANativeWindow_getHeight(window) : PHYS_H;
}

bool presentImage(const uint8_t* rgb, int w, int h) {
    HostAndroid::pump(0);
    if (!window || !useGeometry(Geometry::Image, w, h)) return !quit;
    ANativeWindow_Buffer buf;
    if (ANativeWindow_lock(window, &buf, nullptr) != 0) return !quit;
    auto* dst = static_cast<uint8_t*>(buf.bits);
    const int bw = std::min(w, static_cast<int>(buf.width));
    const int bh = std::min(h, static_cast<int>(buf.height));
    for (int y = 0; y < bh; ++y) {
        uint8_t* row = dst + static_cast<size_t>(y) * buf.stride * 4;
        const uint8_t* in = rgb + static_cast<size_t>(y) * w * 3;
        for (int x = 0; x < bw; ++x) {
            row[x * 4 + 0] = in[x * 3 + 0];
            row[x * 4 + 1] = in[x * 3 + 1];
            row[x * 4 + 2] = in[x * 3 + 2];
            row[x * 4 + 3] = 0xFF;
        }
    }
    ANativeWindow_unlockAndPost(window);
    shownOnce = false;   // the firmware's frame is no longer what is on screen
    return !quit;
}

void attachPanel(TFT_eSPI* p) { panel = p; }

Contact contact() {
    Contact c;
    if (touchDown) {
        c.down = true;
        toFrame(touchX, touchY, c.x, c.y);
    }
    return c;
}

bool homeHeld() { return backHeld; }

void setBacklight(float level) { backlight = level; }

bool screenAsleep() { return panel && panel->asleep(); }

uint32_t presentCount() { return presents; }

const std::string& dataDir() {
    static std::string dir;
    if (dir.empty()) {
        /* The app's private storage: no other app can read it, and it goes
         * when Braino is uninstalled. */
        const char* base = theApp && theApp->activity ? theApp->activity->internalDataPath : nullptr;
        dir = std::string(base ? base : "/data/local/tmp") + "/braino";
        if (!makeDir(dir)) std::fprintf(stderr, "[host] cannot create %s\n", dir.c_str());
    }
    return dir;
}

void radioId(uint8_t out[6]) {
    static uint8_t id[6];
    static bool have = false;
    if (!have) {
        const std::string path = dataDir() + "/radio-id";
        unsigned v[6];
        FILE* f = std::fopen(path.c_str(), "r");
        if (f && std::fscanf(f, "%2x%2x%2x%2x%2x%2x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) == 6) {
            for (int i = 0; i < 6; ++i) id[i] = static_cast<uint8_t>(v[i]);
        } else {
            std::random_device rd;
            for (int i = 0; i < 6; ++i) id[i] = static_cast<uint8_t>(rd());
            if (FILE* w = std::fopen(path.c_str(), "w")) {
                std::fprintf(w, "%02x%02x%02x%02x%02x%02x\n", id[0], id[1], id[2], id[3], id[4], id[5]);
                std::fflush(w);
                ::fsync(::fileno(w));
                std::fclose(w);
            }
        }
        if (f) std::fclose(f);
        have = true;
    }
    std::memcpy(out, id, 6);
}

void restart() {
    /* What ESP.restart() means for a home screen: end the process. Braino is
     * the device's home activity, so the system starts it again at once. */
    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(0);
}

bool onMainThread() { return std::this_thread::get_id() == mainThread; }

}   // namespace HostPlatform
