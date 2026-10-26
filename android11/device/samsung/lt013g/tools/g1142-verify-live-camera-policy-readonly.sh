#!/system/bin/sh
# G1142 physical read-only QA; no audio, camera launch, recording, PIN or flash.
set -eu
check_sha() {
  actual="$(sha256sum "$1" | awk '{print $1}')"
  if [ "$actual" != "$2" ]; then
    echo "G1142_QA_FAIL_SHA $1 actual=$actual"
    exit 4
  fi
  echo "PASS $1 $actual"
}
[ "$(id -u)" = 0 ] || { echo G1142_QA_REFUSE_NEED_ROOT; exit 2; }
[ "$(getprop ro.product.device)" = lt013g ] || {
  echo G1142_QA_REFUSE_NOT_LT013G; exit 3;
}
[ "$(getprop sys.boot_completed)" = 1 ] || {
  echo G1142_QA_FAIL_BOOT_NOT_COMPLETED; exit 5;
}
[ "$(getenforce)" = Enforcing ] || {
  echo G1142_QA_FAIL_NOT_ENFORCING; exit 6;
}
check_sha /dev/block/mmcblk0p9 acc2a0579e0f4975da3b569941534f5b572ad9a7ae2bf4409fd88b5f633dd565
check_sha /dev/block/mmcblk0p10 f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f
check_sha /system_ext/etc/selinux/system_ext_sepolicy.cil 5882b053b412b9207dbf87f1c7541e8d96630fe98ff89ef9e9d8f2921661fb1b
check_sha /system_ext/etc/selinux/system_ext_sepolicy_and_mapping.sha256 19fa6e2522db7a2afdfafa656660f2a70411c69a1e56e86525b514226223248f
check_sha /vendor/etc/selinux/precompiled_sepolicy dbe01ff5e4d94a5da8c71681804da85592ed1d6274a936ef45249bee0cfe20a1
check_sha /vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256 19fa6e2522db7a2afdfafa656660f2a70411c69a1e56e86525b514226223248f
check_sha /system/etc/init/init.lt013g.udc-uevent.rc 32b5ae4bdab4fb21ead45b332620395dfdf36920d8b2293bbebf26c168116343
check_sha /vendor/etc/init/init.lt013g.debug.rc 1d25625666709b2fb5dc6d7a856c630f30f37635f727b06dba4d4ecd10f68781
check_sha /system/bin/screenrecord f9e3669acd0c9d75e4c0f209513c63d8c33867172a995f068a73f00261437634
check_sha /vendor/lib/libril.so 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7
check_sha /vendor/lib/libv4l2_codec2_components.so f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9
[ "$(getprop init.svc.ril-daemon)" = running ] || {
  echo G1142_QA_FAIL_RIL; exit 7;
}
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || {
  echo G1142_QA_FAIL_CODEC2; exit 8;
}
[ "$(getprop sys.usb.state)" = adb ] || {
  echo G1142_QA_FAIL_USB_MODE; exit 9;
}
[ "$(cat /sys/class/udc/12480000.usb/state)" = configured ] || {
  echo G1142_QA_FAIL_UDC; exit 10;
}
[ "$(settings get system sound_effects_enabled)" = 0 ] || {
  echo G1142_QA_FAIL_SYSTEM_SOUNDS; exit 11;
}
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || {
  echo G1142_QA_FAIL_LOCKSCREEN_SOUNDS; exit 12;
}
camera_avcs="$(dmesg | awk 'index($0,"avc: denied") && index($0,"exported_camera_prop") {c++} END {print c+0}')"
udc_avcs="$(dmesg | awk 'index($0,"avc: denied") && index($0,"name=\"uevent\"") {c++} END {print c+0}')"
echo "G1142_QA_OBSERVED_CAMERA_AVC_COUNT=$camera_avcs"
echo "G1142_QA_OBSERVED_UDC_AVC_COUNT=$udc_avcs"
# The absence of an AVC proves nothing about preview/recording unless a
# camera action actually took place; this intentionally triggers NONE.
echo G1142_PHYSICAL_READ_ONLY_POSTBOOT_QA_PASS
