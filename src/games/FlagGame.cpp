// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "FlagGame.h"
#include "map_n_flag.h"
#include "engine/AppRegistry.h"

namespace {
constexpr uint16_t FLAG_BG = 0xFFFF;   // white card behind the flag

/* Shrink a label until it fits the given width, adding an ellipsis. Country
 * names like "United Arab Emirates" do not fit a half-width button at font 2. */
String fitLabel(Ui::Renderer& tft, const String& in, int16_t maxW, uint8_t font) {
    if (tft.textWidth(in, font) <= maxW) return in;
    String s = in;
    while (s.length() > 1 && tft.textWidth(s + ".", font) > maxW) {
        s.remove(s.length() - 1);
    }
    return s + ".";
}

constexpr AppMetadata FLAG_METADATA = {
    "flags",
    "Flags",
    "Guess the Flag",
    "guess the flag",
    "Flags",
    "Name the flag, then its capital.",
    nullptr,
    LauncherIcon::Flag,
    2,
    true,
};
}

const AppMetadata& flagAppMetadata() {
    return FLAG_METADATA;
}

const char* FlagGame::title() const {
    return flagAppMetadata().screenTitle != nullptr
        ? flagAppMetadata().screenTitle
        : flagAppMetadata().title;
}

Rect FlagGame::flagRect() const  { return Rect{80, 48, 160, 120}; }
Rect FlagGame::tierRect() const  { return Rect{132, 31, 56, 16}; }

/* 2x2 grid of wide buttons. Four buttons across the bottom would only be 72px
 * each, which truncates most country names into uselessness. */
Rect FlagGame::answerRect(uint8_t i) const {
    const int16_t col = i % 2;
    const int16_t row = i / 2;
    return Rect{static_cast<int16_t>(6 + col * 158),
                static_cast<int16_t>(186 + row * 27), 150, 25};
}

namespace FC = FlagCourse;
static_assert(FC::AFRICA == CONT_AFRICA && FC::ASIA == CONT_ASIA && FC::EUROPE == CONT_EUROPE &&
                  FC::NORTH_AMERICA == CONT_NORTH_AMERICA &&
                  FC::SOUTH_AMERICA == CONT_SOUTH_AMERICA && FC::OCEANIA == CONT_OCEANIA,
              "FlagCourse's continents are CountryData.h's");

/* The countries of the ladder's level, in table order; the deck deals
 * positions in this list. */
void FlagGame::startLevel() {
    const uint8_t level = ladder_.level();
    poolSize_ = 0;
    for (uint16_t i = 0; i < COUNTRY_FACT_COUNT && poolSize_ < MAX_POOL; ++i) {
        if (FC::asks(level, COUNTRY_FACTS[i].tier, COUNTRY_FACTS[i].continent)) {
            pool_[poolSize_++] = static_cast<uint8_t>(i);
        }
    }
    deck_.reset(poolSize_);
}

bool FlagGame::canOffer(const CountryFact* cand) const {
    if (cand == nullptr || cand == current_) return false;
    if (!FC::offers(ladder_.level(), current_->continent, cand->tier, cand->continent)) {
        return false;
    }
    if (FC::lookalike(cand->iso2, current_->iso2)) return false;
    // Capitals must be distinct too, since the bonus round reuses these.
    if (strcmp(cand->capital, current_->capital) == 0) return false;
    for (uint8_t j = 0; j < OPTION_COUNT; ++j) {
        if (options_[j] == cand) return false;
        if (options_[j] != current_ && options_[j] != nullptr &&
            FC::lookalike(cand->iso2, options_[j]->iso2)) {
            return false;
        }
    }
    return true;
}

void FlagGame::makeOptions() {
    correctBtn_ = static_cast<uint8_t>(random(OPTION_COUNT));
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) options_[i] = current_;

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (i == correctBtn_) continue;
        for (uint8_t attempt = 0; attempt < 80; ++attempt) {
            const CountryFact* cand =
                &COUNTRY_FACTS[static_cast<uint16_t>(random(COUNTRY_FACT_COUNT))];
            if (!canOffer(cand)) continue;
            options_[i] = cand;
            break;
        }
        /* Deterministic fallback: random sampling can in principle exhaust its
         * attempts, and leaving the slot as current_ would show the same
         * country on two buttons. host test flag_course_test proves every
         * question of every level has three to find. */
        if (options_[i] == current_) {
            const uint16_t start = static_cast<uint16_t>(random(COUNTRY_FACT_COUNT));
            for (uint16_t k = 0; k < COUNTRY_FACT_COUNT; ++k) {
                const CountryFact* cand = &COUNTRY_FACTS[(start + k) % COUNTRY_FACT_COUNT];
                if (!canOffer(cand)) continue;
                options_[i] = cand;
                break;
            }
        }
    }
}

/* A plain shuffle. It used to force the capital onto a different button from
 * the flag's answer, which gave one of the four away. */
void FlagGame::shuffleOptionsForBonus() {
    for (int8_t i = static_cast<int8_t>(OPTION_COUNT - 1); i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const CountryFact* tmp = options_[i];
        options_[i] = options_[j];
        options_[j] = tmp;
    }
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (options_[i] == current_) {
            correctBtn_ = i;
            break;
        }
    }
}

void FlagGame::newQuestion() {
    /* A different country means a different flag, four different options and
     * possibly a different level. All of it is content in fixed rectangles
     * drawn over opaque fills, so this is markDirty().
     *
     * The buttons are not optional. The loop in renderDynamic() repaints one
     * only when its state or the round type changed, and a new country leaves
     * both the same while changing every label. */
    markDirty();
    drawnFlag_ = false;
    drawnPrompt_ = false;
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        drawnBtn_[i] = 0xFF;
    }
    if (poolSize_ == 0) { current_ = nullptr; return; }

    item_ = deck_.next(esp_random());
    current_ = &COUNTRY_FACTS[pool_[item_ % poolSize_]];

    selected_ = -1;
    phase_ = Phase::Country;
    feedbackUntil_ = 0;
    makeOptions();
}

void FlagGame::begin(AppContext& host) {
    uint8_t stored = host.courseLevel();
    if (stored == 0) {
        /* A player who climbed the old tiers starts where they were: Medium
         * on the well-known flags, Hard on the whole world. */
        const uint8_t tier = static_cast<uint8_t>(host.getScore("geoTier", 1));
        stored = tier >= 3 ? 8 : (tier == 2 ? 2 : 1);
    }
    ladder_.start(FC::LEVEL_COUNT, stored);
    announce_ = false;
    score_ = 0; rounds_ = 0; capBonus_ = 0;
    startLevel();
    newQuestion();
    markDirty();
}

/* Only the feedback pause is timed. Moved on rather than left to lapse, so a
 * player coming back still sees which answer was right before the next flag. */
void FlagGame::resume(AppContext& host, uint32_t awayMs) {
    (void)host;
    if (phase_ == Phase::FeedbackCountry || phase_ == Phase::FeedbackCapital) {
        movedOn(feedbackUntil_, awayMs);
    }
}

/* One answer to the ladder. Returns true on a step up, which is announced in
 * place of the beep. */
bool FlagGame::climb(AppContext& host, bool right) {
    if (!right) deck_.missed(item_);   // before the ladder moves
    const Course::Ladder::Step step = ladder_.answer(right);
    if (step == Course::Ladder::Step::Stay) return false;
    host.setCourseLevel(ladder_.level());
    startLevel();
    if (step != Course::Ladder::Step::Up) return false;
    announce_ = true;
    host.pulseRgb(0, 255, 40, 450);
    host.playSound(Sound::LevelUp);
    return true;
}

void FlagGame::update(AppContext& host, const TouchPoint& touch) {
    const uint32_t now = millis();

    if ((phase_ == Phase::FeedbackCountry || phase_ == Phase::FeedbackCapital) &&
        now >= feedbackUntil_) {
        if (phase_ == Phase::FeedbackCountry && lastCorrect_) {
            // Earned the capital bonus round for this same country.
            shuffleOptionsForBonus();
            selected_ = -1;
            phase_ = Phase::CapitalBonus;
        } else {
            newQuestion();
        }
        markDirty();
        return;
    }

    if (!touch.justPressed) return;
    if (phase_ != Phase::Country && phase_ != Phase::CapitalBonus) return;

    /* The level button steps up one and wraps, as the tier button did. It is
     * tested after the phase, so a tap during feedback no longer throws the
     * feedback away. */
    if (tierRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        ladder_.setLevel(ladder_.atTop() ? 1 : static_cast<uint8_t>(ladder_.level() + 1));
        host.setCourseLevel(ladder_.level());
        host.playSound(Sound::Select);
        startLevel();
        announce_ = true;
        newQuestion();
        return;
    }

    if (current_ == nullptr) return;

    /* The rows are 2px apart and the slop is 8, so a press near the top of a
     * lower button also fell inside the one above it, and the first match
     * won. A button the press is really inside wins now. */
    int8_t hit = -1;
    for (uint8_t i = 0; i < OPTION_COUNT && hit < 0; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, 0)) hit = static_cast<int8_t>(i);
    }
    for (uint8_t i = 0; i < OPTION_COUNT && hit < 0; ++i) {
        if (answerRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) hit = static_cast<int8_t>(i);
    }
    if (hit < 0) return;

    const uint8_t i = static_cast<uint8_t>(hit);
    selected_ = static_cast<int8_t>(i);
    lastCorrect_ = (i == correctBtn_);

    if (phase_ == Phase::Country) {
        announce_ = false;   // the level's name has stood over its first flag
        ++rounds_;
        if (lastCorrect_) ++score_;
        /* On the last level the flag alone does not move the ladder up: the
         * capital that follows does. A wrong flag still counts. */
        const bool counts = !FC::capitalCounts(ladder_.level()) || !lastCorrect_;
        const bool up = counts && climb(host, lastCorrect_);
        if (!up) lastCorrect_ ? host.beepOk() : host.beepError();
        feedbackUntil_ = now + 1500UL;
        phase_ = Phase::FeedbackCountry;
    } else {
        if (lastCorrect_) ++capBonus_;
        const bool up = FC::capitalCounts(ladder_.level()) && climb(host, lastCorrect_);
        if (!up) lastCorrect_ ? host.beepOk() : host.beepError();
        feedbackUntil_ = now + 1600UL;
        phase_ = Phase::FeedbackCapital;
    }
    markDirty();
}

void FlagGame::renderStatic(AppContext& host) {
    Ui::Renderer& tft = host.display();
    Ui::clear(tft);
    host.drawTopBar(title());

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        drawnBtn_[i] = 0xFF;
    }
    drawnCapital_ = !(phase_ == Phase::CapitalBonus || phase_ == Phase::FeedbackCapital);
    drawnPrompt_ = false;
    drawnScore_ = 0xFFFF;
    drawnRounds_ = 0xFFFF;
    drawnCapBonus_ = 0xFFFF;
    drawnFlag_ = false;
    drawnLevel_ = 0xFF;
}

void FlagGame::renderDynamic(AppContext& host) {
    Ui::Renderer& tft = host.display();

    if (score_ != drawnScore_ || rounds_ != drawnRounds_ || capBonus_ != drawnCapBonus_) {
        tft.fillRect(8, 31, 90, 20, Ui::bg());
        tft.fillRect(GAME_CANVAS_WIDTH - 8 - 90, 31, 90, 20, Ui::bg());
        char buf[16];
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::text(), Ui::bg());
        snprintf(buf, sizeof(buf), "%u/%u", score_, rounds_);
        tft.drawString(buf, 8, 33, 2);
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(Ui::rgb(255, 200, 0), Ui::bg());
        snprintf(buf, sizeof(buf), "+%u", capBonus_);
        tft.drawString(buf, GAME_CANVAS_WIDTH - 8, 33, 2);
        tft.setTextDatum(TL_DATUM);
        drawnScore_ = score_;
        drawnRounds_ = rounds_;
        drawnCapBonus_ = capBonus_;
    }

    /* The difficulty button. Gated on the tier itself rather than on a bool,
     * because the tier also changes on its own -- six correct in a row
     * auto-promotes -- and that used to show the old label until the next full
     * repaint. drawButton fills its fixed rect opaquely, so "Medium" over
     * "Easy" leaves nothing behind. */
    if (drawnLevel_ != ladder_.level()) {
        char lv[12];
        snprintf(lv, sizeof(lv), "Level %u", static_cast<unsigned>(ladder_.level()));
        Ui::drawButton(tft, tierRect(), lv, Ui::panel(), Ui::outline(), Ui::text(), false, 1);
        drawnLevel_ = ladder_.level();
    }

    /* The flag card: drawCountryImageScaled at 2x paints 160x120 and is the
     * single most expensive thing on this screen. It stays put for the whole
     * question INCLUDING the capital bonus, which asks about the same country
     * -- so it is gated. It is dynamic rather than static because a new
     * country must not cost a wipe of the screen.
     *
     * The card is filled before the image goes into it, and the image fills it
     * exactly, so no part of the previous flag survives. The nullptr branch
     * fills the card back to background: without it a flag would be left
     * stranded under the "No countries available" message. */
    if (!drawnFlag_) {
        const Rect fr = flagRect();
        if (current_ != nullptr) {
            tft.fillRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, FLAG_BG);
            tft.drawRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, Ui::outline());
            // 80x60 source drawn at 2x fills the 160x120 card exactly.
            Ui::drawCountryImageScaled(tft, mnf_flag(current_->iso2), fr, FLAG_BG, 2);
        } else {
            tft.fillRect(fr.x - 2, fr.y - 2, fr.w + 4, fr.h + 4, Ui::bg());
        }
        drawnFlag_ = true;
    }

    if (current_ == nullptr) {
        Ui::drawLabel(tft, Rect{20, 110, 280, 20}, "No countries available",
                      Ui::error(), 2, Align::Center);
        return;
    }

    const bool capitalRound = (phase_ == Phase::CapitalBonus || phase_ == Phase::FeedbackCapital);

    /* "Which country?" against "Bonus! Capital of <name>?" -- wildly different
     * lengths, so the line is cleared. It stops at 184, two rows above
     * answerRect(0) at 186. */
    if (!drawnPrompt_ || capitalRound != drawnCapital_) {
        tft.fillRect(6, 166, GAME_CANVAS_WIDTH - 12, 18, Ui::bg());
        const char* countryName = mnf_name(current_->iso2);
        char prompt[64];
        bool named = false;
        if (capitalRound) {
            snprintf(prompt, sizeof(prompt), "%s%s?",
                     FC::capitalCounts(ladder_.level()) ? "Capital of " : "Bonus! Capital of ",
                     countryName ? countryName : current_->iso2);
        } else if (announce_) {
            /* A step up names the new level over its first flag. */
            FC::levelLine(ladder_.level(), prompt, sizeof(prompt));
            named = true;
        } else {
            snprintf(prompt, sizeof(prompt), "Which country?");
        }
        /* Saint Vincent and the Grenadines is wider than the strip at font
         * 2, and was cut short in a 48-byte buffer. */
        const uint8_t font = tft.textWidth(prompt, 2) <= GAME_CANVAS_WIDTH - 14 ? 2 : 1;
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor((capitalRound || named) ? Ui::rgb(255, 200, 0) : Ui::text(), Ui::bg());
        tft.drawString(prompt, GAME_CANVAS_WIDTH / 2, 176, font);
        tft.setTextDatum(TL_DATUM);
        drawnPrompt_ = true;
    }

    const bool showingFeedback =
        (phase_ == Phase::FeedbackCountry || phase_ == Phase::FeedbackCapital);

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const CountryFact* opt = options_[i];
        if (opt == nullptr) continue;

        uint8_t state = 0;
        if (showingFeedback) {
            if (i == correctBtn_) state = 1;
            else if (i == static_cast<uint8_t>(selected_)) state = 2;
        }
        /* The label changes with the round even when the colour does not, so a
         * switch to the capital bonus repaints all four. */
        if (state == drawnBtn_[i] && capitalRound == drawnCapital_) {
            continue;
        }
        const uint16_t fill = state == 1 ? Ui::success()
                            : (state == 2 ? Ui::error() : Ui::panel());
        const uint16_t tc = state == 0 ? Ui::text() : TFT_BLACK;
        const char* raw = capitalRound ? opt->capital : mnf_name(opt->iso2);
        const Rect r = answerRect(i);
        Ui::drawButton(tft, r, fitLabel(tft, raw ? raw : opt->iso2, r.w - 10, 2),
                       fill, Ui::outline(), tc, false, 2);
        drawnBtn_[i] = state;
    }
    drawnCapital_ = capitalRound;
    tft.setTextDatum(TL_DATUM);
}
