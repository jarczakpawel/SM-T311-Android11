#!/usr/bin/env python3
"""Compile ACTUAL GSV/GSA C parsing code and test GNSS zero-satellite reports.

No fabricated coordinates, receiver access, modem traffic or device writes.
"""
from pathlib import Path
import subprocess
import tempfile

source=(Path(__file__).resolve().parents[1]/"gps-v195/gps_proxy.c").read_text()
prefix=r'''#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define LT013G_BRIDGE_NMEA_MAX 960u
#define GNSS_MAX_SVS 64
#define GNSS_CONSTELLATION_GPS 1
#define GNSS_CONSTELLATION_SBAS 2
#define GNSS_CONSTELLATION_GLONASS 3
#define GNSS_CONSTELLATION_QZSS 4
#define GNSS_SV_FLAGS_USED_IN_FIX 4
#define ALOGI(...) ((void)0)
typedef uint8_t GnssConstellationType;
typedef struct {size_t size;int16_t svid;GnssConstellationType constellation;float c_n0_dbhz,elevation,azimuth;uint8_t flags;} GnssSvInfo;
typedef struct {size_t size;int num_svs;GnssSvInfo gnss_sv_list[GNSS_MAX_SVS];} GnssSvStatus;
typedef void (*gnss_sv_status_callback)(GnssSvStatus*);
struct lt013g_cb_nmea {int64_t timestamp_ms;uint32_t length;char data[LT013G_BRIDGE_NMEA_MAX];};
'''

a=source.index("#define V308_GSV_TALKERS")
b=source.index("/* The BCM4752 in this tablet still emits RMC dates",a)
parser=source[a:b]
helper=r'''static GnssSvStatus observed;
static unsigned calls;
static void capture(GnssSvStatus* s) {observed=*s;++calls;}
static void send_nmea(const char *base) {
 struct lt013g_cb_nmea p={0};unsigned sum=0;
 for(const char *q=base+1;*q;q++)sum^=(unsigned char)*q;
 p.length=(uint32_t)snprintf(p.data,sizeof(p.data),"%s*%02X\r\n",base,sum);
 assert(p.length<sizeof(p.data));v312_gsa_from_nmea(&p);v308_gsv_from_nmea(&p,capture);
}
'''

main=r'''
int main(void) {
 unsigned prior;
 send_nmea("$GPGSV,1,1,02,07,45,100,25,12,31,130,20");
 assert(calls==1 && observed.num_svs==2);
 assert(observed.gnss_sv_list[0].constellation==GNSS_CONSTELLATION_GPS);
 send_nmea("$GLGSV,1,1,02,80,12,338,21,87,20,011,28");
 assert(calls==2 && observed.num_svs==4);
 puts("PASS mixed GPS+GLONASS valid GSV callback produces 4 real-derived satellite entries");

 prior=calls;
 send_nmea("$GPGSV,1,1,00");
 assert(calls==prior+1 && observed.num_svs==2);
 for(int i=0;i<observed.num_svs;i++)
  assert(observed.gnss_sv_list[i].constellation==GNSS_CONSTELLATION_GLONASS);
 assert(g_v308_gsv[0].complete_count==0);
 puts("PASS valid GPGSV 00 clears GPS only and retains fresh GLONASS");

 prior=calls;
 send_nmea("$GLGSV,1,1,00");
 assert(calls==prior+1 && observed.num_svs==0);
 assert(g_v308_gsv[1].complete_count==0);
 puts("PASS valid GLGSV 00 clears final constellation and delivers zero satellites");

 send_nmea("$GPGSV,1,1,01,09,40,155,18");
 assert(observed.num_svs==1 && observed.gnss_sv_list[0].svid==9);
 send_nmea("$GLGSV,1,1,02,80,12,338,21,87,20,011,28");
 assert(observed.num_svs==3);
 send_nmea("$GLGSV,1,1,00");
 assert(observed.num_svs==1 && observed.gnss_sv_list[0].svid==9);
 puts("PASS GLGSV 00 clears GLONASS only and retains fresh GPS");

 prior=calls;
 send_nmea("$GLGSV,2,1,00");
 assert(calls==prior && observed.num_svs==1);
 send_nmea("$GPGSV,1,2,00");
 assert(calls==prior && observed.num_svs==1);
 puts("PASS impossible multi-page and out-of-range zero-SV GSV rejected");

 v314_native_sv_received();
 prior=calls;
 send_nmea("$GPGSV,1,1,00");
 assert(calls==prior);
 g_v314_native_sv_at_ms-=V314_NATIVE_SV_FRESH_MS + 1;
 send_nmea("$GPGSV,1,1,00");
 assert(calls==prior+1);
 puts("PASS native SV callback retains freshness precedence; NMEA fallback resumes");

 send_nmea("$GPGSV,1,1,01,193,33,125,24");
 assert(observed.num_svs==1);
 assert(observed.gnss_sv_list[0].svid==193);
 assert(observed.gnss_sv_list[0].constellation==GNSS_CONSTELLATION_QZSS);
 prior=calls;
 send_nmea("$QZGSV,1,1,01,02,44,110,28");
 assert(calls==prior+1 && observed.num_svs==2);
 assert(observed.gnss_sv_list[1].svid==194);
 send_nmea("$GQGSV,1,1,01,193,55,125,29");
 assert(observed.num_svs==1);
 assert(observed.gnss_sv_list[0].svid==193);
 assert(observed.gnss_sv_list[0].c_n0_dbhz==29.0f);
 puts("PASS GPS/QZSS duplicate PRN merges and local QZSS IDs map to GNSS");

 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="g329-gsv-empty-") as td:
    code=Path(td)/"test.c";exe=Path(td)/"test"
    code.write_text(prefix+parser+helper+main)
    subprocess.run(["cc","-std=gnu11","-O2","-Wall","-Wextra",
                    "-Werror", "-Wno-unused-function", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
                    str(code),"-o",str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
