#!/usr/bin/env python3
"""Test the exact Android framework A-GPS OPEN/CLOSED/FAILED source C functions.

Uses synthetic CP and Android mock callbacks, no real GPS, network or modem.
"""
from pathlib import Path
import subprocess, tempfile

s=(Path(__file__).resolve().parents[1]/"gps-v195/gps_proxy.c").read_text()
a=s.index("static int v332_framework_data_conn_open(")
b=s.index("static int proxy_agps_set_server(",a)
f=s[a:b]
a=s.index("static int proxy_agps_data_conn_open_with_apn_ip_type(")
b=s.index("static const AGpsInterface g_agps_interface",a)
overload=s[a:b]
assert "g_v398_framework_data_open = rc == 0" in f
assert f.count("pthread_mutex_lock(&g_v398_link_lock)")>=3
assert "v332_framework_data_conn_open(&payload)" in overload

head=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int ApnIpType;
#define APN_IP_IPV4V6 3
#define APN_IP_IPV4 1
#define LT013G_CMD_AGPS_DATA_CONN_OPEN 24
#define LT013G_CMD_AGPS_DATA_CONN_CLOSED 25
#define LT013G_CMD_AGPS_DATA_CONN_FAILED 26
#define ALOGI(...) do{}while(0)
#define ALOGW(...) do{}while(0)
struct lt013g_cmd_agps_data_conn_open {
 uint16_t apn_ip_type;char apn[256];
};
static pthread_mutex_t g_v398_link_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t g_state_lock=PTHREAD_MUTEX_INITIALIZER;
static uint32_t g_v398_agps_generation=1;
static int g_v398_local_data_open,g_v398_pending_close;
static int g_v398_framework_request_active,g_v398_framework_data_open;
static int g_commands[32],g_count,g_open_rc,g_close_rc,g_fail_rc,g_bump_generation;
static int g_apn_ip_type;static char g_apn[256];
static int send_command(uint32_t cmd,const void *payload,uint32_t size){
 assert(g_count<32);
 g_commands[g_count++]=(int)cmd;
 switch(cmd){
 case LT013G_CMD_AGPS_DATA_CONN_OPEN:{
  const struct lt013g_cmd_agps_data_conn_open *p=payload;
  assert(payload&&size==sizeof(*p));
  g_apn_ip_type=p->apn_ip_type;
  strcpy(g_apn,p->apn);
  if(g_bump_generation) {
    pthread_mutex_lock(&g_state_lock);++g_v398_agps_generation;
    pthread_mutex_unlock(&g_state_lock);g_bump_generation=0;
  }
  return g_open_rc;
 }
 case LT013G_CMD_AGPS_DATA_CONN_CLOSED:
  assert(!payload && size==0);return g_close_rc;
 case LT013G_CMD_AGPS_DATA_CONN_FAILED:
  assert(!payload && size==0);return g_fail_rc;
 default:assert(0);return -EINVAL;
 }
}
'''
tail=r'''
static void reset(void) {
 g_v398_agps_generation=1;
 g_v398_local_data_open=g_v398_pending_close=0;
 g_v398_framework_request_active=g_v398_framework_data_open=0;
 g_count=g_open_rc=g_close_rc=g_fail_rc=g_bump_generation=0;
 g_apn_ip_type=0;g_apn[0]=0;memset(g_commands,0,sizeof(g_commands));
}
int main(void) {
 reset();
 assert(proxy_agps_data_conn_open("internet")==0);
 assert(g_count==1&&g_commands[0]==24&&g_apn_ip_type==3);
 assert(!strcmp(g_apn,"internet")&&g_v398_framework_data_open);
 puts("PASS classic AGPS OPEN calls vendor and sets framework state only on success");

 reset();g_open_rc=-EIO;g_v398_framework_request_active=1;
 assert(proxy_agps_data_conn_open("internet")==-EIO);
 assert(g_count==1&&!g_v398_framework_data_open);
 assert(!g_v398_framework_request_active);
 puts("PASS failed CP OPEN clears framework state and exposes negative rc");

 reset();g_v398_local_data_open=1;
 assert(proxy_agps_data_conn_open("internet")==0);
 assert(g_count==0 && g_v398_local_data_open && !g_v398_framework_data_open);
 puts("PASS active local SUPL suppresses redundant framework OPEN");

 reset();g_v398_pending_close=1;
 assert(proxy_agps_data_conn_open("internet")==0);
 assert(g_count==2&&g_commands[0]==25&&g_commands[1]==24);
 assert(g_v398_framework_data_open&&!g_v398_pending_close);
 puts("PASS framework OPEN closes old pending local CP connection first");

 reset();g_v398_pending_close=1;g_close_rc=-EIO;
 assert(proxy_agps_data_conn_open("internet")==-EIO);
 assert(g_count==1&&g_commands[0]==25 &&g_v398_pending_close);
 assert(!g_v398_framework_data_open);
 puts("PASS failed stale CLOSE prevents framework overlapping OPEN");

 reset();
 assert(proxy_agps_data_conn_open_with_apn_ip_type("cellular",1)==0);
 assert(g_count==1&&g_apn_ip_type==1 && !strcmp(g_apn,"cellular"));
 puts("PASS explicit APN IPv4 overload uses same serialized transaction");

 reset();g_v398_local_data_open=1;
 assert(proxy_agps_data_conn_closed()==0);
 assert(g_count==0&&g_v398_local_data_open);
 assert(proxy_agps_data_conn_failed()==0);
 assert(g_count==0&&g_v398_local_data_open);
 puts("PASS framework CLOSED/FAILED cannot terminate active local session");

 reset();g_v398_framework_data_open=1;g_v398_pending_close=1;
 assert(proxy_agps_data_conn_closed()==0);
 assert(g_count==1&&g_commands[0]==25);
 assert(!g_v398_framework_data_open&&!g_v398_pending_close);
 puts("PASS framework CLOSED forwards CP event and settles pending cleanup");

 reset();g_v398_framework_data_open=1;
 g_fail_rc=-EIO;
 assert(proxy_agps_data_conn_failed()==-EIO);
 assert(g_count==1&&g_commands[0]==26&&!g_v398_framework_data_open);
 puts("PASS framework FAILED propagates vendor rc and resets state");

 reset();g_bump_generation=1;
 assert(proxy_agps_data_conn_open("old_apn")==-ECANCELED);
 assert(g_count==2&&g_commands[0]==24&&g_commands[1]==25);
 assert(!g_v398_framework_data_open&&!g_v398_pending_close);
 puts("PASS superseded CP OPEN is closed before newer SUPL generation");

 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="smt311-g332-framework-") as d:
 c=Path(d)/"g332.c";x=Path(d)/"g332"
 c.write_text(head+f+overload+tail)
 subprocess.run(["cc","-std=gnu11","-O2","-Wall","-Wextra",
                 "-Werror","-pthread",str(c),"-o",str(x)],check=True)
 subprocess.run([str(x)],check=True)
