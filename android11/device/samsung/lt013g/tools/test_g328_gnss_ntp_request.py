#!/usr/bin/env python3
"""Test actual Android11 GPS proxy capability and request-UTC switch cases."""
from pathlib import Path
import subprocess
import tempfile
src=(Path(__file__).resolve().parents[1]/"gps-v195/gps_proxy.c").read_text()
a=src.index("        case LT013G_CB_CAPABILITIES:")
b=src.index("        case LT013G_CB_WAKELOCK_ACQUIRE:",a)
caps=src[a:b]
a=src.index("        case LT013G_CB_REQUEST_UTC:")
b=src.index("        case LT013G_CB_GPSD_EVENT:",a)
request=src[a:b]
assert "payload.capabilities |= GPS_CAPABILITY_ON_DEMAND_TIME" in caps
assert "callbacks->request_utc_time_cb()" in request
head=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define GPS_CAPABILITY_ON_DEMAND_TIME 0x10
#define GPS_CAPABILITY_MSB 0x2
#define GPS_CAPABILITY_MSA 0x4
#define LT013G_CB_CAPABILITIES 8
#define LT013G_CB_WAKELOCK_ACQUIRE 9
#define LT013G_CB_WAKELOCK_RELEASE 10
#define LT013G_CB_REQUEST_UTC 11
#define LT013G_CB_GPSD_EVENT 12
#define ALOGI(...) do {} while (0)
struct lt013g_cb_capabilities{uint32_t capabilities;};
struct packet{uint32_t type,payload_size;unsigned char payload[32];};
struct callbacks{
 void (*set_capabilities_cb)(uint32_t);
 void (*request_utc_time_cb)(void);
};
static uint32_t g_caps;static int g_calls,g_request;
static void capture(uint32_t x){g_caps=x;++g_calls;}
static void utc(void){++g_request;}
static void dispatch(const struct packet *packet, const struct callbacks *callbacks){
 switch(packet->type){
'''
mid=r'''
 default:break;
 }
}
int main(void){
 struct callbacks cbs={.set_capabilities_cb=capture,.request_utc_time_cb=utc};
 struct packet pkt={0};
 struct lt013g_cb_capabilities caps={.capabilities=0x6};
 pkt.type=LT013G_CB_CAPABILITIES;pkt.payload_size=sizeof(caps);
 memcpy(pkt.payload,&caps,sizeof(caps));
 dispatch(&pkt,&cbs);
 assert(g_caps==0x16 && g_calls==1);
 puts("PASS Android GNSS original MSB+MSA capabilities gain ON_DEMAND_TIME bit0x10");
 caps.capabilities=0x16;
 memcpy(pkt.payload,&caps,sizeof(caps));
 dispatch(&pkt,&cbs);
 assert(g_caps==0x16 && g_calls==2);
 puts("PASS repeated capability notification does not change existing time bit");
 pkt.type=LT013G_CB_REQUEST_UTC;pkt.payload_size=0;
 dispatch(&pkt,&cbs);
 assert(g_request==1);
 puts("PASS legacy GPS UTC assistance request triggers Android framework callback");
 cbs.set_capabilities_cb=NULL;cbs.request_utc_time_cb=NULL;
 pkt.type=LT013G_CB_CAPABILITIES;
 dispatch(&pkt,&cbs);
 pkt.type=LT013G_CB_REQUEST_UTC;
 dispatch(&pkt,&cbs);
 assert(g_calls==2 && g_request==1);
 puts("PASS absent framework callbacks cause no fabricated time request");
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="g328-ntp-request-") as t:
 p=Path(t)/"test.c";exe=Path(t)/"test"
 p.write_text(head+caps+request+mid)
 subprocess.run(["cc","-std=gnu11","-O2","-Wall","-Wextra","-Werror",
                 str(p),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
