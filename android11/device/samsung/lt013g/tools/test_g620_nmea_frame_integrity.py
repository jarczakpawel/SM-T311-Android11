#!/usr/bin/env python3
"""Compile the real production GNSS checksum parser and fuzz NMEA framing."""
from pathlib import Path
import subprocess
import tempfile
import sys

source = (Path(__file__).resolve().parents[1] / "gps-v195/gps_proxy.c").read_text()
start = source.index("static int v308_nmea_checksum(")
end = source.index("static int v308_nmea_svid(", start)
parser = source[start:end]
prefix = r'''
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
'''
test = r'''
static unsigned cases;
static void check(const char *frame, size_t n, int expected) {
    char copy[1024];
    memset(copy, 0xA5, sizeof(copy));
    int actual = v308_nmea_checksum(frame, n, copy, sizeof(copy));
    if (actual != expected) {
        fprintf(stderr, "G620_NMEA_FAIL case=%u n=%zu actual=%d expected=%d\n",
                cases + 1, n, actual, expected);
        abort();
    }
    cases++;
}
int main(void) {
    const char *base = "$GPGSV,1,1,00";
    char framed[256];
    unsigned char sum = 0;
    for (const char *p = base + 1; *p; ++p) sum ^= (unsigned char)*p;
    int base_len = snprintf(framed, sizeof(framed), "%s*%02X", base, sum);
    assert(base_len > 0 && (size_t)base_len < sizeof(framed) - 30);
    check(framed, (size_t)base_len, 1);
    strcat(framed, "\r");
    check(framed, strlen(framed), 1);
    framed[base_len] = '\0';
    strcat(framed, "\n");
    check(framed, strlen(framed), 1);
    framed[base_len] = '\0';
    strcat(framed, "\r\n");
    check(framed, strlen(framed), 1);
    size_t valid_len = strlen(framed);
    check(framed, valid_len + 1, 0); /* counted trailing NUL */
    framed[valid_len + 1] = 'X';
    check(framed, valid_len + 2, 0); /* embedded NUL with data after */
    framed[valid_len] = 'X';
    check(framed, valid_len + 1, 0); /* CRLF + junk */
    framed[base_len] = '\n';
    framed[base_len + 1] = 'X';
    check(framed, (size_t)base_len + 2, 0); /* LF + junk */
    framed[base_len] = '\r';
    framed[base_len + 1] = 'X';
    check(framed, (size_t)base_len + 2, 0); /* CR + junk */
    framed[base_len] = 'X';
    check(framed, (size_t)base_len + 1, 0); /* no legal terminator */
    framed[base_len] = '\0';
    check(framed, (size_t)base_len - 1, 0); /* one hex digit */
    check(framed, (size_t)base_len - 2, 0); /* asterisk alone */
    check(framed, (size_t)base_len - 3, 0); /* no checksum */
    framed[base_len - 1] = 'Z';
    check(framed, (size_t)base_len, 0); /* non-hex checksum */
    framed[base_len - 1] = '0';
    framed[base_len - 2] = '0';
    if (sum != 0) check(framed, (size_t)base_len, 0); /* wrong checksum */
    framed[base_len - 1] = 'F';
    framed[base_len - 2] = 'F';
    if (sum != 0xFF) check(framed, (size_t)base_len, 0); /* wrong checksum */
    framed[base_len - 2] = "0123456789ABCDEF"[sum >> 4];
    framed[base_len - 1] = "0123456789ABCDEF"[sum & 15];
    check(framed, (size_t)base_len, 1);
    char too_small[8];
    assert(!v308_nmea_checksum(framed, (size_t)base_len, too_small, sizeof(too_small)));
    cases++;
    /* All 256 possible bytes appended after *HH. Only CR and LF are legal. */
    for (unsigned byte = 0; byte < 256; ++byte) {
        framed[base_len] = (char)byte;
        check(framed, (size_t)base_len + 1, byte == '\r' || byte == '\n');
    }
    /* After CR only a single LF is valid. */
    framed[base_len] = '\r';
    for (unsigned byte = 0; byte < 256; ++byte) {
        framed[base_len + 1] = (char)byte;
        check(framed, (size_t)base_len + 2, byte == '\n');
    }
    printf("G620_NMEA_FRAME_INTEGRITY_%u_CASES_PASS\n", cases);
    return 0;
}
'''
if len(sys.argv) == 3 and sys.argv[1] == "--emit":
    out = Path(sys.argv[2])
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(prefix + parser + test)
    sys.exit(0)
if len(sys.argv) != 1:
    raise SystemExit("usage: test_g620_nmea_frame_integrity.py [--emit OUTPUT_C]")

with tempfile.TemporaryDirectory(prefix="g620-nmea-integrity-") as d:
    source_c = Path(d) / "test.c"
    source_c.write_text(prefix + parser + test)
    for sanitizer in (False, True):
        binary = Path(d) / ("test-asan" if sanitizer else "test")
        flags = (["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
                 if sanitizer else ["-O2"])
        subprocess.run(["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                        *flags, str(source_c), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
