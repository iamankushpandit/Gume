// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

/* The boot splash: what a handheld shows from power-on until Braino's first
 * screen, in place of the operating system's own logo.
 *
 * Unlike everything else the host draws, this is NOT the 320x240 canvas
 * scaled up. It is drawn at the panel's own resolution, because the mark is
 * vector artwork and deserves every pixel the glass has: the badge is
 * rasterised from tools/braino-badge.svg (compiled in, see
 * cmake/EmbedMark.cmake) with 4x4 supersampling, so its edges are smooth at
 * any size. The rasteriser is a port of tools/gen_logo_mask.py's -- the one
 * that makes the device's own bitmaps -- so the two cannot disagree about the
 * shape.
 *
 * Everything else it uses is the firmware's: the Dark theme's colours, the
 * accent the glow is made of, BRAINO_COPYRIGHT for the line at the bottom,
 * and TFT_eSPI's font 2 and font 4 at their native size for the text, which
 * is the same lettering the console uses. A thin bar shows how far the boot
 * has got; see host/CLAUDE.md for what that number measures. */

#include "HostSplash.h"

#include <Arduino.h>
#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <Fonts/Font16.h>
#include <Fonts/Font32rle.h>

#include "AppVersion.h"
#include "BrainoMarkSvg.h"
#include "HostPlatform.h"
#include "ui/Ui.h"

namespace {

/* ------------------------------------------------------------- the mark */

struct Point {
    double x, y;
};
struct Path {
    std::vector<std::vector<Point>> subpaths;
    bool evenOdd = false;
};

/* An SVG elliptical arc as points, by the spec's centre parameterisation --
 * tools/gen_logo_mask.py's flatten_arc(), line for line. */
void flattenArc(double x0, double y0, double rx, double ry, double rotation, bool large,
                bool sweep, double x1, double y1, std::vector<Point>& out) {
    if (rx == 0 || ry == 0 || (x0 == x1 && y0 == y1)) {
        out.push_back({x1, y1});
        return;
    }
    const double phi = rotation * M_PI / 180.0;
    const double cp = std::cos(phi), sp = std::sin(phi);
    const double dx2 = (x0 - x1) / 2, dy2 = (y0 - y1) / 2;
    const double x1p = cp * dx2 + sp * dy2;
    const double y1p = -sp * dx2 + cp * dy2;
    rx = std::fabs(rx);
    ry = std::fabs(ry);
    const double lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry);
    if (lam > 1) {
        rx *= std::sqrt(lam);
        ry *= std::sqrt(lam);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
    const double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
    double factor = den ? std::sqrt(std::max(0.0, num / den)) : 0.0;
    if (large == sweep) factor = -factor;
    const double cxp = factor * rx * y1p / ry;
    const double cyp = -factor * ry * x1p / rx;
    const double cx = cp * cxp - sp * cyp + (x0 + x1) / 2;
    const double cy = sp * cxp + cp * cyp + (y0 + y1) / 2;
    const double t0 = std::atan2((y1p - cyp) / ry, (x1p - cxp) / rx);
    const double t1 = std::atan2((-y1p - cyp) / ry, (-x1p - cxp) / rx);
    double sweepAngle = t1 - t0;
    if (sweep && sweepAngle < 0) sweepAngle += 2 * M_PI;
    if (!sweep && sweepAngle > 0) sweepAngle -= 2 * M_PI;
    constexpr int STEPS = 24;
    for (int i = 1; i <= STEPS; ++i) {
        const double t = t0 + sweepAngle * i / STEPS;
        const double ex = rx * std::cos(t), ey = ry * std::sin(t);
        out.push_back({cp * ex - sp * ey + cx, sp * ex + cp * ey + cy});
    }
}

/* One path's `d`: M, L, Z and arcs, as gen_logo_mask.py's parse_path(). */
std::vector<std::vector<Point>> parsePath(const std::string& d) {
    std::vector<std::vector<Point>> subs;
    std::vector<Point> cur;
    double x = 0, y = 0;
    char cmd = 0;
    size_t i = 0;
    auto number = [&](double& v) {
        while (i < d.size() && (d[i] == ' ' || d[i] == ',' || d[i] == '\n' || d[i] == '\t')) ++i;
        if (i >= d.size() || std::isalpha(static_cast<unsigned char>(d[i]))) return false;
        char* end = nullptr;
        v = std::strtod(d.c_str() + i, &end);
        if (end == d.c_str() + i) return false;
        i = static_cast<size_t>(end - d.c_str());
        return true;
    };
    while (i < d.size()) {
        const char c = d[i];
        if (std::isalpha(static_cast<unsigned char>(c))) {
            cmd = c;
            ++i;
            if (cmd == 'Z' || cmd == 'z') {
                if (cur.size() >= 3) subs.push_back(cur);
                cur.clear();
            }
            continue;
        }
        if (c == ' ' || c == ',' || c == '\n' || c == '\t' || cmd == 0) {
            ++i;
            continue;
        }
        if (cmd == 'M' || cmd == 'm' || cmd == 'L' || cmd == 'l') {
            double a = 0, b = 0;
            if (!number(a) || !number(b)) break;
            const bool rel = (cmd == 'm' || cmd == 'l');
            x = rel ? x + a : a;
            y = rel ? y + b : b;
            if (cmd == 'M' || cmd == 'm') {
                if (cur.size() >= 3) subs.push_back(cur);
                cur.assign(1, {x, y});
                cmd = (cmd == 'm') ? 'l' : 'L';   // implicit line-to, per the spec
            } else {
                cur.push_back({x, y});
            }
        } else if (cmd == 'A' || cmd == 'a') {
            double v[7];
            for (double& n : v) {
                if (!number(n)) return subs;
            }
            const double ex = cmd == 'a' ? x + v[5] : v[5];
            const double ey = cmd == 'a' ? y + v[6] : v[6];
            flattenArc(x, y, v[0], v[1], v[2], v[3] != 0, v[4] != 0, ex, ey, cur);
            x = ex;
            y = ey;
        } else {
            ++i;   // nothing else appears in the badge
        }
    }
    if (cur.size() >= 3) subs.push_back(cur);
    return subs;
}

struct Mark {
    double vx = 0, vy = 0, vw = 1, vh = 1;
    std::vector<Path> paths;
};

const Mark& mark() {
    static Mark m;
    static bool loaded = false;
    if (loaded) return m;
    loaded = true;
    const std::string svg(BRAINO_MARK_SVG);
    const size_t vb = svg.find("viewBox=\"");
    if (vb != std::string::npos) {
        std::sscanf(svg.c_str() + vb + 9, "%lf %lf %lf %lf", &m.vx, &m.vy, &m.vw, &m.vh);
    }
    for (size_t at = svg.find("<path"); at != std::string::npos; at = svg.find("<path", at + 5)) {
        const size_t end = svg.find('>', at);
        const std::string tag = svg.substr(at, end - at);
        const size_t d = tag.find(" d=\"");
        if (d == std::string::npos) continue;
        const size_t close = tag.find('"', d + 4);
        Path p;
        p.evenOdd = tag.find("evenodd") != std::string::npos;
        p.subpaths = parsePath(tag.substr(d + 4, close - d - 4));
        if (!p.subpaths.empty()) m.paths.push_back(std::move(p));
    }
    return m;
}

/* Coverage 0..1 for the whole badge, `height` tall at the artwork's aspect,
 * by the same winding-number scanline fill as gen_logo_mask.py's
 * rasterise(), supersampled `ss` x `ss`. */
std::vector<float> rasterise(int height, int& width, int ss) {
    const Mark& m = mark();
    width = static_cast<int>(std::lround(m.vw * height / m.vh));
    const int W = width * ss, H = height * ss;
    const double scale = static_cast<double>(height * ss) / m.vh;
    std::vector<uint8_t> ink(static_cast<size_t>(W) * H, 0);
    std::vector<int> wind(static_cast<size_t>(W) * H);
    for (const Path& path : m.paths) {
        std::fill(wind.begin(), wind.end(), 0);
        for (const auto& pts : path.subpaths) {
            for (size_t k = 0; k < pts.size(); ++k) {
                const Point a = pts[k], b = pts[(k + 1) % pts.size()];
                const double x0 = (a.x - m.vx) * scale, y0 = (a.y - m.vy) * scale;
                const double x1 = (b.x - m.vx) * scale, y1 = (b.y - m.vy) * scale;
                if (y0 == y1) continue;
                const int step = y1 > y0 ? 1 : -1;
                const double lo = std::min(y0, y1), hi = std::max(y0, y1);
                const int j0 = std::max(0, static_cast<int>(std::ceil(lo - 0.5)));
                const int j1 = std::min(H - 1, static_cast<int>(std::floor(hi - 0.5)));
                for (int row = j0; row <= j1; ++row) {
                    const double t = (row + 0.5 - y0) / (y1 - y0);
                    int xs = static_cast<int>(std::ceil(x0 + (x1 - x0) * t - 0.5));
                    xs = std::max(0, std::min(W, xs));
                    for (int col = xs; col < W; ++col) wind[static_cast<size_t>(row) * W + col] += step;
                }
            }
        }
        for (size_t p = 0; p < ink.size(); ++p) {
            if (path.evenOdd ? (wind[p] % 2 != 0) : (wind[p] != 0)) ink[p] = 1;
        }
    }
    std::vector<float> cover(static_cast<size_t>(width) * height, 0.0f);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int sum = 0;
            for (int sy = 0; sy < ss; ++sy) {
                for (int sx = 0; sx < ss; ++sx) sum += ink[static_cast<size_t>(y * ss + sy) * W + x * ss + sx];
            }
            cover[static_cast<size_t>(y) * width + x] = static_cast<float>(sum) / (ss * ss);
        }
    }
    return cover;
}

/* Three box passes each way is a close Gaussian, and cheap at this size. */
void blur(std::vector<float>& a, int w, int h, int radius) {
    std::vector<float> tmp(a.size());
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {
            float sum = 0;
            for (int x = -radius; x <= radius; ++x) sum += a[static_cast<size_t>(y) * w + std::max(0, std::min(w - 1, x))];
            for (int x = 0; x < w; ++x) {
                tmp[static_cast<size_t>(y) * w + x] = sum / (2 * radius + 1);
                sum += a[static_cast<size_t>(y) * w + std::min(w - 1, x + radius + 1)];
                sum -= a[static_cast<size_t>(y) * w + std::max(0, x - radius)];
            }
        }
        for (int x = 0; x < w; ++x) {
            float sum = 0;
            for (int y = -radius; y <= radius; ++y) sum += tmp[static_cast<size_t>(std::max(0, std::min(h - 1, y))) * w + x];
            for (int y = 0; y < h; ++y) {
                a[static_cast<size_t>(y) * w + x] = sum / (2 * radius + 1);
                sum += tmp[static_cast<size_t>(std::min(h - 1, y + radius + 1)) * w + x];
                sum -= tmp[static_cast<size_t>(std::max(0, y - radius)) * w + x];
            }
        }
    }
}

/* ------------------------------------------------------------- pixels */

struct Rgb {
    float r, g, b;
};

Rgb fromRgb565(uint16_t c) {
    return {((c >> 11) & 0x1F) / 31.0f, ((c >> 5) & 0x3F) / 63.0f, (c & 0x1F) / 31.0f};
}

Rgb mix(Rgb a, Rgb b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

struct Canvas {
    int w, h;
    std::vector<Rgb> px;
    void blend(int x, int y, Rgb c, float alpha) {
        if (x < 0 || y < 0 || x >= w || y >= h || alpha <= 0) return;
        Rgb& p = px[static_cast<size_t>(y) * w + x];
        p = mix(p, c, std::min(alpha, 1.0f));
    }
    void add(int x, int y, Rgb c, float amount) {
        if (x < 0 || y < 0 || x >= w || y >= h || amount <= 0) return;
        Rgb& p = px[static_cast<size_t>(y) * w + x];
        p.r = std::min(1.0f, p.r + c.r * amount);
        p.g = std::min(1.0f, p.g + c.g * amount);
        p.b = std::min(1.0f, p.b + c.b * amount);
    }
};

/* TFT_eSPI's font 2 (bitmap) and font 4 (run-length), at their native size,
 * decoded exactly as HostTft.cpp decodes them for the panel. */
int glyphWidth(int font, char c) {
    if (c < 32 || c > 126) c = ' ';
    return font == 4 ? widtbl_f32[c - 32] : widtbl_f16[c - 32];
}

int textWidth(int font, const char* s) {
    int w = 0;
    for (; *s; ++s) w += glyphWidth(font, *s);
    return w;
}

void drawText(Canvas& cv, int font, const char* s, int x, int y, Rgb colour, float alpha) {
    for (; *s; ++s) {
        char c = *s;
        if (c < 32 || c > 126) c = ' ';
        const int gw = glyphWidth(font, c);
        if (font == 2) {
            const unsigned char* data = chrtbl_f16[c - 32];
            const int rowBytes = (gw + 6) / 8;
            for (int row = 0; row < chr_hgt_f16; ++row) {
                for (int col = 0; col < gw && col < rowBytes * 8; ++col) {
                    if (data[row * rowBytes + col / 8] & (0x80 >> (col % 8))) cv.blend(x + col, y + row, colour, alpha);
                }
            }
        } else {
            const unsigned char* data = chrtbl_f32[c - 32];
            const int total = gw * chr_hgt_f32;
            for (int pc = 0; pc < total;) {
                const uint8_t b = *data++;
                int run = (b & 0x7F) + 1;
                for (; run > 0 && pc < total; --run, ++pc) {
                    if (b & 0x80) cv.blend(x + pc % gw, y + pc / gw, colour, alpha);
                }
            }
        }
        x += gw;
    }
}

void roundedBar(Canvas& cv, int x, int y, int w, int h, Rgb colour, float alpha) {
    const float r = h / 2.0f;
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            /* Distance from the bar's centre line, for round, smooth ends. */
            const float cx = std::max(r, std::min(w - r, i + 0.5f));
            const float d = std::hypot(i + 0.5f - cx, j + 0.5f - r);
            const float a = std::max(0.0f, std::min(1.0f, r - d + 0.5f));
            cv.blend(x + i, y + j, colour, alpha * a);
        }
    }
}

/* ------------------------------------------------------------- layout */

/* Proportions of the panel's height, so the picture is the same at any
 * resolution a handheld has. The mark takes the middle; the bar and the
 * copyright sit in the lower band, small and quiet. */
constexpr float BADGE_H = 0.60f;
constexpr float BADGE_TOP = 0.09f;
constexpr float BAR_Y = 0.79f;
constexpr float BAR_W = 0.42f;    // of the width
constexpr float COPYRIGHT_Y = 0.925f;
constexpr float GLOW_RADIUS = 0.022f;
constexpr float GLOW_STRENGTH = 0.95f;
/* gen_logo_mask.py's WORDMARK_TOP: where the letters start, in SVG units. */
constexpr double WORDMARK_TOP = 300.0;

/* The picture in two layers: everything that never moves, and the glow on
 * its own, so a frame of the breathing glow is one multiply-add a pixel
 * rather than a fresh rasterise and blur. */
struct Base {
    int w = 0, h = 0;
    Canvas canvas{0, 0, {}};
    std::vector<float> glow;   // halo intensity per pixel, before strength
    Rgb glowColour{0, 0, 0};
};

const Base& base(int w, int h) {
    static Base b;
    if (b.w == w && b.h == h) return b;
    b.w = w;
    b.h = h;
    Ui::setTheme(Ui::Theme::Dark);
    const Rgb bg = fromRgb565(Ui::bg());
    const Rgb ink = fromRgb565(Ui::text());
    const Rgb glow = fromRgb565(Ui::accent());
    b.canvas = Canvas{w, h, std::vector<Rgb>(static_cast<size_t>(w) * h, bg)};

    const int badgeH = static_cast<int>(h * BADGE_H);
    int badgeW = 0;
    const std::vector<float> cover = rasterise(badgeH, badgeW, 4);
    const int ox = (w - badgeW) / 2;
    const int oy = static_cast<int>(h * BADGE_TOP);

    /* The glow: the mark's own coverage, blurred, laid under it in the
     * accent colour. Added rather than painted, so it lifts the background
     * the way light does instead of covering it. */
    const int radius = std::max(2, static_cast<int>(h * GLOW_RADIUS));
    const int pad = radius * 4;
    const int gw = badgeW + 2 * pad, gh = badgeH + 2 * pad;
    std::vector<float> halo(static_cast<size_t>(gw) * gh, 0.0f);
    for (int y = 0; y < badgeH; ++y) {
        for (int x = 0; x < badgeW; ++x) halo[static_cast<size_t>(y + pad) * gw + x + pad] = cover[static_cast<size_t>(y) * badgeW + x];
    }
    blur(halo, gw, gh, radius);
    b.glow.assign(static_cast<size_t>(w) * h, 0.0f);
    b.glowColour = glow;
    for (int y = 0; y < gh; ++y) {
        const int cy = oy - pad + y;
        if (cy < 0 || cy >= h) continue;
        for (int x = 0; x < gw; ++x) {
            const int cx = ox - pad + x;
            if (cx < 0 || cx >= w) continue;
            b.glow[static_cast<size_t>(cy) * w + cx] = halo[static_cast<size_t>(y) * gw + x];
        }
    }
    for (int y = 0; y < badgeH; ++y) {
        for (int x = 0; x < badgeW; ++x) b.canvas.blend(ox + x, oy + y, ink, cover[static_cast<size_t>(y) * badgeW + x]);
    }

    /* The trade mark sign, as the device's own badge carries it (see
     * TM_HEIGHT in gen_logo_mask.py): small, at the top right of the
     * wordmark's ink rather than the brain's. */
    const Mark& m = mark();
    const int wordRow = static_cast<int>((WORDMARK_TOP - m.vy) * badgeH / m.vh);
    int inkTop = -1, inkRight = -1;
    for (int y = std::max(0, wordRow); y < badgeH; ++y) {
        for (int x = 0; x < badgeW; ++x) {
            if (cover[static_cast<size_t>(y) * badgeW + x] > 0.5f) {
                if (inkTop < 0) inkTop = y;
                inkRight = std::max(inkRight, x);
            }
        }
    }
    if (inkTop >= 0) {
        drawText(b.canvas, 2, "TM", ox + inkRight + h / 120, oy + inkTop - h / 240, ink, 0.85f);
    }

    const Rgb muted = fromRgb565(Ui::muted());
    const char* line = BRAINO_COPYRIGHT;
    drawText(b.canvas, 2, line, (w - textWidth(2, line)) / 2, static_cast<int>(h * COPYRIGHT_Y), muted, 0.9f);
    return b;
}

}   // namespace

namespace HostSplash {

void render(uint8_t* rgb, int w, int h, float progress, float glow) {
    const Base& b = base(w, h);
    Canvas cv = b.canvas;
    /* Light is added, not painted: the halo lifts what is under it, and on
     * the near-white ink it saturates to white and changes nothing. */
    const float strength = GLOW_STRENGTH * std::max(0.0f, glow);
    for (size_t i = 0; i < cv.px.size(); ++i) {
        const float g = b.glow[i] * strength;
        if (g <= 0.0f) continue;
        Rgb& p = cv.px[i];
        p.r = std::min(1.0f, p.r + b.glowColour.r * g);
        p.g = std::min(1.0f, p.g + b.glowColour.g * g);
        p.b = std::min(1.0f, p.b + b.glowColour.b * g);
    }
    if (progress >= 0) {
        progress = std::min(1.0f, progress);
        const int barW = static_cast<int>(w * BAR_W);
        const int barH = std::max(3, h / 120);
        const int barX = (w - barW) / 2;
        const int barY = static_cast<int>(h * BAR_Y);
        roundedBar(cv, barX, barY, barW, barH, fromRgb565(Ui::panel()), 1.0f);
        const int fill = static_cast<int>(barW * progress);
        if (fill >= barH) roundedBar(cv, barX, barY, fill, barH, fromRgb565(Ui::accent()), 1.0f);
        char pct[8];
        std::snprintf(pct, sizeof(pct), "%d%%", static_cast<int>(progress * 100.0f + 0.5f));
        drawText(cv, 2, pct, (w - textWidth(2, pct)) / 2, barY + barH + h / 60, fromRgb565(Ui::muted()), 0.9f);
    }
    for (size_t i = 0; i < cv.px.size(); ++i) {
        rgb[i * 3 + 0] = static_cast<uint8_t>(cv.px[i].r * 255.0f + 0.5f);
        rgb[i * 3 + 1] = static_cast<uint8_t>(cv.px[i].g * 255.0f + 0.5f);
        rgb[i * 3 + 2] = static_cast<uint8_t>(cv.px[i].b * 255.0f + 0.5f);
    }
}

/* What the bar measures, honestly.
 *
 * Braino is started near the END of the operating system's boot -- by the
 * time it can draw anything, most of the boot has happened behind the static
 * boot logo. So the bar opens at the share of a boot that has already gone:
 * the time since power-on (/proc/uptime) against how long the previous boot
 * took to reach this point, which is written down every time. It then fills
 * the rest as Braino readies, and only says 100% when the first screen is
 * about to appear. The opening number is an estimate from last time; the
 * 100% is not. A first boot, with nothing written down, assumes 20 s. */
void runBootPhase() {
    const char* flag = std::getenv("BRAINO_BOOT_SPLASH");
    if (!flag || flag[0] != '1') return;

    float uptime = 0.0f;
    if (FILE* f = std::fopen("/proc/uptime", "r")) {
        if (std::fscanf(f, "%f", &uptime) != 1) uptime = 0.0f;
        std::fclose(f);
    }
    const std::string record = HostPlatform::dataDir() + "/boot-seconds";
    float expected = 20.0f;
    if (FILE* f = std::fopen(record.c_str(), "r")) {
        float v = 0.0f;
        if (std::fscanf(f, "%f", &v) == 1 && v > 1.0f && v < 600.0f) expected = v;
        std::fclose(f);
    }
    const float start = std::max(0.05f, std::min(0.95f, uptime / expected));

    int w = 0, h = 0;
    HostPlatform::outputSize(w, h);
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    /* Long enough for one full breath of the glow before the first screen. */
    constexpr uint32_t FILL_MS = 1800;
    constexpr uint32_t HOLD_MS = 400;
    /* The glow breathes between 35% and 115% of the static logo's strength,
     * once every 1.6 s -- slow, so it reads as light rather than flicker,
     * which matters on a device for young players. It starts at exactly 100%,
     * the static boot logo's strength, so the hand-over from the logo the OS
     * showed to this picture has no step in it. */
    constexpr float GLOW_MID = 0.75f, GLOW_SWING = 0.40f, GLOW_PERIOD_MS = 1600.0f;
    const float phase0 = std::asin((1.0f - GLOW_MID) / GLOW_SWING);
    const uint32_t t0 = SDL_GetTicks();
    for (;;) {
        const uint32_t t = SDL_GetTicks() - t0;
        const float s = std::min(1.0f, static_cast<float>(t) / FILL_MS);
        const float eased = s * s * (3.0f - 2.0f * s);
        const float glow = GLOW_MID + GLOW_SWING * std::sin(phase0 + 2.0f * static_cast<float>(M_PI) * t / GLOW_PERIOD_MS);
        render(rgb.data(), w, h, start + (1.0f - start) * eased, glow);
        if (!HostPlatform::presentImage(rgb.data(), w, h)) return;
        if (t >= FILL_MS + HOLD_MS) break;
        SDL_Delay(16);
    }

    float ready = uptime;
    if (FILE* f = std::fopen("/proc/uptime", "r")) {
        if (std::fscanf(f, "%f", &ready) != 1) ready = uptime;
        std::fclose(f);
    }
    std::printf("[splash] boot phase %u ms, uptime %.1f s (bar opened at %d%%)\n",
                static_cast<unsigned>(SDL_GetTicks() - t0), ready,
                static_cast<int>(start * 100.0f + 0.5f));
    /* Only a real boot is a measurement. A program started on a machine that
     * has been up for hours says nothing about how long booting takes. */
    if (ready > 600.0f) return;
    if (FILE* f = std::fopen(record.c_str(), "w")) {
        std::fprintf(f, "%.1f\n", ready);
        std::fclose(f);
    }
}

bool writeBmp(const char* path, int w, int h, float progress, float glow) {
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    render(rgb.data(), w, h, progress, glow);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(rgb.data(), w, h, 24, w * 3, SDL_PIXELFORMAT_RGB24);
    if (!s) return false;
    const bool ok = SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

}   // namespace HostSplash
