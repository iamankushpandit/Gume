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
# braino-map-n-flag
#
################################################################################

# One of the three libraries host/CMakeLists.txt would otherwise clone at
# configure time, at the version it pins. As a package it is fetched,
# hash-checked and listed in legal-info under its own licence.
BRAINO_MAP_N_FLAG_VERSION = 3c412cb5cea4dcc5c696abd7a4db78470a97fbd1
BRAINO_MAP_N_FLAG_SITE = $(call github,iamankushpandit,map-n-flag,$(BRAINO_MAP_N_FLAG_VERSION))
BRAINO_MAP_N_FLAG_LICENSE = MIT
BRAINO_MAP_N_FLAG_LICENSE_FILES = LICENSE
BRAINO_MAP_N_FLAG_INSTALL_TARGET = NO

$(eval $(generic-package))
