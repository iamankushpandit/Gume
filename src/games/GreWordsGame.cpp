#include "GreWordsGame.h"
#include "GreWordTable.h"

#include <cstdio>
#include <cstring>

namespace {
/* Progress::pickWeighted takes a plain function pointer, not a closure. */
bool allowAny(uint16_t, void*) { return true; }
constexpr uint32_t REVEAL_MS = 900;
}

const char* GreWordsGame::title() const { return "GRE Words"; }

Rect GreWordsGame::flipTabRect(int16_t w) const {
    return Rect{0, 30, static_cast<int16_t>(w / 2), 22};
}
Rect GreWordsGame::quizTabRect(int16_t w) const {
    return Rect{static_cast<int16_t>(w / 2), 30, static_cast<int16_t>(w - w / 2), 22};
}
Rect GreWordsGame::cardRect() const { return Rect{12, 58, 296, 130}; }
Rect GreWordsGame::optionRect(uint8_t i) const {
    // Below the word, which is font 4 centred at y=70 and so reaches ~83.
    return Rect{10, static_cast<int16_t>(88 + i * 34), 300, 30};
}
Rect GreWordsGame::knewRect()   const { return Rect{12,  192, 92, 30}; }
Rect GreWordsGame::missedRect() const { return Rect{114, 192, 92, 30}; }
Rect GreWordsGame::nextRect()   const { return Rect{216, 192, 92, 30}; }

void GreWordsGame::begin(AppContext& host) {
    progress_.begin(host, "greProg", GRE_WORD_COUNT);
    score_  = 0;
    streak_ = 0;
    mode_   = Mode::Flip;
    nextWord();
    markFullDirty();
}

void GreWordsGame::end(AppContext& host) {
    (void)host;
    // Mastery data is written lazily; hand it back on the way out.
    progress_.flush();
}

void GreWordsGame::nextWord() {
    const uint16_t picked = progress_.pickWeighted(allowAny, this);
    currentWord_ = (picked == 0xFFFF) ? 0 : picked;
    cardFlipped_ = false;
    chosenOption_ = -1;
    revealUntilMs_ = 0;
    wrapStale_ = true;
    buildOptions();
}

void GreWordsGame::buildOptions() {
    correctOption_ = static_cast<uint8_t>(random(OPTION_COUNT));
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (i == correctOption_) {
            optionIndices_[i] = currentWord_;
            continue;
        }
        /* Distractors must be distinct from the answer and from each other,
         * or the round has two right-looking choices. Bounded retries: the
         * table is 250 long, so a collision run this long cannot happen, but
         * an unbounded while() in a frame path is never acceptable. */
        for (uint8_t attempt = 0; attempt < 64; ++attempt) {
            const uint16_t cand = static_cast<uint16_t>(random(GRE_WORD_COUNT));
            if (cand == currentWord_) continue;
            bool clash = false;
            for (uint8_t j = 0; j < i; ++j) {
                if (optionIndices_[j] == cand) { clash = true; break; }
            }
            if (!clash) { optionIndices_[i] = cand; break; }
        }
    }
}

void GreWordsGame::wrapInto(const char* src, char dst[WRAP_LINES][WRAP_COLS + 1],
                            uint8_t& lineCount) {
    lineCount = 0;
    for (uint8_t i = 0; i < WRAP_LINES; ++i) dst[i][0] = '\0';
    if (src == nullptr) return;

    uint16_t pos = 0;
    const uint16_t len = static_cast<uint16_t>(strlen(src));
    while (pos < len && lineCount < WRAP_LINES) {
        uint16_t take = WRAP_COLS;
        if (pos + take >= len) {
            take = static_cast<uint16_t>(len - pos);
        } else {
            // Break on the last space that fits, so words stay whole.
            uint16_t brk = take;
            while (brk > 0 && src[pos + brk] != ' ') --brk;
            if (brk > 0) take = brk;
        }
        memcpy(dst[lineCount], src + pos, take);
        dst[lineCount][take] = '\0';
        ++lineCount;
        pos = static_cast<uint16_t>(pos + take);
        while (pos < len && src[pos] == ' ') ++pos;
    }
}

void GreWordsGame::rewrapIfStale() {
    if (!wrapStale_) return;
    const GreWord& w = GRE_WORDS[currentWord_];
    wrapInto(w.meaning, meaningLines_, meaningLineCount_);
    wrapInto(w.example, exampleLines_, exampleLineCount_);
    wrapStale_ = false;
}

void GreWordsGame::answer(AppContext& host, bool correct) {
    if (correct) {
        host.beepOk();
        score_ += 10;
        ++streak_;
    } else {
        host.beepError();
        streak_ = 0;
    }
    progress_.record(currentWord_, correct);
    progress_.maybeFlush();
    host.saveBestScore("greBest", score_, false);
}

void GreWordsGame::update(AppContext& host, const TouchPoint& touch) {
    const int16_t w = static_cast<int16_t>(host.display().width());

    // A wrong answer holds the correct option highlighted for a moment before
    // moving on, so the round still teaches rather than just scoring.
    if (revealUntilMs_ != 0 && millis() > revealUntilMs_) {
        nextWord();
        markFullDirty();
        return;
    }

    if (!touch.justPressed) return;

    if (flipTabRect(w).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (mode_ != Mode::Flip) { mode_ = Mode::Flip; markFullDirty(); }
        return;
    }
    if (quizTabRect(w).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (mode_ != Mode::Quiz) { mode_ = Mode::Quiz; markFullDirty(); }
        return;
    }

    if (revealUntilMs_ != 0) return;   // mid-reveal: ignore further taps

    if (mode_ == Mode::Flip) {
        if (cardRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            cardFlipped_ = !cardFlipped_;
            markFullDirty();
            return;
        }
        // Knew it / Didn't feed the same mastery data the quiz does, so study
        // mode is not wasted effort -- it just does not score.
        if (knewRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            progress_.record(currentWord_, true);
            progress_.maybeFlush();
            nextWord(); markFullDirty(); return;
        }
        if (missedRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            progress_.record(currentWord_, false);
            progress_.maybeFlush();
            nextWord(); markFullDirty(); return;
        }
        if (nextRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            nextWord(); markFullDirty(); return;
        }
        return;
    }

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (!optionRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        chosenOption_ = static_cast<int8_t>(i);
        const bool correct = (i == correctOption_);
        answer(host, correct);
        revealUntilMs_ = millis() + REVEAL_MS;
        markFullDirty();
        return;
    }
}

void GreWordsGame::renderFlip(AppContext& host) {
    TFT_eSPI& tft = host.display();
    const GreWord& word = GRE_WORDS[currentWord_];
    const Rect card = cardRect();

    tft.fillRoundRect(card.x, card.y, card.w, card.h, 8, Ui::surface());
    tft.drawRoundRect(card.x, card.y, card.w, card.h, 8, Ui::outline());

    tft.setTextDatum(MC_DATUM);
    if (!cardFlipped_) {
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(word.word, card.x + card.w / 2, card.y + 48, 4);
        tft.setTextColor(Ui::muted(), Ui::surface());
        tft.drawString(word.pos, card.x + card.w / 2, card.y + 78, 2);
        tft.drawString("tap to reveal", card.x + card.w / 2, card.y + 110, 1);
    } else {
        tft.setTextColor(Ui::text(), Ui::surface());
        tft.drawString(word.word, card.x + card.w / 2, card.y + 18, 2);
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::success(), Ui::surface());
        for (uint8_t i = 0; i < meaningLineCount_; ++i) {
            tft.drawString(meaningLines_[i], card.x + 10,
                           static_cast<int16_t>(card.y + 36 + i * 14), 1);
        }
        tft.setTextColor(Ui::muted(), Ui::surface());
        for (uint8_t i = 0; i < exampleLineCount_; ++i) {
            tft.drawString(exampleLines_[i], card.x + 10,
                           static_cast<int16_t>(card.y + 82 + i * 14), 1);
        }
    }
    tft.setTextDatum(TL_DATUM);

    Ui::drawButton(tft, knewRect(),   "Knew it", Ui::rgb(45, 154, 96),
                   Ui::outline(), TFT_WHITE, false, 2);
    Ui::drawButton(tft, missedRect(), "Didn't",  Ui::rgb(178, 58, 58),
                   Ui::outline(), TFT_WHITE, false, 2);
    Ui::drawButton(tft, nextRect(),   "Next",    Ui::panel(),
                   Ui::outline(), Ui::text(), false, 2);
}

void GreWordsGame::renderQuiz(AppContext& host) {
    TFT_eSPI& tft = host.display();
    const GreWord& word = GRE_WORDS[currentWord_];

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(Ui::text(), Ui::bg());
    tft.drawString(word.word, static_cast<int16_t>(tft.width() / 2), 70, 4);
    tft.setTextDatum(TL_DATUM);

    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const Rect r = optionRect(i);
        uint16_t fill = Ui::panel();
        uint16_t ink  = Ui::text();
        if (revealUntilMs_ != 0) {
            if (i == correctOption_) {
                fill = Ui::success(); ink = TFT_BLACK;
            } else if (i == chosenOption_) {
                fill = Ui::error();   ink = TFT_WHITE;
            }
        }
        tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, fill);
        tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, Ui::outline());
        tft.setTextColor(ink, fill);
        tft.setTextDatum(ML_DATUM);
        /* Ui::fitted() takes a String and would allocate; the glosses are
         * authored short enough to fit, so draw the flash pointer directly. */
        tft.drawString(GRE_WORDS[optionIndices_[i]].meaning,
                       r.x + 8, static_cast<int16_t>(r.y + r.h / 2), 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void GreWordsGame::render(AppContext& host) {
    TFT_eSPI& tft = host.display();
    const int16_t w = static_cast<int16_t>(tft.width());

    rewrapIfStale();

    if (needsFullRender()) {
        Ui::clear(tft);
        host.drawTopBar(title());
        const Rect fTab = flipTabRect(w);
        const Rect qTab = quizTabRect(w);
        Ui::drawTab(tft, fTab, "Study", mode_ == Mode::Flip);
        Ui::drawTab(tft, qTab, "Quiz",  mode_ == Mode::Quiz);
        Ui::drawTabBaseline(tft, 52, 0, w, mode_ == Mode::Flip ? fTab : qTab);
    }

    if (mode_ == Mode::Flip) {
        renderFlip(host);
    } else {
        renderQuiz(host);
    }

    char buf[24];
    tft.setTextColor(Ui::muted(), Ui::bg());
    tft.setTextDatum(TL_DATUM);
    snprintf(buf, sizeof(buf), "%u pts  x%u",
             static_cast<unsigned>(score_), static_cast<unsigned>(streak_));
    tft.fillRect(8, 226, 150, 12, Ui::bg());
    tft.drawString(buf, 8, 226, 1);

    snprintf(buf, sizeof(buf), "%u / %u",
             static_cast<unsigned>(currentWord_ + 1),
             static_cast<unsigned>(GRE_WORD_COUNT));
    tft.setTextDatum(TR_DATUM);
    tft.fillRect(220, 226, 92, 12, Ui::bg());
    tft.drawString(buf, static_cast<int16_t>(w - 8), 226, 1);
    tft.setTextDatum(TL_DATUM);
}
