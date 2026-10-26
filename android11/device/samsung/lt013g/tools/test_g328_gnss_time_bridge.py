#!/usr/bin/env python3
"""Compile both ACTUAL GPS inject_time bridge halves and verify wire/return ABI.

Synthetic UTC samples only; does not use real satellites, NTP network, GPS
receiver, Android property or live tablet. The legacy server's original
LT013G_CMD_INJECT_TIME switch case is extracted unmodified from source.
"""
from pathlib import Path
import subprocess
import tempfile

base=Path(__file__).resolve().parents[1]/"gps-v195"
proxy=(base/"gps_proxy.c").read_text()
server=(base/"legacy_gps_server.c").read_text()
a=proxy.index("static int proxy_inject_time(")
b=proxy.index("static int proxy_inject_location(",a)
client_code=proxy[a:b]
a=server.index("        case LT013G_CMD_INJECT_TIME:")
b=server.index("        case LT013G_CMD_INJECT_LOCATION:",a)
server_code=server[a:b]
assert "return send_command(LT013G_CMD_INJECT_TIME" in client_code
assert "g_iface->inject_time(payload.time_ms, payload.reference_ms" in server_code
assert 'V359_TIME_INJECT' in server_code

head=r'''
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int64_t GpsUtcTime;
enum { LT013G_CMD_INJECT_TIME=15, LT013G_CMD_INJECT_LOCATION=16 };
struct lt013g_cmd_inject_time {
    int64_t time_ms;
    int64_t reference_ms;
    int32_t uncertainty_ms;
};
struct lt013g_bridge_packet {
    uint32_t type;
    uint32_t payload_size;
    uint8_t payload[128];
};
struct gps_interface {
    int (*inject_time)(GpsUtcTime,int64_t,int);
};
static pthread_mutex_t g_hal_lock=PTHREAD_MUTEX_INITIALIZER;
static struct gps_interface *g_iface;
static struct lt013g_bridge_packet g_packet;
static int g_return, g_engine_calls;
static int64_t g_time_seen, g_ref_seen;
static int g_unc_seen, g_logged;
static void log_line(const char *fmt,...){
    va_list ap;char output[256];va_start(ap,fmt);
    vsnprintf(output,sizeof(output),fmt,ap);va_end(ap);
    assert(strstr(output,"V359_TIME_INJECT")!=NULL);
    ++g_logged;
}
static int legacy_handle_packet(const struct lt013g_bridge_packet *packet){
    int rc=0;
    switch(packet->type){
'''
mid=r'''
    default: return -EINVAL;
    }
}
static int send_command(uint32_t type,const void *data,uint32_t size){
    g_packet.type=type;
    g_packet.payload_size=size;
    assert(size<=sizeof(g_packet.payload));
    memcpy(g_packet.payload,data,size);
    return legacy_handle_packet(&g_packet);
}
'''
tail=r'''
static int engine_inject(GpsUtcTime t,int64_t ref,int uncertainty){
    ++g_engine_calls;
    g_time_seen=t;g_ref_seen=ref;g_unc_seen=uncertainty;
    return g_return;
}
int main(void){
 struct gps_interface iface={.inject_time=engine_inject};
 int rc;
 g_iface=&iface;
 // Epoch 2026-10-16 01:47 UTC, Android time and boot-monotonic reference
 const int64_t utc=1792115222000LL;
 const int64_t reference=1442342;
 rc=proxy_inject_time(utc,reference,81);
 assert(rc==0 && g_packet.payload_size==sizeof(struct lt013g_cmd_inject_time));
 assert(g_time_seen==utc && g_ref_seen==reference && g_unc_seen==81);
 assert(g_engine_calls==1 && g_logged==1);
 puts("PASS Android UTC milliseconds/reference/uncertainty passed to proprietary HAL ABI");

 g_return=-EIO;
 rc=proxy_inject_time(utc+1000,reference+1000,36);
 assert(rc==-EIO && g_engine_calls==2 && g_logged==2);
 assert(g_time_seen==utc+1000 && g_ref_seen==reference+1000);
 puts("PASS original vendor negative rc propagates intact to Android proxy");

 g_iface=NULL;
 rc=proxy_inject_time(utc,reference,10);
 assert(rc==-ENOSYS && g_engine_calls==2 && g_logged==3);
 puts("PASS proprietary GPS interface absent -> ENOSYS, no synthetic acceptance");

 g_packet.type=LT013G_CMD_INJECT_TIME;
 g_packet.payload_size=sizeof(struct lt013g_cmd_inject_time)-1;
 rc=legacy_handle_packet(&g_packet);
 assert(rc==-EINVAL && g_engine_calls==2 && g_logged==3);
 puts("PASS truncated bridge time payload rejected before legacy interface call");

 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="g328-gnss-time-") as td:
    source=Path(td)/"bridge.c";binary=Path(td)/"test"
    source.write_text(head+server_code+mid+client_code+tail)
    subprocess.run(["cc","-std=gnu11","-O2","-Wall","-Wextra","-Werror",
                    "-pthread",str(source),"-o",str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
