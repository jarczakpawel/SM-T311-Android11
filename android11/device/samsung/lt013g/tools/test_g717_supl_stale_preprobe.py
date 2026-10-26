#!/usr/bin/env python3
"""G717: exercise the EXACT new GPS SUPL early-cancellation C guard.

Extracts the real guard from gps_proxy.c and compiles it with pthreads
in a minimal host harness, without touching modem/network/GNSS/device.
"""
from pathlib import Path
import subprocess
import tempfile

DEVICE=Path(__file__).resolve().parents[1]
source=(DEVICE/"gps-v195/gps_proxy.c").read_text()
start=source.index("    generation = work->generation;",source.index("static void *v398_agps_request_worker("))
end=source.index("    addr.s_addr = work->ipv4_net;",start)
early=source[start:end]
worker=source[source.index("static void *v398_agps_request_worker("):source.index("static int v398_defer_agps_request(")]
if not (early.index("pthread_mutex_lock(&g_state_lock)") <
        early.index("still_current = generation == g_v398_agps_generation") <
        early.index("pthread_mutex_unlock(&g_state_lock)") <
        early.index("if (!still_current)") <
        early.index("G717_AGPS_REQUEST_STALE_BEFORE_PROBE") <
        early.index("free(work);")):
    raise SystemExit("G717 FAIL: missing early guard ownership/locking")
if not (worker.index("G717_AGPS_REQUEST_STALE_BEFORE_PROBE") <
        worker.index("v398_probe_default_supl(") <
        worker.index("pthread_mutex_lock(&g_v398_link_lock)") <
        worker.index("generation == g_v398_agps_generation",worker.index("pthread_mutex_lock(&g_v398_link_lock)"))):
    raise SystemExit("G717 FAIL: lost late generation check or early guard")
harness=r"""
#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define ALOGI(...) do {} while (0)
struct v398_agps_request_work { uint32_t generation; };
static pthread_mutex_t g_state_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_v398_agps_generation;
static int g_v398_local_data_open;
static int g_v398_framework_request_active;
static int g_v398_framework_data_open;
static unsigned probe_calls;

static void *test_worker(void *opaque) {
    struct v398_agps_request_work *work=opaque;
    uint32_t generation;
    int still_current;
    if (!work) return NULL;
""" + early + r"""
    ++probe_calls;
    free(work);
    return NULL;
}

static int case_run(uint32_t wanted, uint32_t actual, int local,
                    int framework_req, int framework_data, unsigned expected) {
    struct v398_agps_request_work *work=malloc(sizeof(*work));
    if (!work) return 1;
    work->generation=wanted;
    g_v398_agps_generation=actual;
    g_v398_local_data_open=local;
    g_v398_framework_request_active=framework_req;
    g_v398_framework_data_open=framework_data;
    probe_calls=0;
    test_worker(work);
    return probe_calls!=expected;
}
int main(void) {
    int failures=0;
    failures+=case_run(8,9,0,0,0,0); /* delayed generation */
    failures+=case_run(8,8,1,0,0,0); /* already opened local CP */
    failures+=case_run(8,8,0,1,0,0); /* framework request underway */
    failures+=case_run(8,8,0,0,1,0); /* framework data open */
    failures+=case_run(8,8,0,0,0,1); /* valid work may probe */
    printf("G717 SUPL early generation/status preflight: 5/5 cases %s\n",
            failures==0?"PASS":"FAILED");
    return failures?1:0;
}
"""
with tempfile.TemporaryDirectory(prefix="g717-c-source-") as directory:
    code=Path(directory)/"test.c"
    binary=Path(directory)/"test"
    code.write_text(harness)
    subprocess.run(["cc","-std=c11","-O2","-Wall","-Wextra","-Werror",
                    "-pthread",str(code),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print("G717 source order: early skip before socket/TCP, late authoritative cancellation preserved")
