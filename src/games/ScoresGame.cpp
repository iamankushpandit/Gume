#include "ScoresGame.h"
#include "engine/AppRegistry.h"

const char* ScoresGame::title() const { return "Scores"; }

void ScoresGame::begin(GameHost& host) {
    (void)host.requireCapability(APP_CAP_SCORES, "open scores");
    page_ = 0;
    activeTab_ = Tab::Mine;
    buildDeviceTable(host);
    markFullDirty();
}

namespace {
/* The footer sits a fixed distance off the bottom; rows fill what is left
 * between the tab baseline and it. */
constexpr int16_t SCORES_ROWS_TOP = 56;
constexpr int16_t SCORES_FOOTER_H = 32;
constexpr int16_t SCORES_MARGIN = 8;
}  // namespace

/* Rows share the height between the tabs and the footer, but never get
 * tighter than the 30px pitch this screen was designed with -- so 320x240
 * yields exactly the old 56 + row * 30, and a taller panel spends its extra
 * height on taller rows, which are also easier to hit. */
Rect ScoresGame::rowRect(uint8_t row, int16_t screenW, int16_t screenH) const {
    const int16_t footerY = static_cast<int16_t>(screenH - SCORES_FOOTER_H);
    const int16_t avail = static_cast<int16_t>(footerY - SCORES_ROWS_TOP - 4);
    int16_t pitch = static_cast<int16_t>(avail / ROWS_PER_PAGE);
    if (pitch < 30) {
        pitch = 30;
    }
    return Rect{SCORES_MARGIN,
                static_cast<int16_t>(SCORES_ROWS_TOP + row * pitch),
                static_cast<int16_t>(screenW - SCORES_MARGIN * 2),
                static_cast<int16_t>(pitch - 2)};
}
Rect ScoresGame::mineTabRect() const {
    return Rect{8, 34, 64, 18};
}
Rect ScoresGame::deviceTabRect() const {
    return Rect{80, 34, 80, 18};
}

/* Prev and Next hug the edges, Switch is centred between them. The fractions
 * are the old widths over 320, so the original board keeps 88 / 112 / 88 at
 * x = 8 / 104 / 224 exactly. */
Rect ScoresGame::prevRect(int16_t screenW, int16_t screenH) const {
    return Rect{SCORES_MARGIN, static_cast<int16_t>(screenH - SCORES_FOOTER_H),
                static_cast<int16_t>(screenW * 0.275f), 26};
}
Rect ScoresGame::switchRect(int16_t screenW, int16_t screenH) const {
    const int16_t w = static_cast<int16_t>(screenW * 0.35f);
    return Rect{static_cast<int16_t>((screenW - w) / 2),
                static_cast<int16_t>(screenH - SCORES_FOOTER_H), w, 26};
}
Rect ScoresGame::nextRect(int16_t screenW, int16_t screenH) const {
    const int16_t w = static_cast<int16_t>(screenW * 0.275f);
    return Rect{static_cast<int16_t>(screenW - SCORES_MARGIN - w),
                static_cast<int16_t>(screenH - SCORES_FOOTER_H), w, 26};
}

uint8_t ScoresGame::playedCount(GameHost& host) const {
    uint8_t n = 0;
    for (uint8_t i = 0; i < playableAppCount(); ++i) {
        const AppScoreInfo* score = playableAppAt(i).score();
        if (score != nullptr && host.board().hasScore(score->bestKey)) {
            ++n;
        }
    }
    return n;
}

uint8_t ScoresGame::deviceRowCount() const {
    return deviceRowCount_;
}

void ScoresGame::buildDeviceTable(GameHost& host) {
    Board& board = host.board();
    deviceRowCount_ = 0;

    // Cache holder names to avoid repeated String allocations
    for (uint8_t i = 0; i < board.playerCount(); ++i) {
        String name = board.profileName(i);
        snprintf(holderNames_[i], Board::PROFILE_NAME_MAX + 1, "%s", name.c_str());
    }

    // Build device best rows: one per game anyone has played
    for (uint8_t appIdx = 0; appIdx < playableAppCount(); ++appIdx) {
        const AppDefinition& app = playableAppAt(appIdx);
        const AppScoreInfo* score = app.score();
        if (score == nullptr) {
            continue;
        }

        bool anyPlayed = false;
        uint32_t bestValue = 0;
        uint8_t bestHolder = 0xFF;

        // Check all profiles (0 to playerCount-1, excluding Guest at GUEST_INDEX)
        for (uint8_t profileIdx = 0; profileIdx < board.playerCount(); ++profileIdx) {
            if (!board.hasScoreFor(profileIdx, score->bestKey)) continue;

            anyPlayed = true;
            uint32_t profileScore = board.scoreFor(profileIdx, score->bestKey, 0);

            // Determine if this is better than the current best
            bool isBetter = false;
            if (bestHolder == 0xFF) {
                // First score found
                isBetter = true;
            } else if (score->lowerIsBetter) {
                // Smaller is better
                isBetter = (profileScore < bestValue);
            } else {
                // Larger is better
                isBetter = (profileScore > bestValue);
            }

            if (isBetter) {
                bestValue = profileScore;
                bestHolder = profileIdx;
            }
        }

        if (anyPlayed && deviceRowCount_ < MAX_DEVICE_ROWS) {
            deviceRows_[deviceRowCount_].catalogIndex = appIdx;
            deviceRows_[deviceRowCount_].value = bestValue;
            deviceRows_[deviceRowCount_].holder = bestHolder;
            deviceRowCount_++;
        }
    }

    deviceStale_ = false;
}

void ScoresGame::update(GameHost& host, const TouchPoint& touch) {
    if (!touch.justPressed) return;
    if (!host.requireCapability(APP_CAP_SCORES, "view scores")) {
        return;
    }

    const int16_t sW = static_cast<int16_t>(host.display().width());
    const int16_t sH = static_cast<int16_t>(host.display().height());

    // Tab switching
    if (mineTabRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (activeTab_ != Tab::Mine) {
            activeTab_ = Tab::Mine;
            page_ = 0;
            markFullDirty();
        }
        return;
    }
    if (deviceTabRect().contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        if (activeTab_ != Tab::Device) {
            activeTab_ = Tab::Device;
            page_ = 0;
            if (deviceStale_) {
                buildDeviceTable(host);
            }
            markFullDirty();
        }
        return;
    }

    // Paging and switching
    if (activeTab_ == Tab::Mine) {
        const uint8_t total = playedCount(host);
        const uint8_t pages = max<uint8_t>(1, (total + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);

        if (prevRect(sW, sH).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
            --page_; markFullDirty(); return;
        }
        if (nextRect(sW, sH).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ + 1 < pages) {
            ++page_; markFullDirty(); return;
        }
    } else {
        const uint8_t total = deviceRowCount_;
        const uint8_t pages = max<uint8_t>(1, (total + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);

        if (prevRect(sW, sH).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ > 0) {
            --page_; markFullDirty(); return;
        }
        if (nextRect(sW, sH).contains(touch.x, touch.y, TOUCH_HIT_SLOP) && page_ + 1 < pages) {
            ++page_; markFullDirty(); return;
        }
    }

    if (switchRect(sW, sH).contains(touch.x, touch.y, TOUCH_HIT_SLOP)) {
        host.openProfiles();
        return;
    }
}

void ScoresGame::render(GameHost& host) {
    Board& board = host.board();
    Ui::Renderer& tft = host.display();
    const int16_t sW = static_cast<int16_t>(tft.width());
    const int16_t sH = static_cast<int16_t>(tft.height());
    Ui::clear(tft);
    Ui::drawTopBar(host.board(), title());

    // Draw tab strip
    if (needsFullRender()) {
        Ui::drawTab(tft, mineTabRect(), "Mine", activeTab_ == Tab::Mine);
        Ui::drawTab(tft, deviceTabRect(), "Device", activeTab_ == Tab::Device);
        const Rect& activeTab = (activeTab_ == Tab::Mine) ? mineTabRect() : deviceTabRect();
        Ui::drawTabBaseline(tft, mineTabRect().y + mineTabRect().h,
                            mineTabRect().x, deviceTabRect().x + deviceTabRect().w, activeTab);
    }

    if (activeTab_ == Tab::Mine) {
        // Mine tab: current player's scores
        tft.setTextDatum(TL_DATUM);
        tft.setTextColor(Ui::text(), Ui::bg());
        tft.drawString(board.profileName(board.activeProfile()), 8, 60, 2);

        // Column headings
        if (needsFullRender()) {
            tft.setTextColor(Ui::muted(), Ui::bg());
            tft.setTextDatum(TR_DATUM);
            tft.drawString("best", 244, 64, 1);
            tft.drawString("worst", 306, 64, 1);
            tft.setTextDatum(TL_DATUM);
        }

        const uint8_t total = playedCount(host);
        if (total == 0) {
            Ui::drawLabel(tft, Rect{static_cast<int16_t>(sW * 0.0625f), static_cast<int16_t>(sH * 0.458f), static_cast<int16_t>(sW * 0.875f), 20},
                          "No games played yet", Ui::muted(), 2, Align::Center);
        }

        // Walk the catalog, skipping games this player has not played.
        uint8_t seen = 0;
        uint8_t drawn = 0;
        const uint8_t first = static_cast<uint8_t>(page_ * ROWS_PER_PAGE);
        for (uint8_t i = 0; i < playableAppCount() && drawn < ROWS_PER_PAGE; ++i) {
            const AppDefinition& app = playableAppAt(i);
            const AppScoreInfo* score = app.score();
            if (score == nullptr || !board.hasScore(score->bestKey)) continue;
            if (seen++ < first) continue;

            const Rect r = rowRect(drawn++, sW, sH);
            tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
            tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());

            tft.setTextColor(Ui::text(), Ui::surface());
            tft.setTextDatum(ML_DATUM);
            tft.drawString(score->label, r.x + 8, r.y + r.h / 2, 2);

            const uint32_t best  = board.getScore(score->bestKey, 0);
            const uint32_t worst = board.worstScore(score->bestKey, best);

            tft.setTextDatum(MR_DATUM);
            tft.setTextColor(Ui::success(), Ui::surface());
            char value[24];
            snprintf(value, sizeof(value), "%u%s", best, score->unit);
            tft.drawString(value, 244, r.y + r.h / 2, 2);
            tft.setTextColor(Ui::muted(), Ui::surface());
            snprintf(value, sizeof(value), "%u%s", worst, score->unit);
            tft.drawString(value, 306, r.y + r.h / 2, 2);

            if (score->lowerIsBetter) {
                tft.setTextColor(Ui::muted(), Ui::surface());
                tft.setTextDatum(ML_DATUM);
                tft.drawString("lower is better", r.x + 100, r.y + r.h / 2, 1);
            }
        }

        const uint8_t pages = max<uint8_t>(1, (total + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
        const bool canPrev = page_ > 0;
        const bool canNext = page_ + 1 < pages;
        Ui::drawPagerButton(tft, prevRect(sW, sH), "Prev", canPrev);
        Ui::drawButton(tft, switchRect(sW, sH), "Switch player", Ui::rgb(36, 132, 204),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawPagerButton(tft, nextRect(sW, sH), "Next", canNext);
    } else {
        // Device tab: device-wide best
        const uint8_t playerCount = board.playerCount();

        if (needsFullRender()) {
            if (playerCount == 1) {
                // Only one player, explain the situation
                tft.setTextDatum(TL_DATUM);
                tft.setTextColor(Ui::muted(), Ui::bg());
                tft.drawString("Add a player to compete", 8, 60, 1);
            }
        }

        const uint8_t total = deviceRowCount_;
        if (total == 0) {
            Ui::drawLabel(tft, Rect{static_cast<int16_t>(sW * 0.0625f), static_cast<int16_t>(sH * 0.458f), static_cast<int16_t>(sW * 0.875f), 20},
                          "No games played yet", Ui::muted(), 2, Align::Center);
        } else {
            uint8_t drawn = 0;
            const uint8_t first = static_cast<uint8_t>(page_ * ROWS_PER_PAGE);
            for (uint8_t i = first; i < deviceRowCount_ && drawn < ROWS_PER_PAGE; ++i) {
                const DeviceBest& db = deviceRows_[i];
                const AppScoreInfo* score = playableAppAt(db.catalogIndex).score();
                if (score == nullptr) {
                    continue;
                }

                const Rect r = rowRect(drawn++, sW, sH);
                tft.fillRoundRect(r.x, r.y, r.w, r.h, 4, Ui::surface());
                tft.drawRoundRect(r.x, r.y, r.w, r.h, 4, Ui::outline());

                // Game label
                tft.setTextColor(Ui::text(), Ui::surface());
                tft.setTextDatum(ML_DATUM);
                tft.drawString(score->label, r.x + 8, r.y + r.h / 2, 2);

                // Device best value, right-aligned at x = 236
                char scoreStr[32];
                snprintf(scoreStr, sizeof(scoreStr), "%u%s", db.value, score->unit);
                tft.setTextDatum(MR_DATUM);
                tft.setTextColor(Ui::success(), Ui::surface());
                tft.drawString(scoreStr, 236, r.y + r.h / 2, 2);

                // Holder name, right-aligned at x = 312, gold if current player
                if (db.holder < Board::MAX_PLAYERS) {
                    uint16_t holderColor = (board.activeProfile() == db.holder)
                        ? Ui::rgb(255, 200, 0)
                        : Ui::muted();
                    tft.setTextColor(holderColor, Ui::surface());
                    tft.setTextDatum(MR_DATUM);
                    tft.drawString(holderNames_[db.holder], 312, r.y + r.h / 2, 2);
                }
            }
        }

        const uint8_t pages = max<uint8_t>(1, (total + ROWS_PER_PAGE - 1) / ROWS_PER_PAGE);
        const bool canPrev = page_ > 0;
        const bool canNext = page_ + 1 < pages;
        Ui::drawPagerButton(tft, prevRect(sW, sH), "Prev", canPrev);
        Ui::drawButton(tft, switchRect(sW, sH), "Switch player", Ui::rgb(36, 132, 204),
                       Ui::outline(), TFT_WHITE, false, 2);
        Ui::drawPagerButton(tft, nextRect(sW, sH), "Next", canNext);
    }

    tft.setTextDatum(TL_DATUM);
}
