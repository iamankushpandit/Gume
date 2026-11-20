// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
//
// Part of Braino! -- https://github.com/iamankushpandit/Gume
// Free software under GPL-3.0-or-later. Reusing any part of this file, in
// any work, must keep this notice, credit iamankushpandit as the
// author, and stay under the same licence with corresponding source
// offered. See LICENSE and NOTICE.md.

#pragma once

/* The one way out of the host program once the window is up. The main loop
 * reaches it when the user quits between frames, and delay() reaches it when
 * they quit while the firmware is waiting -- which is most of the time, since
 * the loop spends its spare frame time in delay(). Both used to tear down on
 * their own, and delay()'s copy forgot the Bluetooth reader: a std::thread
 * still joinable when statics are destroyed calls std::terminate, so closing
 * the window mid-wait ended in "terminate called without an active exception"
 * with the beacon left on the air. */
[[noreturn]] void hostExit(int code);
