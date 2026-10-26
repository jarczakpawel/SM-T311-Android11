# G583 passive SM-T311 post-boot acceptance

The shell script is read-only and privacy-limited. It runs as Android shell
without root, writes, network calls, GPS coordinates, user content or raw logs.
Launch from ADB as sh /data/local/tmp/g583_readonly_audit.sh.

The GNSS mStarted=false and zero satellite events do NOT prove failure when
the tablet is on an indoor desk and no app requests position. Enumeration
is not proof of working photos, sensor measurements or stable GPU load.
Fingerprint mismatch means the old Android ROM is still running.
Do not push machine-specific raw output to public Git.
