# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

################################################################################
#
# rocknix-joypad
#
################################################################################

# The commit ROCKNIX pins in projects/ROCKNIX/packages/linux-drivers/rocknix-joypad.
ROCKNIX_JOYPAD_VERSION = 005033d1ddd8746c771f2c81f1174f2d892541c9
ROCKNIX_JOYPAD_SITE = $(call github,hackbar,rocknix-joypad,$(ROCKNIX_JOYPAD_VERSION))
ROCKNIX_JOYPAD_LICENSE = GPL-2.0-or-later
ROCKNIX_JOYPAD_LICENSE_FILES = rocknix-singleadc-joypad.c CREDITS
# The H700 consoles use only the single-ADC driver.
ROCKNIX_JOYPAD_MODULE_MAKE_OPTS = DEVICE=H700

# The driver polls every 10 ms -- three ADC conversions each, about 300
# interrupts a second -- and never sets a ceiling on its interval, so the
# kernel makes 10 ms the ceiling too and refuses a slower one in sysfs. A
# sleeping Braino slows it (host/src/HostSleep.cpp), which needs the room.
define ROCKNIX_JOYPAD_ALLOW_SLOWER_POLLING
	$(SED) 's/^\(\s*\)input_set_poll_interval(input, joypad->poll_interval);$$/&\n\1input_set_max_poll_interval(input, 500);/' \
		$(@D)/rocknix-singleadc-joypad.c
	grep -q 'input_set_max_poll_interval(input, 500);' $(@D)/rocknix-singleadc-joypad.c
endef
ROCKNIX_JOYPAD_POST_PATCH_HOOKS += ROCKNIX_JOYPAD_ALLOW_SLOWER_POLLING

$(eval $(kernel-module))
$(eval $(generic-package))
