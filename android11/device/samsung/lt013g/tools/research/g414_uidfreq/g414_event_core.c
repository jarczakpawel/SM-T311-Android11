/* SPDX-License-Identifier: GPL-2.0-only */
/* Validated arithmetic driven by *caller supplied ordered* scheduler switch,
 * CPU frequency transition and UID purge events.
 *
 * This is NOT yet Linux runtime code. No tracepoint callbacks are hooked.
 * Time is exact only IF events are captured at actual hardware transitions.
 */
#include "g413_event_core.h"
#ifdef __KERNEL__
#include <linux/string.h>
#include <linux/limits.h>
#include <linux/math64.h>
#ifndef UINT64_MAX
#define UINT64_MAX (~(uint64_t)0)
#endif
#else
#include <string.h>
#include <limits.h>
#endif
const uint32_t g413_freq_khz[G413_FREQS]={
  200000,300000,400000,500000,600000,700000,800000,
  900000,1000000,1100000,1200000,1300000,1400000,1500000
};
static int freq_index(uint32_t khz){
    unsigned i;
    for(i=0;i<G413_FREQS;i++)
        if(g413_freq_khz[i]==khz)return (int)i;
    return -1;
}
static struct g413_uid_row *uid_find(struct g413_core *c,uint32_t uid,bool make){
    uint16_t n=c->buckets[uid%G413_UID_BUCKETS];
    while(n){
        struct g413_uid_row *e=&c->uids[n-1];
        if(e->uid==uid)return e;
        n=e->next_plus_one;
    }
    if(!make)return 0;
    if(c->n_uid>=G413_MAX_UIDS)return 0;
    struct g413_uid_row *e=&c->uids[c->n_uid];
    memset(e,0,sizeof(*e));
    e->uid=uid;
    e->valid=1;
    e->next_plus_one=c->buckets[uid%G413_UID_BUCKETS];
    c->buckets[uid%G413_UID_BUCKETS]=(uint16_t)(++c->n_uid);
    return e;
}
bool g413_init(struct g413_core *c,uint32_t khz,uint64_t at_ns){
    if(!c || freq_index(khz)<0)return false;
    memset(c,0,sizeof(*c));
    c->initialized=1;c->last_event_ns=at_ns;c->current_khz=khz;
    return true;
}
static bool valid_time(struct g413_core *c,uint64_t at){
    if(!c || !c->initialized)return false;
    if(at<c->last_event_ns){c->n_errors++;return false;}
    return true;
}
static bool charged(struct g413_core *c,unsigned cpu,uint64_t at){
    struct g413_running *r=&c->cpu[cpu];
    struct g413_uid_row *e;
    uint64_t delta=at-r->since_ns;
    int state;
    if(!r->active || !delta){r->since_ns=at;return true;}
    state=freq_index(c->current_khz);
    if(state<0)return false;
    e=uid_find(c,r->uid,true);
    if(!e){c->n_errors++;return false;}
    if(e->has_cutoff && r->birth_ns<=e->cutoff_ns){r->since_ns=at;return true;}
    if(UINT64_MAX-e->total_ns[state]<delta){c->n_errors++;return false;}
    e->total_ns[state]+=delta;
    r->since_ns=at;
    return true;
}
/*
 * G414 future-only atomic preparation for events that charge BOTH CPUs.
 * Core consumers must serialize all access using one external lock.
 * Before changing any live counters, validate the combined 2-CPU deltas.
 * This is a real robustness fix, not a claim about physical frequency timing.
 */
static bool preflight_both(struct g413_core *c,uint64_t at,int extra_uid,
                           uint32_t extra_uid_id) {
    uint32_t needed[G413_CPUS+1];
    unsigned int n=0, cpu, j;
    int state=freq_index(c->current_khz);
    if(state<0)return false;
    for(cpu=0;cpu<G413_CPUS;cpu++){
        const struct g413_running *r=&c->cpu[cpu];
        struct g413_uid_row *e;
        uint64_t delta;
        uint64_t combined=0;
        if(!r->active)continue;
        if(at<r->since_ns)return false;
        delta=at-r->since_ns;
        e=uid_find(c,r->uid,false);
        if(e && e->has_cutoff && r->birth_ns<=e->cutoff_ns)
            continue; /* will not charge old pre-purge task */
        if(e){
            for(j=0;j<G413_CPUS;j++){
                const struct g413_running *other=&c->cpu[j];
                if(j==cpu || !other->active || other->uid!=r->uid)
                    continue;
                if(other->birth_ns<=e->cutoff_ns && e->has_cutoff)
                    continue;
                if(at<other->since_ns)
                    return false;
                if(UINT64_MAX-combined < (at-other->since_ns))
                    return false;
                combined+=at-other->since_ns;
            }
            if(UINT64_MAX-combined<delta ||
               UINT64_MAX-(combined+delta)<e->total_ns[state])
                return false;
        }else{
            bool seen=false;
            for(j=0;j<n;j++)if(needed[j]==r->uid)seen=true;
            if(!seen)needed[n++]=r->uid;
        }
    }
    if(extra_uid && !uid_find(c,extra_uid_id,false)){
        bool seen=false;
        for(j=0;j<n;j++)if(needed[j]==extra_uid_id)seen=true;
        if(!seen)needed[n++]=extra_uid_id;
    }
    return n<=G413_MAX_UIDS-c->n_uid;
}
bool g413_switch(struct g413_core *c,unsigned cpu,uint64_t at,
                  bool on,uint32_t uid,uint64_t birth){
    if(!valid_time(c,at))return false;
    if(cpu>=G413_CPUS){c->n_errors++;return false;}
    if(on && birth>at){c->n_errors++;return false;}
    if(!charged(c,cpu,at))return false;
    c->cpu[cpu].active=on;
    c->cpu[cpu].uid=uid;
    c->cpu[cpu].birth_ns=birth;
    c->cpu[cpu].since_ns=at;
    c->last_event_ns=at;
    return true;
}
bool g413_frequency_change(struct g413_core *c,uint64_t at,uint32_t new_khz){
    unsigned i;
    if(!valid_time(c,at))return false;
    if(freq_index(new_khz)<0){c->n_errors++;return false;}
    if(!preflight_both(c,at,0,0)){c->n_errors++;return false;}
    /* Linux cpufreq emits frequency trace events separately on BOTH CPUs
     * in a shared policy. The SECOND event has the same current freq and
     * MUST NOT double account the elapsed interval. */
    /* A later same-frequency event is still a checkpoint. The duplicate
     * event on CPU1 at the same timestamp contributes zero extra time. */
    for(i=0;i<G413_CPUS;i++)
        if(!charged(c,i,at))return false;
    c->current_khz=new_khz;c->last_event_ns=at;
    return true;
}
bool g413_remove_uid(struct g413_core *c,uint64_t at,uint32_t uid){
    unsigned i;
    struct g413_uid_row *entry;
    if(!valid_time(c,at))return false;
    /* All rows and CPU deltas must fit BEFORE changing any accounting. */
    if(!preflight_both(c,at,1,uid)){c->n_errors++;return false;}
    /* Must account both CPUs before deleting old accounting. */
    for(i=0;i<G413_CPUS;i++)
        if(!charged(c,i,at))return false;
    entry=uid_find(c,uid,true);
    if(!entry){c->n_errors++;return false;}
    memset(entry->total_ns,0,sizeof(entry->total_ns));
    entry->cutoff_ns=at;
    entry->has_cutoff=1;
    c->last_event_ns=at;
    return true;
}
bool g413_uid_get(const struct g413_core *c,uint32_t uid,uint64_t bins[G413_FREQS]){
    uint16_t n;
    if(!c || !bins || !c->initialized)return false;
    n=c->buckets[uid%G413_UID_BUCKETS];
    while(n){
        const struct g413_uid_row *e=&c->uids[n-1];
        if(e->uid==uid){
            memcpy(bins,e->total_ns,sizeof(e->total_ns));
            return true;
        }
        n=e->next_plus_one;
    }
    memset(bins,0,G413_FREQS*sizeof(*bins));
    return true;
}
bool g413_uid_get_10ms(const struct g413_core *c,uint32_t uid,uint64_t ticks[G413_FREQS]){
    unsigned i;
    if(!g413_uid_get(c,uid,ticks))return false;
    for(i=0;i<G413_FREQS;i++) {
#ifdef __KERNEL__
        ticks[i] = div64_u64(ticks[i], 10000000ULL);
#else
        ticks[i] /= 10000000ULL;
#endif
    }
    return true;
}
