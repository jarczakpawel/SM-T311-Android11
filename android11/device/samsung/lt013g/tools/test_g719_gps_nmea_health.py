#!/usr/bin/env python3
"""G719 NMEA AWK decision tests; output must NEVER include coordinates."""
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
AWK=ROOT/"tools/g719_gps_nmea_health.awk"
def analyze(lines):
    proc=subprocess.run(["awk","-f",str(AWK)],input=lines,text=True,
                        capture_output=True,check=True)
    d={}
    for line in proc.stdout.splitlines():
        k,v=line.split("=",1);d[k]=v
    return d,proc.stdout

no_fix = ("1711111111000,$GPGSV,3,1,11,09,83,335,,04,53,069,,07,46,190,,06,45,248,*75\n"
          "1711111112000,$GPGSV,3,2,11,11,37,295,,16,21,077,,03,20,129,,21,20,304,*7B\n"
          "1711111113000,$GPGSV,3,3,11,26,15,041,,30,13,208,,29,01,355,*4E\n"
          "1711111114000,$GPGSA,A,1,,,,,,,,,,,,,140.0,99.0,99.0*35\n"
          "1711111115000,$GPGGA,072359.88,4932.1234,N,02130.1234,E,0,00,99.0,,,,,,*22\n"
          "1711111116000,$GPRMC,072359.88,V,4932.1234,N,02130.1234,E,,,211026,,,N*20\n")
good=("1711111111000,$GPGSV,1,1,04,09,83,335,34,04,53,069,28,07,46,190,29,06,45,248,35*00\n"
      "1711111112000,$GPGSA,A,3,09,04,07,06,,,,,,,,,1.0,0.8,0.6*00\n"
      "1711111113000,$GPGGA,072359.88,4932.1234,N,02130.1234,E,1,04,0.9,300.0,M,,M,,*00\n"
      "1711111114000,$GPRMC,072359.88,A,4932.1234,N,02130.1234,E,,,211026,,,A*00\n")
cases=[
("NO_TRACKED_SATELLITES",no_fix,"NO_FIX_NO_TRACKED_SIGNAL_IN_BUFFERED_SAMPLE","0"),
("VALID_SATELLITES",good,"NMEA_POSITION_VALID","4"),
("EMPTY_BUFFER","","INSUFFICIENT_NMEA","0"),
("ONLY_GSV",no_fix.split("1711111114000")[0],"INSUFFICIENT_NMEA","0"),
("ZERO_CN0",no_fix.replace("335,,","335,00,"),"NO_FIX_NO_TRACKED_SIGNAL_IN_BUFFERED_SAMPLE","0")
]
for name,sample,verdict,tracked in cases:
    d,out=analyze(sample)
    assert d["VERDICT"]==verdict,(name,d)
    assert d["CN0_POSITIVE_ENTRIES"]==tracked,(name,d)
    assert "4932." not in out and "02130." not in out,name
    assert "G719_PRIVACY_SAFE_NMEA" in out
    print(name,"PASS")
print("G719 5/5 synthetic GNSS RF-and-fix scenarios PASS; coordinates never printed")
