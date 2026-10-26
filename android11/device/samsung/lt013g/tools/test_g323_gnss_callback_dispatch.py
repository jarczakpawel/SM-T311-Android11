#!/usr/bin/env python3
"""Compile and exercise the ACTUAL NMEA dispatch case from gps_proxy.c.

Verifies GnssStatus reporting does not depend on an optional raw NMEA cb.
Uses harmless callback mocks; no emulator, GPS device, modem or USB involved.
"""
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1] / "gps-v195/gps_proxy.c"
text = SOURCE.read_text()
begin = text.index("        case LT013G_CB_NMEA:\n")
end = text.index("        case LT013G_CB_CAPABILITIES:\n", begin)
actual_case = text[begin:end]

head = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define LT013G_BRIDGE_NMEA_MAX 960u
#define LT013G_CB_NMEA 7
struct lt013g_cb_nmea {
    int64_t timestamp_ms;
    uint32_t length;
    char data[LT013G_BRIDGE_NMEA_MAX];
};
struct callback_set {
    void (*nmea_cb)(int64_t, const char*, int);
    void (*gnss_sv_status_cb)(void*);
};
struct bridge_packet {
    uint32_t payload_size;
    unsigned char payload[sizeof(struct lt013g_cb_nmea)];
};
static int g_nmea, g_gsv, g_gsa, g_gnss_source_reports;
static uint32_t g_v346_nmea_count, g_v346_gpgsv_count;
static uint32_t g_v346_glgsv_count, g_v348_gqgsv_count;
static void v346_gnss_report(void) { ++g_gnss_source_reports; }
static pthread_mutex_t g_state_lock = PTHREAD_MUTEX_INITIALIZER;
static struct {int nmea_count, gpsd_last_generation,
               nmea_after_restart, last_nmea_generation;} g_stats;
static void v312_gsa_from_nmea(const struct lt013g_cb_nmea *p) {
    assert(p->length > 0); ++g_gsa;
}
static void v308_gsv_from_nmea(const struct lt013g_cb_nmea *p,
                               void (*cb)(void*)) {
    assert(p->length > 0 && cb); ++g_gsv;
}
static int v309_correct_rmc_date(const struct lt013g_cb_nmea *p,
                                 char *out, size_t length) {
    (void)p; (void)out; (void)length; return 0;
}
static void nmea_callback(int64_t t, const char *p, int n) {
    assert(t == 123 && n > 0 && p[0] == '$'); ++g_nmea;
}
static void satellite_callback(void *p) {(void)p;}
static void dispatch(struct callback_set *callbacks,
                     struct bridge_packet *packet,
                     int session_started) {
    switch (LT013G_CB_NMEA) {
'''
tail = r'''
    default: break;
    }
}
int main(void) {
    struct bridge_packet pkt = {0};
    struct lt013g_cb_nmea nmea = {0};
    struct callback_set cbs = {0};
    const char message[] = "$GPGSV,1,1,01,12,20,100,26*60";
    nmea.timestamp_ms = 123;
    nmea.length = sizeof(message) - 1;
    memcpy(nmea.data, message, sizeof(message));
    pkt.payload_size = sizeof(nmea);
    memcpy(pkt.payload, &nmea, sizeof(nmea));

    // GNSS only: text NMEA callback is not registered.
    cbs.gnss_sv_status_cb = satellite_callback;
    dispatch(&cbs, &pkt, 1);
    assert(g_gsv == 1 && g_gsa == 1 && g_nmea == 0);

    // Raw NMEA only: should still notify NMEA despite no satellite listener.
    cbs.gnss_sv_status_cb = NULL;
    cbs.nmea_cb = nmea_callback;
    dispatch(&cbs, &pkt, 1);
    assert(g_gsv == 1 && g_gsa == 1 && g_nmea == 1);

    // Both listeners with inactive navigation session: only raw NMEA callback.
    cbs.gnss_sv_status_cb = satellite_callback;
    dispatch(&cbs, &pkt, 0);
    assert(g_gsv == 1 && g_gsa == 1 && g_nmea == 2);

    // Both in active session: status and raw NMEA each received once.
    dispatch(&cbs, &pkt, 1);
    assert(g_gsv == 2 && g_gsa == 2 && g_nmea == 3);
    assert(g_stats.nmea_count == 4);
    assert(g_v346_nmea_count == 4);
    assert(g_v346_gpgsv_count == 4);
    assert(g_v346_glgsv_count == 0 && g_v348_gqgsv_count == 0);
    assert(g_gnss_source_reports == 4);
    puts("PASS G323 actual GNSS NMEA dispatch isolated callbacks (4 cases)");
    puts("PASS G585 GNSS NMEA source counters and diagnostic report calls (4 cases)");
    return 0;
}
'''
# The actual code contains an in-situ break, followed by our default case.
with tempfile.TemporaryDirectory(prefix="g323-gnss-dispatch-") as td:
    source = Path(td) / "test.c"
    executable = Path(td) / "test"
    source.write_text(head + actual_case + tail)
    subprocess.run(["cc", "-std=gnu11", "-O2", "-Wall", "-Wextra",
                    "-Werror", "-pthread", str(source), "-o", str(executable)],
                   check=True)
    subprocess.run([str(executable)], check=True)
