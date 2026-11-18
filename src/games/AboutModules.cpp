// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "AboutApp.h"

/* About's modules page: which of the clip-on modules this console has on, and
 * what each one gives it. Read from the module table at render time -- the
 * same rule as the game list and the radio page, because the owner who
 * unclipped the keyboard yesterday is exactly who a typed-in list would lie
 * to. Present and absent alike, so the page also says what could be added.
 *
 * The table fits the page: seven font-1 lines below the heading is what the
 * panel holds in landscape, and the one board with a table has six modules.
 * A seventh fills it; past that it needs paging, not smaller text.
 *
 * Built only where GUME_HAS_ATTAKY_MODULES is set; elsewhere the page does not
 * exist (MODULE_PAGES is 0). */

#if GUME_HAS_ATTAKY_MODULES
#include "hal/modules/Modules.h"

void AboutApp::renderModules(Ui::Renderer& tft) {
    drawLine(tft, 48, "Modules", 2);
    tft.setTextColor(Ui::muted(), Ui::surface());
    drawLine(tft, 74, "What is clipped on, checked at start-up.", 1);

    constexpr int16_t FIRST_Y = 94;
    constexpr int16_t STEP = 14;
    constexpr uint8_t MAX_LINES = 7;   // to y=178, above the panel's foot
    for (uint8_t i = 0; i < Modules::count() && i < MAX_LINES; ++i) {
        char state[40];
        Modules::describe(i, state, sizeof(state));
        char line[64];
        snprintf(line, sizeof(line), "%s: %s", Modules::driver(i).name, state);
        tft.setTextColor(Modules::present(i) ? Ui::text() : Ui::muted(), Ui::surface());
        drawLine(tft, static_cast<int16_t>(FIRST_Y + i * STEP), line, 1);
    }
}

#endif  /* GUME_HAS_ATTAKY_MODULES */
