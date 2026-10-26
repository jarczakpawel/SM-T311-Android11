# SM-T311 G665 - XArray cached node / RCU retirement status

Kernel commits `f74264499`, `4766c365a` are source-only diagnostics. `tools/testing/selftests/smt311/rcu_xarray_node_g665.py` verifies 20 actual local-source anchor conditions (20 PASS) with five mutated-source negative control rejections. It distinguishes `xas_load` walking the current root from `xas_reload` accessing a cached node slot. It traces `xas_shrink` (XA_RETRY_ENTRY old slot), `xas_delete_node` (parent slot NULL), `xas_free_nodes` (XA_RETRY_ENTRY before RCU retirement) and `xa_node_free` (call_rcu).

A real XArray userspace regression source was added at `tools/testing/radix-tree/smt311-xas-g665.c`, with Makefile target `smt311-xas-g665`. **It has not been run**: the host is missing `liburcu` development headers (`urcu.h` and `urcu/uatomic.h`). This is a host build dependency failure, not a PASS and not a kernel regression result. Install the ordinary system liburcu dev package where available, then run the target and evaluate errors.

No ARMv7 no-ref lifetime proof, inode/mapping identity proof or full multithreaded XArray node generation proof. Stages S0 PASS; S1 ACTIVE; S2 BLOCKED; S3 BLOCKED; S4 BLOCKED; S5 BLOCKED; S6 BLOCKED; S7 PARTIAL.

No Android runtime modifications, no device flash. Physical G650 Android 11 Linux 7.3.0-pjarczak RIL running/Enforcing, reference-free RCU default OFF, last BOOT SHA `2d477816fef0dbe2f41ca30747201dbe773e3dc3e710348c118226ee2999b7a3`. Owner alone will publish to GitHub. No backup/PIN automation/audio/camera.
