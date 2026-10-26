#!/usr/bin/env python3
"""Non-destructive host execution test of G329 signed TWRP check script.

Rewrites ALL absolute recovery paths into a temporary directory, substitutes
synthetic G301 contents for known old hashes, and executes the *real* script
extracted from the final ZIP. Never invokes adb or real /dev/block devices.
"""
import hashlib
import subprocess
import tempfile
import zipfile
from pathlib import Path

ART = Path("/home/debian/Projekty/samsung/artifacts/SM-T311-G329-ATOMIC-ENFORCING-OTA-20261016")
ZIP = ART / "SM-T311-G329-G301-TO-G329-BOOT-GNSS-ENFORCING-TWRP-SIGNED.zip"
BOOT_HASH = "9487424414439edf4dd00bf4169bcd684572cc4abe6690af9589632205666436"
PART_SIZE = 8 * 1024 * 1024


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sh(script, mode):
    return subprocess.run(["/bin/sh", str(script), mode], capture_output=True, text=True)


def main():
    with zipfile.ZipFile(ZIP) as z:
        manifest = __import__("json").loads((ART / "G329-COMBINED-MANIFEST.json").read_text())
        rows = manifest["rows"]
        original = z.read("META-INF/t311-g329-check.sh").decode()
        updater=z.read("META-INF/com/google/android/updater-script").decode()
        labels=manifest["physical_selinux_labels_per_file"]
        # Recovery policy is older than the Android 11 GPS exec type.
        # Existing inode labels must be preserved, not relabeled in TWRP.
        assert manifest["set_metadata_explicit_selabel"] is False
        assert manifest["preserve_existing_security_selinux_via_O_TRUNC"] is True
        assert len(labels) == 6
        for row in rows:
            assert 'set_metadata("'+row["path"]+'"' in updater
        assert updater.count('"selabel"') == 0
        newboot = z.read("payload/BOOT-G329.img")
        newsystem = [z.read("payload/" + row["zip"]) for row in rows]
    assert sha(newboot) == BOOT_HASH and len(rows) == 6
    assert Path("/usr/bin/busybox").exists()
    with tempfile.TemporaryDirectory(prefix="g329-dryrun-", dir=ART) as tmp:
        work = Path(tmp)
        boot = work / "fake-boot-partition"
        link = work / "fake-boot-alias"
        payload = work / "fake-signed-boot"
        payload.write_bytes(newboot)
        link.symlink_to(boot)
        synthetic_old_boot = bytes(PART_SIZE)
        boot.write_bytes(synthetic_old_boot)
        oldboot_hash = sha(synthetic_old_boot)
        fake_old = []
        fixtures = []
        script = original.replace("bb=/sbin/busybox", "bb=/usr/bin/busybox")
        script = script.replace('boot="/dev/block/mmcblk0p9"', 'boot="' + str(boot) + '"')
        script = script.replace('payload="/tmp/g329-boot.img"', 'payload="' + str(payload) + '"')
        script = script.replace('[ -b "$boot" ]', '[ -f "$boot" ]')
        script = script.replace(
            "c0370d97d94c2dcedd5f5405ccda2dcf63afa4e2d30f9c07e1be577591e693ed",
            oldboot_hash)
        alias_start = script.index('if [ -L /dev/block/platform/dw_mmc/by-name/BOOT ]; then')
        alias_end = script.index('[ "$(hash "$payload")"', alias_start)
        script = (script[:alias_start]
            + 'alias="' + str(link) + '"\n'
            + '[ -L "$alias" ] || fail "no synthetic BOOT alias" 62\n'
            + '[ "$($bb readlink -f "$alias")" = "$boot" ] || fail "wrong synthetic BOOT mapping" 63\n'
            + script[alias_end:])
        for i, row in enumerate(rows):
            fake = work / ("system_file_" + str(i))
            olddata = ("ORIGINAL-G301-" + row["zip"]).encode()
            fake.write_bytes(olddata)
            synthetic_sha = sha(olddata)
            assert row["old"] != synthetic_sha
            original_check = 'check "' + row["path"] + '" "' + row["old"] + '"'
            synthetic_check = 'check "' + str(fake) + '" "' + synthetic_sha + '"'
            assert original_check in script
            script = script.replace(original_check, synthetic_check)
            # The new flash_boot branch checks SYSTEM independently.
            script = script.replace(row["path"], str(fake))
            fixtures.append(fake)
            fake_old.append(olddata)
        # Refuse to execute if any production block/device or system destination
        # can still be referenced by the generated dry-run code.
        runnable = "\n".join(line for line in script.splitlines()
                              if not line.lstrip().startswith("#"))
        assert "/dev/block/" not in runnable and "/system/" not in runnable
        assert "/sbin/busybox" not in runnable and "/tmp/g329-boot.img" not in runnable
        check = work / "check.sh"
        check.write_text(script)
        syntax = subprocess.run(["/bin/sh", "-n", str(check)], capture_output=True, text=True)
        assert syntax.returncode == 0, syntax.stderr
        def expect(mode, ok):
            result = sh(check, mode)
            assert (result.returncode == 0) == ok, (
                mode, "expected_pass" if ok else "expected_fail",
                result.returncode, result.stdout, result.stderr)
        # Exact G301 expected state -> can start without touching BOOT.
        expect("pre", True)
        # Hardened script must refuse BOOT writes until all six SYSTEM
        # payloads have been verified as installed.
        expect("flash_boot", False)
        assert sha(boot.read_bytes()) == oldboot_hash

        # Unexpected SYSTEM file -> fail BEFORE any writes.
        fixtures[1].write_bytes(b"MISMATCH")
        expect("pre", False)
        assert sha(boot.read_bytes()) == oldboot_hash
        fixtures[1].write_bytes(fake_old[1])

        # Unexpected BOOT -> fail BEFORE writing system.
        boot.write_bytes(b"x" * PART_SIZE)
        expect("pre", False)
        boot.write_bytes(synthetic_old_boot)

        # Signed payload corruption -> fail BEFORE writing BOOT.
        payload.write_bytes(b"damaged")
        expect("pre", False)
        payload.write_bytes(newboot)

        # Restore G301 and perform updater-script's SYSTEM-then-BOOT sequence.
        expect("pre", True)
        for fake, new in zip(fixtures, newsystem):
            fake.write_bytes(new)
        expect("post_system", True)
        expect("flash_boot", True)
        assert sha(boot.read_bytes()[:len(newboot)]) == sha(newboot)
        expect("post", True)

        # Repeated install is idempotent when already at expected hashes.
        expect("pre", True)
        expect("post_system", True)
        expect("flash_boot", True)
        expect("post", True)

        # Detect damaged installed system without pretending success.
        fixtures[4].write_bytes(b"DAMAGED TARGET")
        expect("post", False)
        fixtures[4].write_bytes(newsystem[4])

        # Detect damaged flashed BOOT.
        boot.write_bytes(b"DESTROYED")
        expect("post", False)
    print("PASS G329 recovery-script isolated shell: old state, corrupt SYSTEM, "
          "unknown BOOT, corrupt signed payload, premature BOOT blocked, SYSTEM->BOOT install, "
          "idempotent retry, corrupt installed state")
    print("No real partition mounted, flashed or modified by test.")


if __name__ == "__main__":
    main()
