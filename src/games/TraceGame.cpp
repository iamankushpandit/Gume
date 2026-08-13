#include "TraceGame.h"

namespace {

constexpr int16_t DRAW_X = 80;
constexpr int16_t DRAW_Y = 36;
constexpr int16_t DRAW_W = 160;
constexpr int16_t DRAW_H = 180;
constexpr int16_t COORD_MAX = 200;
constexpr int16_t HIT_DIST2 = 20 * 20;

constexpr Rect PREV_BTN{8,   200, 60, 32};
constexpr Rect NEXT_BTN{252, 200, 60, 32};

// --- Stroke data for A-Z, 0-9 -------------------------------------------
// Each glyph is defined as polyline strokes in a 0-200 coordinate space.
// Coordinates are stored as flat x,y pairs.

// A
static const int16_t A_s0[] = {20,200, 100,10, 180,200};
static const int16_t A_s1[] = {55,120, 145,120};
static const TraceGame::Stroke A_strokes[] = {{A_s0,3},{A_s1,2}};

// B
static const int16_t B_s0[] = {30,200, 30,10, 130,10, 160,30, 160,60, 130,100, 30,100};
static const int16_t B_s1[] = {30,100, 140,100, 170,130, 170,170, 140,200, 30,200};
static const TraceGame::Stroke B_strokes[] = {{B_s0,7},{B_s1,6}};

// C
static const int16_t C_s0[] = {170,40, 130,10, 70,10, 30,50, 30,160, 70,200, 130,200, 170,170};
static const TraceGame::Stroke C_strokes[] = {{C_s0,8}};

// D
static const int16_t D_s0[] = {30,200, 30,10, 110,10, 160,50, 170,100, 160,160, 110,200, 30,200};
static const TraceGame::Stroke D_strokes[] = {{D_s0,8}};

// E
static const int16_t E_s0[] = {160,10, 30,10, 30,200, 160,200};
static const int16_t E_s1[] = {30,105, 130,105};
static const TraceGame::Stroke E_strokes[] = {{E_s0,4},{E_s1,2}};

// F
static const int16_t F_s0[] = {160,10, 30,10, 30,200};
static const int16_t F_s1[] = {30,105, 130,105};
static const TraceGame::Stroke F_strokes[] = {{F_s0,3},{F_s1,2}};

// G
static const int16_t G_s0[] = {170,40, 130,10, 70,10, 30,50, 30,160, 70,200, 130,200, 170,170, 170,110, 120,110};
static const TraceGame::Stroke G_strokes[] = {{G_s0,10}};

// H
static const int16_t H_s0[] = {30,10, 30,200};
static const int16_t H_s1[] = {170,10, 170,200};
static const int16_t H_s2[] = {30,105, 170,105};
static const TraceGame::Stroke H_strokes[] = {{H_s0,2},{H_s1,2},{H_s2,2}};

// I
static const int16_t I_s0[] = {60,10, 140,10};
static const int16_t I_s1[] = {100,10, 100,200};
static const int16_t I_s2[] = {60,200, 140,200};
static const TraceGame::Stroke I_strokes[] = {{I_s0,2},{I_s1,2},{I_s2,2}};

// J
static const int16_t J_s0[] = {60,10, 160,10};
static const int16_t J_s1[] = {130,10, 130,160, 100,200, 60,200, 30,170};
static const TraceGame::Stroke J_strokes[] = {{J_s0,2},{J_s1,5}};

// K
static const int16_t K_s0[] = {40,10, 40,200};
static const int16_t K_s1[] = {170,10, 40,110, 170,200};
static const TraceGame::Stroke K_strokes[] = {{K_s0,2},{K_s1,3}};

// L
static const int16_t L_s0[] = {40,10, 40,200, 170,200};
static const TraceGame::Stroke L_strokes[] = {{L_s0,3}};

// M
static const int16_t M_s0[] = {20,200, 20,10, 100,120, 180,10, 180,200};
static const TraceGame::Stroke M_strokes[] = {{M_s0,5}};

// N
static const int16_t N_s0[] = {30,200, 30,10, 170,200, 170,10};
static const TraceGame::Stroke N_strokes[] = {{N_s0,4}};

// O
static const int16_t O_s0[] = {100,10, 50,10, 20,50, 20,160, 50,200, 150,200, 180,160, 180,50, 150,10, 100,10};
static const TraceGame::Stroke O_strokes[] = {{O_s0,10}};

// P
static const int16_t P_s0[] = {30,200, 30,10, 130,10, 170,40, 170,70, 130,105, 30,105};
static const TraceGame::Stroke P_strokes[] = {{P_s0,7}};

// Q
static const int16_t Q_s0[] = {100,10, 50,10, 20,50, 20,160, 50,200, 150,200, 180,160, 180,50, 150,10, 100,10};
static const int16_t Q_s1[] = {130,150, 190,210};
static const TraceGame::Stroke Q_strokes[] = {{Q_s0,10},{Q_s1,2}};

// R
static const int16_t R_s0[] = {30,200, 30,10, 130,10, 160,40, 160,70, 130,105, 30,105};
static const int16_t R_s1[] = {100,105, 170,200};
static const TraceGame::Stroke R_strokes[] = {{R_s0,7},{R_s1,2}};

// S
static const int16_t S_s0[] = {160,40, 130,10, 70,10, 30,40, 30,70, 70,105, 130,105, 170,140, 170,170, 130,200, 70,200, 40,170};
static const TraceGame::Stroke S_strokes[] = {{S_s0,12}};

// T
static const int16_t T_s0[] = {10,10, 190,10};
static const int16_t T_s1[] = {100,10, 100,200};
static const TraceGame::Stroke T_strokes[] = {{T_s0,2},{T_s1,2}};

// U
static const int16_t U_s0[] = {30,10, 30,160, 60,200, 140,200, 170,160, 170,10};
static const TraceGame::Stroke U_strokes[] = {{U_s0,6}};

// V
static const int16_t V_s0[] = {20,10, 100,200, 180,10};
static const TraceGame::Stroke V_strokes[] = {{V_s0,3}};

// W
static const int16_t W_s0[] = {10,10, 50,200, 100,80, 150,200, 190,10};
static const TraceGame::Stroke W_strokes[] = {{W_s0,5}};

// X
static const int16_t X_s0[] = {20,10, 180,200};
static const int16_t X_s1[] = {180,10, 20,200};
static const TraceGame::Stroke X_strokes[] = {{X_s0,2},{X_s1,2}};

// Y
static const int16_t Y_s0[] = {20,10, 100,110};
static const int16_t Y_s1[] = {180,10, 100,110};
static const int16_t Y_s2[] = {100,110, 100,200};
static const TraceGame::Stroke Y_strokes[] = {{Y_s0,2},{Y_s1,2},{Y_s2,2}};

// Z
static const int16_t Z_s0[] = {20,10, 180,10, 20,200, 180,200};
static const TraceGame::Stroke Z_strokes[] = {{Z_s0,4}};

// 0
static const int16_t D0_s0[] = {100,10, 50,10, 20,50, 20,160, 50,200, 150,200, 180,160, 180,50, 150,10, 100,10};
static const int16_t D0_s1[] = {150,40, 50,170};
static const TraceGame::Stroke D0_strokes[] = {{D0_s0,10},{D0_s1,2}};

// 1
static const int16_t D1_s0[] = {60,50, 100,10, 100,200};
static const int16_t D1_s1[] = {60,200, 140,200};
static const TraceGame::Stroke D1_strokes[] = {{D1_s0,3},{D1_s1,2}};

// 2
static const int16_t D2_s0[] = {30,50, 50,20, 100,10, 150,20, 170,50, 170,80, 30,200, 170,200};
static const TraceGame::Stroke D2_strokes[] = {{D2_s0,8}};

// 3
static const int16_t D3_s0[] = {40,30, 70,10, 130,10, 160,40, 160,70, 130,100, 100,105};
static const int16_t D3_s1[] = {100,105, 140,110, 170,140, 170,170, 140,200, 70,200, 40,180};
static const TraceGame::Stroke D3_strokes[] = {{D3_s0,7},{D3_s1,7}};

// 4
static const int16_t D4_s0[] = {140,200, 140,10, 20,140, 180,140};
static const TraceGame::Stroke D4_strokes[] = {{D4_s0,4}};

// 5
static const int16_t D5_s0[] = {160,10, 40,10, 30,100, 120,90, 160,120, 160,160, 130,200, 60,200, 30,180};
static const TraceGame::Stroke D5_strokes[] = {{D5_s0,9}};

// 6
static const int16_t D6_s0[] = {150,20, 120,10, 70,10, 30,50, 30,160, 60,200, 140,200, 170,170, 170,130, 140,100, 60,100, 30,120};
static const TraceGame::Stroke D6_strokes[] = {{D6_s0,12}};

// 7
static const int16_t D7_s0[] = {30,10, 170,10, 80,200};
static const TraceGame::Stroke D7_strokes[] = {{D7_s0,3}};

// 8
static const int16_t D8_s0[] = {100,105, 60,105, 30,80, 30,40, 60,10, 140,10, 170,40, 170,80, 140,105, 100,105};
static const int16_t D8_s1[] = {100,105, 50,105, 20,130, 20,170, 50,200, 150,200, 180,170, 180,130, 150,105, 100,105};
static const TraceGame::Stroke D8_strokes[] = {{D8_s0,10},{D8_s1,10}};

// 9
static const int16_t D9_s0[] = {170,90, 140,105, 60,105, 30,70, 30,40, 60,10, 140,10, 170,40, 170,160, 140,200, 70,200, 40,180};
static const TraceGame::Stroke D9_strokes[] = {{D9_s0,12}};

static const TraceGame::Glyph GLYPHS[] = {
    {'A', A_strokes, 2}, {'B', B_strokes, 2}, {'C', C_strokes, 1},
    {'D', D_strokes, 1}, {'E', E_strokes, 2}, {'F', F_strokes, 2},
    {'G', G_strokes, 1}, {'H', H_strokes, 3}, {'I', I_strokes, 3},
    {'J', J_strokes, 2}, {'K', K_strokes, 2}, {'L', L_strokes, 1},
    {'M', M_strokes, 1}, {'N', N_strokes, 1}, {'O', O_strokes, 1},
    {'P', P_strokes, 1}, {'Q', Q_strokes, 2}, {'R', R_strokes, 2},
    {'S', S_strokes, 1}, {'T', T_strokes, 2}, {'U', U_strokes, 1},
    {'V', V_strokes, 1}, {'W', W_strokes, 1}, {'X', X_strokes, 2},
    {'Y', Y_strokes, 3}, {'Z', Z_strokes, 1},
    {'0', D0_strokes, 2}, {'1', D1_strokes, 2}, {'2', D2_strokes, 1},
    {'3', D3_strokes, 2}, {'4', D4_strokes, 1}, {'5', D5_strokes, 1},
    {'6', D6_strokes, 1}, {'7', D7_strokes, 1}, {'8', D8_strokes, 2},
    {'9', D9_strokes, 1},
};

constexpr uint8_t GLYPH_COUNT = sizeof(GLYPHS) / sizeof(GLYPHS[0]);

int32_t distToSegment2(int16_t px, int16_t py,
                       int16_t ax, int16_t ay,
                       int16_t bx, int16_t by) {
    int32_t dx = bx - ax;
    int32_t dy = by - ay;
    int32_t len2 = dx * dx + dy * dy;
    if (len2 == 0) {
        int32_t ex = px - ax;
        int32_t ey = py - ay;
        return ex * ex + ey * ey;
    }
    int32_t t = ((px - ax) * dx + (py - ay) * dy);
    if (t < 0) t = 0;
    else if (t > len2) t = len2;
    int32_t projX = (int32_t)ax * len2 + t * dx;
    int32_t projY = (int32_t)ay * len2 + t * dy;
    int32_t ex = (int32_t)px * len2 - projX;
    int32_t ey = (int32_t)py * len2 - projY;
    return (ex / len2) * (ex / len2) + (ey / len2) * (ey / len2);
}

}

const char* TraceGame::title() const {
    return "Trace";
}

void TraceGame::begin(GameHost& host) {
    glyphIndex_ = 0;
    loadGlyph();
    markFullDirty();
}

void TraceGame::loadGlyph() {
    complete_ = false;
    completeAt_ = 0;
    buildSegments();
    for (uint8_t i = 0; i < MAX_SEGMENTS; ++i) traced_[i] = false;
}

int16_t TraceGame::scaleX(int16_t nx) const {
    return DRAW_X + (int16_t)((int32_t)nx * DRAW_W / COORD_MAX);
}

int16_t TraceGame::scaleY(int16_t ny) const {
    return DRAW_Y + (int16_t)((int32_t)ny * DRAW_H / COORD_MAX);
}

void TraceGame::buildSegments() {
    totalSegments_ = 0;
    const Glyph& g = GLYPHS[glyphIndex_];
    for (uint8_t s = 0; s < g.strokeCount; ++s) {
        const Stroke& st = g.strokes[s];
        for (uint8_t i = 0; i + 1 < st.count && totalSegments_ < MAX_SEGMENTS; ++i) {
            segs_[totalSegments_].x1 = scaleX(st.pts[i * 2]);
            segs_[totalSegments_].y1 = scaleY(st.pts[i * 2 + 1]);
            segs_[totalSegments_].x2 = scaleX(st.pts[(i + 1) * 2]);
            segs_[totalSegments_].y2 = scaleY(st.pts[(i + 1) * 2 + 1]);
            ++totalSegments_;
        }
    }
}

uint8_t TraceGame::tracedCount() const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < totalSegments_; ++i) {
        if (traced_[i]) ++n;
    }
    return n;
}

void TraceGame::update(GameHost& host, const TouchPoint& touch) {
    if (complete_) {
        if (touch.justPressed && millis() - completeAt_ > 600) {
            glyphIndex_ = (glyphIndex_ + 1) % GLYPH_COUNT;
            loadGlyph();
            markFullDirty();
        }
        return;
    }

    if (touch.justPressed) {
        if (PREV_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphIndex_ = (glyphIndex_ + GLYPH_COUNT - 1) % GLYPH_COUNT;
            loadGlyph();
            markFullDirty();
            return;
        }
        if (NEXT_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphIndex_ = (glyphIndex_ + 1) % GLYPH_COUNT;
            loadGlyph();
            markFullDirty();
            return;
        }
    }

    if (touch.down) {
        bool changed = false;
        for (uint8_t i = 0; i < totalSegments_; ++i) {
            if (traced_[i]) continue;
            int32_t d2 = distToSegment2(touch.x, touch.y,
                                        segs_[i].x1, segs_[i].y1,
                                        segs_[i].x2, segs_[i].y2);
            if (d2 < HIT_DIST2) {
                traced_[i] = true;
                changed = true;
            }
        }
        if (changed) {
            markDirty();
            if (tracedCount() >= totalSegments_) {
                complete_ = true;
                completeAt_ = millis();
                host.board().beepOk();
                markFullDirty();
            }
        }
    }
}

void TraceGame::drawGuide(TFT_eSPI& tft) {
    const Glyph& g = GLYPHS[glyphIndex_];

    for (uint8_t i = 0; i < totalSegments_; ++i) {
        const Seg& s = segs_[i];
        if (traced_[i]) {
            tft.drawLine(s.x1, s.y1, s.x2, s.y2, Ui::success());
            tft.drawLine(s.x1 + 1, s.y1, s.x2 + 1, s.y2, Ui::success());
            tft.drawLine(s.x1, s.y1 + 1, s.x2, s.y2 + 1, Ui::success());
        } else {
            int32_t dx = s.x2 - s.x1;
            int32_t dy = s.y2 - s.y1;
            int32_t len2 = dx * dx + dy * dy;
            float len = sqrtf((float)len2);
            int steps = max(1, (int)(len / 6.0f));
            for (int j = 0; j <= steps; j += 2) {
                int16_t px = s.x1 + (int16_t)(dx * j / steps);
                int16_t py = s.y1 + (int16_t)(dy * j / steps);
                tft.fillCircle(px, py, 2, Ui::muted());
            }
        }
    }

    for (uint8_t s = 0; s < g.strokeCount; ++s) {
        const Stroke& st = g.strokes[s];
        int16_t sx = scaleX(st.pts[0]);
        int16_t sy = scaleY(st.pts[1]);
        tft.fillCircle(sx, sy, 5, Ui::warning());
        tft.drawCircle(sx, sy, 5, Ui::outline());
    }
}

void TraceGame::drawProgress(TFT_eSPI& tft) {
    uint8_t pct = totalSegments_ > 0
        ? (uint8_t)((uint16_t)tracedCount() * 100 / totalSegments_)
        : 0;
    int16_t barW = 120;
    int16_t barX = (SCREEN_WIDTH - barW) / 2;
    int16_t barY = 222;
    tft.fillRoundRect(barX, barY, barW, 10, 4, Ui::panel());
    tft.drawRoundRect(barX, barY, barW, 10, 4, Ui::outline());
    int16_t fillW = (int16_t)((int32_t)barW * pct / 100);
    if (fillW > 0) {
        tft.fillRoundRect(barX, barY, fillW, 10, 4, Ui::success());
    }
}

void TraceGame::render(GameHost& host) {
    TFT_eSPI& tft = host.board().display();
    const Glyph& g = GLYPHS[glyphIndex_];

    if (needsFullRender()) {
        Ui::clear(tft);
        Ui::drawTopBar(tft, title());
    } else {
        tft.fillRect(DRAW_X - 2, DRAW_Y - 2, DRAW_W + 4, DRAW_H + 4, Ui::bg());
        tft.fillRect(0, 218, SCREEN_WIDTH, 22, Ui::bg());
    }

    tft.setTextColor(Ui::text(), Ui::bg());
    tft.setTextDatum(MC_DATUM);

    char label[2] = {g.label, 0};
    tft.setTextColor(Ui::panel(), Ui::bg());
    tft.drawString(label, DRAW_X + DRAW_W / 2, DRAW_Y + DRAW_H / 2, 1);

    drawGuide(tft);
    drawProgress(tft);

    if (!complete_) {
        Ui::drawPagerButton(tft, PREV_BTN, "Prev", glyphIndex_ > 0);
        Ui::drawPagerButton(tft, NEXT_BTN, "Next", glyphIndex_ + 1 < GLYPH_COUNT);
    }

    if (complete_) {
        tft.fillRoundRect(60, 90, 200, 55, 8, Ui::panel());
        tft.drawRoundRect(60, 90, 200, 55, 8, Ui::success());
        tft.setTextColor(Ui::success(), Ui::panel());
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Great job!", SCREEN_WIDTH / 2, 108, 4);
        tft.drawString("Tap for next", SCREEN_WIDTH / 2, 132, 2);
    }

    tft.setTextDatum(TL_DATUM);
}
