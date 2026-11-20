// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* See HostDrm.h. */

#include "HostDrm.h"
#include "HostTvMode.h"

#if GUME_HOST_DRM

#include <drm/drm.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_mode.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <vector>

namespace HostDrm {
namespace {

struct Buffer {
    uint32_t handle = 0;
    uint32_t fb = 0;
    uint32_t pitch = 0;
    uint64_t size = 0;
    uint32_t* pixels = nullptr;
};

/* One place the picture can go: a connected connector, the mode it prefers
 * and a CRTC that can drive it. */
struct Output {
    uint32_t connector = 0;
    uint32_t type = 0;
    uint32_t crtc = 0;
    drm_mode_modeinfo mode{};
};

int fd = -1;
int uevents = -1;          // kernel hotplug events, no udev needed
Output current;
/* The built-in panel's mode, from the first time it is seen. The firmware is
 * laid out on it for the whole run, so a television plugged in before
 * power-on must not become the size Braino lays out on: unplugged, the panel
 * would be smaller than the frame and stay dark. */
int builtinW = 0, builtinH = 0;
/* An output that would not come back on after sleep (a television unplugged
 * meanwhile): look again on the next outputChanged(), event or not. */
bool recheck = false;

Buffer buffers[2];
int front = 0;
bool flipPending = false;
bool poweredOff = false;

int ioctlRetry(unsigned long request, void* arg) {
    int r;
    do {
        r = ::ioctl(fd, request, arg);
    } while (r == -1 && (errno == EINTR || errno == EAGAIN));
    return r;
}

bool isHdmi(uint32_t type) {
    return type == DRM_MODE_CONNECTOR_HDMIA || type == DRM_MODE_CONNECTOR_HDMIB;
}

const char* typeName(uint32_t type) { return isHdmi(type) ? "HDMI" : "panel"; }

bool makeBuffer(Buffer& b) {
    drm_mode_create_dumb create{};
    create.width = current.mode.hdisplay;
    create.height = current.mode.vdisplay;
    create.bpp = 32;
    if (ioctlRetry(DRM_IOCTL_MODE_CREATE_DUMB, &create) != 0) return false;
    b.handle = create.handle;
    b.pitch = create.pitch;
    b.size = create.size;

    drm_mode_fb_cmd2 fb{};
    fb.width = current.mode.hdisplay;
    fb.height = current.mode.vdisplay;
    fb.pixel_format = DRM_FORMAT_XRGB8888;
    fb.handles[0] = b.handle;
    fb.pitches[0] = b.pitch;
    if (ioctlRetry(DRM_IOCTL_MODE_ADDFB2, &fb) != 0) return false;
    b.fb = fb.fb_id;

    drm_mode_map_dumb map{};
    map.handle = b.handle;
    if (ioctlRetry(DRM_IOCTL_MODE_MAP_DUMB, &map) != 0) return false;
    void* p = ::mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, static_cast<off_t>(map.offset));
    if (p == MAP_FAILED) return false;
    b.pixels = static_cast<uint32_t*>(p);
    std::memset(b.pixels, 0, b.size);
    return true;
}

void freeBuffer(Buffer& b) {
    if (b.pixels) ::munmap(b.pixels, b.size);
    if (b.fb) {
        uint32_t id = b.fb;
        ioctlRetry(DRM_IOCTL_MODE_RMFB, &id);
    }
    if (b.handle) {
        drm_mode_destroy_dumb destroy{};
        destroy.handle = b.handle;
        ioctlRetry(DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    }
    b = Buffer{};
}

/* A television's mode (HostTvMode::pick()), or null if none will do. */
const drm_mode_modeinfo* tvMode(const std::vector<drm_mode_modeinfo>& modes) {
    std::vector<HostTvMode::Mode> m(modes.size());
    for (size_t i = 0; i < modes.size(); ++i) {
        m[i].w = modes[i].hdisplay;
        m[i].h = modes[i].vdisplay;
        m[i].refresh = modes[i].vrefresh;
        m[i].preferred = (modes[i].type & DRM_MODE_TYPE_PREFERRED) != 0;
        m[i].interlaced = (modes[i].flags & DRM_MODE_FLAG_INTERLACE) != 0;
    }
    const int i = HostTvMode::pick(m.data(), static_cast<int>(m.size()));
    return i < 0 ? nullptr : &modes[i];
}

/* Every connected output. A television on HDMI is chosen over the built-in
 * panel whenever one is plugged in and has a mode that holds the panel's
 * frame: the picture goes where the room is looking, and the panel goes
 * dark. Showing both was the alternative, and it buys nothing a player in
 * front of a TV wants -- the console in their hands becomes the controller --
 * while it costs a second conversion of every frame and the panel's lamp. */
bool pickOutput(Output& out) {
    drm_mode_card_res res{};
    if (ioctlRetry(DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) return false;
    std::vector<uint32_t> crtcs(res.count_crtcs), connectors(res.count_connectors),
        encoders(res.count_encoders), fbs(res.count_fbs);
    res.crtc_id_ptr = reinterpret_cast<uintptr_t>(crtcs.data());
    res.connector_id_ptr = reinterpret_cast<uintptr_t>(connectors.data());
    res.encoder_id_ptr = reinterpret_cast<uintptr_t>(encoders.data());
    res.fb_id_ptr = reinterpret_cast<uintptr_t>(fbs.data());
    if (ioctlRetry(DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) return false;

    Output panelOut, hdmi;
    bool havePanel = false, haveHdmi = false;
    for (uint32_t id : connectors) {
        drm_mode_get_connector conn{};
        conn.connector_id = id;
        if (ioctlRetry(DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) continue;
        if (conn.connection != 1 || conn.count_modes == 0) continue;   // 1: connected
        std::vector<drm_mode_modeinfo> modes(conn.count_modes);
        std::vector<uint32_t> encs(conn.count_encoders), props(conn.count_props);
        std::vector<uint64_t> values(conn.count_props);
        conn.modes_ptr = reinterpret_cast<uintptr_t>(modes.data());
        conn.encoders_ptr = reinterpret_cast<uintptr_t>(encs.data());
        conn.props_ptr = reinterpret_cast<uintptr_t>(props.data());
        conn.prop_values_ptr = reinterpret_cast<uintptr_t>(values.data());
        if (ioctlRetry(DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0 || conn.count_modes == 0) continue;

        Output o;
        o.connector = id;
        o.type = conn.connector_type;
        if (isHdmi(o.type)) {
            const drm_mode_modeinfo* m = tvMode(modes);
            if (!m) {
                std::printf("[drm] HDMI: no progressive mode up to %ux%u; staying on the panel\n",
                            HostTvMode::MAX_W, HostTvMode::MAX_H);
                continue;
            }
            o.mode = *m;
        } else {
            o.mode = modes[0];
            for (const auto& m : modes) {
                if (m.type & DRM_MODE_TYPE_PREFERRED) {
                    o.mode = m;
                    break;
                }
            }
        }
        /* The encoder already in use keeps its CRTC; otherwise the first
         * CRTC any of the connector's encoders can reach. */
        for (uint32_t e : encs) {
            drm_mode_get_encoder enc{};
            enc.encoder_id = e;
            if (ioctlRetry(DRM_IOCTL_MODE_GETENCODER, &enc) != 0) continue;
            if (e == conn.encoder_id && enc.crtc_id) {
                o.crtc = enc.crtc_id;
                break;
            }
            for (size_t i = 0; i < crtcs.size() && !o.crtc; ++i) {
                if (enc.possible_crtcs & (1u << i)) o.crtc = crtcs[i];
            }
        }
        if (!o.crtc) continue;
        if (isHdmi(o.type)) {
            hdmi = o;
            haveHdmi = true;
        } else if (!havePanel) {
            panelOut = o;
            havePanel = true;
        }
    }
    if (havePanel && builtinW == 0) {
        builtinW = panelOut.mode.hdisplay;
        builtinH = panelOut.mode.vdisplay;
    }
    /* A television that cannot hold the panel's frame would show nothing. */
    HostTvMode::Mode tv;
    tv.w = hdmi.mode.hdisplay;
    tv.h = hdmi.mode.vdisplay;
    if (haveHdmi && !HostTvMode::holdsPanel(tv, builtinW, builtinH)) {
        std::printf("[drm] HDMI: %ux%u is smaller than the panel; staying on the panel\n",
                    hdmi.mode.hdisplay, hdmi.mode.vdisplay);
        haveHdmi = false;
    }
    if (haveHdmi) {
        out = hdmi;
        return true;
    }
    if (havePanel) {
        out = panelOut;
        return true;
    }
    return false;
}

bool setCrtc(const Buffer& b) {
    drm_mode_crtc crtc{};
    crtc.crtc_id = current.crtc;
    crtc.fb_id = b.fb;
    crtc.set_connectors_ptr = reinterpret_cast<uintptr_t>(&current.connector);
    crtc.count_connectors = 1;
    crtc.mode = current.mode;
    crtc.mode_valid = 1;
    return ioctlRetry(DRM_IOCTL_MODE_SETCRTC, &crtc) == 0;
}

/* Turns a CRTC off, which also turns off what it was driving -- the panel's
 * backlight goes with it when the picture moves to a TV. */
void disableCrtc(uint32_t id) {
    drm_mode_crtc crtc{};
    crtc.crtc_id = id;
    ioctlRetry(DRM_IOCTL_MODE_SETCRTC, &crtc);
}

/* Waits for the flip asked for last frame, so its buffer is free to draw
 * into. Bounded: a flip that never completes must not hang the firmware. */
void waitFlip() {
    if (!flipPending) return;
    pollfd p{fd, POLLIN, 0};
    if (::poll(&p, 1, 50) > 0) {
        char events[1024];
        (void)::read(fd, events, sizeof(events));
    }
    flipPending = false;
}

/* Shows `o`: new buffers at its mode, its CRTC set, the old one turned off. */
bool use(const Output& o) {
    waitFlip();
    const uint32_t oldCrtc = current.crtc;
    freeBuffer(buffers[0]);
    freeBuffer(buffers[1]);
    current = o;
    if (!makeBuffer(buffers[0]) || !makeBuffer(buffers[1])) {
        std::fprintf(stderr, "[drm] could not create the frame buffers: %s\n", std::strerror(errno));
        return false;
    }
    if (oldCrtc && oldCrtc != current.crtc) disableCrtc(oldCrtc);
    if (!setCrtc(buffers[0])) {
        std::fprintf(stderr, "[drm] could not set the mode: %s\n", std::strerror(errno));
        return false;
    }
    front = 0;
    std::printf("[drm] %s: %ux%u@%u, connector %u, crtc %u\n", typeName(current.type),
                current.mode.hdisplay, current.mode.vdisplay, current.mode.vrefresh,
                current.connector, current.crtc);
    return true;
}

void openUevents() {
    uevents = ::socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (uevents < 0) return;
    sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = 1;   // the kernel's own uevents
    if (::bind(uevents, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(uevents);
        uevents = -1;
    }
}

/* Whether the kernel has said a DRM output came or went since last asked. */
bool hotplugEvent() {
    if (uevents < 0) return false;
    bool drm = false;
    char msg[4096];
    for (;;) {
        const ssize_t n = ::recv(uevents, msg, sizeof(msg) - 1, 0);
        if (n <= 0) break;
        msg[n] = '\0';
        bool subsystemDrm = false, hotplug = false;
        for (const char* p = msg; p < msg + n; p += std::strlen(p) + 1) {
            if (std::strcmp(p, "SUBSYSTEM=drm") == 0) subsystemDrm = true;
            if (std::strcmp(p, "HOTPLUG=1") == 0) hotplug = true;
        }
        if (subsystemDrm && hotplug) drm = true;
    }
    return drm;
}

}   // namespace

bool begin() {
    for (int card = 0; card < 4 && fd < 0; ++card) {
        char path[32];
        std::snprintf(path, sizeof(path), "/dev/dri/card%d", card);
        fd = ::open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) continue;
        drm_get_cap cap{DRM_CAP_DUMB_BUFFER, 0};
        Output o;
        if (ioctlRetry(DRM_IOCTL_GET_CAP, &cap) != 0 || !cap.value || !pickOutput(o)) {
            ::close(fd);
            fd = -1;
            continue;
        }
        if (!use(o)) {
            shutdown();
            return false;
        }
    }
    if (fd < 0) {
        std::fprintf(stderr, "[drm] no display: no /dev/dri card with a connected output\n");
        return false;
    }
    openUevents();
    return true;
}

void shutdown() {
    if (uevents >= 0) ::close(uevents);
    uevents = -1;
    if (fd < 0) return;
    waitFlip();
    freeBuffer(buffers[0]);
    freeBuffer(buffers[1]);
    ::close(fd);
    fd = -1;
    current = Output{};
}

int width() { return current.mode.hdisplay; }
int height() { return current.mode.vdisplay; }
int panelWidth() { return builtinW; }
int panelHeight() { return builtinH; }
bool panelShown() { return fd >= 0 && !isHdmi(current.type); }

bool outputChanged() {
    /* Not while off: use() would set a CRTC and light the output. The
     * kernel's events wait in the socket until it is back on. */
    if (fd < 0 || poweredOff) return false;
    const bool event = hotplugEvent();
    if (!event && !recheck) return false;
    const bool retry = recheck;
    recheck = false;
    Output o;
    if (!pickOutput(o)) return false;   // nothing connected for a moment: keep what there is
    /* The same output in the same mode is left alone -- unless it would not
     * come back on, when setting it again is the one thing left to try. */
    if (!retry && o.connector == current.connector && o.mode.hdisplay == current.mode.hdisplay &&
        o.mode.vdisplay == current.mode.vdisplay) {
        return false;
    }
    return use(o);
}

uint32_t* backBuffer(int& stride) {
    waitFlip();
    Buffer& b = buffers[front ^ 1];
    stride = static_cast<int>(b.pitch / 4);
    return b.pixels;
}

void flip() {
    if (fd < 0) return;
    const int back = front ^ 1;
    if (poweredOff) {   // kept for setPower(true) to show
        front = back;
        return;
    }
    drm_mode_crtc_page_flip pf{};
    pf.crtc_id = current.crtc;
    pf.fb_id = buffers[back].fb;
    pf.flags = DRM_MODE_PAGE_FLIP_EVENT;
    if (ioctlRetry(DRM_IOCTL_MODE_PAGE_FLIP, &pf) == 0) {
        flipPending = true;
    } else {
        /* A driver without page flips, or a CRTC something else changed:
         * set it outright, which waits for the blank itself. */
        setCrtc(buffers[back]);
    }
    front = back;
}

void setPower(bool on) {
    if (fd < 0 || !current.crtc || poweredOff == !on) return;
    waitFlip();
    if (on) {
        poweredOff = false;
        const bool ok = setCrtc(buffers[front]);
        std::printf("[drm] %s output %s\n", typeName(current.type), ok ? "on" : "did not come back on");
    } else {
        disableCrtc(current.crtc);
        poweredOff = true;
        std::printf("[drm] %s output off\n", typeName(current.type));
    }
    std::fflush(stdout);
}

}   // namespace HostDrm

#endif   // GUME_HOST_DRM
