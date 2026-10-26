#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
python3 tools/test_g323_gnss_callback_dispatch.py
python3 tools/test_g329_nmea_empty_gsv.py
python3 tools/test_g586_gnss_empty_native_status.py
python3 tools/test_g397_supl_highfd_asan.py
python3 tools/test_g403_supl_bounded_close_retry.py
echo G608_GNSS_HOST_ACCEPTANCE_PASS
