#!/usr/bin/env python3
"""G386: fail-closed SM-T311 GPS UTC/SUPL transport observation gate.

This is an *observation*, not a claim that the mobile modem, DNS server,
antenna, stock gpsd or SELinux is definitively defective. Android uses
per-network fwmark routing; a root shell has different network binding than
NtpTrustedTime's active Network. No private IP, IMSI, NMEA or coordinates.
"""
from pathlib import Path
import argparse
import json
import re

ALLOWED = {
    "device", "boot", "selinux", "sim_state", "gnss_legacy", "gps_power",
    "gps_capabilities", "ignore_jni_time", "ntp_cache", "ntp_errors_buffer",
    "mobile_data_setting", "airplane_setting", "data_connection_state",
    "rmnet1_ipv4_addr_count", "ipv4_nondummy_default_routes",
    "ipv6_nondummy_default_routes", "netd",
    "ntp_probe_time_android_com", "ntp_probe_time_cloudflare_com",
    "ntp_probe_selftests",
}
def check(ok, reason):
    if not ok: raise ValueError(reason)

def inspect(snapshot, ntp_helper, trusted_time, gps_proxy, gps_launcher, config):
    lines=snapshot.strip().splitlines()
    check(lines[0]=="G386_SNAPSHOT_BEGIN" and lines[-1]=="G386_SNAPSHOT_END",
          "missing bounded privacy-safe snapshot markers")
    fields={}
    for item in lines[1:-1]:
        check(item.count("=")==1,"unstructured/private diagnostic field")
        key,value=item.split("=",1)
        check(key in ALLOWED and key not in fields, "unexpected or duplicate field")
        check(re.fullmatch(r"[A-Za-z0-9_.-]+",value) is not None,
              "sensitive or unbounded value rejected")
        fields[key]=value
    check(set(fields)==ALLOWED,"incomplete GNSS diagnostics")
    for key, expected in (
        ("device","lt013g"),("boot","1"),("selinux","Enforcing"),
        ("sim_state","LOADED"),("gnss_legacy","running"),("gps_power","0"),
        ("gps_capabilities","0x16"),("ignore_jni_time","false"),
        ("ntp_cache","null"),("mobile_data_setting","1"),
        ("airplane_setting","0"),("data_connection_state","2"),
        ("rmnet1_ipv4_addr_count","1"),("ipv4_nondummy_default_routes","0"),
        ("ipv6_nondummy_default_routes","0"),("netd","running"),
        ("ntp_probe_time_android_com","DNS_FAILED"),
        ("ntp_probe_time_cloudflare_com","DNS_FAILED"),
        ("ntp_probe_selftests","7"),
    ):
        check(fields[key]==expected,"unexpected physical state: "+key)
    check(1<=int(fields["ntp_errors_buffer"])<=999,
          "actual GNSS NTP failures not observed")
    check("GPS_CAPABILITY_ON_DEMAND_TIME" in gps_proxy and
          "callbacks->set_capabilities_cb(payload.capabilities)" in gps_proxy,
          "GPS ON_DEMAND_TIME bridge missing")
    check('IgnoreJniTime=\\\"true\\\"' in gps_launcher and
          'IgnoreJniTime=\\\"false\\\"' in gps_launcher,
          "private Broadcom time compatibility fix missing")
    check("mNtpTime.forceRefresh()" in ntp_helper and
          'Log.e(TAG, "requestTime failed")' in ntp_helper,
          "Android NtpTimeHelper failure path changed")
    check("getActiveNetwork()" in trusted_time and
          "client.requestTime(serverName, timeoutMillis, network)" in trusted_time,
          "Android binds NTP requests to framework network")
    check('<string translatable="false" name="config_ntpServer">time.android.com</string>' in config,
          "system default NTP reference changed")
    return {
        "result":"PASS_G386_PHYSICAL_NTP_NETWORK_ASSISTANCE_DIAGNOSIS",
        "physical_device":"SM-T311 / lt013g",
        "live_os":"G379, boot1, SELinux Enforcing, SIM LOADED",
        "GNSS_capabilities":"ON_DEMAND_TIME enabled 0x16",
        "BCM4752_private_IgnoreJniTime":False,
        "NtpTimeHelper_failure_count_in_buffer":int(fields["ntp_errors_buffer"]),
        "NetworkTimeUpdateService_NTP_cache":"null",
        "Android_mobile_data_state":"CONNECTED(2)_as_reported_by_TelephonyRegistry",
        "rmnet1_has_ipv4":True,
        "non_dummy_ipv4_default_routes_seen":0,
        "non_dummy_ipv6_default_routes_seen":0,
        "root_UID_SnTP_probe_time_android_com":"DNS_FAILED",
        "root_UID_SnTP_probe_time_cloudflare_com":"DNS_FAILED",
        "gnss_gps_power_after_test":0,
        "underlying_modem_dhcp_DNS_issue_proven":False,
        "framework_NTP_network_binding_tested":False,
        "root_shell_results_prove_all_apps_unreachable":False,
        "GPS_position_fix_proven":False,
        "NTP_server_changed":False,
        "network_settings_changed":False,
        "cellular_reconnect_triggered":False,
        "notes":"Strong correlation: NTP failure coincides with no non-dummy default route. Android can use per-Network routing; avoid pretending proof of an individual provider outage."
    }

def main():
    a=argparse.ArgumentParser()
    a.add_argument("--snapshot", type=Path, required=True)
    a.add_argument("--aosp", type=Path, required=True)
    a.add_argument("--device", type=Path, required=True)
    a.add_argument("--json-output",type=Path)
    p=a.parse_args()
    snap=p.snapshot.read_text()
    base=p.aosp/"frameworks/base"
    args=( (base/"services/core/java/com/android/server/location/gnss/NtpTimeHelper.java").read_text(),
           (base/"core/java/android/util/NtpTrustedTime.java").read_text(),
           (p.device/"gps-v195/gps_proxy.c").read_text(),
           (p.device/"gps-v195/legacy_launcher.cpp").read_text(),
           (base/"core/res/res/values/config.xml").read_text())
    report=inspect(snap,*args)
    bad={
        "wrong_model":snap.replace("device=lt013g","device=lt02",1),
        "wrong_time_capabilities":snap.replace("gps_capabilities=0x16","gps_capabilities=0x6",1),
        "jni_time_disabled_again":snap.replace("ignore_jni_time=false","ignore_jni_time=true",1),
        "ntp_cache_not_empty":snap.replace("ntp_cache=null","ntp_cache=valid",1),
        "no_ntp_failures":re.sub(r"ntp_errors_buffer=\d+","ntp_errors_buffer=0",snap),
        "fake_route_present":snap.replace("ipv4_nondummy_default_routes=0","ipv4_nondummy_default_routes=1",1),
        "DNS_probe_succeeded":snap.replace("ntp_probe_time_android_com=DNS_FAILED","ntp_probe_time_android_com=OK",1),
        "unrecognized_location_field":snap.replace("G386_SNAPSHOT_END","latitude=50.0\nG386_SNAPSHOT_END"),
    }
    for name,corrupt in bad.items():
        try: inspect(corrupt,*args)
        except ValueError: continue
        raise AssertionError("G386 negative test FAIL OPEN: "+name)
    report["negative_fixture_controls_passed"]=len(bad)
    if p.json_output:
        p.json_output.parent.mkdir(parents=True,exist_ok=True)
        p.json_output.write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps(report,indent=2))

if __name__=="__main__": main()
