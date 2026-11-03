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

$(eval $(kernel-module))
$(eval $(generic-package))
