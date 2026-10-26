#!/system/bin/sh
# G1154: SM-T311 read-only cross-subsystem release audit.
# Never: root, PIN/PUK, audio playback, GNSS mutation, camera start, write,
# flash, backup, reboot, wifi reconnect, USB OTG role switch.
set -u
PASS=0
FAIL=0
WARN=0
ok() { echo "PASS G1154_$1"; PASS=$((PASS+1)); }
bad() { echo "FAIL G1154_$1"; FAIL=$((FAIL+1)); }
note() { echo "NEEDS_TEST G1154_$1"; WARN=$((WARN+1)); }
eq() {
  desc="$1"; actual="$2"; desired="$3"
  if [ "$actual" = "$desired" ]; then ok "$desc"; else bad "$desc"; fi
}
sha() { sha256sum "$1" 2>/dev/null | awk '{print $1}'; }
sha_check() {
  title="$1"; f="$2"; expected="$3"
  if [ -r "$f" ] && [ "$(sha "$f")" = "$expected" ]; then
    ok "$title"
  else
    bad "$title"
  fi
}
eq ANDROID11 "$(getprop ro.build.version.release)" 11
eq BOOT_COMPLETED "$(getprop sys.boot_completed)" 1
eq KERNEL_G1130_7_3 "$(uname -r)" 7.3.0-pjarczak
eq SELINUX_ENFORCING "$(getenforce)" Enforcing
eq DEVICE_SM_T311 "$(getprop ro.product.device)" lt013g
eq RIL_SERVICE "$(getprop init.svc.ril-daemon)" running
eq XMM6262_SIM_LOADED "$(getprop gsm.sim.state)" LOADED
eq C2_HARDWARE_HAL_SERVICE "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" running
eq WIFI_DAEMON "$(getprop init.svc.wificond)" running
eq SOUND_EFFECTS_OFF "$(settings get system sound_effects_enabled)" 0
eq LOCKSCREEN_SOUNDS_OFF "$(settings get system lockscreen_sounds_enabled)" 0
sha_check G1153_DECODER_MEMFD_POLICY /vendor/etc/selinux/precompiled_sepolicy c56ffc6db7fc2fed5db8e1778ea0d8d7ae45f8214769e518c27e657d7a0a7bb9
sha_check G1142_CAMERA_PROPERTIES /system_ext/etc/selinux/system_ext_sepolicy.cil 5882b053b412b9207dbf87f1c7541e8d96630fe98ff89ef9e9d8f2921661fb1b
sha_check G1153_POLICY_MAPPING_MATCH /vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256 19fa6e2522db7a2afdfafa656660f2a70411c69a1e56e86525b514226223248f
sha_check RIL_XMM6262_BINARY /vendor/lib/libril.so 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
sha_check HARDWARE_CODEC2_MFC_LIBRARY /vendor/lib/libv4l2_codec2_components.so f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
sha_check BCM4752_GPS_LIBRARY /vendor/lib/hw/gps.default.so c85b7b9e28f6bc8ac35d94d9b56011efde66b7d555dab1e7f7e3a5d90e7215a8
sha_check AUDIO_PRIMARY_STOCK /vendor/lib/hw/audio.primary.lt013g.so 284657375ed169fa12778ceb4c78fd71c18310b8f6d42c8eddf429c61232a820
sha_check AUDIO_RIL_BRIDGE /vendor/bin/t311-audio-ril afcfb90944f53746c05e3bd033d5421782a0428eccf473b8060fe133603f8e65
sha_check MESA_EGL_MALI400 /vendor/lib/egl/libEGL_mesa.so 7e9945a06c53ff64e033b6e0bd03a54a714f403c3f3e3668247fce3f95863281
sha_check MESA_GLES2_MALI400 /vendor/lib/egl/libGLESv2_mesa.so f411ca31fe86217f673a5f07655bfde9e59cedcf44b801f24633176deed4b96d
sha_check G1154_MEDIA_CODECS_CONFIG /vendor/etc/media_codecs.xml d0bbf0a71f8e57f6afb7fce6d8ef7d7a1da334f99c26f16d8206d0eea9f1539e
sha_check G1154_HW_CODEC2_CONFIG /vendor/etc/media_codecs_c2.xml 35e893f9cc9ae0b1eecb0b814cd4bc51e43aaabeae2e135a3549efc55f0d8f72
sha_check G1154_SAMSUNG_VIDEO_CODECS_CONFIG /vendor/etc/media_codecs_samsung_video.xml 288ab152311c99a88aec191e388d1adcea76110bf4bea3ad2735b6fc19e57213
sha_check G1154_AUDIO_POLICY_CONFIG /vendor/etc/audio_policy_configuration.xml df5800f2fd959bbd4787f22dce29714c895e6e5a51a362087fce535ee687b682
sha_check INIT_VENDOR_ACCEPTED /vendor/etc/init/init.lt013g.debug.rc 1d25625666709b2fb5dc6d7a856c630f30f37635f727b06dba4d4ecd10f68781
sha_check SCREENRECORDER_VERIFIED /system/bin/screenrecord f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634
if grep -Eq '^SwapTotal:[[:space:]]+[1-9]' /proc/meminfo; then
  ok ZRAM_SWAP_TOTAL_NONZERO
else
  bad ZRAM_SWAP_NOT_ACTIVE
fi
if ls -d /sys/block/zram0 >/dev/null 2>&1; then ok ZRAM_DEVICE_PRESENT; else bad ZRAM_DEVICE_MISSING; fi
if dumpsys media.audio_flinger 2>/dev/null | grep -Fq 'Sample rate: 48000 Hz'; then
  ok AUDIO_48KHZ_MIXER
else
  note AUDIO_48KHZ_MIXER_NOT_VISIBLE
fi
if ps -A 2>/dev/null | grep -Fq 'lt013g-gps-bridge-host'; then ok GNSS_BRIDGE_PRESENT; else note GNSS_BRIDGE_STATUS_UNVERIFIED; fi
if ps -A 2>/dev/null | grep -Fq 'cameraserver'; then ok CAMERA_SERVER_PRESENT; else note CAMERA_SERVER_STATUS_UNVERIFIED; fi
if ps -A 2>/dev/null | grep -Fq 'audioserver'; then ok AUDIO_SERVER_PRESENT; else note AUDIO_SERVER_STATUS_UNVERIFIED; fi
if [ "$(getprop sys.usb.state)" = adb ]; then ok USB_ADB_CONNECTED; else note USB_STATE_NOT_ADB; fi
security="$(getprop ro.build.version.security_patch)"
if [ "$security" = 2024-02-05 ]; then
  note SECURITY_PATCH_2024_02_05_NOT_2026_QUALIFIED
else
  note SECURITY_PATCH_REQUIRES_CVE_AUDIT
fi
note CDA_HLS_EXACT_STREAM_FRAME_OUTPUT_NOT_TESTED_HERE
note CAMERA2_RECORD_AUDIO_ROTATION_LONG_RUN_NOT_TESTED_HERE
note GNSS_OUTDOOR_OTG_PENDRIVE_VOICE_SMS_USER_REPORTED_WORKING_NOT_PROBED_HERE
note MFC_LONG_H264_ENCODER_P128_UNRESOLVED
echo "G1154_READONLY_RESULTS PASS=$PASS FAIL=$FAIL NEEDS_TEST=$WARN"
if [ "$FAIL" -gt 0 ]; then exit 1; fi
echo G1154_BASELINE_SOURCE_AUDIT_PASS_NOT_COMPLETE_HARDWARE_CERTIFICATION
exit 0
