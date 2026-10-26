/* SPDX-License-Identifier: GPL-2.0-only */
/* G413 future-only event accounting CORE, no kernel hooks, no actual ROM. */
#ifndef G413_EVENT_CORE_H
#define G413_EVENT_CORE_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef u64 uint64_t;
typedef u32 uint32_t;
typedef u16 uint16_t;
typedef u8 uint8_t;
#else
#include <stdint.h>
#include <stdbool.h>
#endif
#define G413_CPUS 2
#define G413_FREQS 14
#define G413_MAX_UIDS 4096
#define G413_UID_BUCKETS 1024
struct g413_uid_row {
    uint64_t total_ns[G413_FREQS];
    uint64_t cutoff_ns;
    uint32_t uid;
    uint16_t next_plus_one;
    uint8_t valid;
    uint8_t has_cutoff;
};
struct g413_running {
    uint32_t uid;
    uint64_t birth_ns;
    uint64_t since_ns;
    uint8_t active;
};
struct g413_core {
    struct g413_uid_row uids[G413_MAX_UIDS];
    uint16_t buckets[G413_UID_BUCKETS];
    struct g413_running cpu[G413_CPUS];
    uint64_t last_event_ns;
    uint32_t current_khz;
    uint32_t n_uid;
    uint32_t n_errors;
    uint8_t initialized;
};
extern const uint32_t g413_freq_khz[G413_FREQS];
bool g413_init(struct g413_core *c,uint32_t khz,uint64_t at_ns);
bool g413_switch(struct g413_core *c,unsigned cpu,uint64_t at_ns,
                  bool next_running,uint32_t next_uid,uint64_t task_birth_ns);
bool g413_frequency_change(struct g413_core *c,uint64_t at_ns,uint32_t new_khz);
bool g413_remove_uid(struct g413_core *c,uint64_t at_ns,uint32_t uid);
bool g413_uid_get(const struct g413_core *c,uint32_t uid,
                   uint64_t bins_ns[G413_FREQS]);
bool g413_uid_get_10ms(const struct g413_core *c,uint32_t uid,
                        uint64_t bins_10ms[G413_FREQS]);
#endif
