#!/system/bin/sh
# G1153: READ ONLY H264 Codec2 C2 FMQ playback source-policy receipt.
# Never create media, enable audio, change SIM PIN, mount a partition or flash.
set -eu
fail() { echo "G1153_RUNTIME_FAIL_$1"; exit 7; }
sha() { sha256sum "$1" | awk '{print $1}'; }
[ "$(getprop ro.product.device)" = lt013g ] || fail_DEVICE
[ "$(getprop sys.boot_completed)" = 1 ] || fail_BOOT
[ "$(getenforce)" = Enforcing ] || fail_SELINUX
[ "$(uname -r)" = 7.3.0-pjarczak ] || fail_KERNEL
[ "$(sha /vendor/etc/selinux/precompiled_sepolicy)" = c56ffc6db7fc2fed5db8e1778ea0d8d7ae45f8214769e518c27e657d7a0a7bb9 ] || fail_G1153_COMPILED_POLICY
[ "$(sha /system_ext/etc/selinux/system_ext_sepolicy.cil)" = 5882b053b412b9207dbf87f1c7541e8d96630fe98ff89ef9e9d8f2921661fb1b ] || fail_CAMERA_PROPERTY_POLICY
[ "$(sha /vendor/etc/selinux/precompiled_sepolicy.system_ext_sepolicy_and_mapping.sha256)" = 19fa6e2522db7a2afdfafa656660f2a70411c69a1e56e86525b514226223248f ] || fail_MAPPING
[ "$(sha /vendor/lib/libril.so)" = 4e6bb8a9465005a01f5bebd92a7e0ad247c0c4b609809c2600547bc97999bcb7 ] || fail_RIL
[ "$(sha /vendor/lib/libv4l2_codec2_components.so)" = f735ad9ed95bff53eb0f928476b61b27f9875e23465f3f196bfe5d24877dd7c9 ] || fail_CODEC2
[ "$(getprop init.svc.ril-daemon)" = running ] || fail_RIL_SERVICE
[ "$(getprop init.svc.android-hardware-media-c2-v4l2-hal-1-0)" = running ] || fail_CODEC2_SERVICE
[ "$(settings get system sound_effects_enabled)" = 0 ] || fail_SOUND_EFFECT
[ "$(settings get system lockscreen_sounds_enabled)" = 0 ] || fail_LOCK_SOUND
echo G1153_PHYSICAL_H264_FMQ_POLICY_AND_BOOT_RIL_CODEC2_PASS
echo G1153_POLICY_SHA=c56ffc6db7fc2fed5db8e1778ea0d8d7ae45f8214769e518c27e657d7a0a7bb9
echo G1153_ONLY_STATIC_POLICY_AND_RUNTIME_SERVICE_QA_NO_MOVIE_PLAYBACK_CLAIM
