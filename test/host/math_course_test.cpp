// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

// Host-side test of games/MathCourse.h: the twelve steps of the Math game,
// the questions each one asks and the four buttons each question offers.
// MathCourse.h is pure C++ with no Arduino in it, and this file is what
// proves that still holds, since it will not compile otherwise.
//
// It is not run by CI and not picked up by pio test (the folder name does not
// start with test_). Build and run it with any host C++17 compiler:
//
//   g++ -std=c++17 -O2 -Wall -Wextra -I src -I src/games test/host/math_course_test.cpp -o math_course_test && ./math_course_test
//
// What it checks. Every question of every numbered level: that it is the
// level's kind of question, that the numbered pool holds no question twice
// and is the size the header says. Every drawn level, a few thousand times.
// For all of them, over many random streams: four different options, exactly
// one right, none negative, none the screen cannot show, the level's own
// mistake among them, and from level 6 one that shares the answer's last
// digit. And the text: every equation, option, level name and story measured
// against the widths the Math screen leaves for it.
#include "games/MathCourse.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

using namespace MathCourse;

static int failures = 0;
static int checks = 0;
#define CHECK(cond, ...)                                                  \
    do {                                                                  \
        ++checks;                                                         \
        if (!(cond)) {                                                    \
            ++failures;                                                   \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                     \
            std::printf("\n");                                            \
        }                                                                 \
    } while (0)

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 1) {}
    uint32_t next() {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return s;
    }
};

/* TFT_eSPI's font 2 advance widths, characters 32..127 (widtbl_f16), and
 * font 1, which is six pixels a character. The Math screen draws its prompt
 * strip and its stories in these. */
static const unsigned char FONT2[96] = {
    6, 3, 4, 9, 8, 9, 9, 3, 7, 7, 8, 6, 3, 6, 5, 7,
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 3, 3, 6, 6, 6, 8,
    9, 8, 8, 8, 8, 8, 8, 8, 8, 4, 8, 8, 7, 10, 8, 8,
    8, 8, 8, 8, 8, 8, 8, 10, 8, 8, 8, 4, 7, 4, 7, 9,
    4, 7, 7, 7, 7, 7, 6, 7, 7, 4, 5, 6, 4, 8, 7, 8,
    7, 8, 6, 6, 5, 7, 8, 8, 6, 7, 7, 5, 3, 5, 8, 6,
};
static int widthFont2(const char* s, size_t n) {
    int w = 0;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        w += (c >= 32 && c < 128) ? FONT2[c - 32] : 8;
    }
    return w;
}
static int widthFont2(const char* s) { return widthFont2(s, std::strlen(s)); }
static int widthFont1(const char*, size_t n) { return static_cast<int>(n) * 6; }

static int widthFont4(const char* s) {
    int w = 0;
    for (const char* p = s; *p; ++p) {
        if (*p >= '0' && *p <= '9') w += FONT4_DIGIT;
        else if (*p == ' ') w += FONT4_SPACE;
        else if (*p == '+') w += FONT4_PLUS;
        else if (*p == '-') w += 8;
        else if (*p == '=') w += FONT4_EQUALS;
        else if (*p == '?') w += FONT4_QUESTION;
        else w += 25;   // anything else is a mistake; make it loud
    }
    return w;
}

/* Greedy word wrap, as Ui::wrapLines does it: how many lines `text` needs. */
static int linesNeeded(const char* text, int maxW, int (*width)(const char*, size_t)) {
    int lines = 0;
    const char* p = text;
    while (*p) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* lineStart = p;
        const char* lastFit = nullptr;
        const char* q = p;
        while (true) {
            const char* wordEnd = q;
            while (*wordEnd && *wordEnd != ' ') ++wordEnd;
            if (width(lineStart, static_cast<size_t>(wordEnd - lineStart)) > maxW && lastFit) break;
            lastFit = wordEnd;
            if (!*wordEnd) break;
            q = wordEnd + 1;
        }
        ++lines;
        p = lastFit;
    }
    return lines;
}

static int ones(int v) { return v % 10; }
static int tens(int v) { return (v / 10) % 10; }

/* Is `q` the kind of question level `n` asks? */
static bool belongs(uint8_t n, const Question& q) {
    const int a = q.a, b = q.b, c = q.c;
    const bool sum = q.form == Form::Sum;
    const bool diff = q.form == Form::Difference;
    const bool missing = !sum && !diff;
    switch (n) {
        case 1: return sum && a >= 1 && b >= 1 && c <= 10;
        case 2: return diff && b >= 1 && b <= a && a <= 10;
        case 3: {
            if (!missing) return false;
            if (q.form == Form::MissingStart) return false;
            const int total = q.form == Form::MissingTaken ? a : c;
            return total >= 5 && total <= 10 && q.answer >= 0 && q.answer <= total;
        }
        case 4:
            if (!sum) return false;
            if (a == 10) return b >= 1 && b <= 9;
            if (b == 10) return a >= 1 && a <= 9;
            return a >= 2 && a <= 9 && b >= 2 && b <= 9 && c >= 11;
        case 5: return diff && a >= 11 && a <= 19 && b >= 1 && b <= 9;
        case 6: {
            const bool tensOnly = a % 10 == 0 && b % 10 == 0 && a >= 10 && b >= 10;
            const bool someOnes = a >= 10 && a <= 99 && b >= 1 && b <= 9;
            if (!(tensOnly || someOnes)) return false;
            if (sum) return c <= 99 && ones(a) + ones(b) <= 9;
            return diff && ones(a) >= ones(b) && c >= 1;
        }
        case 7:
            if (a < 10 || a > 99 || b < 1 || b > 9) return false;
            if (sum) return ones(a) + b >= 10 && c <= 99;
            return diff && a >= 20 && ones(a) < b;
        case 8:
            if (a < 10 || a > 99 || b < 10 || b > 99) return false;
            if (sum) return ones(a) + ones(b) <= 9 && tens(a) + tens(b) <= 9;
            return diff && ones(a) >= ones(b) && tens(a) >= tens(b) && c >= 1;
        case 9:
            return sum && a >= 10 && a <= 99 && b >= 10 && b <= 99 && ones(a) + ones(b) >= 10;
        case 10:
            return diff && a >= 10 && a <= 99 && b >= 10 && b <= 99 && ones(a) < ones(b) && c >= 1;
        case 11:
            if (a < 100 || a > 999 || b < 10 || b > 999) return false;
            if (sum) return ones(a) + ones(b) >= 10;
            return diff && c >= 1 && ones(a) < ones(b);
        case 12:
            return missing && a >= 10 && a <= 99 && b >= 10 && b <= 99 && c >= 10 && c <= 99;
        default:
            return false;
    }
}

static bool equationTrue(const Question& q) {
    const bool add = adds(q.form);
    if (q.c != (add ? q.a + q.b : q.a - q.b)) return false;
    switch (q.form) {
        case Form::Sum:
        case Form::Difference: return q.answer == q.c;
        case Form::MissingFirst:
        case Form::MissingStart: return q.answer == q.a;
        default: return q.answer == q.b;
    }
}

static int sharesLastDigit(const Question& q) {
    int n = 0;
    for (int i = 0; i < 4; ++i) {
        if (i != q.correct && q.options[i] % 10 == q.answer % 10) ++n;
    }
    return n;
}

/* The mistake each level is built to catch, where it is a legal option. */
static bool hasItsMistake(uint8_t n, const Question& q) {
    int16_t want = -1;
    switch (n) {
        case 3: {
            const int whole = q.form == Form::MissingTaken ? q.a : q.c;
            const int shown = q.form == Form::MissingTaken ? q.c : (q.form == Form::MissingFirst ? q.b : q.a);
            want = static_cast<int16_t>(whole + shown);
            break;
        }
        case 4: want = (q.a == 10 || q.b == 10) ? (q.a == 10 ? q.b : q.a) : static_cast<int16_t>(q.answer - 10); break;
        case 5: case 10: want = smallerFromLarger(q.a, q.b); break;
        case 9: want = forgotCarry(q.a, q.b); break;
        case 7: case 11: want = adds(q.form) ? forgotCarry(q.a, q.b) : smallerFromLarger(q.a, q.b); break;
        case 12:
            switch (q.form) {
                case Form::MissingFirst: want = static_cast<int16_t>(q.c + q.b); break;
                case Form::MissingSecond: want = static_cast<int16_t>(q.c + q.a); break;
                case Form::MissingTaken: want = static_cast<int16_t>(q.a + q.c); break;
                default: want = static_cast<int16_t>(q.c - q.b); break;
            }
            break;
        default: return true;
    }
    if (want == q.answer || want < level(n).floor || want > MAX_SHOWN) return true;
    for (int i = 0; i < 4; ++i) {
        if (q.options[i] == want) return true;
    }
    return false;
}

static void checkOptions(uint8_t n, const Question& q, const char* where) {
    int right = 0;
    for (int i = 0; i < 4; ++i) {
        if (q.options[i] == q.answer) ++right;
        CHECK(q.options[i] >= level(n).floor, "%s: option %d below the floor", where, q.options[i]);
        CHECK(q.options[i] <= MAX_SHOWN, "%s: option %d too large", where, q.options[i]);
        for (int j = i + 1; j < 4; ++j) {
            CHECK(q.options[i] != q.options[j], "%s: option %d twice", where, q.options[i]);
        }
        char label[12];
        std::snprintf(label, sizeof(label), "%d", q.options[i]);
        CHECK(widthFont4(label) <= LABEL_W, "%s: label %s too wide", where, label);
    }
    CHECK(right == 1, "%s: %d right options", where, right);
    CHECK(q.correct < 4 && q.options[q.correct] == q.answer, "%s: the correct slot is wrong", where);
    if (n >= 6) {
        CHECK(sharesLastDigit(q) >= 1, "%s: no wrong option shares the last digit of %d", where, q.answer);
    }
    CHECK(hasItsMistake(n, q), "%s: the level's own mistake is missing", where);
}

static void checkShown(uint8_t n, const Question& q, const char* where) {
    CHECK(belongs(n, q), "%s: not a level %u question (%d, %d, %d)", where, n, q.a, q.b, q.c);
    CHECK(equationTrue(q), "%s: the equation is not true", where);
    CHECK(q.a >= 0 && q.b >= 0 && q.c >= 0, "%s: a negative number", where);
    CHECK(q.a <= MAX_OPERAND && q.b <= MAX_OPERAND, "%s: an operand over %d", where, MAX_OPERAND);
    char eq[24];
    std::snprintf(eq, sizeof(eq), "unset");
    equation(q, eq, sizeof(eq));
    CHECK(std::strlen(eq) <= static_cast<size_t>(EQUATION_CHARS), "%s: '%s' is longer than the bound", where, eq);
    CHECK(widthFont4(eq) <= QUESTION_TEXT_W, "%s: '%s' is %d px", where, eq, widthFont4(eq));
    CHECK(std::strchr(eq, '?') != nullptr, "%s: '%s' asks nothing", where, eq);
}

static void testNumbered() {
    const uint16_t expected[5] = {45, 55, 153, 54, 81};
    for (uint8_t n = 1; n <= 5; ++n) {
        CHECK(numbered(n), "level %u is numbered", n);
        CHECK(poolSize(n) == expected[n - 1], "level %u has %u questions, not %u", n, poolSize(n),
              expected[n - 1]);
        std::set<uint32_t> seen;
        for (uint16_t item = 0; item < poolSize(n); ++item) {
            Question q;
            CHECK(makeNumbered(n, item, q), "level %u item %u not made", n, item);
            char where[48];
            std::snprintf(where, sizeof(where), "level %u item %u", n, item);
            checkShown(n, q, where);
            seen.insert(token(n, q));
            for (uint32_t seed = 1; seed <= 64; ++seed) {
                Question o = q;
                makeOptions(n, o, seed * 2654435761U + item);
                checkOptions(n, o, where);
            }
        }
        CHECK(seen.size() == poolSize(n), "level %u: %zu different questions in a pool of %u", n,
              seen.size(), poolSize(n));
        Question q;
        CHECK(!makeNumbered(n, poolSize(n), q), "level %u: an item past the end was made", n);
    }
    for (uint8_t n = 6; n <= LEVEL_COUNT; ++n) {
        CHECK(!numbered(n), "level %u is drawn", n);
    }
}

static void testDrawn() {
    for (uint8_t n = 6; n <= LEVEL_COUNT; ++n) {
        Rng rng(1000 + n);
        std::set<uint32_t> distinct;
        uint32_t recent[10] = {};
        int repeats = 0;
        for (int i = 0; i < 4000; ++i) {
            /* What the game does: redraw while the question is recent. */
            Question q;
            uint32_t tok = 0;
            for (int tries = 0; tries < 20; ++tries) {
                makeDrawn(n, rng.next(), q);
                tok = token(n, q);
                bool isRecent = false;
                for (uint32_t r : recent) isRecent = isRecent || r == tok;
                if (!isRecent) break;
            }
            for (uint32_t r : recent) repeats += r == tok;
            recent[i % 10] = tok;
            distinct.insert(tok);
            char where[48];
            std::snprintf(where, sizeof(where), "level %u draw %d", n, i);
            checkShown(n, q, where);
            makeOptions(n, q, rng.next());
            checkOptions(n, q, where);
        }
        CHECK(repeats == 0, "level %u: %d questions came back within ten", n, repeats);
        CHECK(distinct.size() > 300, "level %u: only %zu different questions in 4000", n, distinct.size());
    }
}

static void testOptionsAreNotAGiveaway() {
    /* The right answer turns up on each of the four buttons alike. */
    for (uint8_t n = 1; n <= LEVEL_COUNT; ++n) {
        Rng rng(7 + n);
        int slot[4] = {};
        int rank[4] = {};
        for (int i = 0; i < 4000; ++i) {
            Question q;
            makeDrawn(n, rng.next(), q);
            makeOptions(n, q, rng.next());
            ++slot[q.correct];
            int r = 0;
            for (int k = 0; k < 4; ++k) r += q.options[k] < q.answer;
            ++rank[r];
        }
        for (int s = 0; s < 4; ++s) {
            CHECK(slot[s] > 800 && slot[s] < 1200, "level %u: right on button %d %d times in 4000", n, s,
                  slot[s]);
        }
        /* And it is not found by its size. If it were nearly always one of
         * the middle two, striking out the smallest and the largest would
         * leave a coin toss with no sum done; if it were nearly always an
         * end, the ends would give it away. Half is what chance gives; a
         * level's own mistake sits on one side and pulls it off that --
         * on level 10 both of them sit above, which is the most any level
         * leans. */
        const int ends = rank[0] + rank[3];
        CHECK(ends > 1200 && ends < 2800, "level %u: the answer is an end button %d times in 4000", n,
              ends);
        for (int r = 0; r < 4; ++r) {
            CHECK(rank[r] < 2200, "level %u: the answer is rank %d of 4 in %d of 4000", n, r, rank[r]);
        }
    }
    /* A 0 only where 0 can be an answer. */
    for (uint8_t n = 1; n <= LEVEL_COUNT; ++n) {
        CHECK((level(n).floor == 0) == (n == 2 || n == 3), "level %u's floor", n);
    }
}

static void testText() {
    char line[LEVEL_LINE_MAX];
    for (uint8_t n = 1; n <= LEVEL_COUNT; ++n) {
        levelLine(n, line, sizeof(line));
        CHECK(std::strstr(line, level(n).name) != nullptr, "level %u's line lost its name: '%s'", n, line);
        CHECK(widthFont2(line) <= PROMPT_W, "'%s' is %d px at font 2, the strip is %d", line,
              widthFont2(line), PROMPT_W);
    }

    /* The longest story there can be: the largest numbers, both names of the
     * longest kind, the longest thing, in every shape. */
    for (int add = 0; add < 2; ++add) {
        Question q;
        if (add) set(q, Form::Sum, MAX_OPERAND, MAX_OPERAND);
        else set(q, Form::Difference, MAX_OPERAND, static_cast<int16_t>(MAX_OPERAND - 1));
        for (uint8_t shape = 0; shape < STORY_SHAPES; ++shape) {
            for (uint8_t who = 0; who < NAME_COUNT; ++who) {
                for (uint8_t other = 0; other < NAME_COUNT; ++other) {
                    for (uint8_t thing = 0; thing < THING_COUNT; ++thing) {
                        char big[512];
                        tellStory(q, shape, who, other, thing, big, sizeof(big));
                        CHECK(std::strlen(big) < STORY_MAX, "a story of %zu characters: %s", std::strlen(big), big);
                        const int fit2 = linesNeeded(big, QUESTION_TEXT_W, widthFont2);
                        const int fit1 = linesNeeded(big, QUESTION_TEXT_W, widthFont1);
                        CHECK(fit2 <= 3 || fit1 <= 4, "'%s' fits neither 3 lines of font 2 nor 4 of font 1", big);
                        if (shape == 2 && !add) {
                            /* "Ava has 7. Ava has 5." would be nonsense. */
                            CHECK(std::strstr(big, ". ") != nullptr, "comparison");
                            char first[16] = {};
                            char second[16] = {};
                            std::sscanf(big, "%15s", first);
                            const char* dot = std::strstr(big, ". ");
                            std::sscanf(dot + 2, "%15s", second);
                            CHECK(std::strcmp(first, second) != 0, "the same name twice: %s", big);
                        }
                    }
                }
            }
        }
    }

    /* Every story a level can tell has at least two of each thing, so the
     * verbs and plurals are right, and no comparison asks for none. */
    for (uint8_t n = 1; n <= LEVEL_COUNT; ++n) {
        Rng rng(31 + n);
        int told = 0;
        for (int i = 0; i < 3000; ++i) {
            Question q;
            makeDrawn(n, rng.next(), q);
            if (!storyFits(n, q)) continue;
            ++told;
            char story[STORY_MAX];
            tellStory(q, rng.next(), story, sizeof(story));
            CHECK(std::strstr(story, " 1 ") == nullptr && std::strstr(story, " 0 ") == nullptr,
                  "level %u: '%s'", n, story);
            CHECK(std::strstr(story, " 0.") == nullptr, "level %u: '%s'", n, story);
        }
        if (n < 4 || n == 12) CHECK(told == 0, "level %u told %d stories", n, told);
        else CHECK(told > 1000, "level %u told only %d stories in 3000", n, told);
    }
}

static void testLadderReachesTheTop() {
    /* All right from the start: every level, and the top in six answers a
     * level. All wrong: back to the bottom, and no lower. */
    Course::Ladder l;
    l.start(LEVEL_COUNT, 0);
    int answers = 0;
    while (!l.atTop() && answers < 1000) {
        l.answer(true);
        ++answers;
    }
    CHECK(l.level() == LEVEL_COUNT, "everything right reached level %u", l.level());
    CHECK(answers == (LEVEL_COUNT - 1) * Course::Ladder::UP_AT, "the top took %d answers", answers);
    for (int i = 0; i < 1000; ++i) l.answer(false);
    CHECK(l.level() == 1, "everything wrong came down to %u", l.level());

    /* And a numbered level dealt through its deck asks every question it has
     * before the pass ends: no question twice while another is unseen. */
    for (uint8_t n = 1; n <= 5; ++n) {
        Course::QuestionDeck deck;
        deck.reset(poolSize(n));
        Rng rng(500 + n);
        std::set<uint16_t> pass;
        const uint16_t firstPass = static_cast<uint16_t>(poolSize(n) - poolSize(n) / 8);
        for (uint16_t i = 0; i < firstPass; ++i) {
            const uint16_t item = deck.next(rng.next());
            CHECK(pass.insert(item).second, "level %u: item %u twice in a pass", n, item);
        }
    }
}

int main() {
    testNumbered();
    testDrawn();
    testOptionsAreNotAGiveaway();
    testText();
    testLadderReachesTheTop();
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
