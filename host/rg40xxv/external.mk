# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: Copyright (C) 2026 iamankushpandit <https://github.com/iamankushpandit>
#
# Part of Braino! -- https://github.com/iamankushpandit/Gume
# Free software under GPL-3.0-or-later. Reusing any part of this file, in
# any work, must keep this notice, credit iamankushpandit as the
# author, and stay under the same licence with corresponding source
# offered. See LICENSE and NOTICE.md.

include $(sort $(wildcard $(BR2_EXTERNAL_BRAINO_PATH)/package/*/*.mk))

# ROCKNIX's kernel patches, applied the way ROCKNIX's build applies them: in
# its order (7.2, mainline, H700) with patch's default fuzz. Buildroot's own
# BR2_LINUX_KERNEL_PATCH uses -F0, and two hunks of 0002-rg35xx-enable-HDMI-LCD
# only apply with the fuzz ROCKNIX's build always had. Unlike ROCKNIX's build,
# a hunk that does not apply at all stops this one.
define BRAINO_LINUX_APPLY_ROCKNIX_PATCHES
	set -e; for p in $(BR2_EXTERNAL_BRAINO_PATH)/rocknix/linux/1-7.2/*.patch \
		$(BR2_EXTERNAL_BRAINO_PATH)/rocknix/linux/2-mainline/*.patch \
		$(BR2_EXTERNAL_BRAINO_PATH)/rocknix/linux/3-h700/*.patch; do \
		echo "rocknix: applying $$(basename $$p)"; \
		patch -d $(LINUX_DIR) -p1 -s -t -N --no-backup-if-mismatch < $$p; \
	done
endef
LINUX_POST_PATCH_HOOKS += BRAINO_LINUX_APPLY_ROCKNIX_PATCHES

# ROCKNIX's own device trees for the RG40XX V replace the in-tree ones after
# the patches are applied, and the kernel is told to build them, so
# BR2_LINUX_KERNEL_INTREE_DTS_NAME can name them like any in-tree board.
define BRAINO_LINUX_INSTALL_ROCKNIX_DTS
	for d in $(BR2_EXTERNAL_BRAINO_PATH)/rocknix/dts/*.dts; do \
		b=$$(basename $$d .dts); \
		cp -f $$d $(LINUX_DIR)/arch/arm64/boot/dts/allwinner/; \
		grep -q "$$b.dtb" $(LINUX_DIR)/arch/arm64/boot/dts/allwinner/Makefile || \
			echo 'dtb-$$(CONFIG_ARCH_SUNXI) += '"$$b"'.dtb' >> $(LINUX_DIR)/arch/arm64/boot/dts/allwinner/Makefile; \
	done
endef
LINUX_POST_PATCH_HOOKS += BRAINO_LINUX_INSTALL_ROCKNIX_DTS

# SDL's ALSA backend starts a thread that wakes ten times a second, for ever,
# to notice a sound card being plugged in. This console's one card is soldered
# on, and the thread was a sixth of everything that woke a sleeping console.
# SDL still lists the cards once, at start.
define BRAINO_SDL2_NO_ALSA_HOTPLUG_THREAD
	$(SED) 's/^#define SDL_ALSA_HOTPLUG_THREAD 1$$/#define SDL_ALSA_HOTPLUG_THREAD 0/' \
		$(@D)/src/audio/alsa/SDL_alsa_audio.c
	grep -q '^#define SDL_ALSA_HOTPLUG_THREAD 0$$' $(@D)/src/audio/alsa/SDL_alsa_audio.c
endef
SDL2_POST_PATCH_HOOKS += BRAINO_SDL2_NO_ALSA_HOTPLUG_THREAD
