# SPDX-License-Identifier: GPL-2.0-only
# Standalone SM-T311 ARMv7 user-space tests, not linked into kernel BOOT.
CROSS_COMPILE ?= arm-linux-gnueabihf-
CC := $(CROSS_COMPILE)gcc
CFLAGS := -march=armv7-a -mfpu=neon -mfloat-abi=hard -O2 -static -Wall -Wextra -Werror -pthread
TARGETS := memfd_hotread.arm32 ext4_rename_truncate_race.arm32 pread_cacheline_isolation.arm32 pread_hotpage.arm32

all: $(TARGETS)

%.arm32: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(TARGETS)

.PHONY: all clean

# G716: active release gate after COMPLETE removal of ref-free RCU port.
# G700/G713 experiment binary tools remain historical; do not rerun for G716.
.PHONY: check-clean-no-ref-port
check-clean-no-ref-port:
	python3 clean_no_ref_port_g716.py

# G714: inspect archived physical read-only G650 CPU/memory/ZRAM snapshot.
# Does not re-read the device, tune governors or rebuild BOOT.
.PHONY: check-passive-resource-g714
check-passive-resource-g714:
	python3 g714_passive_resource_gate.py
