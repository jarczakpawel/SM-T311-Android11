// G641 SM-T311 2-process reciprocal pipe microbenchmark, test only.
#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
static uint64_t now(void) {struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))_exit(3);return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;}
static int cmp(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return(x>y)-(x<y);}
static void pin(int mode,int child){cpu_set_t c;CPU_ZERO(&c);CPU_SET(mode==2&&child?1:0,&c);if(mode==0)CPU_SET(1,&c);if(sched_setaffinity(0,sizeof(c),&c)){perror("affinity");_exit(4);}}
static void send1(int fd){char c='.';if(write(fd,&c,1)!=1){perror("write");_exit(5);}}
static void recv1(int fd){char c;if(read(fd,&c,1)!=1||c!='.'){perror("read");_exit(6);}}
int main(int argc,char**argv){int n=argc>1?atoi(argv[1]):10000,mode=argc>2?atoi(argv[2]):0;if(n<100||n>200000||mode<0||mode>2)return 2;uint64_t *samples=calloc(n,sizeof(*samples));if(!samples)return 3;int a[2],b[2];if(pipe(a)||pipe(b))return 4;pin(mode,0);pid_t pid=fork();if(pid<0)return 5;if(!pid){close(a[1]);close(b[0]);pin(mode,1);for(int i=0;i<n+128;i++){recv1(a[0]);send1(b[1]);}close(a[0]);close(b[1]);_exit(0);}close(a[0]);close(b[1]);for(int i=0;i<128;i++){send1(a[1]);recv1(b[0]);}uint64_t t0=now();for(int i=0;i<n;i++){uint64_t ti=now();send1(a[1]);recv1(b[0]);samples[i]=now()-ti;}uint64_t elapsed=now()-t0;close(a[1]);close(b[0]);int st;waitpid(pid,&st,0);if(!WIFEXITED(st)||WEXITSTATUS(st))return 7;qsort(samples,n,sizeof(*samples),cmp);printf("G641 mode=%d n=%d mean_us=%.3f p50_us=%.3f p95_us=%.3f p99_us=%.3f\n",mode,n,(double)elapsed/n/1000.,samples[n/2]/1000.,samples[n*95/100]/1000.,samples[n*99/100]/1000.);free(samples);return 0;}
