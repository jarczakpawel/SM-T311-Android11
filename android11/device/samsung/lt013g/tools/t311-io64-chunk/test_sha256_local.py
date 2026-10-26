import hashlib
import random
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parent
driver = """
#include "sha256_local.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    unsigned step = (unsigned)strtoul(argv[1], NULL, 10);
    unsigned char data[8192], digest[32];
    G563Sha256 sha;
    g563_sha256_init(&sha);
    for (;;) {
        size_t n = fread(data, 1, step, stdin);
        if (n) g563_sha256_update(&sha, data, n);
        if (n < step) break;
    }
    if (ferror(stdin)) return 3;
    g563_sha256_final(&sha, digest);
    for (int i=0; i<32; ++i) printf("%02x", digest[i]);
    puts("");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="g563_sha256_") as folder:
    path = Path(folder)
    (path / "test.c").write_text(driver)
    subprocess.run(["gcc", "-O2", "-Wall", "-Wextra", "-Werror", "-std=c99",
                    "-I", str(root), str(path / "test.c"),
                    str(root / "sha256_local.c"), "-o", str(path / "test")], check=True)
    rng = random.Random(563)
    sizes = [0, 1, 2, 3, 15, 31, 55, 56, 57, 63, 64, 65, 119, 120, 127,
             128, 129, 255, 256, 511, 512, 1000, 4095, 4096, 4097,
             8191, 8192, 8193, 65536, 1048576]
    count = 0
    for size in sizes:
        data = rng.randbytes(size)
        expected = hashlib.sha256(data).hexdigest()
        for step in (1, 7, 31, 64, 65, 255, 4096, 8192):
            run = subprocess.run([str(path / "test"), str(step)], input=data,
                                 stdout=subprocess.PIPE, check=True)
            if run.stdout.decode().strip() != expected:
                raise AssertionError(f"size={size}, read_step={step}")
            count += 1
    print(f"G563_SHA256_SELFTEST_PASS vectors={count}, against Python hashlib")
