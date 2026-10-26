// G645: non-privileged SM-T311 ext4 cache zone probe. RCU untouched.
// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
enum { PAGE=4096, MiB=1048576 };
static char path[]="/data/local/tmp/.g645-t311-zone-XXXXXX";
static int testfd=-1;
typedef struct { long normal, high, free_normal, free_high; } counts;
static void cleanup(void) { if(testfd>=0)close(testfd);unlink(path); }
static void interrupted(int sig) { cleanup();_exit(128+sig); }
static void fail(const char *what) { perror(what); exit(1); }
static counts zones(void) {
 FILE *f=fopen("/proc/zoneinfo","r");if(!f)fail("zoneinfo");
 counts c={0};char buf[256],field[75];long n;int z=-1,seen=0;
 while(fgets(buf,sizeof(buf),f)){
  if(!strncmp(buf,"Node ",5)) {
   if(strstr(buf,"zone   Normal"))z=0;
   else if(strstr(buf,"zone  HighMem"))z=1;
   else z=-1;
  }
  if(z<0||sscanf(buf," %74s %ld",field,&n)!=2)continue;
  if(!strcmp(field,"nr_zone_inactive_file")||!strcmp(field,"nr_zone_active_file")){
   if(z==0)c.normal+=n;else c.high+=n;
   seen |= 1<<z;
  }else if(!strcmp(field,"nr_free_pages")){
   if(z==0)c.free_normal=n;else c.free_high=n;
  }
 }
 fclose(f);if(seen!=3){fprintf(stderr,"missing zoneinfo stats\n");exit(2);}
 return c;
}
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))fail("clock");return(uint64_t)t.tv_sec*1000000000ull+t.tv_nsec;}
static long resident(void *addr,size_t n){
 unsigned char *v=calloc(n/PAGE,1);if(!v)fail("calloc");
 if(mincore(addr,n,v))fail("mincore");
 long total=0;for(size_t i=0;i<n/PAGE;i++)total+=!!(v[i]&1);
 free(v);return total;
}
static void print(const char *name,counts c){
 printf("G645 %s normal_file=%ld highmem_file=%ld normal_free=%ld highmem_free=%ld\n",name,c.normal,c.high,c.free_normal,c.free_high);
}
int main(int argc,char **argv){
 int mib=argc==2?atoi(argv[1]):16;if(mib<4||mib>32){fprintf(stderr,"4-32 MiB allowed\n");return 2;}
 size_t len=(size_t)mib*MiB;char *b=malloc(PAGE);if(!b)fail("malloc");memset(b,0x5a,PAGE);
 signal(SIGINT,interrupted);signal(SIGTERM,interrupted);
 testfd=mkstemp(path);if(testfd<0)fail("mkstemp");
 atexit(cleanup);
 for(size_t i=0;i<len;i+=PAGE)if(write(testfd,b,PAGE)!=PAGE)fail("write");
 if(fdatasync(testfd))fail("fdatasync");
 int advise=posix_fadvise(testfd,0,(off_t)len,POSIX_FADV_DONTNEED);
 if(advise){fprintf(stderr,"fadvise DONTNEED errno=%d\n",advise);return 3;}
 void *addr=mmap(NULL,len,PROT_READ,MAP_SHARED,testfd,0);
 if(addr==MAP_FAILED)fail("mmap");
 long residency_before=resident(addr,len);counts before=zones();print("BEFORE",before);
 uint64_t begin=now(),sum=0;
 for(size_t i=0;i<len;i+=PAGE){if(pread(testfd,b,PAGE,i)!=PAGE)fail("pread cold");sum+=(unsigned char)b[0];}
 uint64_t duration=now()-begin;
 counts after=zones();long residency_after=resident(addr,len);print("AFTER_COLD",after);
 for(size_t i=0;i<len;i+=PAGE){if(pread(testfd,b,PAGE,i)!=PAGE)fail("pread hot");sum+=(unsigned char)b[0];}
 counts hot=zones();print("AFTER_SECOND_PASS",hot);
 printf("G645 RESULT test_MiB=%d resident_before=%ld resident_after=%ld expected=%zu delta_normal=%ld delta_highmem=%ld cold_ms=%.3f checksum=%" PRIu64 "\n",
        mib,residency_before,residency_after,len/PAGE,hot.normal-before.normal,hot.high-before.high,(double)duration/1e6,sum);
 printf("G645 CAUTION: zone deltas can include other apps; not direct count of RCU-eligible hot folios\n");
 munmap(addr,len);free(b);return 0;
}
