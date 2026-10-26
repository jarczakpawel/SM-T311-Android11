import json
from pathlib import Path
j=json.loads((Path(__file__).parent/"gnss_desk_retest.json").read_text())
a=j["baseline_2026_10_16"];b=j["retest_2026_10_20"]
assert a["duration_seconds"] == b["duration_seconds"] == 70
assert (a["max_cn0_dbhz"],b["max_cn0_dbhz"]) == (32,10)
assert (a["max_satellites_above_25dbhz"],b["max_satellites_above_25dbhz"]) == (2,0)
assert a["fixes"] == b["fixes"] ==0
assert b["hal_started"] and b["returned_to_idle"] and b["test_app_uninstalled"]
assert b["sv_events"] ==115 and b["max_visible"] ==11
assert "NOT 11 RF-strength" in j["conclusion"]
assert "No location coordinates" in j["privacy"]
print("G583_GNSS_SANITIZED_SOURCE_METRICS_PASS")
print("G583_GNSS_HAL_CALLBACKS_AND_IDLE_PASS")
print("G583_GNSS_RF_STRENGTH_REGRESSION_SUSPECTED_NOT_PROVEN")
print("G583_NO_FIX_REQUIRED_INDOORS")

c=j["retest_2026_10_20_positive_signal_counter"]
assert c["status_callbacks"] == 90
assert c["max_sv_entries"] == 12
assert c["max_satellites_with_positive_cn0"] == 3
assert c["max_cn0_dbhz"] == 11
assert c["max_satellites_at_least_25dbhz"] == 0
assert c["fixes"] == 0 and c["gps_idle_after_test"] and c["probe_app_uninstalled"]
print("G585_GNSS_NONZERO_CNO_PHYSICAL_RESULTS_PASS")
