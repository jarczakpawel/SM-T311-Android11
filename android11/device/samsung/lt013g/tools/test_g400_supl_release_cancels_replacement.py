#!/usr/bin/env python3
"""Compile exact SM-T311 SUPL callback & deferred worker and schedule race without modem.

The pre-fix --expect-bug gate must show CP OPEN leaked by REQUEST/RELEASE
before deferred worker runs. Corrected source must close CP in this order.
No real network, sockets, ADB commands, GPS or SIM operations.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

GPS=Path(__file__).resolve().parents[1]/'gps-v195'
SOURCE=(GPS/'gps_proxy.c').read_text()
if '--expect-bug' in sys.argv:
    baseline=subprocess.run(['git','-C',str(GPS.parent),'show',
        'd8bf4e8:gps-v195/gps_proxy.c'],
        capture_output=True,text=True,check=True)
    SOURCE=baseline.stdout
    assert 'retire_orphan = pending_close && !framework_active;' not in SOURCE
else:
    assert 'retire_orphan = pending_close && !framework_active;' in SOURCE
a=SOURCE.index('        case LT013G_CB_AGPS_STATUS:')
b=SOURCE.index('        case LT013G_CB_AGPS_RIL_REQUEST_SETID:',a)
callback_case=SOURCE[a:b]
a=SOURCE.index('struct v398_agps_request_work {')
b=SOURCE.index('static int v398_probe_default_supl(',a)
work_struct=SOURCE[a:b]
a=SOURCE.index('struct v398_local_close_work {')
b=SOURCE.index('/* BCM4752 emits GPS and GLONASS',a)
workers=SOURCE[a:b]
# Use exact G396 framework bridge shell, with the true source worker replacing stub.
import importlib.util
sp=importlib.util.spec_from_file_location('g396',Path(__file__).parent/'test_g396_supl_missing_ip_callback.py')
mod=importlib.util.module_from_spec(sp)
sp.loader.exec_module(mod)
prefix=mod.HEAD[:mod.HEAD.index('static int v398_defer_agps_request(')]
dispatch=mod.HEAD[mod.HEAD.index('static void dispatch_mock('):]
helpers=mod.TAIL[:mod.TAIL.index('int main(void){')]
environment=r'''
#include <unistd.h>
#define APN_IP_IPV4 1u
#define ALOGE(...) do { if (0) fprintf(stderr, __VA_ARGS__); } while (0)
#define ALOGW(...) do { if (0) fprintf(stderr, __VA_ARGS__); } while (0)
static pthread_mutex_t g_v398_link_lock=PTHREAD_MUTEX_INITIALIZER;
static int g_probe_rc=0;
static int g_cp_is_open=0,g_total_open=0,g_total_close=0,g_total_cmds=0;
static int g_fw_released=0;
struct Pending { void *(*worker)(void*);void *opaque; };
static struct Pending g_pending[10];
static int g_queued=0;
static int v398_probe_default_supl(uint32_t ip,int port){
 (void)ip;(void)port;return g_probe_rc;
}
static int send_command(uint32_t type,const void *data,uint32_t sz){
 (void)data;(void)sz;
 ++g_total_cmds;
 if (type==LT013G_CMD_AGPS_DATA_CONN_OPEN) {
  assert(!g_cp_is_open&&"overlapping old legacy CP SUPL session");
  g_cp_is_open=1;++g_total_open;return 0;
 }
 assert(type==LT013G_CMD_AGPS_DATA_CONN_CLOSED);
 ++g_total_close;g_cp_is_open=0;return 0;
}
static int mock_create(pthread_t *thread,const pthread_attr_t *a,
                       void *(*fn)(void*),void *opaque){
 (void)a;*thread=(pthread_t)1;
 assert(g_queued<10);
 g_pending[g_queued++]=(struct Pending){fn,opaque};
 return 0;
}
static int mock_detach(pthread_t thread){(void)thread;return 0;}
#define pthread_create mock_create
#define pthread_detach mock_detach
'''
extra=r'''
static void drain_one(void) {
 assert(g_queued>0);
 struct Pending job=g_pending[0];
 for(int i=1;i<g_queued;i++)g_pending[i-1]=g_pending[i];
 --g_queued;
 job.worker(job.opaque);
}
static void drain_all(void){while(g_queued)drain_one();}
static void reset_race(void){
 g_v397_supl_ipv4_valid=g_agps_server_valid=0;
 g_v397_supl_ipv4_net=0;
 g_agps_callbacks_valid=1;
 g_agps_callbacks_storage.status_cb=capture;
 g_v398_agps_generation=1;g_v398_local_data_open=0;
 g_v398_pending_close=0;g_v398_framework_request_active=0;
 g_v398_framework_data_open=0;
 g_calls=g_defer_calls=g_close_calls=0;g_probe_ip=0;
 g_probe_rc=0;
 g_cp_is_open=g_total_open=g_total_close=g_total_cmds=0;
 g_queued=g_fw_released=0;
 memset(&g_last,0,sizeof(g_last));
}
static void request(void){
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,0,0,NULL);
}
static void release_supl(void){
 fire(AGPS_TYPE_SUPL,GPS_RELEASE_AGPS_DATA_CONN,0,0,NULL);
}
int main(void){
 // Case A: open old native CP, replace, release while worker queued.
 reset_race();
 g_cp_is_open=1;g_total_open=1;g_v398_local_data_open=1;
 request();
 assert(g_queued==1&&!g_v398_local_data_open&&g_v398_pending_close);
 release_supl();
 drain_all();
#ifdef EXPECT_OLD_LEAK
 assert(g_cp_is_open && g_total_close==0 && g_v398_pending_close);
 puts("PASS actual G397 race reproduced: old CP remains OPEN after REQUEST -> RELEASE -> cancelled worker");
#else
 assert(!g_cp_is_open && g_total_close==1 && !g_v398_pending_close);
 puts("PASS replacement local CP drained after release cancels queued request");
#endif
 // Case B: one live framework-owned CP old session, same race.
 reset_race();
 g_cp_is_open=1;g_total_open=1;g_v398_framework_data_open=1;
 request();
 assert(g_queued==1&&g_v398_pending_close&&!g_v398_framework_data_open);
 release_supl();
 drain_all();
#ifdef EXPECT_OLD_LEAK
 assert(g_cp_is_open && g_total_close==0);
 puts("PASS actual G397 framework-owned old CP also leaked on release");
#else
 assert(!g_cp_is_open&&g_total_close==1&&!g_v398_pending_close);
 puts("PASS replacement framework CP drained after release cancels queued request");
#endif
#ifndef EXPECT_OLD_LEAK
 // C: request + release without any old CP, no fake CLOSED.
 reset_race();
 request();release_supl();drain_all();
 assert(!g_cp_is_open && !g_total_close && !g_total_open);
 puts("PASS RELEASE while original CP absent does not send spurious CLOSED");

 // D: regular local RELEASE with no deferred replacement closes old CP once.
 reset_race();
 g_cp_is_open=1;g_total_open=1;g_v398_local_data_open=1;
 release_supl();drain_all();
 assert(!g_cp_is_open&&g_total_close==1);
 puts("PASS ordinary local-only RELEASE still closes existing CP once");

 // E: queued new REQUEST worker runs first and drains old CP before opening
 // replacement; subsequent RELEASE schedules exactly one close.
 reset_race();
 g_cp_is_open=1;g_total_open=1;g_v398_local_data_open=1;
 g_agps_server_valid=1;g_agps_server_cache.port=7275;
 g_v397_supl_ipv4_valid=1;g_v397_supl_ipv4_net=htonl(0x7f000001);
 request();drain_one();
 assert(g_cp_is_open&&g_total_open==2&&g_total_close==1);
 release_supl();drain_all();
 assert(!g_cp_is_open&&g_total_close==2&&!g_v398_pending_close);
 puts("PASS replacement request-first ordering OPEN, close, OPEN, RELEASE close");

 // F: already pending CP-close gets new request then release: exactly one CLOSED.
 reset_race();
 g_cp_is_open=1;g_v398_pending_close=1;
 request();release_supl();drain_all();
 assert(!g_cp_is_open && g_total_close==1);
 puts("PASS pending previous close drained even across stale worker generations");

 // G: active framework owner of the CP plus a pending old-close flag
 // remains managed by framework, never arbitrarily drained by local release.
 reset_race();
 g_cp_is_open=1;g_v398_framework_data_open=1;g_v398_pending_close=1;
 release_supl();
 assert(g_calls==1&&g_total_close==0&&g_queued==0);
 puts("PASS active framework owner is never closed by the local orphan cleanup");

 // H: live independent framework session RELEASE must reach framework cb;
 // don't close its CP with the local deferred close instead.
 reset_race();
 g_cp_is_open=1;g_v398_framework_data_open=1;
 release_supl();
 assert(g_calls==1&&g_queued==0&&g_total_close==0);
 puts("PASS framework-owned active RELEASE callback preserved for framework owner");

 // I: never manufacture a close while framework only requested network but
 // has not obtained CP OPEN yet.
 reset_race();
 g_v398_framework_request_active=1;
 release_supl();drain_all();
 assert(g_calls==1&&g_total_close==0);
 puts("PASS only-pending framework network has no CP close command");
#endif
 return 0;
}
'''
def run():
    old='--expect-bug' in sys.argv
    header=prefix+environment+work_struct+workers+dispatch
    final=header+callback_case+helpers+extra
    with tempfile.TemporaryDirectory(prefix='smt311-g400-race-') as d:
        c=Path(d)/'actual.c';out=Path(d)/'actual'
        c.write_text(final)
        cmd=['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-pthread',
             '-I',str(GPS),str(c),'-o',str(out)]
        if old: cmd.insert(1,'-DEXPECT_OLD_LEAK')
        cp=subprocess.run(cmd,capture_output=True,text=True)
        if cp.returncode:raise SystemExit('Actual C worker/source compile failed:\n'+cp.stderr[:3800])
        res=subprocess.run([str(out)],capture_output=True,text=True)
        if res.returncode:raise SystemExit('Actual C scheduler failed:\n'+res.stderr[:2300])
        print(res.stdout.strip())
        print('G400 real C source callback+worker deterministic synthetic race harness PASS; NO modem/network/device use.')
if __name__=='__main__':run()
