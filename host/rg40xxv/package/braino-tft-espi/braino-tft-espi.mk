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
# braino-tft-espi
#
################################################################################

# One of the three libraries host/CMakeLists.txt would otherwise clone at
# configure time, at the version it pins. As a package it is fetched,
# hash-checked and listed in legal-info under its own licence.
BRAINO_TFT_ESPI_VERSION = V2.5.43
BRAINO_TFT_ESPI_SITE = $(call github,Bodmer,TFT_eSPI,$(BRAINO_TFT_ESPI_VERSION))
BRAINO_TFT_ESPI_LICENSE = MIT, BSD-2-Clause
BRAINO_TFT_ESPI_LICENSE_FILES = license.txt
BRAINO_TFT_ESPI_INSTALL_TARGET = NO

$(eval $(generic-package))
