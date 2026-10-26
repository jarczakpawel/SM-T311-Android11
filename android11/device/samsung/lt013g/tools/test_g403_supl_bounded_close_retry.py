#!/usr/bin/env python3
"""Compile genuine local CP-CLOSED worker from G401 or proposed G403 and prove
bounded transport error recovery with deterministic synthetic modem responses.
No ADB, actual network, modem, GPS, SIM or user data.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
SRC=Path(__file__).resolve().parents[1]/'gps-v195/gps_proxy.c'
source=SRC.read_text()
old='--expect-original-failure' in sys.argv
if old:
    source=subprocess.run(['git','-C',str(SRC.parents[1]),'show',
                            '7dfa0f7:gps-v195/gps_proxy.c'],check=True,capture_output=True,text=True).stdout
start=source.index('struct v398_local_close_work {')
end=source.index('static void *v398_agps_request_worker(',start)
workers=source[start:end]
h=r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LT013G_CMD_AGPS_DATA_CONN_CLOSED 25
#define ALOGI(...) do {} while (0)
#define ALOGW(...) do {} while (0)
#define ALOGE(...) do {} while (0)
static pthread_mutex_t g_v398_link_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_state_lock=PTHREAD_MUTEX_INITIALIZER;
static int g_v398_pending_close;
static int g_v398_local_data_open;
static int g_v398_framework_data_open;
static int cp_open,close_count,failed_responses,attempt_count,sleep_count;
static int mock_create_fail,queued_count;
static int fail_left;
static void *(*queued_fn)(void *);
static void *queued_arg;
static int send_command(uint32_t op,const void *data,uint32_t size) {
 assert(op==LT013G_CMD_AGPS_DATA_CONN_CLOSED);
 assert(data==NULL&&size==0);
 ++attempt_count;
 if(fail_left>0) {--fail_left;++failed_responses;return -EIO;}
 cp_open=0;++close_count;return 0;
}
static int fake_usleep(unsigned int time_us) {
 assert(time_us>0 && time_us<=500000);
 ++sleep_count;
 return 0;
}
#define usleep fake_usleep
static int fake_create(pthread_t *thread,const pthread_attr_t *attr,
                       void *(*fn)(void*),void* arg) {
 (void)attr;
 if(mock_create_fail) return EAGAIN;
 assert(!queued_count);
 *thread=(pthread_t)1;
 queued_fn=fn;queued_arg=arg;queued_count=1;
 return 0;
}
static int fake_detach(pthread_t t) {(void)t;return 0;}
#define pthread_create fake_create
#define pthread_detach fake_detach
'''
test=r'''
static void reset(void){
 g_v398_pending_close=g_v398_local_data_open=g_v398_framework_data_open=0;
 cp_open=close_count=failed_responses=attempt_count=sleep_count=0;
 mock_create_fail=queued_count=fail_left=0;queued_fn=NULL;queued_arg=NULL;
}
static void drain(void){
 assert(queued_count==1 && queued_fn);
 void *(*fn)(void*)=queued_fn;void *arg=queued_arg;
 queued_count=0;queued_fn=NULL;queued_arg=NULL;
 fn(arg);
}
int main(void){
 // Case A: old OPEN, transient CLOSE fails exactly once, then succeeds.
 reset();cp_open=1;g_v398_pending_close=1;fail_left=1;
 v398_defer_local_close(5);drain();
#ifdef EXPECT_G401_FAILURE
 assert(cp_open&&g_v398_pending_close&&attempt_count==1&&sleep_count==0);
 puts("PASS real original G401 closes ZERO CP connections after transient -EIO");
#else
 assert(!cp_open&&!g_v398_pending_close&&attempt_count==2&&close_count==1&&sleep_count==1);
 puts("PASS new worker retries once and succeeds without extra CLOSE");
#endif

#ifndef EXPECT_G401_FAILURE
 // Case B: two transient command failures, last safe retry succeeds.
 reset();cp_open=1;g_v398_pending_close=1;fail_left=2;
 v398_defer_local_close(5);drain();
 assert(!cp_open&&!g_v398_pending_close&&attempt_count==3&&close_count==1&&sleep_count==2);
 puts("PASS two transient CP errors recovered, no repeated successful CLOSE");

 // Case C: permanently failing transport stops strictly at three sends.
 reset();cp_open=1;g_v398_pending_close=1;fail_left=20;
 v398_defer_local_close(5);drain();
 assert(cp_open&&g_v398_pending_close&&attempt_count==3&&sleep_count==2&&close_count==0);
 puts("PASS persistent CP fault bounded at three commands, old pending flag preserved");

 // Case D: no pending old CP, no send, no backoff.
 reset();v398_defer_local_close(6);drain();
 assert(attempt_count==0&&sleep_count==0);
 puts("PASS no pending CP does not create fictitious command");

 // Case E: local CP session became active before worker runs, do not close it.
 reset();cp_open=1;g_v398_pending_close=1;g_v398_local_data_open=1;
 v398_defer_local_close(6);drain();
 assert(cp_open&&attempt_count==0&&g_v398_pending_close);
 puts("PASS local ownership suppresses stale CLOSED");

 // Case F: after failure, a later natural replacement triggers a new worker
 // and can clear pending without unbounded hidden scheduler.
 reset();cp_open=1;g_v398_pending_close=1;fail_left=20;
 v398_defer_local_close(7);drain();
 assert(g_v398_pending_close&&attempt_count==3);
 fail_left=0;v398_defer_local_close(8);drain();
 assert(!cp_open&&!g_v398_pending_close&&attempt_count==4);
 puts("PASS failed bounded retries retain existing deferred follow-up semantics");

 // Case G: CP already successfully closed by another ordered worker before
 // obsolete queued work; newer worker must not issue a second CLOSE.
 reset();g_v398_pending_close=1;cp_open=1;
 v398_defer_local_close(9);
 g_v398_pending_close=0;cp_open=0;drain();
 assert(attempt_count==0 && !g_v398_pending_close);
 puts("PASS already settled CP pending skips obsolete close");

 // Case H: thread creation fails, no live state mutation by constructor.
 reset();g_v398_pending_close=1;cp_open=1;mock_create_fail=1;
 v398_defer_local_close(10);
 assert(queued_count==0&&cp_open&&g_v398_pending_close);
 puts("PASS failed worker spawn keeps old CP flagged pending without blind blocking writes");
#endif
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='SMT311-G403-CP-RETRY-') as td:
    f=Path(td)/'original.c';out=Path(td)/'test';f.write_text(h+workers+test)
    cc=['cc','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-Wno-unused-function','-pthread',str(f),'-o',str(out)]
    if old:cc.insert(1,'-DEXPECT_G401_FAILURE')
    c=subprocess.run(cc,capture_output=True,text=True)
    if c.returncode:raise SystemExit('G403 original C compile FAIL:\n'+c.stderr[:3600])
    q=subprocess.run([str(out)],capture_output=True,text=True)
    if q.returncode:raise SystemExit('G403 mock execution FAIL:\n'+q.stdout+'\n'+q.stderr[:1500])
    print(q.stdout.strip())
    print('G403 ACTUAL C source native close worker compiled fixture PASS, no device or modem operations.')
