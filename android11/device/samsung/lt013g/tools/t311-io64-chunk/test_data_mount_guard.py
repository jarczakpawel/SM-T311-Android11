"""Host-only check of production G570 mount/device/free-space gate.

A temporary derivative substitutes the physical p21 stat and recovery-presence
check, but reuses the actual preflight_data() C comparison and statvfs code.
There are no ADB commands or real /dev/block accesses.
"""
import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
source = (ROOT / "copy.c").read_text()
with tempfile.TemporaryDirectory(prefix="g570_data_mount_test_") as folder:
    t = Path(folder)
    fake_data = t / "data"
    fake_data.mkdir()
    start = source.index("static void require_recovery(void) {")
    end = source.index("static void check_unmounted(dev_t device) {")
    source = source[:start] + r'''
static void require_recovery(void) {
    if (!getenv("G570_HOST_TEST_ONLY")) fail("TEST_MARKER");
}
''' + source[end:]
    assert source.count('stat("/data", &mountpoint)') == 1
    source = source.replace('stat("/data", &mountpoint)',
                            f'stat("{fake_data}", &mountpoint)', 1)
    assert source.count('stat("/dev/block/mmcblk0p21", &physical)') == 1
    source = source.replace('stat("/dev/block/mmcblk0p21", &physical)',
                            'mock_physical_block(&physical)', 1)
    assert source.count('statvfs("/data", &vfs)') == 1
    source = source.replace('statvfs("/data", &vfs)',
                            f'mock_vfs("{fake_data}", &vfs)', 1)
    inject = r'''
static int mock_physical_block(struct stat *out) {
    struct stat directory;
    if (stat("G570_TEST_DATA_DIR", &directory)) return -1;
    memset(out, 0, sizeof(*out));
    out->st_mode = getenv("G570_BAD_BLOCK") ? S_IFREG : S_IFBLK;
    out->st_rdev = directory.st_dev;
    if (getenv("G570_BAD_PARTITION")) out->st_rdev += 1;
    return 0;
}
static int mock_vfs(const char *path, struct statvfs *out) {
    int result = statvfs(path, out);
    if (result == 0 && getenv("G570_READ_ONLY")) out->f_flag |= ST_RDONLY;
    return result;
}
'''.replace("G570_TEST_DATA_DIR", str(fake_data))
    i = source.index("static void preflight_data(void) {")
    source = source[:i] + inject + source[i:]
    host = t / "copy-host-guard.c"
    host.write_text(source)
    binary = t / "preflight-data-host"
    subprocess.run([
        "gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-std=gnu11",
        "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE64_SOURCE",
        "-I", str(ROOT), str(host), str(ROOT / "sha256_local.c"),
        "-o", str(binary)], check=True, capture_output=True, text=True)
    base_env = dict(os.environ, G570_HOST_TEST_ONLY="1")
    for name, extra, permitted, keyword in (
        ("mounted_writable_user_data", {}, True, "G570_DATA_MOUNT_VERIFIED free_bytes="),
        ("unmounted_or_wrong_partition", {"G570_BAD_PARTITION": "1"}, False, "DATA_NOT_MOUNTED_FROM_P21"),
        ("not_a_block_device", {"G570_BAD_BLOCK": "1"}, False, "DATA_BLOCK_DEVICE"),
        ("mounted_read_only", {"G570_READ_ONLY": "1"}, False, "DATA_READ_ONLY"),
    ):
        result = subprocess.run([str(binary), "preflight-data"],
                                env=dict(base_env, **extra),
                                capture_output=True, text=True)
        assert (result.returncode == 0) == permitted, (name, result)
        assert keyword in result.stdout + result.stderr, (name, result)
        print(f"G570_DATA_MOUNT_{name}=PASS", flush=True)
    result = subprocess.run([str(binary), "preflight-data"],
                            env=os.environ, capture_output=True, text=True)
    assert result.returncode != 0 and "TEST_MARKER" in result.stderr
    print("G570_DATA_MOUNT_RECOVERY_MARKER_REQUIRED=PASS", flush=True)
    print("G570_DATA_MOUNT_NO_PHYSICAL_BLOCK_OR_ADB_USED=YES", flush=True)
