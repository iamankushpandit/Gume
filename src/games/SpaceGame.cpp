// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SpaceGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t BLUE = 0x24BD;
constexpr uint16_t GREEN = 0x05D1;
constexpr uint16_t RED = 0xE8E4;
constexpr uint16_t YELLOW = 0xFEC0;

constexpr AppScoreInfo SPACE_SCORE = {
    "space", "Space", "spaceBest", "pts", false
};

constexpr AppMetadata SPACE_METADATA = {
    "space",
    "Space",
    "Space Quiz",
    "planets & the air above",
    "Space",
    "The solar system, and the air above us.",
    &SPACE_SCORE,
    LauncherIcon::Space,
    38,
    true,
};

/* One question.
 *
 * `options[0]` is ALWAYS the right answer; which button it lands on is decided
 * per question by shuffling an order array. Storing it at a fixed index is
 * what keeps the table readable -- a `correct` column is one transcription
 * error away from teaching a child the wrong thing, and nothing in a build
 * would catch it.
 *
 * `fact` is the sentence shown after answering. Two lines of font 1 across the
 * strip is about 96 characters; anything longer is silently cut, so keep them
 * short enough to read aloud. */
struct SpaceQuestion {
    uint8_t level;              // 1 easiest, 3 hardest
    const char* question;
    const char* options[4];
    const char* fact;
};

/* Option labels are capped at about 18 characters by the 132px button, not by
 * this table -- renderDynamic() drops to font 1 for the long ones. Past that
 * they are cut by the driver with no mark, so check a new one on the panel. */
const SpaceQuestion QUESTIONS[] = {
    // ---- level 1: the things a young child has already heard of ----
    {1, "Which planet do we live on?",
     {"Earth", "Mars", "Venus", "Jupiter"},
     "Earth is the only world we know of with liquid water on its surface."},
    {1, "Which planet is closest to the Sun?",
     {"Mercury", "Venus", "Earth", "Mars"},
     "Mercury is so close that it races all the way round the Sun in 88 days."},
    {1, "What is the Sun?",
     {"A star", "A planet", "A moon", "A comet"},
     "The Sun is a star, like the ones you see at night, only very much nearer."},
    {1, "Which planet is called the Red Planet?",
     {"Mars", "Saturn", "Neptune", "Mercury"},
     "Mars looks red because its dust is full of rust, the same as an old nail."},
    {1, "How many planets go round our Sun?",
     {"Eight", "Nine", "Seven", "Ten"},
     "Eight since 2006, when Pluto was moved to a new group: the dwarf planets."},
    {1, "Which planet has bright rings round it?",
     {"Saturn", "Earth", "Mars", "Mercury"},
     "Saturn's rings are countless pieces of ice and rock, most smaller than a car."},
    {1, "What goes round the Earth?",
     {"The Moon", "The Sun", "Mars", "Venus"},
     "The Moon takes about 27 days to go all the way round us."},
    {1, "Which planet is the largest?",
     {"Jupiter", "Saturn", "Earth", "Neptune"},
     "Jupiter is so big that every other planet would fit inside it together."},
    {1, "What makes day and night?",
     {"Earth spinning", "The Moon", "Clouds", "The Sun moving"},
     "Earth turns once a day, so your side faces the Sun and then faces away."},
    {1, "Which planet has a Great Red Spot?",
     {"Jupiter", "Mars", "Venus", "Uranus"},
     "The Great Red Spot is a storm on Jupiter, wider than the whole Earth."},
    {1, "How long does Earth take to circle the Sun?",
     {"One year", "One day", "One month", "One week"},
     "That trip is what a year is: one lap of the Sun."},
    {1, "Where does the Moon's light come from?",
     {"The Sun", "Its own fire", "The Earth", "The stars"},
     "The Moon makes no light at all. It is a mirror for sunlight."},

    // ---- level 2: the solar system in order, and the air above us ----
    {2, "Which planet is the hottest?",
     {"Venus", "Mercury", "Mars", "Jupiter"},
     "Venus beats Mercury: its thick clouds hold the heat in like a blanket."},
    {2, "Which layer of air do we breathe in?",
     {"Troposphere", "Stratosphere", "Mesosphere", "Thermosphere"},
     "The troposphere is the bottom layer, and all our weather happens in it."},
    {2, "Which layer sits just above the troposphere?",
     {"Stratosphere", "Mesosphere", "Exosphere", "Thermosphere"},
     "The stratosphere is calm and dry, and it gets warmer as you go up."},
    {2, "Where is the ozone layer?",
     {"Stratosphere", "Troposphere", "Mesosphere", "Exosphere"},
     "Ozone up there soaks up the Sun's ultraviolet light before it reaches us."},
    {2, "Where do airliners fly?",
     {"Stratosphere", "Mesosphere", "Thermosphere", "Exosphere"},
     "Jets climb into the calm bottom of the stratosphere, above the weather."},
    {2, "Where do most meteors burn up?",
     {"Mesosphere", "Troposphere", "Stratosphere", "Exosphere"},
     "Shooting stars are grains of dust burning up about 80 km above you."},
    {2, "Which layer of air is the outermost?",
     {"Exosphere", "Thermosphere", "Mesosphere", "Stratosphere"},
     "The exosphere is so thin that its air slowly leaks away into space."},
    {2, "What lies between Mars and Jupiter?",
     {"Asteroid belt", "A ring", "A comet", "A moon"},
     "Millions of rocks circle there, left over from when the planets formed."},
    {2, "Which planet spins on its side?",
     {"Uranus", "Neptune", "Saturn", "Mars"},
     "Uranus is tipped right over, so it rolls round the Sun rather than spins."},
    {2, "What is Pluto called now?",
     {"A dwarf planet", "A planet", "A moon", "A star"},
     "Pluto is too small to have swept its orbit clear, so it got its own group."},
    {2, "Which planet is furthest from the Sun?",
     {"Neptune", "Uranus", "Saturn", "Jupiter"},
     "Neptune takes 165 Earth years to go round once."},
    {2, "What is a comet mostly made of?",
     {"Ice and dust", "Iron", "Gas only", "Rock only"},
     "A comet grows its tail when the Sun warms it and the ice turns to gas."},
    {2, "What holds the planets round the Sun?",
     {"Gravity", "Magnets", "Wind", "Air"},
     "The same pull that brings a dropped ball down holds Earth on its path."},
    {2, "How long does sunlight take to reach us?",
     {"About 8 minutes", "One second", "One hour", "One day"},
     "Sunlight you feel now left the Sun about eight minutes ago."},
    {2, "Which planet has no moon at all?",
     {"Venus", "Earth", "Mars", "Jupiter"},
     "Venus and Mercury are the only planets with no moon of their own."},

    // ---- level 3: for a player who already knows the order ----
    {3, "Which layer does the Space Station orbit in?",
     {"Thermosphere", "Stratosphere", "Mesosphere", "Troposphere"},
     "The station circles about 400 km up, inside the very thin thermosphere."},
    {3, "Which layer are the auroras seen in?",
     {"Thermosphere", "Troposphere", "Stratosphere", "Exosphere"},
     "The northern lights glow where the Sun's particles hit that thin air."},
    {3, "What is the line where space begins called?",
     {"The Karman line", "The equator", "The horizon", "The tropic"},
     "By agreement space starts 100 km up, at the Karman line."},
    {3, "Which gas is most of the air?",
     {"Nitrogen", "Oxygen", "Carbon dioxide", "Hydrogen"},
     "Air is about 78% nitrogen. Only a fifth of it is the oxygen you use."},
    {3, "Which layer of air is the coldest?",
     {"Mesosphere", "Troposphere", "Thermosphere", "Stratosphere"},
     "The top of the mesosphere reaches about -90C, the coldest air there is."},
    {3, "What is the Sun mostly made of?",
     {"Hydrogen", "Oxygen", "Iron", "Carbon"},
     "The Sun squeezes hydrogen into helium, and that is where its light comes from."},
    {3, "What does a light year measure?",
     {"Distance", "Time", "Weight", "Heat"},
     "It is how far light travels in a year: about 9.5 trillion kilometres."},
    {3, "Which planet has the biggest volcano?",
     {"Mars", "Earth", "Venus", "Jupiter"},
     "Olympus Mons on Mars stands about three times as tall as Everest."},
    {3, "Why is the sky blue?",
     {"Air scatters it", "The sea", "Oxygen is blue", "Ice crystals"},
     "Air scatters blue light in every direction, so it reaches you from all over."},
    {3, "What is our galaxy called?",
     {"The Milky Way", "Andromeda", "Orion", "Cassiopeia"},
     "Our galaxy holds a few hundred billion stars, and the Sun is one of them."},
    {3, "What gives us the seasons?",
     {"Earth's tilt", "Being nearer", "The Moon", "Clouds"},
     "Earth leans over, so each half gets more sunlight for part of the year."},
    {3, "What is Saturn's largest moon?",
     {"Titan", "Europa", "Io", "Callisto"},
     "Titan has a thick orange atmosphere and lakes, though of methane not water."},
    {3, "Which planet is nearly Earth's size?",
     {"Venus", "Mars", "Mercury", "Jupiter"},
     "Venus is almost our twin in size, and nothing like us in any other way."},
    {3, "What covers most of the Moon?",
     {"Craters and dust", "Water", "Forest", "Ice caps"},
     "With no air to wear them away, the Moon's craters stay for billions of years."},
};

constexpr uint8_t QUESTION_COUNT = sizeof(QUESTIONS) / sizeof(QUESTIONS[0]);

/* Every question at level 1 must still be reachable at level 3, so the pool
 * only ever grows. The table is ordered by level, which makes that easy to
 * see, but nothing depends on the ordering -- the filter is the `level` field. */
static_assert(QUESTION_COUNT >= 24, "too few questions to avoid obvious repeats");
}

const AppMetadata& spaceAppMetadata() {
    return SPACE_METADATA;
}

const char* SpaceGame::title() const {
    return spaceAppMetadata().screenTitle != nullptr
        ? spaceAppMetadata().screenTitle
        : spaceAppMetadata().title;
}

void SpaceGame::begin(AppContext& host) {
    score_ = 0;
    streak_ = 0;
    best_ = static_cast<uint16_t>(host.getScore(spaceAppMetadata().score->bestKey, 0));
    recent_.reset();
    newQuestion();
    markDirty();
}

uint8_t SpaceGame::level() const {
    return min<uint8_t>(3, 1 + score_ / 6);
}

/* The same four rects Math uses. Two screens with the same shape should not
 * put their buttons in two nearly-identical places. */
Rect SpaceGame::answerRect(uint8_t index) const {
    const int16_t col = index % 2;
    const int16_t row = index / 2;
    return Rect{static_cast<int16_t>(18 + col * 152),
                static_cast<int16_t>(144 + row * 46), 132, 38};
}

/* Three lines of font 2 plus a little daylight. The note strip below starts at
 * 125 and the top button at 144, so this panel may not grow downwards without
 * both of those moving -- which is why they are derived from it below rather
 * than typed in twice. */
Rect SpaceGame::questionRect() const {
    return Rect{20, 66, 280, 58};
}

Rect SpaceGame::noteRect() const {
    const Rect q = questionRect();
    const int16_t top = static_cast<int16_t>(q.y + q.h + 1);
    return Rect{14, top, static_cast<int16_t>(GAME_CANVAS_WIDTH - 28),
                static_cast<int16_t>(answerRect(0).y - top - 1)};
}

void SpaceGame::newQuestion() {
    const uint8_t want = level();
    uint16_t pick = 0;
    /* Reject anything above the current level or seen recently. The pool at
     * level 1 is twelve questions and the history window is ten, so the
     * rejection has to give up rather than spin: after 40 tries take the first
     * question of a low enough level that is not the one just asked. */
    bool found = false;
    for (uint8_t attempt = 0; attempt < 40 && !found; ++attempt) {
        const uint16_t candidate = static_cast<uint16_t>(random(QUESTION_COUNT));
        if (QUESTIONS[candidate].level > want) continue;
        if (recent_.recentlyUsed(candidate)) continue;
        pick = candidate;
        found = true;
    }
    if (!found) {
        for (uint16_t i = 0; i < QUESTION_COUNT; ++i) {
            if (QUESTIONS[i].level <= want && i != question_) {
                pick = i;
                break;
            }
        }
    }
    recent_.remember(pick);
    question_ = pick;

    for (uint8_t i = 0; i < 4; ++i) {
        order_[i] = i;
    }
    for (uint8_t i = 3; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const uint8_t tmp = order_[i];
        order_[i] = order_[j];
        order_[j] = tmp;
    }
    for (uint8_t i = 0; i < 4; ++i) {
        if (order_[i] == 0) {
            correctButton_ = i;
            break;
        }
    }

    selected_ = -1;
    answered_ = false;

    /* Everything a new question changes, invalidated together -- and the
     * button trackers are in here for the reason docs/RENDER_AUDIT.md gives:
     * the two buttons that were never highlighted go from state 0 to state 0,
     * so without this they would keep the previous question's labels. */
    drawnQuestion_ = false;
    drawnHeader_ = false;
    drawnNote_ = 0xFF;
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
}

void SpaceGame::update(AppContext& host, const TouchPoint& touch) {
    if (!touch.justPressed) {
        return;
    }

    if (answered_) {
        newQuestion();
        /* A new question, four new labels and the prompt back. All of it is
         * content in rects that have not moved, so this is markDirty(): the
         * panel, the strip and the buttons each paint over themselves
         * opaquely. */
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
            if (host.saveBestScore(spaceAppMetadata().score->bestKey, score_, false)) {
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

void SpaceGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    /* Nothing below here is painted yet, so every tracker says "unknown"
     * rather than "unchanged". */
    for (uint8_t i = 0; i < 4; ++i) {
        drawnButton_[i] = 0xFF;
    }
    drawnScore_ = 0xFFFF;
    drawnStreak_ = 0xFFFF;
    drawnHeader_ = false;
    drawnQuestion_ = false;
    drawnNote_ = 0xFF;
}

/* The panel fills itself opaquely before the text goes down, so a short
 * question cannot leave the tail of a long one behind it. */
void SpaceGame::drawQuestion(Ui::Renderer& tft) {
    const Rect r = questionRect();
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, Ui::panel());
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 8, Ui::outline());

    const int16_t textW = static_cast<int16_t>(r.w - 16);
    char lines[3][Ui::WRAP_LINE_MAX];
    uint8_t font = 2;
    uint8_t needed = Ui::wrapLines(tft, QUESTIONS[question_].question, textW, font, lines, 3);
    if (needed > 3) {
        /* Font 2 could not say it in three lines. Nothing is cut: it is asked
         * again at font 1, which fits four lines in the same panel. */
        font = 1;
        needed = Ui::wrapLines(tft, QUESTIONS[question_].question, textW, font, lines, 3);
    }
    const uint8_t shown = min<uint8_t>(needed, 3);
    const int16_t lineH = font == 2 ? 18 : 11;
    int16_t y = static_cast<int16_t>(r.y + (r.h - shown * lineH) / 2);

    tft.setTextColor(Ui::text(), Ui::panel());
    tft.setTextDatum(TC_DATUM);
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], static_cast<int16_t>(r.x + r.w / 2), y, font);
        y = static_cast<int16_t>(y + lineH);
    }
    tft.setTextDatum(TL_DATUM);
}

/* One line of prompt before an answer, up to two of fact after one. The two
 * are different heights as well as different lengths, so the strip is cleared
 * whole before either is written rather than relying on an opaque background
 * colour behind the glyphs. */
void SpaceGame::drawNote(Ui::Renderer& tft) {
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

    char lines[2][Ui::WRAP_LINE_MAX];
    const uint8_t needed = Ui::wrapLines(tft, QUESTIONS[question_].fact, r.w, 1, lines, 2);
    const uint8_t shown = min<uint8_t>(needed, 2);
    tft.setTextColor(selected_ == correctButton_ ? GREEN : RED, Ui::bg());
    for (uint8_t i = 0; i < shown; ++i) {
        tft.drawString(lines[i], cx, static_cast<int16_t>(r.y + i * 10), 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void SpaceGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (!drawnQuestion_) {
        drawQuestion(tft);
        drawnQuestion_ = true;
    }

    /* Level and Best only move with a new question or a new record, but Score
     * and Streak move on every answer and Streak RESETS -- the largest shrink
     * this screen does -- so both ends are cleared before either is written,
     * and the right-hand rect is measured leftward from the margin. */
    if (!drawnHeader_ || score_ != drawnScore_ || streak_ != drawnStreak_) {
        tft.fillRect(10, 33, 150, 36, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 10 - 150, 33, 150, 36, Ui::bg());
        char buf[32];
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.setTextDatum(TL_DATUM);
        snprintf(buf, sizeof(buf), "Level %u", static_cast<unsigned>(level()));
        tft.drawString(buf, 10, 35, 2);
        snprintf(buf, sizeof(buf), "Correct %u", static_cast<unsigned>(score_));
        tft.drawString(buf, 10, 52, 1);
        tft.setTextDatum(TR_DATUM);
        snprintf(buf, sizeof(buf), "Streak %u", static_cast<unsigned>(streak_));
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 35, 2);
        if (best_ > 0) {
            snprintf(buf, sizeof(buf), "Best %u", static_cast<unsigned>(best_));
        } else {
            snprintf(buf, sizeof(buf), "Best --");
        }
        tft.drawString(buf, GAME_CANVAS_WIDTH - 10, 52, 1);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnStreak_ = streak_;
        drawnHeader_ = true;
    }

    /* Only the buttons whose colour changed. Ui::drawButton fills its rect
     * opaquely, so a recolour erases what was under it. */
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
        const char* label = QUESTIONS[question_].options[order_[i]];
        /* An option like "Carbon dioxide" is wider than the button at font 2.
         * Measured rather than guessed: TFT_eSPI cuts a string at the viewport
         * edge with no mark, mid-word, and gen_screens.py cannot reproduce
         * that -- so a label that overflows looks fine in every mock-up. */
        const uint8_t font = tft.textWidth(label, 2) <= r.w - 10 ? 2 : 1;
        Ui::drawButton(tft, r, label, fill, TFT_DARKGREY, ink, false, font);
        drawnButton_[i] = state;
    }

    const uint8_t note = answered_ ? (selected_ == correctButton_ ? 1 : 2) : 0;
    if (note != drawnNote_) {
        drawNote(tft);
        drawnNote_ = note;
    }
}
