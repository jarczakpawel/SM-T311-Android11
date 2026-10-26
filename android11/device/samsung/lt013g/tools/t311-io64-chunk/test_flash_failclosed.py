import contextlib
import io
import runpy
import subprocess
import sys
from pathlib import Path
from types import SimpleNamespace

if len(sys.argv) != 2:
    sys.exit("usage: test_flash_failclosed.py G563_ARTIFACT_DIR")
artifact = Path(sys.argv[1]).resolve()
installer = Path(__file__).with_name("stage_and_flash.py")
original_run = subprocess.run


def exercise(case, resume, wrong_recovery=False, fail_existing=False, tamper_transfer=False):
    log = []

    def fake_adb(command, *args, **kwargs):
        if not command or command[0] != "adb" or command[1:3] != ["-s", "FAKE_TWRP"]:
            raise AssertionError(f"unexpected subprocess: {command!r}")
        argv = command[3:]
        log.append(argv)
        if argv == ["get-state"]:
            result = "device"
        elif argv == ["shell", "getprop ro.twrp.version"]:
            result = "NOT_TWRP" if wrong_recovery else "3.1.1-0"
        elif argv == ["shell", "id -u"]:
            result = "0"
        elif argv == ["shell", "blockdev --getsize64 /dev/block/mmcblk0p20"]:
            result = "2235564032"
        elif argv == ["shell", "sha256sum /dev/block/mmcblk0p9"]:
            result = "81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce  /dev/block/mmcblk0p9"
        elif argv == ["shell", "sha256sum /dev/block/mmcblk0p10"]:
            result = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f  /dev/block/mmcblk0p10"
        elif argv == ["shell", "df -k /data"]:
            result = "Filesystem 1K-blocks Used Available Use% Mounted on\n/dev/block/p21 12154824 10000000 2100000 82% /data"
        elif argv == ["shell", "/data/local/tmp/SM-T311-G563-IO64-CHUNK/t311_io64_chunk preflight"]:
            result = "G563_IO64_PREFLIGHT_PASS"
        elif argv[:1] == ["push"] and argv[-1].endswith("/chunk1.raw.gz"):
            result = "1 file pushed"
        elif argv[:1] == ["push"]:
            result = "1 file pushed"
        elif argv[:1] == ["shell"] and "verify-installed" in argv[1]:
            if fail_existing:
                return SimpleNamespace(returncode=7, stdout="", stderr="G563_IO64_FAIL SHA_MISMATCH")
            part = int(argv[1].split("verify-installed ")[1][0])
            result = f"G564_IO64_INSTALLED_PART_VERIFIED index={part}"
        elif argv[:1] == ["shell"] and "sha256sum /data/local/tmp" in argv[1]:
            if not tamper_transfer:
                raise AssertionError("expected mock transfer damage")
            result = "0" * 64 + "  chunk1.raw.gz"
        elif argv[:1] == ["shell"] and argv[1].startswith("mkdir -p "):
            result = ""
        elif argv[:1] == ["shell"] and argv[1].startswith("chmod 700 "):
            result = ""
        else:
            raise AssertionError(f"unexpected adb command reached: {argv!r}")
        return SimpleNamespace(returncode=0, stdout=result + "\n", stderr="")

    subprocess.run = fake_adb
    stdout = io.StringIO()
    stderr = io.StringIO()
    original_argv = sys.argv
    sys.argv = [
        str(installer),
        str(artifact),
        "--serial", "FAKE_TWRP",
        "--flash-token", "I_AUTHORIZE_G563_RAW_SYSTEM_P20",
        "--resume-from", str(resume),
    ]
    try:
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            try:
                runpy.run_path(str(installer), run_name="__main__")
            except (SystemExit, RuntimeError) as exc:
                assert str(exc), case
            else:
                raise AssertionError("unsafe fixture unexpectedly completed")
    finally:
        sys.argv = original_argv
        subprocess.run = original_run
    assert not any(" copy " in arg for command in log for arg in command), case
    assert not any("/dev/block/mmcblk0p20" in arg and "sha256sum" in arg
                   for command in log for arg in command), case
    output = stdout.getvalue() + stderr.getvalue()
    if wrong_recovery:
        assert "original TWRP3.1.1-0 required" in output or "getprop ro.twrp.version" in str(log)
        assert not any(command[0] == "push" for command in log)
    if fail_existing:
        assert any("verify-installed 0 " in str(command) for command in log)
        assert not any(command[0] == "push" and command[-1].endswith(".raw.gz") for command in log)
    if tamper_transfer:
        assert any("verify-installed 0 " in str(command) for command in log)
        assert any(command[0] == "push" and command[-1].endswith("chunk1.raw.gz") for command in log)
    print(f"G564_FAILCLOSED_PASS {case} fake_adb_calls={len(log)} no_system_write=YES", flush=True)


exercise("wrong_recovery", 0, wrong_recovery=True)
exercise("resume_previous_chunk_SHA_mismatch", 2, fail_existing=True)
exercise("resumed_transfer_corruption", 1, tamper_transfer=True)


def exercise_virtual_resume_success():
    import json
    manifest = json.loads((artifact / "manifest.json").read_text())
    calls = []

    def fake_adb(command, *args, **kwargs):
        if command[:3] != ["adb", "-s", "FAKE_TWRP"]:
            raise AssertionError("Unexpected real command")
        argv = command[3:]
        calls.append(argv)
        if argv == ["get-state"]:
            result = "device"
        elif argv == ["shell", "getprop ro.twrp.version"]:
            result = "3.1.1-0"
        elif argv == ["shell", "id -u"]:
            result = "0"
        elif argv == ["shell", "blockdev --getsize64 /dev/block/mmcblk0p20"]:
            result = "2235564032"
        elif argv == ["shell", "sha256sum /dev/block/mmcblk0p9"]:
            result = "81b01abb5612a9a777330705c02c4bc1f49386da4d5df0950f955246f9fa6bce  p9"
        elif argv == ["shell", "sha256sum /dev/block/mmcblk0p10"]:
            result = "f818c29f65d2082cb642cac85e27715c5255c87aac627bb542b777129d4f784f  p10"
        elif argv == ["shell", "df -k /data"]:
            result = "Filesystem 1K-blocks Used Available Use% Mounted on\n/dev/block/p21 12154824 10000000 2100000 82% /data"
        elif argv == ["shell", "/data/local/tmp/SM-T311-G563-IO64-CHUNK/t311_io64_chunk preflight"]:
            result = "G563_IO64_PREFLIGHT_PASS"
        elif argv[0] == "push":
            result = "1 file pushed"
        elif argv[0] == "shell":
            cmd = argv[1]
            if cmd.startswith("mkdir -p ") or cmd.startswith("chmod 700 "):
                result = ""
            elif "verify-installed " in cmd:
                part = int(cmd.split("verify-installed ")[1][0])
                result = f"G564_IO64_INSTALLED_PART_VERIFIED index={part}"
            elif cmd.startswith("sha256sum /data/local/tmp/"):
                part = int(cmd.split("chunk")[1][0])
                result = f"{manifest['parts'][part]['gzip_sha256']}  chunk{part}.raw.gz"
            elif cmd.startswith("gzip -dc "):
                result = ""
            elif " t311_io64_chunk copy " in cmd:
                raise AssertionError("unexpected native path formatting")
            elif "t311_io64_chunk copy " in cmd:
                part = int(cmd.split("copy ")[1][0])
                result = f"G563_IO64_PART_WRITE_VERIFY_PASS index={part}"
            elif cmd.startswith("rm -f "):
                result = ""
            elif cmd == "sync; sha256sum /dev/block/mmcblk0p20":
                result = "ea9ad056e22a912224dd24d09f986d1e0e514459297fce20bbd51eb16ff3fff8  p20"
            else:
                raise AssertionError("unknown mock shell: " + cmd)
        else:
            raise AssertionError(f"unknown mock adb: {argv!r}")
        return SimpleNamespace(returncode=0, stdout=result + "\n", stderr="")

    stdout = io.StringIO()
    stderr = io.StringIO()
    old_argv = sys.argv
    subprocess.run = fake_adb
    sys.argv = [
        str(installer), str(artifact), "--serial", "FAKE_TWRP",
        "--flash-token", "I_AUTHORIZE_G563_RAW_SYSTEM_P20",
        "--resume-from", "2",
    ]
    try:
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            runpy.run_path(str(installer), run_name="__main__")
    finally:
        subprocess.run = original_run
        sys.argv = old_argv
    command_strings = [cmd[1] for cmd in calls if cmd[0] == "shell"]
    prior = [x for x in command_strings if "verify-installed" in x][:2]
    assert [int(x.split("verify-installed ")[1][0]) for x in prior] == [0, 1]
    writes = [x for x in command_strings if "t311_io64_chunk copy " in x]
    assert [int(x.split("copy ")[1][0]) for x in writes] == [2, 3, 4], writes
    assert all("G563_FLASH_SYSTEM_P20" in x for x in writes)
    assert "G563_FULL_SYSTEM_SHA_PASS" in stdout.getvalue()
    assert not any("reboot" in x for x in command_strings)
    print("G564_MOCK_RESUME_SUCCESS_PASS verified_parts=0,1 virtual_writes=2,3,4 no_real_ADB_calls=YES")


exercise_virtual_resume_success()
