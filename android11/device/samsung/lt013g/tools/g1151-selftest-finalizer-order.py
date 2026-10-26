#!/usr/bin/env python3
"""G1151 static CI smoke test for SM-T311 finalizer.

This test does not build, flash, back up, launch USB or touch SIM. It
validates the only allowed sequence after a genuine native build success.
"""
from pathlib import Path
import subprocess
import hashlib
import fcntl
from tempfile import TemporaryDirectory

TOOLS = Path(__file__).resolve().parent
DEVICE = TOOLS.parent
ANDROID = DEVICE.parents[2]
FINALIZER = TOOLS / "g1148-finish-source-canonical-target-files.sh"
RESUME = TOOLS / "g1150-resume-native-after-apex-mke2fs.sh"
CONF = TOOLS / "g1150-android11-mke2fs-2019-compat.conf"
BOARD = DEVICE / "BoardConfig.mk"

def refuse(msg):
    raise SystemExit("G1151_SOURCE_FINALIZER_ORDER_REFUSE: " + msg)

def single_pos(script, marker):
    n = script.count(marker)
    if n != 1:
        refuse("expected once (" + str(n) + "): " + marker)
    return script.index(marker)

def check():
    for path in (FINALIZER, RESUME):
        subprocess.run(["bash", "-n", str(path)], check=True)
    for path, arg, marker in (
        (FINALIZER, None, "G1148_REFUSE"),
        (RESUME, None, "G1150_REFUSE"),
    ):
        x = subprocess.run(["bash", str(path)], cwd=ANDROID,
                           text=True, stdout=subprocess.PIPE,
                           stderr=subprocess.PIPE, check=False, timeout=20)
        if x.returncode != 20 or marker not in (x.stdout + x.stderr):
            refuse("unacknowledged script unexpectedly passes: " + path.name)
    source = FINALIZER.read_text()
    ordered = (
        'if [[ "${1:-}" != "--native-build-confirmed-stopped" ]]',
        'python3 "$QA/g1159-verify-native-job-terminal-receipt.py"',
        'python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --staging-gate',
        'python3 "$CLEAN"  # default DRY RUN',
        'export MKE2FS_CONFIG=',
        'python3 "$CLEAN" --apply --native-build-finished',
        'rm -- "$IMAGE"',
        'export BUILD_NUMBER=SMT311G1148',
        'm -j4 systemimage',
        'python3 "$QA/g1139-verify-android-system-ext4-image.py"',
        'TARGETFILES_BUILD_START=$(date +%s)',
        'm -j4 target-files-package',
        'python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --release-gate',
        'python3 "$QA/g1143-verify-target-files-zip-actual-artifacts.py" "$ZIP"',
        '--all-crc --receipt "$QA_RECEIPT"',
        'python3 "$ROOT/device/samsung/lt013g/tools/g1152-write-qualified-release-manifest.py"',
        '--target-files "$ZIP" --qa-receipt "$QA_RECEIPT"',
        '--output "$RELEASE_MANIFEST"',
    )
    indexes = [single_pos(source, item) for item in ordered if
               item not in (
                   'python3 "$QA/g1139-verify-android-system-ext4-image.py"',
               )]
    # G1163: a successful main-job terminal receipt is reusable, so it
    # cannot by itself prevent two overlapping finalizer invocations.
    # Require a cross-process flock fd held throughout entire G1148 job.
    for token in ('exec 9>"$LOCK"', 'if ! flock -n 9;',
                  'PASS G1163_EXCLUSIVE_ROM_FINALIZER_LOCK_HELD'):
        if source.count(token) != 1:
            refuse("G1163 mandatory cross-process finalizer lock missing")
    if not (source.index('if [[ "${1:-}" != "--native-build-confirmed-stopped" ]]')
            < source.index('if ! flock -n 9;')
            < source.index('python3 "$QA/g1159-verify-native-job-terminal-receipt.py"')
            < source.index('python3 "$CLEAN" --apply --native-build-finished')):
        refuse("G1163 finalizer lock must be acquired before any release mutation")
    with TemporaryDirectory(prefix="g1163-lock-test-") as td:
        lockpath=Path(td)/"g1163-test.lock"
        with lockpath.open("w") as fd:
            fcntl.flock(fd.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            denied=subprocess.run(["flock","-n",str(lockpath),"true"],check=False)
            if denied.returncode == 0:
                refuse("G1163 second process bypassed held exclusive flock")
            fcntl.flock(fd.fileno(), fcntl.LOCK_UN)
        allowed=subprocess.run(["flock","-n",str(lockpath),"true"],check=False)
        if allowed.returncode != 0:
            refuse("G1163 lock not released after holder exits")
    print("PASS G1163_REAL_FLOCK_DENIES_SECOND_INVOCATION_THEN_ALLOWS_EXIT")
    # G1159: PS from workspace_run is process-isolated from native build
    # job6974d254eda6. Requiring a receipt MUST precede even the first
    # cleanup, staged source probes, or other release mutations.
    receipt_gate = 'python3 "$QA/g1159-verify-native-job-terminal-receipt.py"'
    if not (source.index('if [[ "${1:-}" != "--native-build-confirmed-stopped" ]]')
            < source.index(receipt_gate)
            < source.index('python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --staging-gate')
            < source.index('python3 "$CLEAN" --apply --native-build-finished')):
        refuse("G1159 native job terminal receipt must be checked before release actions")
    terminal_ci = Path("/home/debian/Projekty/samsung/linux-7.3-port/tools/g1159-selftest-native-build-terminal-receipt.py")
    subprocess.run(["python3", str(terminal_ci)], check=True, timeout=20)
    print("PASS G1159_TERMINAL_JOB_RECEIPT_FAIL_CLOSED_SOURCE_GATE")
    # G1161: do not remove any generated app/build.prop/system.img
    # until the entire preflight confirms old image/config/prop symlinks.
    mutation='python3 "$CLEAN" --apply --native-build-finished'
    require=(
        'if [[ -L "$IMAGE" || ! -f "$IMAGE" ]]',
        '[[ -f "$MKE2FS_CONFIG" && ! -L "$MKE2FS_CONFIG" ]]',
        'for file in "${PROP_OUTPUTS[@]}"; do',
        'PASS G1161_ALL_SOURCE_ARTIFACT_PRECONDITIONS_BEFORE_OUTPUT_MUTATION',
    )
    for marker in require:
        if marker not in source or source.index(marker) > source.index(mutation):
            refuse("G1161 prerequisite occurs after deletion: " + marker)
    if not (source.index(mutation) < source.index('rm -- "$file"')
            < source.index('rm -- "$IMAGE"')):
        refuse("G1161 generated-output mutation order changed")
    print("PASS G1161_NO_OUTPUT_PRUNE_BEFORE_ALL_EXISTENCE_AND_SYMLINK_CHECKS")
    # G1156: FIXED circular source-policy gate. A stale build product
    # must not block its own rebuild (source-only before any mutation),
    # but the actual new G1153 vendor CIL and precompiled policy MUST be
    # required after m systemimage and BEFORE read-only ext4 inspection.
    g1146 = 'python3 "$QA/g1146-verify-mfc-policy-source-pruning.py"'
    source_preflight = g1146 + ' --source-only'
    if source.count(source_preflight) != 1 or source.count(g1146) != 2:
        refuse("G1156 source-only preflight plus strict post-build gate required")
    policy_post = source.rindex(g1146)
    if not (source.index('python3 "$QA/g1137-verify-g1130-kernel-android-publication-gate.py" --staging-gate')
            < source.index(source_preflight)
            < source.index('python3 "$CLEAN"  # default DRY RUN')
            < source.index('m -j4 systemimage')
            < policy_post
            < source.index('python3 "$QA/g1139-verify-android-system-ext4-image.py"')):
        refuse("G1156 policy source and strict compiled gate out of order")
    real_qa = Path("/home/debian/Projekty/samsung/linux-7.3-port/tools/g1146-verify-mfc-policy-source-pruning.py").read_text()
    if 'if source_only:' not in real_qa or 'if not VENDOR_CIL.is_file()' not in real_qa:
        refuse("G1156 binary vendor-CIL gate missing or source-only cannot be verified")
    # G1157: execution-level negative tests, not just textual markers.
    # They use disposable synthetic .te files only: verify that a stale
    # or absent generated system_ext CIL CANNOT deadlock source preflight,
    # while strict postbuild and missing source grant are still refused.
    g1157 = Path("/home/debian/Projekty/samsung/linux-7.3-port/tools/g1157-selftest-selinux-source-vs-compiled.py")
    subprocess.run(["python3", str(g1157)], check=True, timeout=20)
    print("PASS G1157_CAMERA_POLICY_SOURCE_ONLY_INDEPENDENT_FROM_STAGED_CIL")
    # G1158: G1137 itself previously demanded compiled Camera2 CIL even
    # in --staging-gate, circumventing G1146's G1157 source-only fix.
    # This synthetic test uses an object which raises on any staged read.
    g1158 = Path("/home/debian/Projekty/samsung/linux-7.3-port/tools/g1158-selftest-g1137-camera-prebuild-stage.py")
    subprocess.run(["python3", str(g1158)], check=True, timeout=20)
    print("PASS G1158_G1137_STAGING_CAMERA_CIL_READ_IS_IMPOSSIBLE")
    print("PASS G1156_SOURCE_FIRST_BINARY_AFTER_SYSTEMIMAGE_POLICY_RELEASE_GATE")
    # The ext4 integrity verifier must occur between completed SYSTEM build
    # and target-files packaging. A second invocation after packaging is OK.
    img_gate = 'python3 "$QA/g1139-verify-android-system-ext4-image.py"'
    if source.count(img_gate) != 2:
        refuse("must qualify actual image before AND after package")
    before, after = [i for i in range(len(source))
                     if source.startswith(img_gate, i)]
    if not (source.index('m -j4 systemimage') < before <
            source.index('m -j4 target-files-package') < after):
        refuse("image validation must bracket target-files packaging")
    if indexes != sorted(indexes):
        refuse("required release stages are out of order")
    if "MKE2FS_CONFIG" not in (BOARD.read_text()) or (
            "BUILD_BROKEN_NINJA_USES_ENV_VARS" not in BOARD.read_text()):
        refuse("Ninja does not whitelist MKE2FS_CONFIG")
    ext4_conf = CONF.read_text()
    if "orphan_file" in [
            c.strip() for line in ext4_conf.splitlines()
            if line.strip().startswith("features =")
            for c in line.partition("=")[2].split(",")]:
        refuse("AOSP mke2fs incompatible orphan_file is enabled")
    if "MKE2FS_CONFIG" not in RESUME.read_text():
        refuse("native resume wrapper lacks compat config")
    if source.count("lineage_lt013g-target_files-*.zip") != 1:
        refuse("fresh-release archive name discovery missing")
    # G1155: new USB init rc source removes an SELinux-denied /proc printk
    # write. AOSP job may still have a stale generated rc before final
    # rebuild, so assert real sparse ext4 and target-files gates pin ONLY
    # the corrected SOURCE SHA, never accepted old physical staging.
    canonical = ANDROID / "device/samsung/lt01-common/known-good-v20260730/init.lt013g.usb-v110.rc"
    required_usb_sha = "bf30e5424eb9dc4e69edb2d5eae9e2e5547f99ab0a6d25eecaa28e4bfdc71633"
    if hashlib.sha256(canonical.read_bytes()).hexdigest() != required_usb_sha:
        refuse("new source USB init unexpectedly drifted")
    if any(line.strip().startswith("write /proc/sys/kernel/printk")
           for line in canonical.read_text().splitlines()):
        refuse("deprecated vendor init kernel printk write reappeared")
    for path in (
        Path("/home/debian/Projekty/samsung/linux-7.3-port/tools/g1139-verify-android-system-ext4-image.py"),
        Path("/home/debian/Projekty/samsung/linux-7.3-port/tools/g1143-verify-target-files-zip-actual-artifacts.py"),
    ):
        src = path.read_text()
        if src.count(required_usb_sha) != 1 or "init.lt013g.usb-v110.rc" not in src:
            refuse("new USB init SHA release image/ZIP guard missing: " + str(path))
    print("PASS G1155_CORRECTED_USB_INIT_IMMUTABLE_SHA_REQUIRED_IN_EXT4_AND_ZIP")
    print("PASS G1151_FINALIZER_SOURCE_ORDER_PREIMAGE_POSTIMAGE_REAL_ZIP")
    print("PASS G1151_ANDROID11_MKE2FS_COMPAT_NINJA_WHITELIST")
    print("PASS G1151_NO_ACK_WRAPPERS_REFUSE_RC20_WITHOUT_MUTATION")
    print("NOTE: no actual SYSTEM/ZIP built or flashed by this test")

if __name__ == "__main__":
    check()
