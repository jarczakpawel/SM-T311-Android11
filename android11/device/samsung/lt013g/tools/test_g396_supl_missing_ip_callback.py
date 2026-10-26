#!/usr/bin/env python3
"""Compile ACTUAL GPS AGPS callback against host C Android11 HIDL/JNI semantics.

Runs mock AGPS messages and a synthetic non-network V398 deferred request.
No GPS, SIM, route, socket, DNS, firmware or physical device access.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
GPS=ROOT/'gps-v195'
BASE=Path(__file__).resolve().parents[4]
BRIDGE=(GPS/'gps_proxy.c').read_text()
HIDL=(BASE/'hardware/interfaces/gnss/1.0/default/AGnss.cpp').read_text()
JNI=(BASE/'frameworks/base/services/core/jni/com_android_server_location_GnssLocationProvider.cpp').read_text()
FRAMEWORK=(BASE/'frameworks/base/services/core/java/com/android/server/location/gnss/GnssNetworkConnectivityHandler.java').read_text()
# Deduced actual AOSP v1.0 contract, checked against exact source.
assert 'case AF_INET:' in HIDL and 'case AF_INET6:' in HIDL
assert 'Invalid ss_family found:' in HIDL
assert 'if (INADDR_NONE == ip)' in JNI
assert 'byteArray = convertToIpV4(ipAddr);' in JNI
assert 'if (suplIpAddr != null)' in FRAMEWORK
assert 'networkRequestBuilder.addTransportType(NetworkCapabilities.TRANSPORT_CELLULAR);' in FRAMEWORK
start=BRIDGE.index('        case LT013G_CB_AGPS_STATUS:')
end=BRIDGE.index('        case LT013G_CB_AGPS_RIL_REQUEST_SETID:',start)
case=BRIDGE[start:end]
assert 'INADDR_NONE' in case
assert 'v398_defer_agps_request' in case
HEAD=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bridge_protocol.h"
#define AGPS_TYPE_SUPL 1u
#define AGPS_TYPE_C2K 2u
#define GPS_REQUEST_AGPS_DATA_CONN 1u
#define GPS_RELEASE_AGPS_DATA_CONN 2u
#define ALOGI(...) do { if (0) fprintf(stderr,__VA_ARGS__); } while (0)
typedef struct {
 size_t size;
 uint16_t type;
 uint16_t status;
 uint32_t ipaddr;
 struct sockaddr_storage addr;
} AGpsStatus;
typedef void (*agps_status_callback)(AGpsStatus *);
static pthread_mutex_t g_state_lock=PTHREAD_MUTEX_INITIALIZER;
static int g_v397_supl_ipv4_valid, g_agps_server_valid, g_agps_callbacks_valid;
static uint32_t g_v397_supl_ipv4_net;
static struct {int port;} g_agps_server_cache;
static struct {agps_status_callback status_cb;} g_agps_callbacks_storage;
static uint32_t g_v398_agps_generation;
static int g_v398_local_data_open,g_v398_pending_close;
static int g_v398_framework_request_active,g_v398_framework_data_open;
static int g_calls,g_defer_calls,g_close_calls;
static uint32_t g_probe_ip;
static AGpsStatus g_last;
static void capture(AGpsStatus *s){++g_calls;g_last=*s;}
static int v398_defer_agps_request(const AGpsStatus *s,
  agps_status_callback cb,uint32_t ip,int port){
 (void)port;
 ++g_defer_calls;g_probe_ip=ip;
 assert(ip!=INADDR_NONE && "forbidden real connect to callback sentinel");
 cb((AGpsStatus*)s);
 return 0;
}
static void v398_defer_local_close(uint32_t generation){
 (void)generation;++g_close_calls;
}
static void dispatch_mock(const struct lt013g_bridge_packet *packet){
 switch(packet->type){
'''
TAIL=r'''
 default: break;
 }
}
static void reset_state(void){
 g_v397_supl_ipv4_valid=g_agps_server_valid=0;
 g_v397_supl_ipv4_net=0;
 g_agps_callbacks_valid=1;
 g_agps_callbacks_storage.status_cb=capture;
 g_v398_agps_generation=1;g_v398_local_data_open=0;
 g_v398_pending_close=0;g_v398_framework_request_active=0;
 g_v398_framework_data_open=0;
 g_calls=g_defer_calls=g_close_calls=0;g_probe_ip=0;
 memset(&g_last,0,sizeof(g_last));
}
static void fire(uint16_t type,uint16_t status,uint16_t family,uint32_t ipaddr,const uint8_t *addr){
 struct lt013g_bridge_packet packet={0};
 struct lt013g_cb_agps_status payload={0};
 packet.type=LT013G_CB_AGPS_STATUS;
 payload.type=type;payload.status=status;payload.family=family;
 payload.ipaddr=ipaddr;
 if(addr)memcpy(payload.address,addr,16);
 packet.payload_size=sizeof(payload);
 memcpy(packet.payload,&payload,sizeof(payload));
 dispatch_mock(&packet);
}
int main(void){
 reset_state();
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,0,0,NULL);
 assert(g_defer_calls==1&&g_probe_ip==0);
 assert(g_calls==1&&g_last.type==AGPS_TYPE_SUPL);
 assert(g_last.status==GPS_REQUEST_AGPS_DATA_CONN);
 assert(g_last.addr.ss_family==AF_INET);
 assert(((struct sockaddr_in*)&g_last.addr)->sin_addr.s_addr==INADDR_NONE);
 /* Proven JNI: convertToIpV4(INADDR_NONE) produces null -> framework requests SUPL
  * without a synthesized route to 255.255.255.255. */
 puts("PASS unknown-IP SUPL request uses HIDL AF_INET + JNI NULL marker; local probe receives zero IP");

 reset_state();g_v398_framework_request_active=1;
 fire(AGPS_TYPE_SUPL,GPS_RELEASE_AGPS_DATA_CONN,0,0,NULL);
 assert(g_calls==1&&g_defer_calls==0&&g_close_calls==0);
 assert(g_last.addr.ss_family==AF_INET);
 assert(((struct sockaddr_in*)&g_last.addr)->sin_addr.s_addr==INADDR_NONE);
 puts("PASS unknown-IP SUPL RELEASE reaches existing framework owner");

 reset_state();g_v398_local_data_open=1;
 fire(AGPS_TYPE_SUPL,GPS_RELEASE_AGPS_DATA_CONN,0,0,NULL);
 assert(g_calls==0&&g_defer_calls==0&&g_close_calls==1);
 assert(g_v398_pending_close==1);
 puts("PASS local-only SUPL RELEASE does not issue extra framework callback");

 reset_state();
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,0,0x01020304,NULL);
 assert(g_calls==1&&g_defer_calls==1);
 assert(g_last.addr.ss_family==AF_INET);
 assert(((struct sockaddr_in*)&g_last.addr)->sin_addr.s_addr==htonl(0x01020304));
 assert(g_probe_ip==htonl(0x01020304));
 puts("PASS legacy host-order IPv4 preserved for resolved SUPL requests");

 reset_state();g_v397_supl_ipv4_valid=1;
 g_v397_supl_ipv4_net=htonl(0x08080808);
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,0,0,NULL);
 assert(g_calls==1&&g_defer_calls==1);
 assert(g_probe_ip==htonl(0x08080808));
 assert(((struct sockaddr_in*)&g_last.addr)->sin_addr.s_addr==htonl(0x08080808));
 puts("PASS cached real resolved SUPL IPv4 preserves direct probe");

 reset_state();
 uint8_t addr4[16]={198,51,100,13};
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,AF_INET,0,addr4);
 assert(g_calls==1&&g_defer_calls==1);
 assert(g_last.addr.ss_family==AF_INET);
 assert(((struct sockaddr_in*)&g_last.addr)->sin_addr.s_addr==
   (uint32_t)(addr4[0]|(addr4[1]<<8)|(addr4[2]<<16)|(addr4[3]<<24)));
 puts("PASS actual native AF_INET SUPL address not overwritten");

 reset_state();
 uint8_t addr6[16]={0x20,0x01,0x0d,0xb8};
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,AF_INET6,0,addr6);
 assert(g_calls==1&&g_defer_calls==1);
 assert(g_last.addr.ss_family==AF_INET6);
 assert(!memcmp(&((struct sockaddr_in6*)&g_last.addr)->sin6_addr,addr6,16));
 assert(g_probe_ip==0);
 puts("PASS native IPv6 status remains IPv6 and never fabricates IPv4");

 reset_state();
 fire(AGPS_TYPE_C2K,GPS_REQUEST_AGPS_DATA_CONN,0,0,NULL);
 assert(g_calls==1&&g_last.addr.ss_family==AF_UNSPEC);
 puts("PASS fallback scoped to SM-T311 SUPL type only");

 reset_state();g_agps_callbacks_valid=0;
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,0,0,NULL);
 assert(g_calls==0&&g_defer_calls==0);
 puts("PASS missing registered framework callback safely ignored");

 reset_state();g_agps_callbacks_storage.status_cb=NULL;
 fire(AGPS_TYPE_SUPL,GPS_REQUEST_AGPS_DATA_CONN,0,0,NULL);
 assert(g_calls==0&&g_defer_calls==0);
 puts("PASS null framework callback safely ignored");
 return 0;
}
'''
def check(code,expect_pass):
    with tempfile.TemporaryDirectory(prefix='smt311-g396-supl-') as d:
        cc=Path(d)/'test.c'
        exe=Path(d)/'test'
        cc.write_text(HEAD+code+TAIL)
        build=subprocess.run(['cc','-std=gnu11','-O2','-Wall','-Wextra',
                              '-Werror','-pthread','-I',str(GPS),str(cc),'-o',str(exe)],
                             capture_output=True,text=True)
        if build.returncode:
            raise AssertionError('actual source does not compile: '+build.stderr[:2500])
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if expect_pass:
            if result.returncode:
                raise AssertionError('actual source real C harness failed: '+result.stderr)
            print(result.stdout.strip())
        else:
            if result.returncode == 0:
                raise AssertionError('unsafe mutation not detected by real C harness')
def main():
    check(case,True)
    changes=[
      ('missing_HIDL_callback_marker','in->sin_addr.s_addr = INADDR_NONE;','in->sin_addr.s_addr = 0;'),
      ('bad_HIDL_address_family','in->sin_family = AF_INET;','in->sin_family = AF_UNSPEC;'),
      ('send_nonzero_local_probe_ip','if (request_ipv4 == INADDR_NONE)\n                        request_ipv4 = 0;','if (request_ipv4 == INADDR_NONE)\n                        request_ipv4 = INADDR_NONE;'),
      ('wrong_callback_marker_type','payload.type == AGPS_TYPE_SUPL &&','payload.type == AGPS_TYPE_C2K &&'),
      ('drop_RELEASE_missing_IP','payload.status == GPS_RELEASE_AGPS_DATA_CONN','payload.status == 9999'),
      ('break_valid_legacy_ip_conversion','compat_ipv4_net = htonl(payload.ipaddr);','compat_ipv4_net = payload.ipaddr;'),
      ('drop_cached_server_ip','compat_ipv4_net = g_v397_supl_ipv4_net;','compat_ipv4_net = 0;'),
      ('replace_REAL_ipv4_address','memcpy(&in->sin_addr, payload.address, sizeof(in->sin_addr));','memset(&in->sin_addr, 0, sizeof(in->sin_addr));'),
      ('replace_REAL_ipv6_address','memcpy(&in6->sin6_addr, payload.address, sizeof(in6->sin6_addr));','memset(&in6->sin6_addr, 0, sizeof(in6->sin6_addr));'),
      ('allow_no_callback','if (cb && payload.type == AGPS_TYPE_SUPL &&','if (payload.type == AGPS_TYPE_SUPL &&'),
    ]
    for label,a,b in changes:
        if a not in case: raise AssertionError('missing mutation target: '+label)
        altered=case.replace(a,b,1)
        # negative compilations that no longer compile are not evidence of a runtime failure
        try:
            check(altered,False)
        except AssertionError as e:
            if 'does not compile' in str(e):
                raise
            raise
        print('PASS G396 mutant rejected:',label)
    print('G396 ACTUAL LEGACY C SUPL CALLBACK/JNI NULL MARKER 10 functional cases +10 negative mutant rejections PASS; no GPS/network calls')
if __name__=='__main__':main()
