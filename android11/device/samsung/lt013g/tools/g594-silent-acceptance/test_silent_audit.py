from pathlib import Path
import re
source=(Path(__file__).parent/'readonly_sm_t311.sh').read_text()
assert source.startswith('#!/system/bin/sh')
assert 'G594_BEGIN' in source and 'G594_END' in source
for prohibited in (r'\bpm install\b',r'\bpm uninstall\b',
                   r'\bsetprop\b',r'\bsettings put\b',
                   r'\bsvc usb set',r'\binput tap\b',
                   r'\bam start\b',r'\breboot\b',r'\bkill -',
                   r'\bservice call\b',r'\bsu -c\b',
                   r'\bdumpsys media.camera\b',r'\bplay\b',
                   r'\bservice call phone\b'):
    # Comments contain human prose; test only executable lines.
    body='\n'.join(line for line in source.splitlines() if not line.lstrip().startswith('#'))
    assert not re.search(prohibited,body),prohibited
assert not re.search(r'^\s*(?:rm|dd|mount|umount|printf .*>)\s',body,re.M)
assert source.count('echo BOOT_COMPLETED')==1
assert source.count('echo SYSTEM_VENDOR_FINGERPRINT_MATCH')==2
assert source.count('echo TRUSTED_WIFI_AP_COUNT')==1
assert source.count('echo DWC2_ENDPOINT_STOP_TIMEOUT_COUNT')==1
assert 'kernel="$(logcat' not in source
assert 'crash="$(logcat' not in source
print('G594_READONLY_NO_CAMERA_NO_AUDIO_NO_UI_NO_MODEM_ACTIONS_PASS')

assert source.count('echo CPUFREQ_TOTAL_TRANSITIONS') == 1
assert source.count('echo CPUFREQ_200MHZ_TIME_TICKS') == 1
assert source.count('echo CPUFREQ_1400MHZ_TIME_TICKS') == 1
print('G594_CPUFREQ_TRANSITION_COUNTERS_READONLY_PASS')
