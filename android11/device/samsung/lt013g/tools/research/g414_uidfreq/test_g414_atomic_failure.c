#include "g413_event_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>
static struct g413_core s;
static void ensure_unchanged(uint64_t left_before,uint64_t right_before,
                             uint64_t total_before,uint32_t freq_before,
                             uint64_t epoch_before,uint32_t uid,
                             unsigned bin){
    uint64_t bins[G413_FREQS];
    assert(s.cpu[0].since_ns==left_before);
    assert(s.cpu[1].since_ns==right_before);
    assert(s.current_khz==freq_before);
    assert(s.last_event_ns==epoch_before);
    assert(g413_uid_get(&s,uid,bins));
    assert(bins[bin]==total_before);
}
int main(void){
    uint64_t bins[G413_FREQS];
    assert(g413_init(&s,200000,0));
    /* Reserve exactly 4095 of 4096 UID slots using real public API. */
    for(uint32_t i=0;i<4095;i++)assert(g413_remove_uid(&s,0,i));
    assert(s.n_uid==4095);
    assert(g413_switch(&s,0,1,1,50000,1));
    assert(g413_switch(&s,1,1,1,50001,1));
    assert(!g413_frequency_change(&s,10000000,400000));
    assert(s.n_uid==4095);
    ensure_unchanged(1,1,0,200000,1,0,0);
    /* Even remove_uid of a nonexistent UID must reject BEFORE any
     * already accumulated active-CPU partial credit happens. */
    assert(!g413_remove_uid(&s,10000001,80000));
    assert(s.n_uid==4095);
    ensure_unchanged(1,1,0,200000,1,0,0);
    assert(g413_init(&s,200000,0));
    assert(g413_remove_uid(&s,0,100));
    assert(g413_switch(&s,0,0,1,100,1)==0);/* impossible birth in future */
    assert(g413_switch(&s,0,0,1,100,0));
    assert(g413_switch(&s,1,0,1,100,0));
    /* Prior removal at exactly birth=0 means this old generation is
     * discarded, so use fresh task birth=1 at time=1 for real charges. */
    assert(g413_switch(&s,0,1,1,100,1));
    assert(g413_switch(&s,1,1,1,100,1));
    assert(g413_uid_get(&s,100,bins));
    assert(bins[0]==0);
    s.uids[0].total_ns[0]=UINT64_MAX-20;
    /* each CPU would add 19, combined 38: overflow. */
    assert(!g413_frequency_change(&s,20,300000));
    assert(s.current_khz==200000 && s.last_event_ns==1);
    assert(s.cpu[0].since_ns==1 && s.cpu[1].since_ns==1);
    assert(s.uids[0].total_ns[0]==UINT64_MAX-20);
    printf("PASS G414 atomic preflight: 4095/4096 full UID pool rejects 2-CPU charging without partial data; simultaneous UID overflow rejected before changing counters or event time\n");
    return 0;
}
