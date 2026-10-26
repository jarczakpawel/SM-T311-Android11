import gzip
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

SOURCE = Path(__file__).resolve().parent
PART_OFFSET = 2147483648
PART_BYTES = 88080384
SYSTEM_BYTES = 2235564032
EXPECTED = "d76f97fe99ab9d1e368b1abb54d513f74d72b45af3a27dfac60ce123b72779f5"

if len(sys.argv) != 2:
    sys.exit("usage: test_sparse_io64.py VERIFIED_G563_ARTIFACT_DIRECTORY")
artifact = Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix="g565_io64_sparse_", dir="/tmp") as folder:
    tmp = Path(folder)
    chunks = tmp / "staging"
    chunks.mkdir()
    part = chunks / "chunk4.raw"
    h = hashlib.sha256()
    count = 0
    with gzip.open(artifact / "chunk4.raw.gz", "rb") as z, part.open("wb") as out:
        for data in iter(lambda: z.read(1048576), b""):
            out.write(data)
            h.update(data)
            count += len(data)
    assert count == PART_BYTES and h.hexdigest() == EXPECTED

    disk = tmp / "fake_p20_regular_file.sparse"
    with disk.open("wb") as fd:
        fd.truncate(SYSTEM_BYTES)
    assert disk.stat().st_size == SYSTEM_BYTES
    source = (SOURCE / "copy.c").read_text()
    old_device = '#define BLOCK_DEVICE "/dev/block/mmcblk0p20"'
    old_chunks = '#define CHUNK_DIR "/data/local/tmp/SM-T311-G563-IO64-CHUNK"'
    assert source.count(old_device) == source.count(old_chunks) == 1
    source = source.replace(old_device, f'#define BLOCK_DEVICE "{disk}"')
    source = source.replace(old_chunks, f'#define CHUNK_DIR "{chunks}"')
    start = source.index("static void require_recovery(void) {")
    stop = source.index("static int64_t length_of_part(int part) {")
    mock = """
static void require_recovery(void) {
    const char *m = getenv("G565_SPARSE_TEST_ONLY");
    if (!m || strcmp(m, "HOST_TEST_NO_REAL_DEVICE")) fail("TEST_MARKER");
}

static int open_system(int write_access) {
    require_recovery();
    int fd = open(BLOCK_DEVICE, (write_access ? O_RDWR : O_RDONLY) |
                            O_CLOEXEC | O_LARGEFILE);
    if (fd < 0) fail("HOST_SPARSE_OPEN");
    struct stat64 st;
    if (fstat64(fd, &st) || !S_ISREG(st.st_mode) ||
            st.st_size != SYSTEM_SIZE) fail("HOST_SPARSE_GEOMETRY");
    return fd;
}

"""
    source = source[:start] + mock + source[stop:]
    (tmp / "copy-host-test.c").write_text(source)
    binary = tmp / "io64-test-only"
    build = [
        "gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-std=c11",
        "-D_FILE_OFFSET_BITS=64", "-D_LARGEFILE64_SOURCE",
        "-I", str(SOURCE), str(tmp / "copy-host-test.c"),
        str(SOURCE / "sha256_local.c"), "-o", str(binary)
    ]
    subprocess.run(build, check=True, capture_output=True, text=True)
    env = dict(os.environ, G565_SPARSE_TEST_ONLY="HOST_TEST_NO_REAL_DEVICE")

    def check(name, args, success, expected_text=None):
        proc = subprocess.run([str(binary), *args], env=env,
                              capture_output=True, text=True)
        passed = (proc.returncode == 0) == success
        output = proc.stdout + proc.stderr
        if expected_text is not None:
            passed = passed and expected_text in output
        if not passed:
            raise AssertionError(f"{name} rc={proc.returncode} {output[:400]}")
        print(f"G565_IO64_SPARSE_{name}=PASS", flush=True)

    check("PREFLIGHT", ["preflight"], True, "G563_IO64_PREFLIGHT_PASS")
    no_marker_env = dict(os.environ)
    no_marker_env.pop("G565_SPARSE_TEST_ONLY", None)
    no_marker = subprocess.run([str(binary), "preflight"], env=no_marker_env,
                               capture_output=True, text=True)
    assert no_marker.returncode != 0 and "TEST_MARKER" in no_marker.stderr
    print("G565_IO64_SPARSE_NO_HOST_TEST_MARKER_BLOCK=PASS", flush=True)
    with disk.open("r+b") as fd:
        fd.truncate(SYSTEM_BYTES - 1)
    check("WRONG_GEOMETRY_BLOCK", ["preflight"], False, "HOST_SPARSE_GEOMETRY")
    with disk.open("r+b") as fd:
        fd.truncate(SYSTEM_BYTES)
    saved = chunks / "chunk4.saved"
    part.rename(saved)
    part.symlink_to(saved)
    try:
        check("SYMLINK_SOURCE_BLOCK", ["copy", "4", EXPECTED,
                                    "G563_FLASH_SYSTEM_P20"], False, "SOURCE_OPEN")
    finally:
        part.unlink()
        saved.rename(part)
    check("BEFORE_WRITE_READBACK_BLOCK", ["verify-installed", "4", EXPECTED], False,
          "SHA_MISMATCH")
    check("NO_WRITE_TOKEN_BLOCK", ["copy", "4", EXPECTED], False,
          "EXPLICIT_FLASH_TOKEN")
    check("WRONG_WRITE_TOKEN_BLOCK", ["copy", "4", EXPECTED, "WRONG"], False,
          "EXPLICIT_FLASH_TOKEN")
    check("WRONG_SHA_BLOCK", ["copy", "4", "0" * 64, "G563_FLASH_SYSTEM_P20"],
          False, "SHA_MISMATCH")
    check("BAD_INDEX_BLOCK", ["copy", "5", EXPECTED, "G563_FLASH_SYSTEM_P20"],
          False, "PART_INDEX")
    with disk.open("rb") as fd:
        fd.seek(PART_OFFSET)
        assert fd.read(16) == bytes(16)
    check("COPY_FINAL_ABOVE_2G", ["copy", "4", EXPECTED, "G563_FLASH_SYSTEM_P20"],
          True, "G563_IO64_PART_WRITE_VERIFY_PASS index=4")
    check("RESTART_READBACK", ["verify-installed", "4", EXPECTED], True,
          "G564_IO64_INSTALLED_PART_VERIFIED index=4")
    with disk.open("rb") as fd, part.open("rb") as ref:
        fd.seek(PART_OFFSET)
        assert fd.read(4096) == ref.read(4096)
    with disk.open("r+b") as fd:
        fd.seek(PART_OFFSET + 4096)
        value = fd.read(1)
        fd.seek(PART_OFFSET + 4096)
        fd.write(bytes([value[0] ^ 1]))
    check("CORRUPTED_INSTALLED_BLOCK", ["verify-installed", "4", EXPECTED],
          False, "SHA_MISMATCH")
    check("CORRUPTED_COPY_REPAIRS", ["copy", "4", EXPECTED, "G563_FLASH_SYSTEM_P20"],
          True, "G563_IO64_PART_WRITE_VERIFY_PASS index=4")
    check("POST_REPAIR_READBACK", ["verify-installed", "4", EXPECTED], True,
          "G564_IO64_INSTALLED_PART_VERIFIED index=4")
    with disk.open("rb") as fd:
        assert fd.read(256) == bytes(256)
    assert disk.stat().st_size == SYSTEM_BYTES
    assert disk.stat().st_blocks * 512 < 200000000
    sha = hashlib.sha256()
    with disk.open("rb") as fd:
        for block in iter(lambda: fd.read(1048576), b""):
            sha.update(block)
    full_sha = sha.hexdigest()
    check("G569_FULL_SYSTEM_64BIT_HASH", ["verify-system", full_sha], True,
          f"G569_IO64_FULL_SYSTEM_VERIFIED sha256={full_sha}")
    print("G569_IO64_FINAL_READBACK_64BIT_SYSTEM_SHA=PASS", flush=True)
    print("G565_SPARSE_FINAL_OFFSET_2147483648_AND_GEOMETRY=PASS", flush=True)
    print("G565_NO_PHYSICAL_BLOCK_DEVICE_OR_ADB_ACCESSED=YES", flush=True)
