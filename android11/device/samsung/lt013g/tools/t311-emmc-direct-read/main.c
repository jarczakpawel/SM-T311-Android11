#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BLOCK 4096
#define MBLOCKS 4096
#define NREAD 1536
static const char *path="/data/local/tmp/.t311-emmc-io-benchmark.tmp";
static int64_t diff(struct timespec a,struct timespec b){
 return (int64_t)(a.tv_sec-b.tv_sec)*1000000000LL+a.tv_nsec-b.tv_nsec;
}
static int cmp64(const void *a,const void *b){
 int64_t x=*(const int64_t*)a,y=*(const int64_t*)b;
 return (x>y)-(x<y);
}
static uint32_t prng(uint32_t *x){
 *x^=*x<<13;*x^=*x>>17;*x^=*x<<5;return *x;
}
int main(int argc,char **argv){
 if(argc!=2 || (strcmp(argv[1],"init") && strcmp(argv[1],"read"))){
  fprintf(stderr,"usage init | read\n");return 2;
 }
 int init=!strcmp(argv[1],"init");
 int flags=O_DIRECT|O_CLOEXEC|(init?(O_CREAT|O_EXCL|O_WRONLY):O_RDONLY);
 int fd=open(path,flags,0600);
 if(fd<0){perror("open");return 3;}
 void *b=NULL;
 if(posix_memalign(&b,BLOCK,BLOCK)!=0){close(fd);return 4;}
 memset(b,0x6d,BLOCK);
 struct timespec start,end;
 clock_gettime(CLOCK_MONOTONIC,&start);
 int status=0;
 if(init){
  for(unsigned i=0;i<MBLOCKS;i++)if(pwrite(fd,b,BLOCK,(off_t)i*BLOCK)!=BLOCK){
   perror("init pwrite");status=5;break;
  }
  if(!status && fdatasync(fd)<0){perror("fdatasync");status=6;}
  clock_gettime(CLOCK_MONOTONIC,&end);
  printf("EMMC_PREP=16MiB_BLOCK4096_STATUS=%d_WALL_MS=%.3f\n",status,diff(end,start)/1e6);
 }else{
  struct stat sb;
  if(fstat(fd,&sb)<0 || sb.st_size!=(off_t)MBLOCKS*BLOCK){status=7;goto done;}
  int64_t durations[NREAD];
  uint32_t rnd=0x3112026;
  uint64_t total=0;
  for(unsigned i=0;i<NREAD;i++){
   off_t at=(off_t)(prng(&rnd)&(MBLOCKS-1))*BLOCK;
   struct timespec a,z;
   clock_gettime(CLOCK_MONOTONIC,&a);
   if(pread(fd,b,BLOCK,at)!=BLOCK){perror("direct pread");status=8;break;}
   clock_gettime(CLOCK_MONOTONIC,&z);
   durations[i]=diff(z,a);total+=(uint64_t)durations[i];
  }
  clock_gettime(CLOCK_MONOTONIC,&end);
  if(!status){
   qsort(durations,NREAD,sizeof(durations[0]),cmp64);
   printf("EMMC_DIRECT_4K_RANDOM_N=%d MB=%.2f WALL_MS=%.3f MEAN_US=%.2f P50_US=%.2f P95_US=%.2f P99_US=%.2f MAX_US=%.2f IOPS=%.2f\n",
     NREAD,NREAD*BLOCK/1048576.,diff(end,start)/1e6,
     (double)total/NREAD/1000.,durations[NREAD/2]/1000.,
     durations[NREAD*95/100]/1000.,durations[NREAD*99/100]/1000.,
     durations[NREAD-1]/1000.,NREAD*1e9/(double)diff(end,start));
  }
 }
done:
 free(b);close(fd);return status;
}
