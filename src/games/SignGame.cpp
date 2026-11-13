// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SignGame.h"
#include "engine/AppRegistry.h"

namespace {
constexpr AppScoreInfo SIGN_SCORE = {
    "signs", "Sign ABC", "signBest", "in a row", false
};

constexpr AppMetadata SIGN_METADATA = {
    "signs",
    "Sign ABC",
    nullptr,
    "hand alphabet A to Z",
    "Sign ABC",
    "Learn the ASL hand alphabet, A to Z.",
    &SIGN_SCORE,
    LauncherIcon::Signs,
    40,
    true,
};

/* Hands that differ only in a detail: the six fists, the raised fingers where
 * crossing or spreading is the whole difference, the sideways pointers, and
 * three pairs that share a shape and differ by a motion or a curl. */
constexpr const char* LOOK_ALIKES[] = {
    "AEMNST", "KRUV", "GHPQ", "IJ", "DZ", "CO",
};

uint8_t lookAlikeGroup(uint8_t letter) {
    const char c = static_cast<char>('A' + letter);
    for (uint8_t g = 0; g < sizeof(LOOK_ALIKES) / sizeof(LOOK_ALIKES[0]); ++g) {
        if (strchr(LOOK_ALIKES[g], c) != nullptr) return g;
    }
    return 0xFF;
}

constexpr uint32_t FEEDBACK_RIGHT_MS = 1200;
/* Longer after a miss: the right answer is lit, and looking at it is the
 * lesson. */
constexpr uint32_t FEEDBACK_WRONG_MS = 2200;
}  // namespace

const AppMetadata& signAppMetadata() {
    return SIGN_METADATA;
}

const char* SignGame::title() const {
    return signAppMetadata().screenTitle != nullptr
        ? signAppMetadata().screenTitle
        : signAppMetadata().title;
}

// ---- the course -------------------------------------------------------------

uint8_t SignGame::lessonStart(uint8_t lesson) {
    return static_cast<uint8_t>(lesson * LESSON_SIZE);
}

uint8_t SignGame::lessonEnd(uint8_t lesson) {
    const uint8_t end = static_cast<uint8_t>((lesson + 1) * LESSON_SIZE);
    return end < LETTERS ? end : LETTERS;
}

void SignGame::goTo(Stage stage) {
    stage_ = stage;
    /* Every stage is a different screen, so this is the one place a full
     * repaint is earned. Within a stage nothing clears the screen. */
    markFullDirty();
}

void SignGame::startLesson(uint8_t lesson) {
    lesson_ = lesson;
    teachIndex_ = 0;
    goTo(Stage::Teach);
}

bool SignGame::inPracticePool(uint16_t index, void* ctx) {
    const SignGame* g = static_cast<const SignGame*>(ctx);
    return index < lessonEnd(static_cast<uint8_t>(g->passed_ - 1)) &&
           !g->recent_.recentlyUsed(index);
}

bool SignGame::inReviewPool(uint16_t index, void* ctx) {
    return index < lessonStart(static_cast<const SignGame*>(ctx)->lesson_);
}

void SignGame::startQuiz() {
    /* Every new letter is asked at least once; the rest of the six are earlier
     * letters (from lesson 2) and then the new ones again. */
    const uint8_t start = lessonStart(lesson_);
    const uint8_t count = static_cast<uint8_t>(lessonEnd(lesson_) - start);
    uint8_t n = 0;
    for (uint8_t i = 0; i < count && n < QUIZ_LEN; ++i) {
        quizLetters_[n++] = static_cast<uint8_t>(start + i);
    }
    for (uint8_t r = 0; lesson_ > 0 && r < REVIEW_IN_QUIZ && n < QUIZ_LEN; ++r) {
        const uint16_t picked = progress_.pickWeighted(inReviewPool, this);
        quizLetters_[n++] = static_cast<uint8_t>(picked == 0xFFFF ? random(start) : picked);
    }
    while (n < QUIZ_LEN) {
        quizLetters_[n++] = static_cast<uint8_t>(start + random(count));
    }
    for (uint8_t i = QUIZ_LEN - 1; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const uint8_t t = quizLetters_[i];
        quizLetters_[i] = quizLetters_[j];
        quizLetters_[j] = t;
    }
    /* The same hand twice running is a free answer; move the second along. */
    for (uint8_t i = 1; i < QUIZ_LEN; ++i) {
        if (quizLetters_[i] != quizLetters_[i - 1]) continue;
        const uint8_t k = static_cast<uint8_t>((i + 1) % QUIZ_LEN);
        const uint8_t t = quizLetters_[i];
        quizLetters_[i] = quizLetters_[k];
        quizLetters_[k] = t;
    }

    quizNum_ = 0;
    quizRight_ = 0;
    kind_ = random(2) ? Kind::Read : Kind::Find;
    goTo(Stage::Quiz);
    newQuestion();
}

void SignGame::finishQuiz(AppContext& host) {
    lastPassed_ = quizRight_ >= PASS_MARK;
    if (lastPassed_) {
        if (lesson_ == passed_ && passed_ < LESSONS) {
            ++passed_;
            host.setScore("signLesson", passed_);
        }
        host.playSound(passed_ == LESSONS && lesson_ == LESSONS - 1 ? Sound::Victory
                                                                    : Sound::LevelUp);
    }
    progress_.flush();
    goTo(Stage::Result);
}

bool SignGame::hasOption(uint8_t letter, uint8_t upTo) const {
    for (uint8_t j = 0; j < upTo; ++j) {
        if (options_[j] == letter) return true;
    }
    return false;
}

void SignGame::makeOptions(uint8_t poolEnd) {
    /* Built with the right answer in slot 0, then shuffled. Only letters the
     * player has been taught -- everything before poolEnd -- may appear. */
    options_[0] = current_;
    uint8_t filled = 1;

    const uint8_t lookAlikePct =
        static_cast<uint8_t>(25 + progress_.masteryPercent() / 2);
    const uint8_t group = lookAlikeGroup(current_);
    if (group != 0xFF) {
        const char* members = LOOK_ALIKES[group];
        const uint8_t n = static_cast<uint8_t>(strlen(members));
        for (uint8_t tries = 0; tries < 12 && filled < OPTION_COUNT; ++tries) {
            if (static_cast<uint8_t>(random(100)) >= lookAlikePct) continue;
            const uint8_t cand = static_cast<uint8_t>(members[random(n)] - 'A');
            if (cand >= poolEnd || hasOption(cand, filled)) continue;
            options_[filled++] = cand;
        }
    }
    while (filled < OPTION_COUNT) {
        const uint8_t cand = static_cast<uint8_t>(random(poolEnd));
        if (hasOption(cand, filled)) continue;
        options_[filled++] = cand;
    }

    for (uint8_t i = OPTION_COUNT - 1; i > 0; --i) {
        const uint8_t j = static_cast<uint8_t>(random(i + 1));
        const uint8_t t = options_[i];
        options_[i] = options_[j];
        options_[j] = t;
    }
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        if (options_[i] == current_) correctSlot_ = i;
    }
}

void SignGame::newQuestion() {
    uint8_t poolEnd = 0;
    if (stage_ == Stage::Quiz) {
        current_ = quizLetters_[quizNum_];
        poolEnd = lessonEnd(lesson_);
        /* Alternate, so a lesson asks both ways round. */
        if (quizNum_ > 0) kind_ = kind_ == Kind::Read ? Kind::Find : Kind::Read;
    } else {
        poolEnd = lessonEnd(static_cast<uint8_t>(passed_ - 1));
        const uint16_t picked = progress_.pickWeighted(inPracticePool, this);
        current_ = static_cast<uint8_t>(picked == 0xFFFF ? random(poolEnd) : picked);
        kind_ = random(2) ? Kind::Read : Kind::Find;
    }
    recent_.remember(current_);
    makeOptions(poolEnd);
    phase_ = Phase::Asking;
    chosen_ = -1;
    ++question_;
    markDirty();
}

void SignGame::answer(AppContext& host, uint8_t choice) {
    chosen_ = static_cast<int8_t>(choice);
    const bool right = choice == correctSlot_;
    progress_.record(current_, right);
    progress_.maybeFlush();
    if (stage_ == Stage::Practice) ++rounds_;
    if (right) {
        if (stage_ == Stage::Quiz) ++quizRight_;
        else ++score_;
        ++streak_;
        host.beepOk();
        host.saveBestScore(SIGN_SCORE.bestKey, streak_, false);
    } else {
        streak_ = 0;
        host.beepError();
    }
    phase_ = Phase::Feedback;
    feedbackUntil_ = millis() + (right ? FEEDBACK_RIGHT_MS : FEEDBACK_WRONG_MS);
    markDirty();
}

// ---- lifecycle --------------------------------------------------------------

void SignGame::begin(AppContext& host) {
    progress_.begin(host, "signProg", LETTERS);
    recent_.reset();
    const uint32_t passed = host.getScore("signLesson", 0);
    passed_ = static_cast<uint8_t>(passed > LESSONS ? LESSONS : passed);
    lesson_ = passed_ < LESSONS ? passed_ : 0;
    score_ = 0;
    rounds_ = 0;
    streak_ = 0;
    goTo(Stage::Lessons);
}

void SignGame::end(AppContext& host) {
    (void)host;
    progress_.flush();
}

void SignGame::update(AppContext& host, const TouchPoint& touch) {
    if (phase_ == Phase::Feedback && millis() >= feedbackUntil_ &&
        (stage_ == Stage::Quiz || stage_ == Stage::Practice)) {
        phase_ = Phase::Asking;
        if (stage_ == Stage::Quiz && ++quizNum_ >= QUIZ_LEN) {
            finishQuiz(host);
        } else {
            newQuestion();
        }
        return;
    }
    if (!touch.justPressed) return;

    if (stage_ != Stage::Lessons &&
        lessonsChipRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        goTo(Stage::Lessons);
        return;
    }

    switch (stage_) {
        case Stage::Lessons: updateLessons(host, touch); return;
        case Stage::Teach:   updateTeach(host, touch);   return;
        case Stage::Result:  updateResult(host, touch);  return;
        case Stage::Quiz:
        case Stage::Practice:
            break;
    }

    if (phase_ != Phase::Asking) return;
    for (uint8_t i = 0; i < OPTION_COUNT; ++i) {
        const Rect r = kind_ == Kind::Read ? letterRect(i) : handCardRect(i);
        if (r.contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
            answer(host, i);
            return;
        }
    }
}

void SignGame::updateLessons(AppContext& host, const TouchPoint& touch) {
    for (uint8_t i = 0; i <= LESSONS; ++i) {
        if (!lessonRect(i).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) continue;
        if (i < LESSONS && i <= passed_) {
            host.playSound(Sound::Select);
            startLesson(i);
        } else if (i == LESSONS && passed_ > 0) {
            host.playSound(Sound::Select);
            recent_.reset();
            goTo(Stage::Practice);
            newQuestion();
        } else {
            /* Locked: say so, gently -- the tile already reads "locked". */
            host.playSound(Sound::Wrong);
        }
        return;
    }
}

void SignGame::updateTeach(AppContext& host, const TouchPoint& touch) {
    const uint8_t count = static_cast<uint8_t>(lessonEnd(lesson_) - lessonStart(lesson_));
    if (backRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (teachIndex_ == 0) return;
        --teachIndex_;
        host.playSound(Sound::Whoosh);
        markDirty();
    } else if (nextRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (teachIndex_ + 1 >= count) {
            host.playSound(Sound::Select);
            startQuiz();
            return;
        }
        ++teachIndex_;
        host.playSound(Sound::Whoosh);
        markDirty();
    }
}

void SignGame::updateResult(AppContext& host, const TouchPoint& touch) {
    const bool first = resultRect(0).contains(touch.x, touch.y, TOUCH_HIT_SLOP);
    const bool second = resultRect(1).contains(touch.x, touch.y, TOUCH_HIT_SLOP);
    if (!first && !second) return;
    host.playSound(Sound::Select);
    if (lastPassed_) {
        if (second) {
            goTo(Stage::Lessons);
        } else if (lesson_ + 1 < LESSONS) {
            startLesson(static_cast<uint8_t>(lesson_ + 1));
        } else {
            recent_.reset();
            goTo(Stage::Practice);
            newQuestion();
        }
    } else if (first) {
        startLesson(lesson_);           // look at the hands again
    } else {
        startQuiz();                    // straight back into the quiz
    }
}
