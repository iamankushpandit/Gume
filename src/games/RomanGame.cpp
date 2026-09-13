#include "RomanGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

constexpr AppScoreInfo ROMAN_SCORE = {
    "roman", "Roman", "romanBest", "pts", false
};

constexpr AppMetadata ROMAN_METADATA = {
    "roman",
    "Roman",
    "Roman Numerals",
    "read them and write them",
    "Roman",
    "Read and write Roman numerals.",
    &ROMAN_SCORE,
    LauncherIcon::Roman,
    39,
    true,
};

/* The notation itself, once. Both the writer and the decomposition below walk
 * this table, so a numeral and its explanation cannot disagree: the pieces the
 * player is shown ARE the pieces that were used to build it. */
struct RomanPiece {
    uint16_t value;
    const char* symbol;
};

const RomanPiece PIECES[] = {
    {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"},
    {100, "C"},  {90, "XC"},  {50, "L"},  {40, "XL"},
    {10, "X"},   {9, "IX"},   {5, "V"},   {4, "IV"},
    {1, "I"},
};
constexpr uint8_t PIECE_COUNT = sizeof(PIECES) / sizeof(PIECES[0]);

/* The same table with the four subtractive pairs removed: this is what a
 * player writes before anyone has explained that IV is not IIII. Offered as a
 * wrong answer on purpose. */
const RomanPiece PLAIN_PIECES[] = {
    {1000, "M"}, {500, "D"}, {100, "C"}, {50, "L"}, {10, "X"}, {5, "V"}, {1, "I"},
};
constexpr uint8_t PLAIN_PIECE_COUNT = sizeof(PLAIN_PIECES) / sizeof(PLAIN_PIECES[0]);

void writeRoman(uint16_t value, const RomanPiece* table, uint8_t count,
                char* out, size_t outLen) {
    size_t at = 0;
    for (uint8_t i = 0; i < count && at + 3 < outLen; ++i) {
        while (value >= table[i].value) {
            const char* s = table[i].symbol;
            while (*s != '\0' && at + 1 < outLen) {
                out[at++] = *s++;
            }
            value = static_cast<uint16_t>(value - table[i].value);
        }
    }
    out[at] = '\0';
}

void toRoman(uint16_t value, char* out, size_t outLen) {
    writeRoman(value, PIECES, PIECE_COUNT, out, outLen);
}

/** One letter's value, or 0 for anything that is not a numeral. */
uint16_t letterValue(char c) {
    switch (c) {
        case 'I': return 1;
        case 'V': return 5;
        case 'X': return 10;
        case 'L': return 50;
        case 'C': return 100;
        case 'D': return 500;
        case 'M': return 1000;
        default:  return 0;
    }
}

/* What the numeral says if you add every letter up and never subtract: XIV
 * read as 10 + 1 + 5 = 16. This is the misreading the whole screen is about,
 * so it is computed rather than hand-listed. */
uint16_t readAdditively(const char* roman) {
    uint32_t total = 0;
    for (const char* p = roman; *p != '\0'; ++p) {
        total += letterValue(*p);
    }
    return static_cast<uint16_t>(total);
}

/* "XIV = X + IV = 10 + 4". Returns false when the number has too many pieces
 * for the line to be worth reading -- MMMDCCCLXXXVIII taken apart is fourteen
 * terms, which teaches nobody anything. */
bool explain(uint16_t value, char* out, size_t outLen) {
    char roman[24];
    const char* symbols[8];
    uint16_t values[8];
    uint8_t used = 0;

    uint16_t left = value;
    for (uint8_t i = 0; i < PIECE_COUNT; ++i) {
        while (left >= PIECES[i].value) {
            if (used >= 8) return false;
            symbols[used] = PIECES[i].symbol;
            values[used] = PIECES[i].value;
            ++used;
            left = static_cast<uint16_t>(left - PIECES[i].value);
        }
    }
    if (used == 0 || used > 6) return false;

    toRoman(value, roman, sizeof(roman));
    int at = snprintf(out, outLen, "%s = ", roman);
    if (at < 0 || static_cast<size_t>(at) >= outLen) return false;
    for (uint8_t i = 0; i < used; ++i) {
        at += snprintf(out + at, outLen - at, "%s%s", i == 0 ? "" : " + ", symbols[i]);
        if (static_cast<size_t>(at) >= outLen) return false;
    }
    at += snprintf(out + at, outLen - at, " = ");
    if (static_cast<size_t>(at) >= outLen) return false;
    for (uint8_t i = 0; i < used; ++i) {
        at += snprintf(out + at, outLen - at, "%s%u",
                       i == 0 ? "" : " + ", static_cast<unsigned>(values[i]));
        if (static_cast<size_t>(at) >= outLen) return false;
    }
    if (used > 1) {
        snprintf(out + at, outLen - at, " = %u", static_cast<unsigned>(value));
    }
    return true;
}

/* The symbols in play at each level, and the one rule that matters there.
 * Both lines are on the screen for as long as the level is, because looking a
 * symbol up is the skill at this age, not remembering it. */
struct LevelKey {
    const char* symbols;
    const char* rule;
};

const LevelKey LEVEL_KEYS[5] = {
    {"I=1   V=5   X=10",
     "Count up: I, II, III. Then IV is one before V."},
    {"I=1   V=5   X=10",
     "A smaller sign BEFORE a bigger one is taken away."},
    {"I=1   V=5   X=10   L=50",
     "XL is 10 before 50 = 40. LX is 50 and 10 = 60."},
    {"I=1  V=5  X=10  L=50  C=100",
     "Only I, X and C are ever put in front to subtract."},
    {"I=1 V=5 X=10 L=50 C=100 D=500 M=1000",
     "Biggest first, and never four of the same in a row."},
};
}

const AppMetadata& romanAppMetadata() {
    return ROMAN_METADATA;
}

const char* RomanGame::title() const {
    return romanAppMetadata().screenTitle != nullptr
        ? romanAppMetadata().screenTitle
        : romanAppMetadata().title;
}

void RomanGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    best_ = static_cast<uint16_t>(host.getScore(romanAppMetadata().score->bestKey, 0));
    recent_.reset();
    newQuestion();
    markDirty();
}

uint8_t RomanGame::level() const {
    return min<uint8_t>(5, 1 + score_ / 5);
}

/* Five rungs, each one adding the next symbol. The ceilings are where the new
 * letter stops being needed rather than round numbers: 39 is the largest that
 * needs no L, 99 the largest that needs no C, 399 the largest that needs no D. */
uint16_t RomanGame::levelCeiling() const {
    static const uint16_t CEILING[5] = {10, 39, 99, 399, 3999};
    return CEILING[level() - 1];
}

/* And the floor is where the level's NEW material starts, so a player who has
 * just reached level 3 is actually asked about L rather than being given XII
 * again. newQuestion() drops back to the full range one time in four, or the
 * smaller numbers would never be revised. */
uint16_t RomanGame::levelFloor() const {
    static const uint16_t FLOOR[5] = {1, 11, 40, 100, 400};
    return FLOOR[level() - 1];
}

Rect RomanGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152),
                static_cast<int16_t>(144 + row * 46), 132, 38};
}

Rect RomanGame::keyRect() const {
    return Rect{10, 50, static_cast<int16_t>(GAME_CANVAS_WIDTH - 20), 22};
}

Rect RomanGame::questionRect() const {
    return Rect{20, 74, 280, 50};
}

Rect RomanGame::noteRect() const {
    const Rect q = questionRect();
    const int16_t top = static_cast<int16_t>(q.y + q.h + 1);
    return Rect{14, top, static_cast<int16_t>(GAME_CANVAS_WIDTH - 28),
                static_cast<int16_t>(answerRect(0).y - top - 1)};
}

void RomanGame::newQuestion() {
    const uint16_t ceiling = levelCeiling();
    const uint16_t floor = levelFloor();

    uint16_t pick = 1;
    bool askNumber = true;
    for (uint8_t attempt = 0; attempt < 30; ++attempt) {
        /* Three questions in four come from the level's new range; the fourth
         * revises everything below it. */
        const uint16_t low = (random(4) == 0) ? 1 : floor;
        pick = static_cast<uint16_t>(random(low, ceiling + 1));
        askNumber = random(2) == 0;
        const uint32_t token = static_cast<uint32_t>(pick) * 2 + (askNumber ? 1 : 0);
        if (!recent_.recentlyUsed(token)) {
            recent_.remember(token);
            break;
        }
    }

    value_ = pick;
    askNumber_ = askNumber;
    if (askNumber_) {
        toRoman(value_, subject_, sizeof(subject_));
    } else {
        snprintf(subject_, sizeof(subject_), "%u", static_cast<unsigned>(value_));
    }
    makeOptions();

    selected_ = -1;
    answered_ = false;

    /* Everything a new question changes. The button trackers are in here
     * because a button that was never highlighted goes from state 0 to state 0
     * and would otherwise keep the previous question's label -- the trap
     * docs/RENDER_AUDIT.md warns about. The key strip is NOT invalidated here:
     * it belongs to the level, and repainting it per question would be two
     * lines of text redrawn for nothing. */
    drawnQuestion_ = false;
    drawnHeader_ = false;
    drawnNote_ = 0xFF;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

bool RomanGame::optionExists(const char* candidate, uint8_t upTo) const {
    for (uint8_t i = 0; i < upTo; ++i) {
        if (strcmp(options_[i], candidate) == 0) return true;
    }
    return false;
}

/* The first wrong answer is the mistake this screen exists to correct; the
 * rest are near misses. A distractor nobody would ever pick tests nothing. */
void RomanGame::makeOptions() {
    correctButton_ = static_cast<uint8_t>(random(4));
    for (uint8_t i = 0; i < 4; ++i) {
        options_[i][0] = '\0';
    }

    if (askNumber_) {
        snprintf(options_[correctButton_], TEXT_MAX, "%u", static_cast<unsigned>(value_));
    } else {
        toRoman(value_, options_[correctButton_], TEXT_MAX);
    }

    /* The classic error, computed from this very numeral: read every letter as
     * an addition. For XIV that is 16; for a number with no subtractive pair
     * it equals the answer, and then it is simply not offered. */
    char roman[TEXT_MAX];
    toRoman(value_, roman, sizeof(roman));
    char teaching[TEXT_MAX];
    teaching[0] = '\0';
    if (askNumber_) {
        const uint16_t misread = readAdditively(roman);
        if (misread != value_) {
            snprintf(teaching, sizeof(teaching), "%u", static_cast<unsigned>(misread));
        }
    } else {
        char plain[TEXT_MAX];
        writeRoman(value_, PLAIN_PIECES, PLAIN_PIECE_COUNT, plain, sizeof(plain));
        if (strcmp(plain, roman) != 0) {
            snprintf(teaching, sizeof(teaching), "%s", plain);
        }
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (i == correctButton_) continue;

        if (teaching[0] != '\0' && !optionExists(teaching, 4)) {
            snprintf(options_[i], TEXT_MAX, "%s", teaching);
            teaching[0] = '\0';
            continue;
        }

        char candidate[TEXT_MAX];
        uint8_t attempts = 0;
        do {
            const int16_t spread = level() <= 2 ? 3 : (level() <= 4 ? 12 : 60);
            int32_t other = static_cast<int32_t>(value_) + random(-spread, spread + 1);
            if (other < 1) other = static_cast<int32_t>(value_) + 1;
            if (other > 3999) other = static_cast<int32_t>(value_) - 1;
            if (other == value_) other = static_cast<int32_t>(value_) + 1 + random(3);
            if (askNumber_) {
                snprintf(candidate, sizeof(candidate), "%u", static_cast<unsigned>(other));
            } else {
                toRoman(static_cast<uint16_t>(other), candidate, sizeof(candidate));
            }
            ++attempts;
        } while (optionExists(candidate, 4) && attempts < 40);
        /* Forty near misses all taken is possible only for a tiny value, so
         * walk outwards until something is free rather than shipping a blank
         * button. */
        uint16_t walk = value_;
        while (optionExists(candidate, 4)) {
            ++walk;
            if (askNumber_) {
                snprintf(candidate, sizeof(candidate), "%u", static_cast<unsigned>(walk));
            } else {
                toRoman(walk, candidate, sizeof(candidate));
            }
        }
        snprintf(options_[i], TEXT_MAX, "%s", candidate);
    }
}

void RomanGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        const uint8_t before = level();
        newQuestion();
        /* The key strip is the one thing on this screen that belongs to the
         * level rather than the question. It is repainted only when the level
         * actually moves -- which is every fifth correct answer, not every
         * tap -- and it is still only two lines, not a screen. */
        if (level() != before) {
            drawnKeyLevel_ = 0xFF;
        }
        markDirty();
        return;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        if (!answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            continue;
        }
        selected_ = static_cast<int8_t>(i);
        answered_ = true;
        if (i == correctButton_) {
            ++score_;
            ++streak_;
            if (host.saveBestScore(romanAppMetadata().score->bestKey, score_, false)) {
                best_ = score_;
            }
            host.beepOk();
        } else {
            streak_ = 0;
            host.beepError();
        }
        markDirty();
        return;
    }
}

void RomanGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnHeader_ = false;
    drawnQuestion_ = false;
    drawnKeyLevel_ = 0xFF;
    drawnNote_ = 0xFF;
}

void RomanGame::drawKey(Ui::Renderer& tft) {
    const Rect r = keyRect();
    /* Cleared whole: the symbol line grows a term per level and the rule line
     * is a different length at each of the five, so neither can be relied on
     * to paint over its predecessor. */
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    const LevelKey& key = LEVEL_KEYS[level() - 1];
    tft.setTextDatum(TC_DATUM);
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(key.symbols, cx, r.y, 1);
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.drawString(key.rule, cx, static_cast<int16_t>(r.y + 11), 1);
    tft.setTextDatum(TL_DATUM);
}

void RomanGame::drawQuestion(Ui::Renderer& tft) {
    const Rect r = questionRect();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, Ui::panel());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, Ui::outline());

    tft.setTextDatum(TC_DATUM);
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);
    tft.setTextColor(Ui::muted(), Ui::panel());
    tft.drawString(askNumber_ ? "What number is this?" : "Write this in Roman numerals",
                   cx, static_cast<int16_t>(r.y + 3), 2);
    tft.setTextColor(Ui::text(), Ui::panel());
    /* MMMDCCCLXXXVIII at font 4 is wider than the panel, so the subject drops
     * a font rather than being cut mid-numeral by the driver -- which would
     * leave a numeral that is a different number. */
    const uint8_t font = tft.textWidth(subject_, 4) <= r.w - 16 ? 4 : 2;
    tft.drawString(subject_, cx, static_cast<int16_t>(r.y + (font == 4 ? 21 : 26)), font);
    tft.setTextDatum(TL_DATUM);
}

void RomanGame::drawNote(Ui::Renderer& tft) {
    const Rect r = noteRect();
    tft.fillRect(r.x, r.y, r.w, r.h, Ui::bg());
    tft.setTextDatum(TC_DATUM);
    const int16_t cx = static_cast<int16_t>(r.x + r.w / 2);

    if (!answered_) {
        tft.setTextColor(YELLOW, Ui::bg());
        tft.drawString("Tap the answer", cx, static_cast<int16_t>(r.y + 1), 2);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    char working[80];
    if (!explain(value_, working, sizeof(working))) {
        char roman[TEXT_MAX];
        toRoman(value_, roman, sizeof(roman));
        snprintf(working, sizeof(working), "%s = %u", roman, static_cast<unsigned>(value_));
    }
    char lines[2][Ui::WRAP_LINE_MAX];
    const uint8_t needed = Ui::wrapLines(tft, working, r.w, 1, lines, 2);
    const uint8_t shown = min<uint8_t>(needed, 2);
    tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], cx, static_cast<int16_t>(r.y + i * 10), 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void RomanGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (drawnKeyLevel_ != level()) {
        drawKey(tft);
        drawnKeyLevel_ = level();
    }

    if (!drawnQuestion_) {
        drawQuestion(tft);
        drawnQuestion_ = true;
    }

    /* One header row, cleared at both ends before either is written: Correct
     * grows and Level is redrawn beside it, and a right-aligned string that
     * gets narrower leaves its tail at the LEFT. */
    if (!drawnHeader_ || score_ != drawnScore_) {
        tft.fillRect(10, 32, 120, 18, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 10 - 190, 32, 190, 18, Ui::bg());
        char buf[40];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(level()));
        tft.drawString(buf, 10, 33, 2);
        tft.setTextDatum(TR_DATUM);
        if (best_ > 0) {
            snprintf(buf, sizeof(buf), "Correct %u   Best %u",
                     static_cast<unsigned>(score_), static_cast<unsigned>(best_));
        } else {
            snprintf(buf, sizeof(buf), "Correct %u", static_cast<unsigned>(score_));
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 33, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnHeader_ = true;
    }

    for (uint8_t i = 0; i < 4; ++i) {
        uint8_t state = 0;
        if (answered_) {
            if (i == correctButton_) state = 1;
            else if (i == selected_) state = 2;
        }
        if (state == drawnButton_[i]) {
            continue;
        }
        const uint16_t fill = state == 1 ? GREEN : (state == 2 ? RED : BLUE);
        const uint16_t ink = state == 0 ? TFT_WHITE : TFT_BLACK;
        const Rect r = answerRect(i);
        const uint8_t font = tft.textWidth(options_[i], 4) <= r.w - 10 ? 4 : 2;
        Ui::drawButton(tft, r, options_[i], fill, TFT_DARKGREY, ink, false, font);
        drawnButton_[i] = state;
    }

    const uint8_t note = answered_ ? (selected_ == correctButton_ ? 1 : 2) : 0;
    if (note != drawnNote_) {
        drawNote(tft);
        drawnNote_ = note;
    }
}
