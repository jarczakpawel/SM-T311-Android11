#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <tinyalsa/asoundlib.h>

static int64_t ns_diff(struct timespec t, struct timespec o) {
    return (int64_t)(t.tv_sec-o.tv_sec)*1000000000LL+t.tv_nsec-o.tv_nsec;
}

int main(int argc, char **argv) {
    if (argc != 2) return fprintf(stderr,"usage period=[256|512|1024]\n"), 2;
    int period=atoi(argv[1]);
    if (period!=256 && period!=512 && period!=1024) return 2;
    struct pcm_config c={0};
    c.channels=2;
    c.rate=44100;
    c.period_size=(unsigned)period;
    c.period_count=4;
    c.format=PCM_FORMAT_S16_LE;
    c.start_threshold=(unsigned)period;
    c.avail_min=(unsigned)period;
    struct pcm *pcm=pcm_open(0,0,PCM_OUT,&c);
    if (!pcm || !pcm_is_ready(pcm)) {
        printf("ALSA_OPEN_FAIL_PERIOD=%d ERROR=%s\n",period,pcm?pcm_get_error(pcm):"null");
        if (pcm)pcm_close(pcm);
        return 3;
    }
    unsigned real_buffer=pcm_get_buffer_size(pcm);
    printf("ALSA_OPEN_PASS=1 PERIOD=%d COUNT=%u RATE=%u CHANNELS=%u BUFFER_FRAMES=%u NOMINAL_HW_BUFFER_MS=%.3f\n",
           period,c.period_count,c.rate,c.channels,real_buffer,real_buffer*1000.0/c.rate);
    int16_t *zeros=calloc((size_t)period*2,sizeof(*zeros));
    if (!zeros) {pcm_close(pcm);return 4;}
    struct timespec start,stop,cpu_start,cpu_stop;
    clock_gettime(CLOCK_MONOTONIC,&start);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&cpu_start);
    unsigned writes=0;
    unsigned failures=0;
    int64_t worst_ns=0;
    const unsigned limit=(44100U*2U+(unsigned)period-1U)/(unsigned)period;
    for(unsigned i=0;i<limit;i++) {
        struct timespec before,after;
        clock_gettime(CLOCK_MONOTONIC,&before);
        int ret=pcm_write(pcm,zeros,(unsigned)period*2*sizeof(*zeros));
        clock_gettime(CLOCK_MONOTONIC,&after);
        int64_t write_ns=ns_diff(after,before);
        if(write_ns>worst_ns)worst_ns=write_ns;
        if(ret) {
            printf("ALSA_WRITE_FAILED=%d RET=%d ERROR=%s\n",i,ret,pcm_get_error(pcm));
            failures++;
            break;
        }
        writes++;
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&cpu_stop);
    clock_gettime(CLOCK_MONOTONIC,&stop);
    unsigned avail=0;
    struct timespec hwts={0};
    int htstatus=pcm_get_htimestamp(pcm,&avail,&hwts);
    printf("ALSA_RESULT_PERIOD=%d WRITES=%u WRITTEN_FRAMES=%u WRITE_ERRORS=%u WALL_MS=%.3f PROCESS_CPU_MS=%.3f MAX_WRITE_MS=%.3f HTSTAMP_RET=%d HW_AVAIL=%u\n",
           period,writes,writes*(unsigned)period,failures,ns_diff(stop,start)/1e6,
           ns_diff(cpu_stop,cpu_start)/1e6,worst_ns/1e6,htstatus,avail);
    free(zeros);
    pcm_close(pcm);
    return (!failures && writes==limit)?0:5;
}
