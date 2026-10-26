T311_PREBOOT_ABI_CHECK := /home/debian/Projekty/samsung/linux-7.3-port/tools/verify-android11-release-abi.py

T311_RECOVERY_PREBUILT := device/samsung/lt013g/prebuilt/recovery-current.img
# G1139: the DEFAULT is the EXACT physically accepted G1130 BOOT.
# Earlier G958/G730/G716 are explicit historical opt-ins only.
# Rebuilding an image NEVER authorizes installing or flashing.
T311_BOOT_VARIANT ?= g1130-verified
ifeq ($(T311_BOOT_VARIANT),g1130-verified)
T311_RELEASE_BOOT := device/samsung/lt013g/prebuilt/BOOT-7.3.0-pjarczak-G1130-VERIFIED-8MiB.img
T311_RELEASE_BOOT_SHA := acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
T311_BOOT_ABI_CHECK := device/samsung/lt013g/tools/g1139-verify-pinned-g1130-release.py
else ifeq ($(T311_BOOT_VARIANT),g958-current)
T311_RELEASE_BOOT := device/samsung/lt013g/prebuilt/private-active-g958/boot.img
T311_RELEASE_BOOT_SHA := e42c1b09b4c4d3c91b1a3c5330619d37560287821994d269ba67a970069cd5fd
T311_BOOT_ABI_CHECK := device/samsung/lt013g/tools/g960-source-integrity.py
else ifeq ($(T311_BOOT_VARIANT),g716)
T311_RELEASE_BOOT := device/samsung/lt013g/prebuilt/BOOT-7.3.0-pjarczak-G716-CLEAN-HOST-ONLY-8MiB.img
T311_RELEASE_BOOT_SHA := 5d275fb7977d95e9e3515ff571efe675359e0874e1f8c5aef4b9c925d19182c1
T311_BOOT_ABI_CHECK := $(T311_PREBOOT_ABI_CHECK)
else ifeq ($(T311_BOOT_VARIANT),g730-current)
T311_RELEASE_BOOT := device/samsung/lt013g/prebuilt/BOOT-7.3.0-pjarczak-G730-OTG-EVAL-8MiB.img
T311_RELEASE_BOOT_SHA := e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b
T311_BOOT_ABI_CHECK := device/samsung/lt013g/tools/g766-verify-active-boot-release.py
else ifeq ($(T311_BOOT_VARIANT),g731-otg-eval)
T311_RELEASE_BOOT := device/samsung/lt013g/prebuilt/BOOT-7.3.0-pjarczak-G730-OTG-EVAL-8MiB.img
T311_RELEASE_BOOT_SHA := e53ad50d43e8aa92beccf537b5d2c9cc955f91d8fe2ff64d76a7f174b311177b
T311_BOOT_ABI_CHECK := device/samsung/lt013g/tools/g732-verify-otg-eval-build.py
else
$(error SM-T311: unsupported T311_BOOT_VARIANT '$(T311_BOOT_VARIANT)')
endif

$(INSTALLED_BOOTIMAGE_TARGET): $(T311_RELEASE_BOOT)
	$(hide) python3 $(T311_BOOT_ABI_CHECK) --source --boot
	$(call pretty,"SM-T311 $(T311_BOOT_VARIANT) pinned BOOT (INSTALL NOT AUTHORIZED): $@")
	$(hide) test "$$(stat -c %s $(T311_RELEASE_BOOT))" = "8388608"
	$(hide) test "$$(sha256sum $(T311_RELEASE_BOOT) | cut -d' ' -f1)" = "$(T311_RELEASE_BOOT_SHA)"
	$(hide) cp $(T311_RELEASE_BOOT) $@
	$(hide) test "$$(sha256sum $@ | cut -d' ' -f1)" = "$(T311_RELEASE_BOOT_SHA)"
	$(hide) $(call assert-max-image-size,$@,$(BOARD_BOOTIMAGE_PARTITION_SIZE))

.PHONY: bootimage-nodeps
bootimage-nodeps: $(T311_RELEASE_BOOT)
	$(hide) python3 $(T311_BOOT_ABI_CHECK) --source --boot
	@echo "make $@: $(T311_BOOT_VARIANT) pinned BOOT; NEVER treat as flash authorization"
	$(hide) test "$$(stat -c %s $(T311_RELEASE_BOOT))" = "8388608"
	$(hide) test "$$(sha256sum $(T311_RELEASE_BOOT) | cut -d' ' -f1)" = "$(T311_RELEASE_BOOT_SHA)"
	$(hide) cp $(T311_RELEASE_BOOT) $(INSTALLED_BOOTIMAGE_TARGET)
	$(hide) test "$$(sha256sum $(INSTALLED_BOOTIMAGE_TARGET) | cut -d' ' -f1)" = "$(T311_RELEASE_BOOT_SHA)"
	$(hide) $(call assert-max-image-size,$(INSTALLED_BOOTIMAGE_TARGET),$(BOARD_BOOTIMAGE_PARTITION_SIZE))

$(INSTALLED_RECOVERYIMAGE_TARGET): $(T311_RECOVERY_PREBUILT)
	@echo "Target existing recovery image: $@"
	$(hide) cp -f $(T311_RECOVERY_PREBUILT) $@
	$(hide) test "$$(stat -c %s $@)" -le "$(BOARD_RECOVERYIMAGE_PARTITION_SIZE)"
