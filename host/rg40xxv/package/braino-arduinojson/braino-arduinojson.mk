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
# braino-arduinojson
#
################################################################################

# One of the three libraries host/CMakeLists.txt would otherwise clone at
# configure time, at the version it pins. As a package it is fetched,
# hash-checked and listed in legal-info under its own licence.
BRAINO_ARDUINOJSON_VERSION = v7.4.3
BRAINO_ARDUINOJSON_SITE = $(call github,bblanchon,ArduinoJson,$(BRAINO_ARDUINOJSON_VERSION))
BRAINO_ARDUINOJSON_LICENSE = MIT
BRAINO_ARDUINOJSON_LICENSE_FILES = LICENSE.txt
BRAINO_ARDUINOJSON_INSTALL_TARGET = NO

$(eval $(generic-package))
