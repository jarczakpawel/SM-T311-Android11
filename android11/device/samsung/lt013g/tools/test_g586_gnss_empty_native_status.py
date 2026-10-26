#!/usr/bin/env python3
"""Exercise EXACT C guard from the real gps_proxy.c via native compiler.

Native zero-count GPS callbacks may arrive between checksum-complete GPGSV
snapshots.  Avoid clearing Android GNSS, but never fabricate GNSS status.
"""
from pathlib import Path
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "gps-v195/gps_proxy.c").read_text()
first = source.index("static int v586_gps_nmea_fresh(void) {")
last = source.index("static int v347_merged_nmea_fresh(void) {", first)
helper = source[first:last]
match = re.search(
    r"if \(!v347_merged_nmea_fresh\(\) &&\s*"
    r"!\(count == 0 && v586_gps_nmea_fresh\(\)\)\)\s*"
    r"callbacks->sv_status_cb\(&status\);", source)
assert match, "G586 actual producer condition drifted"
guard = match.group(0).replace("callbacks->sv_status_cb(&status);", "return 1;")
program = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define V308_GSV_STALE_MS 4000LL
struct v308_gsv_group {unsigned complete_count; int64_t complete_at_ms;};
static struct v308_gsv_group g_v308_gsv[3];
static int64_t fake_time;
static int merged;
static int64_t v347_now_ms(void) { return fake_time; }
static int v347_merged_nmea_fresh(void) { return merged; }
""" + helper + r"""
static int should_report_legacy_native(int count) {
""" + guard + r"""
    return 0;
}
int main(void) {
    // Without NMEA, even empty native remains authoritative.
    fake_time = 10000;
    assert(should_report_legacy_native(0) == 1);
    assert(should_report_legacy_native(6) == 1);

    // Fresh GPS-only NMEA: suppress only empty native update.
    g_v308_gsv[0].complete_at_ms = 9000;
    g_v308_gsv[0].complete_count = 11;
    assert(should_report_legacy_native(0) == 0);
    assert(should_report_legacy_native(1) == 1);

    // Stale GPS/NMEA: restore legacy native status.
    fake_time = 13001;
    assert(should_report_legacy_native(0) == 1);

    // Exactly at stale threshold remains fresh, next ms expires.
    fake_time = 13000;
    assert(should_report_legacy_native(0) == 0);
    fake_time = 13001;
    assert(should_report_legacy_native(0) == 1);

    // Empty GSV snapshot must not hide real native zero-count.
    fake_time = 10000;
    g_v308_gsv[0].complete_count = 0;
    assert(should_report_legacy_native(0) == 1);

    // No monotonic time or impossible future GSV data: reject.
    g_v308_gsv[0].complete_count = 11;
    fake_time = 0;
    assert(should_report_legacy_native(0) == 1);
    fake_time = 8000;
    assert(should_report_legacy_native(0) == 1);

    // Existing GPS+GL/QZSS merge precedence remains higher.
    merged = 1;
    fake_time = 10000;
    assert(should_report_legacy_native(0) == 0);
    assert(should_report_legacy_native(6) == 0);
    puts("G586_ACTUAL_GPS_PROXY_FRESH_GSV_ZERO_NATIVE_GUARD_10_CASES_PASS");
    return 0;
}
"""
with tempfile.TemporaryDirectory(prefix="g586-native-gnss-") as tmp:
    file = Path(tmp) / "test.c"
    out = Path(tmp) / "test"
    file.write_text(program)
    subprocess.run(
        ["cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-O2",
         "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
         str(file), "-o", str(out)], check=True)
    subprocess.run([str(out)], check=True)
