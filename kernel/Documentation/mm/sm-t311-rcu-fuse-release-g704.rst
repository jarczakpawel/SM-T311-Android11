G704 - FUSE passthrough diagnostic release decision
=================================================

Samsung SM-T311, Linux 7.3.0-pjarczak, Android 11, 2026-10-21.

Decision: do not add a separate Kconfig switch just to restore the
optional passthrough_successful_opens counter in RCU-OFF G700.
Keep the G700 RCU-OFF + DEBUG_FS configuration unchanged.

The actual G686 and G700 ARM32 host links show that the conditional
counter expands the internal struct fuse_conn layout, adds one
atomic64 update per successful passthrough open and adds the
passthrough_opens entry under fusectl. It is not the same as the
existing passthrough support, which remains compiled in both builds.
The G700 source omits the optional diagnostic but retains FUSE
passthrough and DEBUG_FS. This is an intentional difference, not
evidence of a runtime failure.

Importantly, fuse_conn_init is exported via EXPORT_SYMBOL_GPL and
__ksymtab_fuse_conn_init exists in both linked ELF files. Both
configs allow modules (CONFIG_MODULES=y) without MODVERSIONS.
Neither host build has a .ko file and the last physical G650 module
snapshot found none loaded. This does not prove the layout/API
compatibility of third-party modules built for another configuration.
Such a module must be assessed against its matching kernel build;
do not infer ABI safety from 26742 matching symbol names.

The offline release check reads existing source, Kconfig, ELF exports
and pinned BOOT only; it neither compiles nor uses ADB::

  python3 tools/testing/selftests/smt311/rcu_fuse_release_contract_g704.py

12/12 new static/config/ELF checks passed. Machine-readable report:
Documentation/mm/sm-t311-rcu-fuse-release-g704.json.

Release gates remain unchanged. G700 host BOOT SHA256 is
aec9655acc727ab01d0bde3711414f9606c58c5fdcafb6ae5c52a2d3346edc7f.
It is NOT installed or authorized for unattended flash. Current
physical BOOT p9 and RECOVERY p10 hashes and tested restoration
remain unresolved. No new executable C code, TWRP boot, backups,
sound tests, old regression replays, or physical changes in G704.

S0 PASS, S1 ACTIVE, S2-S6 BLOCKED, S7 PARTIAL, S8 ACTIVE.
Next G705: audit standalone kernel-module ABI exposure and FUSE
integration without guessing about third-party modules; continue
RCU safety S1-S3 and physical BOOT/RECOVERY S6 separately.
