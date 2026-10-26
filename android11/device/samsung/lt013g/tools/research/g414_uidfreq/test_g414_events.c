#include "g413_event_core.h"
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#define MS UINT64_C(1000000)
static struct g413_core core;
static void verify(uint32_t uid,unsigned index,uint64_t expected) {
    uint64_t bins[G413_FREQS];
    assert(g413_uid_get(&core,uid,bins));
    assert(bins[index]==expected);
}
int main(void){
    uint64_t bins[G413_FREQS];
    struct g413_core *c=&core;
    assert(g413_init(c,200000,0));
    assert(g413_switch(c,0,0,true,1000,0));
    assert(g413_switch(c,1,0,true,2000,0));
    assert(g413_frequency_change(c,5*MS,400000));
    assert(g413_frequency_change(c,5*MS,400000)); /* second CPU trace event */
    verify(1000,0,5*MS);verify(2000,0,5*MS);
    assert(g413_switch(c,0,8*MS,true,3000,8*MS));
    verify(1000,2,3*MS);
    assert(g413_switch(c,1,10*MS,false,0,0));
    verify(2000,2,5*MS);
    assert(g413_frequency_change(c,15*MS,800000));
    verify(3000,2,7*MS);
    assert(g413_remove_uid(c,20*MS,3000));
    verify(3000,2,0);verify(3000,6,0);
    assert(g413_switch(c,0,22*MS,true,3000,21*MS)); /* reused UID, new birth */
    assert(g413_frequency_change(c,26*MS,1000000));
    verify(3000,6,4*MS);
    assert(g413_switch(c,0,32*MS,false,0,0));
    verify(3000,8,6*MS);
    assert(g413_uid_get_10ms(c,3000,bins));
    assert(bins[6]==0 && bins[8]==0);/* valid 4ms + 6ms separate bins */
    assert(g413_uid_get(c,3000,bins));
    assert(bins[6]+bins[8]==10*MS); /* no premature per-slice truncation */
    assert(!g413_frequency_change(c,33*MS,1700000));/* unsupported frequency */
    assert(c->n_errors==1);
    assert(!g413_switch(c,0,20*MS,false,0,0));/* stale event rejected */
    assert(c->n_errors==2);
    assert(!g413_switch(c,3,34*MS,false,0,0));
    assert(c->n_errors==3);
    assert(!g413_switch(c,0,34*MS,true,1000,35*MS));
    assert(c->n_errors==4);
    printf("PASS G413 two CPU shared-frequency transition accounting, double trace event suppressed, mid-5ms transitions split, idle/task birth/purge/UID reuse, monotonic input gate, unsupported freq rejected\n");
    return 0;
}
