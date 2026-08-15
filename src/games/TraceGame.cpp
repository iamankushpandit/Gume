#include "TraceGame.h"

namespace {

constexpr int16_t DRAW_X = 80;
constexpr int16_t DRAW_Y = 36;
constexpr int16_t DRAW_W = 160;
constexpr int16_t DRAW_H = 180;
constexpr int16_t COORD_MAX = 200;
constexpr int16_t WAYPOINT_SPACING = 20;
constexpr int16_t HIT_RADIUS = 16;
constexpr uint32_t PULSE_PERIOD_MS = 500;

constexpr Rect PREV_BTN{8,   200, 60, 32};
constexpr Rect NEXT_BTN{252, 200, 60, 32};

constexpr Rect MODE_ABC{72,  32, 54, 22};
constexpr Rect MODE_abc{130, 32, 54, 22};
constexpr Rect MODE_123{188, 32, 54, 22};

// --- Uppercase A-Z (unchanged) ---
static const int16_t A_s0[] = {20,200, 100,10, 180,200};
static const int16_t A_s1[] = {55,120, 145,120};
static const TraceGame::Stroke A_strokes[] = {{A_s0,3},{A_s1,2}};

static const int16_t B_s0[] = {30,200, 30,10, 130,10, 160,30, 160,60, 130,100, 30,100};
static const int16_t B_s1[] = {30,100, 140,100, 170,130, 170,170, 140,200, 30,200};
static const TraceGame::Stroke B_strokes[] = {{B_s0,7},{B_s1,6}};

static const int16_t C_s0[] = {170,40, 130,10, 70,10, 30,50, 30,160, 70,200, 130,200, 170,170};
static const TraceGame::Stroke C_strokes[] = {{C_s0,8}};

static const int16_t D_s0[] = {30,200, 30,10, 110,10, 160,50, 170,100, 160,160, 110,200, 30,200};
static const TraceGame::Stroke D_strokes[] = {{D_s0,8}};

static const int16_t E_s0[] = {160,10, 30,10, 30,200, 160,200};
static const int16_t E_s1[] = {30,105, 130,105};
static const TraceGame::Stroke E_strokes[] = {{E_s0,4},{E_s1,2}};

static const int16_t F_s0[] = {160,10, 30,10, 30,200};
static const int16_t F_s1[] = {30,105, 130,105};
static const TraceGame::Stroke F_strokes[] = {{F_s0,3},{F_s1,2}};

static const int16_t G_s0[] = {170,40, 130,10, 70,10, 30,50, 30,160, 70,200, 130,200, 170,170, 170,110, 120,110};
static const TraceGame::Stroke G_strokes[] = {{G_s0,10}};

static const int16_t H_s0[] = {30,10, 30,200};
static const int16_t H_s1[] = {170,10, 170,200};
static const int16_t H_s2[] = {30,105, 170,105};
static const TraceGame::Stroke H_strokes[] = {{H_s0,2},{H_s1,2},{H_s2,2}};

static const int16_t I_s0[] = {60,10, 140,10};
static const int16_t I_s1[] = {100,10, 100,200};
static const int16_t I_s2[] = {60,200, 140,200};
static const TraceGame::Stroke I_strokes[] = {{I_s0,2},{I_s1,2},{I_s2,2}};

static const int16_t J_s0[] = {60,10, 160,10};
static const int16_t J_s1[] = {130,10, 130,160, 100,200, 60,200, 30,170};
static const TraceGame::Stroke J_strokes[] = {{J_s0,2},{J_s1,5}};

static const int16_t K_s0[] = {40,10, 40,200};
static const int16_t K_s1[] = {170,10, 40,110, 170,200};
static const TraceGame::Stroke K_strokes[] = {{K_s0,2},{K_s1,3}};

static const int16_t L_s0[] = {40,10, 40,200, 170,200};
static const TraceGame::Stroke L_strokes[] = {{L_s0,3}};

static const int16_t M_s0[] = {20,200, 20,10, 100,120, 180,10, 180,200};
static const TraceGame::Stroke M_strokes[] = {{M_s0,5}};

static const int16_t N_s0[] = {30,200, 30,10, 170,200, 170,10};
static const TraceGame::Stroke N_strokes[] = {{N_s0,4}};

static const int16_t O_s0[] = {100,10, 50,10, 20,50, 20,160, 50,200, 150,200, 180,160, 180,50, 150,10, 100,10};
static const TraceGame::Stroke O_strokes[] = {{O_s0,10}};

static const int16_t P_s0[] = {30,200, 30,10, 130,10, 170,40, 170,70, 130,105, 30,105};
static const TraceGame::Stroke P_strokes[] = {{P_s0,7}};

static const int16_t Q_s0[] = {100,10, 50,10, 20,50, 20,160, 50,200, 150,200, 180,160, 180,50, 150,10, 100,10};
static const int16_t Q_s1[] = {130,150, 190,210};
static const TraceGame::Stroke Q_strokes[] = {{Q_s0,10},{Q_s1,2}};

static const int16_t R_s0[] = {30,200, 30,10, 130,10, 160,40, 160,70, 130,105, 30,105};
static const int16_t R_s1[] = {100,105, 170,200};
static const TraceGame::Stroke R_strokes[] = {{R_s0,7},{R_s1,2}};

static const int16_t S_s0[] = {160,40, 130,10, 70,10, 30,40, 30,70, 70,105, 130,105, 170,140, 170,170, 130,200, 70,200, 40,170};
static const TraceGame::Stroke S_strokes[] = {{S_s0,12}};

static const int16_t T_s0[] = {10,10, 190,10};
static const int16_t T_s1[] = {100,10, 100,200};
static const TraceGame::Stroke T_strokes[] = {{T_s0,2},{T_s1,2}};

static const int16_t U_s0[] = {30,10, 30,160, 60,200, 140,200, 170,160, 170,10};
static const TraceGame::Stroke U_strokes[] = {{U_s0,6}};

static const int16_t V_s0[] = {20,10, 100,200, 180,10};
static const TraceGame::Stroke V_strokes[] = {{V_s0,3}};

static const int16_t W_s0[] = {10,10, 50,200, 100,80, 150,200, 190,10};
static const TraceGame::Stroke W_strokes[] = {{W_s0,5}};

static const int16_t X_s0[] = {20,10, 180,200};
static const int16_t X_s1[] = {180,10, 20,200};
static const TraceGame::Stroke X_strokes[] = {{X_s0,2},{X_s1,2}};

static const int16_t Y_s0[] = {20,10, 100,110};
static const int16_t Y_s1[] = {180,10, 100,110};
static const int16_t Y_s2[] = {100,110, 100,200};
static const TraceGame::Stroke Y_strokes[] = {{Y_s0,2},{Y_s1,2},{Y_s2,2}};

static const int16_t Z_s0[] = {20,10, 180,10, 20,200, 180,200};
static const TraceGame::Stroke Z_strokes[] = {{Z_s0,4}};

// --- Lowercase a-z ---
// Guides: y=10 ascender top, y=70 x-height top, y=160 baseline, y=200 descender bottom
static const int16_t a_s0[] = {130,70, 130,160};
static const int16_t a_s1[] = {130,95, 100,70, 60,90, 55,130, 80,160, 130,140};
static const TraceGame::Stroke a_strokes[] = {{a_s0,2},{a_s1,6}};

static const int16_t b_s0[] = {50,10, 50,160, 80,200, 130,200, 160,160, 160,90, 130,50, 80,50, 50,90};
static const TraceGame::Stroke b_strokes[] = {{b_s0,9}};

static const int16_t c_s0[] = {150,70, 100,50, 60,50, 50,80, 50,140, 60,160, 100,160, 150,140};
static const TraceGame::Stroke c_strokes[] = {{c_s0,8}};

static const int16_t d_s0[] = {150,10, 150,160, 120,200, 70,200, 40,160, 40,90, 70,50, 120,50, 150,90};
static const TraceGame::Stroke d_strokes[] = {{d_s0,9}};

static const int16_t e_s0[] = {50,100, 130,100, 140,80, 130,50, 80,50, 50,70, 60,160, 100,160, 150,140};
static const TraceGame::Stroke e_strokes[] = {{e_s0,9}};

static const int16_t f_s0[] = {130,10, 130,160};
static const int16_t f_s1[] = {60,40, 140,40};
static const TraceGame::Stroke f_strokes[] = {{f_s0,2},{f_s1,2}};

static const int16_t g_s0[] = {150,50, 150,160, 120,200, 70,200, 40,160, 40,90, 70,50, 120,50, 150,90};
static const int16_t g_s1[] = {40,200, 50,210, 100,220, 140,210};
static const TraceGame::Stroke g_strokes[] = {{g_s0,9},{g_s1,4}};

static const int16_t h_s0[] = {50,10, 50,160};
static const int16_t h_s1[] = {50,100, 80,50, 130,50, 150,80, 150,160};
static const TraceGame::Stroke h_strokes[] = {{h_s0,2},{h_s1,5}};

static const int16_t i_s0[] = {80,50, 80,160};
static const int16_t i_s1[] = {80,20, 80,25};
static const TraceGame::Stroke i_strokes[] = {{i_s0,2},{i_s1,2}};

static const int16_t j_s0[] = {110,50, 110,160, 80,200, 50,200, 40,180};
static const int16_t j_s1[] = {110,20, 110,25};
static const TraceGame::Stroke j_strokes[] = {{j_s0,5},{j_s1,2}};

static const int16_t k_s0[] = {50,10, 50,160};
static const int16_t k_s1[] = {150,50, 50,100, 140,160};
static const TraceGame::Stroke k_strokes[] = {{k_s0,2},{k_s1,3}};

static const int16_t l_s0[] = {80,10, 80,160};
static const TraceGame::Stroke l_strokes[] = {{l_s0,2}};

static const int16_t m_s0[] = {50,160, 50,80, 80,50, 120,50, 130,80, 130,160};
static const int16_t m_s1[] = {130,80, 150,50, 180,50, 190,80, 190,160};
static const TraceGame::Stroke m_strokes[] = {{m_s0,6},{m_s1,5}};

static const int16_t n_s0[] = {50,160, 50,80, 80,50, 130,50, 150,80, 150,160};
static const TraceGame::Stroke n_strokes[] = {{n_s0,6}};

static const int16_t o_s0[] = {100,50, 60,50, 40,80, 40,130, 60,160, 100,160, 140,130, 140,80, 100,50};
static const TraceGame::Stroke o_strokes[] = {{o_s0,9}};

static const int16_t p_s0[] = {50,50, 50,200};
static const int16_t p_s1[] = {50,100, 80,50, 130,50, 160,80, 160,130, 130,160, 80,160, 50,130};
static const TraceGame::Stroke p_strokes[] = {{p_s0,2},{p_s1,8}};

static const int16_t q_s0[] = {150,50, 150,200};
static const int16_t q_s1[] = {150,130, 120,160, 70,160, 40,130, 40,80, 70,50, 120,50, 150,80};
static const TraceGame::Stroke q_strokes[] = {{q_s0,2},{q_s1,8}};

static const int16_t r_s0[] = {50,160, 50,80, 80,50, 130,50, 150,70};
static const TraceGame::Stroke r_strokes[] = {{r_s0,5}};

static const int16_t s_s0[] = {150,70, 130,50, 80,50, 50,70, 50,90, 80,110, 120,110, 150,130, 150,150, 120,160, 70,160, 50,150};
static const TraceGame::Stroke s_strokes[] = {{s_s0,12}};

static const int16_t t_s0[] = {100,30, 100,150, 110,165, 130,170};
static const int16_t t_s1[] = {60,50, 140,50};
static const TraceGame::Stroke t_strokes[] = {{t_s0,4},{t_s1,2}};

static const int16_t u_s0[] = {50,50, 50,130, 80,160, 130,160, 150,130, 150,50};
static const TraceGame::Stroke u_strokes[] = {{u_s0,6}};

static const int16_t v_s0[] = {40,50, 100,160, 160,50};
static const TraceGame::Stroke v_strokes[] = {{v_s0,3}};

static const int16_t w_s0[] = {30,50, 60,160, 100,90, 140,160, 170,50};
static const TraceGame::Stroke w_strokes[] = {{w_s0,5}};

static const int16_t x_s0[] = {40,50, 150,160};
static const int16_t x_s1[] = {150,50, 40,160};
static const TraceGame::Stroke x_strokes[] = {{x_s0,2},{x_s1,2}};

static const int16_t y_s0[] = {40,50, 100,140, 150,50};
static const int16_t y_s1[] = {100,140, 80,190, 50,200};
static const TraceGame::Stroke y_strokes[] = {{y_s0,3},{y_s1,3}};

static const int16_t z_s0[] = {40,50, 150,50, 40,160, 150,160};
static const TraceGame::Stroke z_strokes[] = {{z_s0,4}};

// --- Digits 0-9 (unchanged) ---
static const int16_t D0_s0[] = {100,10, 50,10, 20,50, 20,160, 50,200, 150,200, 180,160, 180,50, 150,10, 100,10};
static const int16_t D0_s1[] = {150,40, 50,170};
static const TraceGame::Stroke D0_strokes[] = {{D0_s0,10},{D0_s1,2}};

static const int16_t D1_s0[] = {60,50, 100,10, 100,200};
static const int16_t D1_s1[] = {60,200, 140,200};
static const TraceGame::Stroke D1_strokes[] = {{D1_s0,3},{D1_s1,2}};

static const int16_t D2_s0[] = {30,50, 50,20, 100,10, 150,20, 170,50, 170,80, 30,200, 170,200};
static const TraceGame::Stroke D2_strokes[] = {{D2_s0,8}};

static const int16_t D3_s0[] = {40,30, 70,10, 130,10, 160,40, 160,70, 130,100, 100,105};
static const int16_t D3_s1[] = {100,105, 140,110, 170,140, 170,170, 140,200, 70,200, 40,180};
static const TraceGame::Stroke D3_strokes[] = {{D3_s0,7},{D3_s1,7}};

static const int16_t D4_s0[] = {140,200, 140,10, 20,140, 180,140};
static const TraceGame::Stroke D4_strokes[] = {{D4_s0,4}};

static const int16_t D5_s0[] = {160,10, 40,10, 30,100, 120,90, 160,120, 160,160, 130,200, 60,200, 30,180};
static const TraceGame::Stroke D5_strokes[] = {{D5_s0,9}};

static const int16_t D6_s0[] = {150,20, 120,10, 70,10, 30,50, 30,160, 60,200, 140,200, 170,170, 170,130, 140,100, 60,100, 30,120};
static const TraceGame::Stroke D6_strokes[] = {{D6_s0,12}};

static const int16_t D7_s0[] = {30,10, 170,10, 80,200};
static const TraceGame::Stroke D7_strokes[] = {{D7_s0,3}};

static const int16_t D8_s0[] = {100,105, 60,105, 30,80, 30,40, 60,10, 140,10, 170,40, 170,80, 140,105, 100,105};
static const int16_t D8_s1[] = {100,105, 50,105, 20,130, 20,170, 50,200, 150,200, 180,170, 180,130, 150,105, 100,105};
static const TraceGame::Stroke D8_strokes[] = {{D8_s0,10},{D8_s1,10}};

static const int16_t D9_s0[] = {170,90, 140,105, 60,105, 30,70, 30,40, 60,10, 140,10, 170,40, 170,160, 140,200, 70,200, 40,180};
static const TraceGame::Stroke D9_strokes[] = {{D9_s0,12}};

static const TraceGame::Glyph GLYPHS[] = {
    // Uppercase
    {'A', A_strokes, 2}, {'B', B_strokes, 2}, {'C', C_strokes, 1},
    {'D', D_strokes, 1}, {'E', E_strokes, 2}, {'F', F_strokes, 2},
    {'G', G_strokes, 1}, {'H', H_strokes, 3}, {'I', I_strokes, 3},
    {'J', J_strokes, 2}, {'K', K_strokes, 2}, {'L', L_strokes, 1},
    {'M', M_strokes, 1}, {'N', N_strokes, 1}, {'O', O_strokes, 1},
    {'P', P_strokes, 1}, {'Q', Q_strokes, 2}, {'R', R_strokes, 2},
    {'S', S_strokes, 1}, {'T', T_strokes, 2}, {'U', U_strokes, 1},
    {'V', V_strokes, 1}, {'W', W_strokes, 1}, {'X', X_strokes, 2},
    {'Y', Y_strokes, 3}, {'Z', Z_strokes, 1},
    // Lowercase
    {'a', a_strokes, 2}, {'b', b_strokes, 1}, {'c', c_strokes, 1},
    {'d', d_strokes, 1}, {'e', e_strokes, 1}, {'f', f_strokes, 2},
    {'g', g_strokes, 2}, {'h', h_strokes, 2}, {'i', i_strokes, 2},
    {'j', j_strokes, 2}, {'k', k_strokes, 2}, {'l', l_strokes, 1},
    {'m', m_strokes, 2}, {'n', n_strokes, 1}, {'o', o_strokes, 1},
    {'p', p_strokes, 2}, {'q', q_strokes, 2}, {'r', r_strokes, 1},
    {'s', s_strokes, 1}, {'t', t_strokes, 2}, {'u', u_strokes, 1},
    {'v', v_strokes, 1}, {'w', w_strokes, 1}, {'x', x_strokes, 2},
    {'y', y_strokes, 2}, {'z', z_strokes, 1},
    // Digits
    {'0', D0_strokes, 2}, {'1', D1_strokes, 2}, {'2', D2_strokes, 1},
    {'3', D3_strokes, 2}, {'4', D4_strokes, 1}, {'5', D5_strokes, 1},
    {'6', D6_strokes, 1}, {'7', D7_strokes, 1}, {'8', D8_strokes, 2},
    {'9', D9_strokes, 1},
};

static_assert(sizeof(GLYPHS) / sizeof(GLYPHS[0]) == 62, "GLYPHS must have 62 entries");

}

const char* TraceGame::title() const {
    return "Trace";
}

void TraceGame::begin(AppContext& host) {
    (void)host;
    glyphIndex_ = 0;
    glyphSet_ = GlyphSet::Upper;
    loadGlyph();
    markFullDirty();
}

uint8_t TraceGame::getSetFirstIndex() const {
    switch (glyphSet_) {
        case GlyphSet::Upper: return UPPER_FIRST;
        case GlyphSet::Lower: return LOWER_FIRST;
        case GlyphSet::Digit: return DIGIT_FIRST;
    }
    return 0;
}

uint8_t TraceGame::getSetLastIndex() const {
    switch (glyphSet_) {
        case GlyphSet::Upper: return LOWER_FIRST - 1;
        case GlyphSet::Lower: return DIGIT_FIRST - 1;
        case GlyphSet::Digit: return GLYPH_COUNT_TOTAL - 1;
    }
    return 0;
}

void TraceGame::loadGlyph() {
    glyphIndex_ = constrain(glyphIndex_, getSetFirstIndex(), getSetLastIndex());
    complete_ = false;
    completeAt_ = 0;
    activeStroke_ = 0;
    nextPoint_ = 0;
    lastPulseChange_ = millis();
    pulseState_ = false;
    resampleWaypoints();
    markFullDirty();
}

void TraceGame::resampleWaypoints() {
    const Glyph& g = GLYPHS[glyphIndex_];
    uint8_t totalPts = 0;

    for (uint8_t s = 0; s < g.strokeCount && s < MAX_STROKES; ++s) {
        const Stroke& st = g.strokes[s];
        strokeStart_[s] = totalPts;

        float accumulated = 0.0f;
        uint8_t lastIdx = 0;

        for (uint8_t i = 0; i + 1 < st.count; ++i) {
            int16_t x1 = st.pts[i*2], y1 = st.pts[i*2+1];
            int16_t x2 = st.pts[(i+1)*2], y2 = st.pts[(i+1)*2+1];
            float dx = x2 - x1;
            float dy = y2 - y1;
            float seg = sqrtf(dx*dx + dy*dy);

            accumulated += seg;
            float segAccum = 0.0f;

            for (uint8_t j = lastIdx; j <= i; ++j) {
                int16_t px = st.pts[j*2], py = st.pts[j*2+1];
                int16_t nx = (j + 1 < st.count) ? st.pts[(j+1)*2] : px;
                int16_t ny = (j + 1 < st.count) ? st.pts[(j+1)*2+1] : py;

                float pdx = nx - px;
                float pdy = ny - py;
                float plen = sqrtf(pdx*pdx + pdy*pdy);

                while (segAccum + plen >= WAYPOINT_SPACING && totalPts < MAX_POINTS) {
                    float ratio = (WAYPOINT_SPACING - segAccum) / (plen > 0.0001f ? plen : 1.0f);
                    pts_[totalPts].x = scaleX((int16_t)(px + pdx * ratio));
                    pts_[totalPts].y = scaleY((int16_t)(py + pdy * ratio));
                    totalPts++;
                    segAccum -= WAYPOINT_SPACING;
                }
                segAccum += plen;
            }
            lastIdx = i;
        }

        // Add final point of stroke
        if (totalPts < MAX_POINTS) {
            pts_[totalPts].x = scaleX(st.pts[(st.count-1)*2]);
            pts_[totalPts].y = scaleY(st.pts[(st.count-1)*2+1]);
            totalPts++;
        }

        strokeLen_[s] = totalPts - strokeStart_[s];
    }
    strokeCount_ = g.strokeCount;
}

int16_t TraceGame::scaleX(int16_t nx) const {
    return DRAW_X + (int16_t)((int32_t)nx * DRAW_W / COORD_MAX);
}

int16_t TraceGame::scaleY(int16_t ny) const {
    return DRAW_Y + (int16_t)((int32_t)ny * DRAW_H / COORD_MAX);
}

void TraceGame::updatePulsePhase() {
    uint32_t now = millis();
    if (now - lastPulseChange_ >= PULSE_PERIOD_MS / 2) {
        pulseState_ = !pulseState_;
        lastPulseChange_ = now;
        markDirty();
    }
}

void TraceGame::update(AppContext& host, const TouchPoint& touch) {
    if (complete_) {
        if (touch.justPressed && millis() - completeAt_ > 600) {
            glyphIndex_ = (glyphIndex_ + 1) % GLYPH_COUNT_TOTAL;
            loadGlyph();
        }
        return;
    }

    updatePulsePhase();

    if (touch.justPressed) {
        if (MODE_ABC.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Upper;
            glyphIndex_ = UPPER_FIRST;
            loadGlyph();
            return;
        }
        if (MODE_abc.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Lower;
            glyphIndex_ = LOWER_FIRST;
            loadGlyph();
            return;
        }
        if (MODE_123.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphSet_ = GlyphSet::Digit;
            glyphIndex_ = DIGIT_FIRST;
            loadGlyph();
            return;
        }
        if (PREV_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphIndex_ = (glyphIndex_ == getSetFirstIndex())
                ? getSetLastIndex()
                : glyphIndex_ - 1;
            loadGlyph();
            return;
        }
        if (NEXT_BTN.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            glyphIndex_ = (glyphIndex_ == getSetLastIndex())
                ? getSetFirstIndex()
                : glyphIndex_ + 1;
            loadGlyph();
            return;
        }
    }

    if (!touch.down) {
        lastPulseChange_ = millis();
        pulseState_ = false;
    }

    if (touch.down && activeStroke_ < strokeCount_) {
        uint8_t startIdx = strokeStart_[activeStroke_];
        uint8_t nextPtIdx = startIdx + nextPoint_;

        if (nextPtIdx < MAX_POINTS && nextPoint_ < strokeLen_[activeStroke_]) {
            int16_t dx = touch.x - pts_[nextPtIdx].x;
            int16_t dy = touch.y - pts_[nextPtIdx].y;
            int32_t d2 = (int32_t)dx*dx + (int32_t)dy*dy;

            if (d2 < (int32_t)HIT_RADIUS * HIT_RADIUS) {
                nextPoint_++;
                markDirty();

                if (nextPoint_ >= strokeLen_[activeStroke_]) {
                    host.beepOk();
                    activeStroke_++;
                    nextPoint_ = 0;

                    if (activeStroke_ >= strokeCount_) {
                        complete_ = true;
                        completeAt_ = millis();
                        host.beepOk();
                        markFullDirty();
                    }
                }
            }
        }
    }
}

void TraceGame::drawModeTabs(TFT_eSPI& tft) {
    const uint16_t activeBg = Ui::warning();
    const uint16_t activeTxt = Ui::panel();
    const uint16_t inactiveBg = Ui::panel();
    const uint16_t inactiveTxt = Ui::text();

    auto drawTab = [&](Rect r, const char* label, bool active) {
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, active ? activeBg : inactiveBg);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());
        tft.setTextColor(active ? activeTxt : inactiveTxt, active ? activeBg : inactiveBg);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(label, r.x + r.w/2, r.y + r.h/2, 1);
    };

    drawTab(MODE_ABC, "ABC", glyphSet_ == TraceGame::GlyphSet::Upper);
    drawTab(MODE_abc, "abc", glyphSet_ == TraceGame::GlyphSet::Lower);
    drawTab(MODE_123, "123", glyphSet_ == TraceGame::GlyphSet::Digit);
}

void TraceGame::drawGuide(TFT_eSPI& tft) {
    // Draw completed strokes in success color
    for (uint8_t s = 0; s < activeStroke_ && s < strokeCount_; ++s) {
        uint8_t start = strokeStart_[s];
        for (uint8_t i = 0; i + 1 < strokeLen_[s] && start + i < MAX_POINTS; ++i) {
            uint8_t idx1 = start + i;
            uint8_t idx2 = start + i + 1;
            tft.drawLine(pts_[idx1].x, pts_[idx1].y, pts_[idx2].x, pts_[idx2].y, Ui::success());
            tft.drawLine(pts_[idx1].x+1, pts_[idx1].y, pts_[idx2].x+1, pts_[idx2].y, Ui::success());
        }
    }

    // Draw active stroke
    if (activeStroke_ < strokeCount_) {
        uint8_t start = strokeStart_[activeStroke_];
        uint16_t inked = min(nextPoint_, (uint8_t)strokeLen_[activeStroke_]);

        // Draw inked portion
        for (uint8_t i = 0; i + 1 < inked && start + i < MAX_POINTS; ++i) {
            uint8_t idx1 = start + i;
            uint8_t idx2 = start + i + 1;
            tft.drawLine(pts_[idx1].x, pts_[idx1].y, pts_[idx2].x, pts_[idx2].y, Ui::success());
            tft.drawLine(pts_[idx1].x+1, pts_[idx1].y, pts_[idx2].x+1, pts_[idx2].y, Ui::success());
        }

        // Draw all waypoints
        for (uint8_t i = 0; i < strokeLen_[activeStroke_] && start + i < MAX_POINTS; ++i) {
            uint8_t idx = start + i;
            uint16_t color;

            if (i < inked) {
                // Already claimed
                color = Ui::success();
            } else if (i == inked && pulseState_) {
                // Next dot, pulsing
                tft.fillCircle(pts_[idx].x, pts_[idx].y, 6, Ui::warning());
                continue;
            } else if (i == inked) {
                // Next dot, not pulsing
                tft.fillCircle(pts_[idx].x, pts_[idx].y, 4, Ui::warning());
                continue;
            } else {
                // Unclaimed
                color = Ui::muted();
            }

            tft.fillCircle(pts_[idx].x, pts_[idx].y, 3, color);
        }

        // Draw numbered badge on first point
        uint8_t badgeNum = activeStroke_ + 1;
        tft.fillCircle(pts_[start].x, pts_[start].y, 7, Ui::warning());
        tft.setTextColor(Ui::panel(), Ui::warning());
        tft.setTextDatum(MC_DATUM);
        char badge[2];
        badge[0] = '0' + badgeNum;
        badge[1] = 0;
        tft.drawString(badge, pts_[start].x, pts_[start].y, 1);
    }

    // Draw unclaimed strokes as faint dots
    for (uint8_t s = activeStroke_ + 1; s < strokeCount_; ++s) {
        uint8_t start = strokeStart_[s];
        for (uint8_t i = 0; i < strokeLen_[s] && start + i < MAX_POINTS; ++i) {
            tft.fillCircle(pts_[start + i].x, pts_[start + i].y, 2, Ui::muted());
        }
    }
}

void TraceGame::drawProgress(TFT_eSPI& tft) {
    uint16_t totalPoints = 0;
    uint16_t claimedPoints = 0;

    for (uint8_t s = 0; s < strokeCount_; ++s) {
        totalPoints += strokeLen_[s];
        if (s < activeStroke_) {
            claimedPoints += strokeLen_[s];
        } else if (s == activeStroke_) {
            claimedPoints += nextPoint_;
        }
    }

    uint8_t pct = totalPoints > 0 ? (uint8_t)((uint32_t)claimedPoints * 100 / totalPoints) : 0;
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

void TraceGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    const Glyph& g = GLYPHS[glyphIndex_];

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        drawModeTabs(tft);

        tft.fillRect(DRAW_X - 2, DRAW_Y - 2, DRAW_W + 4, DRAW_H + 4, Ui::bg());

        tft.setTextColor(Ui::panel(), Ui::bg());
        tft.setTextDatum(MC_DATUM);
        char label[2] = {g.label, 0};
        tft.drawString(label, DRAW_X + DRAW_W / 2, DRAW_Y + DRAW_H / 2, 1);

        Ui::drawPagerButton(tft, PREV_BTN, "Prev", true);
        Ui::drawPagerButton(tft, NEXT_BTN, "Next", true);
    } else {
        tft.fillRect(DRAW_X - 2, DRAW_Y - 2, DRAW_W + 4, DRAW_H + 4, Ui::bg());
        tft.fillRect(0, 218, SCREEN_WIDTH, 22, Ui::bg());
    }

    drawGuide(tft);
    drawProgress(tft);

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
