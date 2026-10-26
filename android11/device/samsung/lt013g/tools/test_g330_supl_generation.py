#!/usr/bin/env python3
"""Compile ACTUAL SUPL close/request workers against synthetic CP responses.

Exercises reorderings without GPS, internet, cellular data or device access.
"""
from pathlib import Path
import subprocess
import tempfile

src=(Path(__file__).resolve().parents[1]/"gps-v195/gps_proxy.c").read_text()
a=src.index("struct v398_agps_request_work {")
b=src.index("static int v398_probe_default_supl(",a)
work_decl=src[a:b]
a=src.index("struct v398_local_close_work {")
b=src.index("static void v398_defer_local_close(",a)
close_code=src[a:b]
a=src.index("static void *v398_agps_request_worker(")
b=src.index("static int v398_defer_agps_request(",a)
request_code=src[a:b]
assert "g_v398_pending_close = 0;" in close_code
assert "pthread_mutex_lock(&g_v398_link_lock)" in close_code
assert "pthread_mutex_lock(&g_v398_link_lock)" in request_code
assert "g_v398_pending_close = 1;" in request_code

head=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
typedef struct {unsigned value;} AGpsStatus;
typedef void (*agps_status_callback)(AGpsStatus*);
struct lt013g_cmd_agps_data_conn_open{int apn_ip_type;char apn[64];};
#define APN_IP_IPV4 1
#define LT013G_CMD_AGPS_DATA_CONN_OPEN 60u
#define LT013G_CMD_AGPS_DATA_CONN_CLOSED 61u
#define ALOGI(...) do {if (0) fprintf(stderr,__VA_ARGS__);} while (0)
#define ALOGW(...) do {if (0) fprintf(stderr,__VA_ARGS__);} while (0)
static pthread_mutex_t g_state_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_v398_link_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_v398_agps_generation;
static int g_v398_local_data_open,g_v398_pending_close;
static int g_v398_framework_request_active,g_v398_framework_data_open;
static int g_commands[32],g_command_count,g_framework_calls;
static int g_probe_result,g_close_result;
static int g_release_during_open;
static void fw_cb(AGpsStatus *status){assert(status);++g_framework_calls;}
static int v398_probe_default_supl(uint32_t ip,int port){
 (void)ip;(void)port;return g_probe_result;
}
static int send_command(uint32_t type,const void *payload,uint32_t size){
 if(type==LT013G_CMD_AGPS_DATA_CONN_OPEN){
  assert(payload&&size==sizeof(struct lt013g_cmd_agps_data_conn_open));
  g_commands[g_command_count++]=1;
  if(g_release_during_open){
   ++g_v398_agps_generation;
   g_release_during_open=0;
  }
  return 0;
 }
 assert(type==LT013G_CMD_AGPS_DATA_CONN_CLOSED);
 assert(!payload&&!size);
 g_commands[g_command_count++]=2;
 return g_close_result;
}
'''
tail=r'''
static void reset(void){
 g_v398_agps_generation=1;
 g_v398_local_data_open=0;g_v398_pending_close=0;
 g_v398_framework_data_open=0;g_v398_framework_request_active=0;
 g_command_count=g_framework_calls=0;
 g_probe_result=g_close_result=g_release_during_open=0;
 memset(g_commands,0,sizeof(g_commands));
}
static void close_sync(unsigned gen){
 struct v398_local_close_work *w=calloc(1,sizeof(*w));
 assert(w);w->generation=gen;v398_local_close_worker(w);
}
static void req_sync(unsigned gen){
 struct v398_agps_request_work *w=calloc(1,sizeof(*w));
 assert(w);
 w->generation=gen;w->ipv4_net=htonl(0x7f000001);w->port=7275;
 w->framework_cb=fw_cb;
 v398_agps_request_worker(w);
}
int main(void){
 reset();
 g_v398_agps_generation=8;g_v398_pending_close=1;
 close_sync(7);
 assert(g_command_count==1&&g_commands[0]==2&&!g_v398_pending_close);
 puts("PASS delayed CLOSED clears pending old CP state after generation advanced");

 reset();
 g_v398_agps_generation=9;g_v398_pending_close=1;
 req_sync(9);
 assert(g_command_count==2&&g_commands[0]==2&&g_commands[1]==1);
 assert(g_v398_local_data_open&&!g_v398_pending_close);
 close_sync(8);
 assert(g_command_count==2);
 puts("PASS new OPEN drains previous CLOSED first; stale close cannot close new session");

 reset();
 g_v398_agps_generation=11;
 req_sync(10);
 assert(g_command_count==0&&g_framework_calls==0);
 puts("PASS obsolete SUPL request cannot reopen CP or request framework network");

 reset();
 g_v398_agps_generation=12;g_release_during_open=1;
 req_sync(12);
 assert(g_command_count==2&&g_commands[0]==1&&g_commands[1]==2);
 assert(!g_v398_local_data_open&&!g_v398_pending_close);
 puts("PASS RELEASE during local OPEN triggers immediate matching CLOSED");

 reset();
 g_v398_agps_generation=13;g_v398_pending_close=1;
 g_close_result=-EIO;
 req_sync(13);
 assert(g_command_count==1&&g_commands[0]==2);
 assert(g_framework_calls==1&&!g_v398_local_data_open&&g_v398_pending_close);
 puts("PASS failed stale CLOSED blocks local OPEN and falls back to framework");

 reset();
 g_v398_agps_generation=14;g_probe_result=-ETIMEDOUT;
 req_sync(14);
 assert(g_command_count==0&&g_framework_calls==1);
 puts("PASS unreachable default SUPL preserves standard cellular framework fallback");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="smt311-g330-supl-") as td:
    c=Path(td)/"supl.c";exe=Path(td)/"supl"
    c.write_text(head+work_decl+close_code+request_code+tail)
    subprocess.run(["cc","-std=gnu11","-O2","-Wall","-Wextra",
                    "-Werror","-pthread",str(c),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
