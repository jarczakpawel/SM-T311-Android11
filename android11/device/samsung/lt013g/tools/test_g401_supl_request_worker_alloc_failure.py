#!/usr/bin/env python3
"""Native G400 callback + deferred request + framework OPEN: failed worker allocation.

Original G400 actual code from Git ca84644. No device/network/SIM operations.
"""
from pathlib import Path
import subprocess,sys,tempfile,importlib.util
GPS=Path(__file__).resolve().parents[1]/'gps-v195'
SOURCE=(GPS/'gps_proxy.c').read_text()
baseline='--expect-original-bug' in sys.argv
if baseline:
 SOURCE=subprocess.run(['git','-C',str(GPS.parent),'show',
                        'ca84644:gps-v195/gps_proxy.c'],
                        check=True,text=True,capture_output=True).stdout
spec=importlib.util.spec_from_file_location('g400',Path(__file__).parent/'test_g400_supl_release_cancels_replacement.py')
g400=importlib.util.module_from_spec(spec)
spec.loader.exec_module(g400)
def cut(a,b):return SOURCE[SOURCE.index(a):SOURCE.index(b,SOURCE.index(a))]
status=cut('        case LT013G_CB_AGPS_STATUS:','        case LT013G_CB_AGPS_RIL_REQUEST_SETID:')
struct=cut('struct v398_agps_request_work {','static int v398_probe_default_supl(')
close_code=cut('struct v398_local_close_work {','/* BCM4752 emits GPS and GLONASS')
framework=cut('static int v332_framework_data_conn_open(','static int proxy_agps_data_conn_open(')
prefix=g400.prefix
environment=g400.environment.replace(
    'static int mock_create(pthread_t *thread',
    'static int g_thread_create_fail;\nstatic int mock_create(pthread_t *thread').replace(
    ' (void)a;*thread=(pthread_t)1;',
    ' (void)a;if (g_thread_create_fail) {g_thread_create_fail=0;return EAGAIN;}*thread=(pthread_t)1;')
# G400 shared synthetic environment now supplies the Android ALOGW macro.
injection=r'''
static int g_alloc_fail_request;
static void *fake_calloc(size_t cnt,size_t elem) {
 if(g_alloc_fail_request && elem==sizeof(struct v398_agps_request_work)){
  g_alloc_fail_request=0;return NULL;
 }
 return calloc(cnt,elem);
}
#define calloc fake_calloc
'''
tail=r'''
 default: break;
 }
}
static void issue(int opened, int failure_kind, int release_early) {
 struct lt013g_bridge_packet packet={0};
 struct lt013g_cb_agps_status payload={0};
 packet.type=LT013G_CB_AGPS_STATUS;
 payload.type=AGPS_TYPE_SUPL;
 payload.status=GPS_REQUEST_AGPS_DATA_CONN;
 packet.payload_size=sizeof(payload);
 memcpy(packet.payload,&payload,sizeof(payload));
 g_v398_agps_generation=1;
 g_v398_pending_close=0;
 g_v398_local_data_open=opened;
 g_v398_framework_data_open=0;
 g_v398_framework_request_active=0;
 g_cp_is_open=opened;g_total_open=opened;g_total_close=g_calls=0;
 g_queued=0;
 g_alloc_fail_request=(failure_kind==1);
 g_thread_create_fail=(failure_kind==2);
 dispatch_mock(&packet);
 assert(!g_alloc_fail_request && !g_thread_create_fail);
 assert(g_calls==1);
#ifdef EXPECT_OLD_LEAK
 assert(opened==1 && failure_kind==1 && !release_early);
 assert(g_queued==0);
#else
 assert(g_queued==opened && g_v398_framework_request_active);
 assert(!g_v398_local_data_open);
 assert(g_v398_pending_close==opened);
#endif
 if(release_early){
  payload.status=GPS_RELEASE_AGPS_DATA_CONN;
  memcpy(packet.payload,&payload,sizeof(payload));
  dispatch_mock(&packet);
#ifndef EXPECT_OLD_LEAK
  assert(g_calls==2);
  while(g_queued){struct Pending job=g_pending[--g_queued];job.worker(job.opaque);}
  assert(!g_cp_is_open && g_total_close==opened);
  puts("PASS G401 early RELEASE drains retired CP despite failed worker creation");
#endif
  return;
 }
 struct lt013g_cmd_agps_data_conn_open actual={0};
 actual.apn_ip_type=APN_IP_IPV4;
 int rc=v332_framework_data_conn_open(&actual);
 assert(rc==0);
#ifdef EXPECT_OLD_LEAK
 assert(g_cp_is_open&&g_total_close==0&&g_total_open==1 &&
        !g_v398_framework_data_open && g_v398_local_data_open);
 puts("PASS reproduced G400 old local OPEN suppresses new framework OPEN after OOM");
#else
 assert(g_cp_is_open && g_total_close==opened && g_total_open==opened+1 &&
        g_v398_framework_data_open && !g_v398_local_data_open);
 puts("PASS G401 fallback safely replaces old CP or creates first session");
 while(g_queued){struct Pending job=g_pending[--g_queued];job.worker(job.opaque);}
 assert(g_total_close==opened && g_cp_is_open && g_v398_framework_data_open);
 puts("PASS G401 obsolete async CLOSED cannot close newer framework CP");
#endif
}
int main(void){
 g_agps_callbacks_valid=1;g_agps_callbacks_storage.status_cb=capture;
 issue(1,1,0);
#ifndef EXPECT_OLD_LEAK
 issue(1,2,0);
 issue(0,1,0);
 issue(0,2,0);
 issue(1,1,1);
 issue(1,2,1);
 issue(0,1,1);
 issue(0,2,1);
#endif
 return 0;
}
'''
src=prefix+environment+struct+injection+close_code+framework+g400.dispatch+status+tail
with tempfile.TemporaryDirectory(prefix='lt013g-g401-source-OOM-') as t:
 f=Path(t)/'actual.c';exe=Path(t)/'actual';f.write_text(src)
 args=['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
       '-pthread','-I',str(GPS),str(f),'-o',str(exe)]
 if baseline:args.insert(1,'-DEXPECT_OLD_LEAK')
 cp=subprocess.run(args,capture_output=True,text=True)
 if cp.returncode:raise SystemExit('G401 compile fail:\n'+cp.stderr[:4600])
 pr=subprocess.run([str(exe)],capture_output=True,text=True)
 if pr.returncode:raise SystemExit('G401 mock run fail:\n'+pr.stderr[:2000])
 print(pr.stdout.strip())
 print('G401 exact extracted C source OOM gate PASS')
