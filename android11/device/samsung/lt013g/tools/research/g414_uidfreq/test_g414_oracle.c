#include "g413_event_core.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>
#define USER_CNT 6
#define STEPS 75000
static struct g413_core core;
static uint64_t oracle[USER_CNT][G413_FREQS],cutoff[USER_CNT];
static unsigned char was_removed[USER_CNT];
static struct {
    uint8_t active;
    unsigned uid;
    uint64_t birth,last;
} cpus[2];
static unsigned state;
static unsigned long long events=0;
static uint32_t seed=0x71c2e1e1;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static void settle(unsigned cpu,uint64_t now){
    if(cpus[cpu].active && !(was_removed[cpus[cpu].uid] &&
       cpus[cpu].birth<=cutoff[cpus[cpu].uid]))
       oracle[cpus[cpu].uid][state]+=now-cpus[cpu].last;
    cpus[cpu].last=now;
}
static void check(void){
    uint64_t b[G413_FREQS];
    for(unsigned uid=0;uid<USER_CNT;uid++){
        assert(g413_uid_get(&core,uid,b));
        for(unsigned f=0;f<G413_FREQS;f++)
            if(b[f]!=oracle[uid][f]){
                fprintf(stderr,"mismatch event %llu uid %u state %u expected %llu got %llu\n",events,uid,f,
                   (unsigned long long)oracle[uid][f],(unsigned long long)b[f]);
                abort();
            }
    }
}
int main(void){
    assert(g413_init(&core,g413_freq_khz[0],0));
    uint64_t now=0;
    for(unsigned step=0;step<STEPS;step++){
        now+=(rnd()%10000u)+1u; /* actual <10us events */
        unsigned kind=rnd()%8u;
        if(kind<4){
            unsigned cpu=rnd()%2u;
            unsigned uid=rnd()%USER_CNT;
            unsigned active=(rnd()%6u)!=0;
            uint64_t birth=now-(rnd()%40000u < now ? rnd()%40000u:0);
            if(birth>now)birth=now;
            settle(cpu,now);
            cpus[cpu].active=active;
            cpus[cpu].uid=uid;cpus[cpu].birth=birth;cpus[cpu].last=now;
            assert(g413_switch(&core,cpu,now,active,uid,birth));
        }else if(kind<7){
            unsigned next=rnd()%G413_FREQS;
            settle(0,now);settle(1,now);
            assert(g413_frequency_change(&core,now,g413_freq_khz[next]));
            state=next;
            /* Two cpufreq trace events per transition for shared CPU policy:
             * second event must not double count or advance task time. */
            assert(g413_frequency_change(&core,now,g413_freq_khz[next]));
        }else{
            unsigned uid=rnd()%USER_CNT;
            settle(0,now);settle(1,now);
            memset(oracle[uid],0,sizeof(oracle[uid]));
            was_removed[uid]=1;cutoff[uid]=now;
            assert(g413_remove_uid(&core,now,uid));
        }
        events++;
        check();
    }
    printf("PASS G413 independent 75000-event CPU0+CPU1 oracle: shared-policy transition splitting, repeated trace frequency events, idle/context switches, random UID purge & birth without divergent nanos; uid_count=%u\n",core.n_uid);
    return 0;
}
