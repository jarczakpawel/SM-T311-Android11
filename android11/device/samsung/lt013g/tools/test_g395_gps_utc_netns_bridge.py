#!/usr/bin/env python3
"""G395 SM-T311: regression gate for UTC assistance and real shared GNSS netns.

Actual mounted runtime netns was read-only inspected, and the immutable stock
GPS XML was extracted from an existing host tar. No time/GPS/privacy-active
commands, modem changes, flash or network use.
"""
import argparse
import json
from pathlib import Path
import re
import tarfile

def require(v, msg):
    if not v:
        raise AssertionError(msg)

def source_contract(files, runtime):
    time=files['ntp']
    framework=files['framework']
    jni=files['jni']
    hal=files['hal']
    gps=files['gps_proxy']
    server=files['server']
    launcher=files['launcher']
    stock=files['stock_xml']
    header=files['bridge']
    for snippet in ('mNtpTime.forceRefresh()',
                    'mNtpTime.getCachedTimeResult()',
                    'ntpResult.getAgeMillis() < NTP_INTERVAL',
                    'mCallback.injectTime(time, timeReference, (int) certainty)'):
        require(snippet in time,'NTP to GNSS injection chain broken: '+snippet)
    for snippet in ('mNtpTimeHelper.onNetworkAvailable()',
                    'native_inject_time(time, timeReference, uncertainty)',
                    'mNtpTimeHelper.retrieveAndInjectNtpTime()'):
        require(snippet in framework,'GNSS framework does not deliver UTC: '+snippet)
    require('gnssHal->injectTime(time, timeReference, uncertainty)' in jni,
            'JNI loses GNSS HIDL UTC command')
    require('mGnssIface->inject_time(timeMs, timeReferenceMs, uncertaintyMs)' in hal,
            'GNSS HAL does not forward UTC to legacy gps_default')
    require('GPS_CAPABILITY_ON_DEMAND_TIME' in gps and
            'payload.capabilities |= GPS_CAPABILITY_ON_DEMAND_TIME' in gps,
            'GNSS capabilities hide time-assistance requirement')
    require('case LT013G_CB_REQUEST_UTC:' in gps and
            'callbacks->request_utc_time_cb()' in gps,
            'legacy request UTC never forwarded to Android11')
    require('static int proxy_inject_time(GpsUtcTime time, int64_t reference, int uncertainty)' in gps and
            'send_command(LT013G_CMD_INJECT_TIME, &payload, sizeof(payload))' in gps,
            'proxy injection no longer framed into fixed bridge command')
    for part in ('payload.time_ms = time;','payload.reference_ms = reference;',
                 'payload.uncertainty_ms = uncertainty;'):
        require(part in gps,'UTC source field is dropped: '+part)
    require('LT013G_CMD_INJECT_TIME = 15' in header and
            'struct lt013g_cmd_inject_time {' in header and
            'int64_t time_ms;' in header and
            'int64_t reference_ms;' in header and
            'int32_t uncertainty_ms;' in header,
            'wire ABI field layout changed')
    require('case LT013G_CMD_INJECT_TIME:' in server and
            'packet->payload_size < sizeof(struct lt013g_cmd_inject_time)' in server and
            'memcpy(&payload, packet->payload, sizeof(payload));' in server and
            'g_iface->inject_time(payload.time_ms, payload.reference_ms,' in server,
            'legacy API25 gpsd fails to invoke real HAL clock injection')
    require('IgnoreJniTime="true"' in stock,
            'genuine immutable Samsung Broadcom XML source not original')
    require('SuplEnable="true"' in stock and
            'SuplUseApn="false"' in stock,
            'stock Broadcom SUPL behavior inadvertently changed')
    require('applyGpsLogMode(root);' in launcher and
            'xml.replace(timePos, fromTime.size(), "IgnoreJniTime=\\"false\\"");' in launcher,
            'private GPS launcher no longer enables corrected UTC input')
    require('const std::string path = root + "/system/etc/gps.xml";' in launcher,
            'GNSS config patch no longer targets actual private chroot')
    require('!bindHostSocketIfPresent(root, "dnsproxyd")' in launcher and
            '!bindHostSocketIfPresent(root, "fwmarkd")' in launcher and
            '!bindHostSocketIfPresent(root, "netd")' in launcher,
            'GNSS jail no longer exposes Android host network resolver sockets')
    require('CLONE_NEWNET' not in launcher and
            'unshare(CLONE_NEWNET)' not in launcher and
            'setns(' not in launcher,
            'GNSS launcher was unexpectedly moved into a new network namespace')
    require(runtime['model']=='SM-T311' and runtime['boot_completed']==1 and
            runtime['selinux']=='Enforcing' and runtime['sim']=='LOADED' and
            runtime['gps_power']==0,
            'unexpected live device state; do not infer accepted GPS lock')
    net=runtime['netns']
    require(set(net)=={'init','system_server','launcher','gpsd'},
            'real runtime netns participants absent')
    require(all(re.fullmatch(r'net:\[\d+\]',x) for x in net.values()),
            'untrusted or malformed namespace output')
    require(len(set(net.values()))==1,
            'physical Android and GNSS unexpectedly split into network namespaces')
    require(runtime['gnss_fix_accepted'] is False and
            runtime['network_ntp_supl_accepted'] is False and
            runtime['g389_installed'] is False,
            'do not conflate source/routing proof with actual GPS network or fix')
    return {'status':'G395_END_TO_END_UTC_BRIDGE_SOURCE_AND_SHARED_NETNS_PASS',
            'host_system_and_gnss_share_netns':True,
            'network_ntp_supl_accepted':False,
            'native_gps_utc_injection_physically_accepted':False,
            'stable_gps3d_fix_physically_accepted':False,
            'private_position_data_collected':False,
            'firmware_or_network_modified':False}

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--android',type=Path,required=True)
    p.add_argument('--runtime',type=Path,required=True)
    a=p.parse_args()
    base=a.android
    gps=base/'device/samsung/lt013g/gps-v195'
    paths={
        'ntp':base/'frameworks/base/services/core/java/com/android/server/location/gnss/NtpTimeHelper.java',
        'framework':base/'frameworks/base/services/core/java/com/android/server/location/gnss/GnssLocationProvider.java',
        'jni':base/'frameworks/base/services/core/jni/com_android_server_location_GnssLocationProvider.cpp',
        'hal':base/'hardware/interfaces/gnss/1.0/default/Gnss.cpp',
        'gps_proxy':gps/'gps_proxy.c',
        'server':gps/'legacy_gps_server.c',
        'launcher':gps/'legacy_launcher.cpp',
        'bridge':gps/'bridge_protocol.h',
    }
    files={k:v.read_text() for k,v in paths.items()}
    with tarfile.open(gps/'legacy-rootfs.tar') as tf:
        orig=tf.extractfile('./system/etc/gps.xml')
        require(orig is not None,'OEM GPS XML missing from immutable tar')
        files['stock_xml']=orig.read().decode()
    runtime=json.loads(a.runtime.read_text())
    result=source_contract(files,runtime)
    print('PASS G395 UTC chain Android11 NTP->JNI->HIDL->proxy IPC->API25 gpsd and stock IgnoreJniTime runtime correction')
    print('PASS G395 true physical system_server/launcher/gpsd all same network namespace; GNSS still idle and route/fix pending')
    cases=[
        ('ntp_cache_or_response_missing','ntp','mNtpTime.getCachedTimeResult()','mNtpTime.getCachedUnsupportedResult()'),
        ('ntp_stale_cached_result','ntp','ntpResult.getAgeMillis() < NTP_INTERVAL','ntpResult.getAgeMillis() > NTP_INTERVAL'),
        ('drop_ntp_inject_cb','ntp','mCallback.injectTime(time, timeReference, (int) certainty)','mCallback.injectUnsupportedTime()'),
        ('drop_framework_net_notif','framework','mNtpTimeHelper.onNetworkAvailable()','mNtpTimeHelper.onNetworkUnsupported()'),
        ('drop_framework_native_inject','framework','native_inject_time(time, timeReference, uncertainty)','native_unsupported(time, timeReference, uncertainty)'),
        ('drop_native_HIDL_call','jni','gnssHal->injectTime(time, timeReference, uncertainty)','gnssHal->injectUnsupportedTime(time, timeReference, uncertainty)'),
        ('drop_HIDL_legacy_call','hal','mGnssIface->inject_time(timeMs, timeReferenceMs, uncertaintyMs)','mGnssIface->inject_unsupported(timeMs, timeReferenceMs, uncertaintyMs)'),
        ('mask_ondemand_capability','gps_proxy','payload.capabilities |= GPS_CAPABILITY_ON_DEMAND_TIME','payload.capabilities &= ~GPS_CAPABILITY_ON_DEMAND_TIME'),
        ('drop_legacy_utc_callback','gps_proxy','callbacks->request_utc_time_cb()','callbacks->unknown_time_cb()'),
        ('proxy_time_units_wrong','gps_proxy','payload.time_ms = time;','payload.time_ms = time / 1000;'),
        ('proxy_uncertainty_dropped','gps_proxy','payload.uncertainty_ms = uncertainty;','payload.uncertainty_ms = 0;'),
        ('broken_wire_command','bridge','LT013G_CMD_INJECT_TIME = 15','LT013G_CMD_INJECT_TIME = 99'),
        ('broken_native_api25','server','g_iface->inject_time(payload.time_ms, payload.reference_ms,','g_iface->unknown_time(payload.time_ms, payload.reference_ms,'),
        ('remove_broadcom_jni_time_fix','launcher','xml.replace(timePos, fromTime.size(), "IgnoreJniTime=\\"false\\"");','xml.replace(timePos, fromTime.size(), "IgnoreJniTime=\\"true\\"");'),
        ('broadcom_jailed_wrong_config','launcher','root + "/system/etc/gps.xml"','root + "/system/etc/gps_inactive.xml"'),
        ('missing_dns_socket','launcher','!bindHostSocketIfPresent(root, "dnsproxyd")','!bindHostSocketIfPresent(root, "nonexistent")'),
        ('unexpected_new_netns','launcher','if (chroot(root.c_str()) != 0','if (unshare(CLONE_NEWNET) || chroot(root.c_str()) != 0'),
        ('stock_XML_jni_wrong','stock_xml','IgnoreJniTime="true"','IgnoreJniTime="false"'),
    ]
    for label,key,old,new in cases:
        require(old in files[key], 'synthetic mutation not applicable '+label)
        clone=dict(files)
        clone[key]=files[key].replace(old,new)
        try:source_contract(clone,runtime)
        except AssertionError:print('PASS G395 rejects wrong GNSS UTC path:',label)
        else:raise AssertionError('FAIL OPEN '+label)
    broken=json.loads(json.dumps(runtime))
    broken['netns']['gpsd']='net:[4026532111]'
    try:source_contract(files,broken)
    except AssertionError:print('PASS G395 rejects separate GNSS network namespace')
    else:raise AssertionError('FAIL OPEN GNSS net namespace')
    print('G395 GPS ASSISTANCE UTC+NETNS SOURCE/REAL DEVICE CONTRACT PASS: 19/19 fail-closed mutants, no GNSS/network state changed')
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':main()
