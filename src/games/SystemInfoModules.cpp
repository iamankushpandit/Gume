// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#include "SystemInfoApp.h"

/* The Board tab's Module section: one row per module this board can wear,
 * and what boot found. Read from the module table rather than typed in -- the
 * same derive-do-not-restate rule as every other row here -- so a module that
 * gains a driver appears without this file changing, and a module that came
 * off appears as not found rather than as a claim.
 *
 * Built only where GUME_HAS_ATTAKY_MODULES is set. */

#if GUME_HAS_ATTAKY_MODULES
#include "hal/modules/Modules.h"

void SystemInfoApp::addModuleRows() {
    rows_.addSection("Module");
    for (uint8_t i = 0; i < Modules::count(); ++i) {
        char state[40];
        Modules::describe(i, state, sizeof(state));
        rows_.addRow(Modules::driver(i).name, state,
                     Modules::present(i) ? Ui::text() : Ui::muted());
    }
}

#endif  /* GUME_HAS_ATTAKY_MODULES */
