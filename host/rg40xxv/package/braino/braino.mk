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
# braino
#
################################################################################

# This repository, as it stands: host/rg40xxv is two levels below its root.
BRAINO_SITE = $(realpath $(BR2_EXTERNAL_BRAINO_PATH)/../..)
BRAINO_SITE_METHOD = local
# build.sh works the commit out (a worktree's .git names a Windows path that
# git in WSL cannot follow) and passes it down.
BRAINO_VERSION = $(or $(BRAINO_GIT_COMMIT),local)
BRAINO_SUBDIR = host
BRAINO_LICENSE = GPL-3.0-or-later
BRAINO_LICENSE_FILES = LICENSE NOTICE.md
# The three libraries are packages of their own (package/braino-*), so they
# are fetched and listed in legal-info like everything else.
BRAINO_DEPENDENCIES = sdl2 libcurl braino-tft-espi braino-arduinojson braino-map-n-flag

BRAINO_CONF_OPTS = \
	-DFETCHCONTENT_FULLY_DISCONNECTED=ON \
	-DFETCHCONTENT_SOURCE_DIR_TFT_ESPI=$(BRAINO_TFT_ESPI_DIR) \
	-DFETCHCONTENT_SOURCE_DIR_ARDUINOJSON=$(BRAINO_ARDUINOJSON_DIR) \
	-DFETCHCONTENT_SOURCE_DIR_MAP_N_FLAG=$(BRAINO_MAP_N_FLAG_DIR) \
	-DBRAINO_HOST_DRM=ON \
	-DBRAINO_HOST_BATTERY=ON \
	-DBRAINO_HOST_STRICT_SECRETS=ON \
	-DBRAINO_HOST_SCREEN_POWER=ON

# The copied tree has no .git, so the build stamp is told which commit it is.
BRAINO_MAKE_ENV = BRAINO_BUILD_COMMIT=$(or $(BRAINO_GIT_COMMIT),unknown) \
	BRAINO_BUILD_BRANCH=$(or $(BRAINO_GIT_BRANCH),unknown)

define BRAINO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(BRAINO_BUILDDIR)/braino $(TARGET_DIR)/usr/bin/braino
endef

$(eval $(cmake-package))
