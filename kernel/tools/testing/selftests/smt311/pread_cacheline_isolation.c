/* SPDX-License-Identifier: GPL-2.0-only
 * G651 Cortex-A9 PMU benchmark: pin 2 threads to CPU0/1, read 16 bytes.
 * "shared": one FD, same inode/page; "samefile": independent FDs same inode;
 * "split": independent FDs, separate inodes/cache folios.
 * No kernel change, no root, own temp data only.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#define N 4096
#define PAD 128
static char path[]="/data/local/tmp/.g651-pmu-XXXXXX";
static int fd[2]={-1,-1},secs;
static off_t offset[2];
static atomic_int go,done,bad;
struct worker { _Alignas(PAD) unsigned long count; int id; char unused[PAD-2*sizeof(long)]; };
static struct worker w[2];
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static void cleanup(void) {
  char a[192],b[192];
  snprintf(a,sizeof(a),"%s/a",path);snprintf(b,sizeof(b),"%s/b",path);
  if(fd[0]>=0)close(fd[0]);
  if(fd[1]>=0 && fd[1]!=fd[0])close(fd[1]);
  unlink(a);unlink(b);rmdir(path);
}
static int seed(const char *name,unsigned char v) {
  int f=open(name,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);
  if(f<0)return -1;
  unsigned char data[N];memset(data,v,sizeof(data));
  if(pwrite(f,data,N,0)!=N||fsync(f)){close(f);return -1;}
  close(f);return 0;
}
static void *readloop(void *ptr) {
  struct worker *a=ptr;
  cpu_set_t mask; CPU_ZERO(&mask);CPU_SET(a->id,&mask);
  if(pthread_setaffinity_np(pthread_self(),sizeof(mask),&mask))atomic_fetch_add(&bad,1);
  unsigned char buf[16];
  while(!atomic_load_explicit(&go,memory_order_acquire))sched_yield();
  unsigned long n=0;
  while(!atomic_load_explicit(&done,memory_order_relaxed)) {
    if(pread(fd[a->id],buf,sizeof(buf),offset[a->id])!=sizeof(buf)) { atomic_fetch_add(&bad,1);break; }
    n++;
  }
  a->count=n;
  return NULL;
}
int main(int argc,char **argv) {
  if(argc!=3){fprintf(stderr,"usage: g651 [shared|samefile|twopages|split] SECONDS(1..5)\n");return 2;}
  secs=atoi(argv[2]);
  if(secs<1||secs>5)return 3;
  if(strcmp(argv[1],"shared")&&strcmp(argv[1],"samefile")&&strcmp(argv[1],"twopages")&&strcmp(argv[1],"split"))return 4;
  if(!mkdtemp(path)){perror("mkdtemp");return 5;}
  char a[192],b[192];
  snprintf(a,sizeof(a),"%s/a",path);snprintf(b,sizeof(b),"%s/b",path);
  if(seed(a,17)||seed(b,19)){perror("seed");cleanup();return 6;}
  /*
   * Same inode but TWO separate page-cache folios is the crucial control:
   * it keeps inode and address_space sharing, only removes folio sharing.
   */
  if(!strcmp(argv[1],"twopages")) {
    int ex=open(a,O_WRONLY|O_CLOEXEC);
    unsigned char second[N];memset(second,23,sizeof(second));
    if(ex<0 || pwrite(ex,second,N,N)!=N || close(ex)) {
      perror("seed second folio");cleanup();return 11;
    }
    offset[1]=N;
  }
  fd[0]=open(a,O_RDONLY|O_CLOEXEC);
  fd[1]=!strcmp(argv[1],"shared") ? fd[0] :
        open(!strcmp(argv[1],"split")?b:a,O_RDONLY|O_CLOEXEC);
  if(fd[0]<0||fd[1]<0){perror("open");cleanup();return 7;}
  unsigned char buf[16];
  for(int i=0;i<5000;i++){
    if(pread(fd[0],buf,sizeof(buf),0)!=sizeof(buf) ||
       pread(fd[1],buf,sizeof(buf),0)!=sizeof(buf)){cleanup();return 8;}
  }
  w[0].id=0;w[1].id=1;
  pthread_t t[2];
  if(pthread_create(&t[0],NULL,readloop,&w[0])||
     pthread_create(&t[1],NULL,readloop,&w[1])){cleanup();return 9;}
  double before=now();
  atomic_store_explicit(&go,1,memory_order_release);
  struct timespec req={.tv_sec=secs};
  nanosleep(&req,NULL);
  atomic_store_explicit(&done,1,memory_order_relaxed);
  for(int j=0;j<2;j++)pthread_join(t[j],NULL);
  double elapsed=now()-before;
  unsigned long count=w[0].count+w[1].count;
  int errors=atomic_load(&bad);
  printf("G651_TEST mode=%s threads=2 bytes=16 duration=%.4f reads=%lu reads_per_sec=%.0f cpu0_reads=%lu cpu1_reads=%lu errors=%d\n",
         argv[1],elapsed,count,count/elapsed,w[0].count,w[1].count,errors);
  cleanup();
  return errors?10:0;
}
