// Host-built static ARM32 user benchmark; creates only its own 4KiB file.
// No root, no drop_caches, no system property changes, no external I/O.
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sched.h>
#include <sys/stat.h>
#include <stdatomic.h>

#define TEST_PATH "/data/local/tmp/g575_hotpage_4k.dat"
static int fd;
static int length_bytes;
static int iterations_seconds;
static atomic_int start_flag;
static atomic_int stop_flag;
static atomic_ulong completed;

static double sec_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void *reader(void *x) {
    uintptr_t i=(uintptr_t)x;
    cpu_set_t mask;
    CPU_ZERO(&mask);
    CPU_SET(i%2,&mask);
    pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask);
    unsigned char buf[4096];
    while (!atomic_load_explicit(&start_flag,memory_order_acquire)) sched_yield();
    unsigned long work=0;
    while (!atomic_load_explicit(&stop_flag,memory_order_relaxed)) {
        if (pread(fd,buf,length_bytes,0) != length_bytes) {
            fprintf(stderr,"G575_PREAD_IO_FAIL errno=%d\n",errno);
            abort();
        }
        work++;
    }
    atomic_fetch_add(&completed,work);
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fputs("usage: g575-bench THREADS(1|2) BYTES(16|4096) SECONDS(1..10)\n",stderr);
        return 2;
    }
    int threads=atoi(argv[1]);length_bytes=atoi(argv[2]);iterations_seconds=atoi(argv[3]);
    if ((threads!=1 && threads!=2) ||
        (length_bytes!=16 && length_bytes!=4096) ||
        iterations_seconds<1 || iterations_seconds>10) return 3;
    fd=open(TEST_PATH,O_CREAT|O_RDWR|O_CLOEXEC,0600);
    if (fd<0) {perror("open");return 4;}
    char buf[4096]={0};
    if (pwrite(fd,buf,sizeof(buf),0)!=sizeof(buf) || fsync(fd)) return 5;
    // Warm the tested folio before measuring; never intentionally clear page cache.
    for(int i=0;i<1000;i++) if(pread(fd,buf,16,0)!=16) return 6;
    pthread_t t[2];
    for(int i=0;i<threads;i++)
        if(pthread_create(&t[i],NULL,reader,(void*)(uintptr_t)i)) return 7;
    atomic_store(&start_flag,1);
    double start=sec_now();
    struct timespec sleep_for={.tv_sec=iterations_seconds,.tv_nsec=0};
    nanosleep(&sleep_for,NULL);
    atomic_store(&stop_flag,1);
    for(int i=0;i<threads;i++) pthread_join(t[i],NULL);
    double seconds=sec_now()-start;
    unsigned long count=atomic_load(&completed);
    printf("G575_BENCH threads=%d bytes=%d elapsed=%.4f reads=%lu reads_per_sec=%.0f\n",
           threads,length_bytes,seconds,count,(double)count/seconds);
    close(fd);
    unlink(TEST_PATH);
    return 0;
}
