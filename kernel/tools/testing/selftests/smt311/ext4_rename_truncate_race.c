/* G649 SM-T311 only: bounded ext4 pagecache consistency under simultaneous
 * atomic inode replacement and referenced pread. Never enables no-ref RCU.
 * Own temporary directory under /data/local/tmp, removed on clean exit.
 * SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#define SIZE 4096
#define WRITES 400
#define READS 12000
static char dir[]="/data/local/tmp/.g649-ext4-XXXXXX";
static char live[160],tmp[160];
static atomic_uint bad,opens,reads,renames;
static void cleanup(void) {
    unlink(tmp);unlink(live);rmdir(dir);
}
static void err(const char *msg) { perror(msg);cleanup();exit(1); }
static uint8_t byte_at(uint32_t gen,size_t pos) {
    uint32_t x=gen*2654435761U ^ (uint32_t)pos*747796405U ^ 0x9e3779b9U;
    x^=x>>16;x*=2246822519U;x^=x>>13;return (uint8_t)(x>>7);
}
static void fill(uint8_t *buf,uint32_t gen) {
    memcpy(buf,&gen,sizeof(gen));
    for(size_t j=sizeof(gen);j<SIZE;j++)buf[j]=byte_at(gen,j);
}
static int install(uint32_t gen) {
    uint8_t buf[SIZE]; fill(buf,gen);
    int fd=open(tmp,O_CREAT|O_TRUNC|O_WRONLY|O_CLOEXEC,0600);
    if(fd<0)return -1;
    ssize_t n=pwrite(fd,buf,SIZE,0);
    int close_rc=close(fd);
    if(n!=SIZE || close_rc)return -1;
    if(rename(tmp,live))return -1;
    atomic_fetch_add(&renames,1);
    return 0;
}
static void *writer(void *arg) {
    (void)arg;
    for(uint32_t i=1;i<=WRITES;i++) {
        if(install(i)) {perror("G649 writer");atomic_fetch_add(&bad,1);break;}
        if(i%32==0)sched_yield();
    }
    return NULL;
}
static void *reader(void *arg) {
    (void)arg;
    uint8_t buf[SIZE];
    for(unsigned i=0;i<READS;i++) {
        int fd=open(live,O_RDONLY|O_CLOEXEC);
        if(fd<0) { perror("G649 open");atomic_fetch_add(&bad,1);break;}
        atomic_fetch_add(&opens,1);
        ssize_t n=pread(fd,buf,SIZE,0);
        close(fd);
        if(n!=SIZE) {
            fprintf(stderr,"G649 unexpected read size=%zd errno=%d\n",n,errno);
            atomic_fetch_add(&bad,1);break;
        }
        uint32_t gen;memcpy(&gen,buf,sizeof(gen));
        if(gen>WRITES) {atomic_fetch_add(&bad,1);break;}
        for(size_t j=4;j<SIZE;j++) {
            if(buf[j]!=byte_at(gen,j)) {
                fprintf(stderr,"G649 immutable inode data mismatch gen=%u offset=%zu\n",gen,j);
                atomic_fetch_add(&bad,1);return NULL;
            }
        }
        atomic_fetch_add(&reads,1);
    }
    return NULL;
}
/* Concurrent truncate on one open inode: data mixing is legal while the
 * writer operates. Only verify syscall size/bounds, absence of errors, and
 * final stable page after both threads finish; do NOT claim atomic reads.
 */
#define TRUNC_CYCLES 2500
#define TRUNC_READS 12000
static int same_inode_fd=-1;
static atomic_uint trunc_error, trunc_reads;
static void *truncate_writer(void *arg) {
    (void)arg;
    uint8_t b[SIZE];fill(b,900U);
    for(int i=0;i<TRUNC_CYCLES;i++) {
        if(ftruncate(same_inode_fd,0) ||
           ftruncate(same_inode_fd,SIZE) ||
           pwrite(same_inode_fd,b,SIZE,0)!=SIZE) {
            perror("G649 concurrent truncate writer");
            atomic_fetch_add(&trunc_error,1);return NULL;
        }
        if((i&63)==0)sched_yield();
    }
    return NULL;
}
static void *truncate_reader(void *arg) {
    (void)arg;
    uint8_t b[SIZE];
    for(int i=0;i<TRUNC_READS;i++) {
        ssize_t n=pread(same_inode_fd,b,SIZE,0);
        if(n<0 || n>SIZE) {
            fprintf(stderr,"G649 concurrent pread error size=%zd errno=%d\n",n,errno);
            atomic_fetch_add(&trunc_error,1);return NULL;
        }
        atomic_fetch_add(&trunc_reads,1);
    }
    return NULL;
}
static int concurrent_truncate(void) {
    char file[160];snprintf(file,sizeof(file),"%s/truncate-race",dir);
    same_inode_fd=open(file,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);
    if(same_inode_fd<0)return -1;
    uint8_t b[SIZE],out[SIZE];fill(b,900U);
    if(pwrite(same_inode_fd,b,SIZE,0)!=SIZE)goto bad;
    pthread_t wr,rd;
    if(pthread_create(&wr,NULL,truncate_writer,NULL))goto bad;
    if(pthread_create(&rd,NULL,truncate_reader,NULL)) {
        pthread_join(wr,NULL);goto bad;
    }
    pthread_join(wr,NULL);
    pthread_join(rd,NULL);
    if(atomic_load(&trunc_error)||atomic_load(&trunc_reads)!=TRUNC_READS)
        goto bad;
    if(pread(same_inode_fd,out,SIZE,0)!=SIZE||memcmp(out,b,SIZE))
        goto bad;
    close(same_inode_fd);same_inode_fd=-1;unlink(file);return 0;
bad:
    close(same_inode_fd);same_inode_fd=-1;unlink(file);return -1;
}
static int sequential_hole_truncate(void) {
    char aux[160]; snprintf(aux,sizeof(aux),"%s/hole",dir);
    int fd=open(aux,O_CREAT|O_EXCL|O_RDWR|O_CLOEXEC,0600);
    if(fd<0)return -1;
    uint8_t in[SIZE],out[SIZE];
    fill(in,777);
    if(pwrite(fd,in,SIZE,0)!=SIZE||pwrite(fd,in,SIZE,SIZE)!=SIZE)goto fail;
    if(fallocate(fd,FALLOC_FL_PUNCH_HOLE|FALLOC_FL_KEEP_SIZE,0,SIZE))goto fail;
    if(pread(fd,out,SIZE,0)!=SIZE)goto fail;
    for(size_t j=0;j<SIZE;j++)if(out[j])goto fail;
    if(pread(fd,out,SIZE,SIZE)!=SIZE||memcmp(out,in,SIZE))goto fail;
    if(ftruncate(fd,19))goto fail;
    if(pread(fd,out,SIZE,0)!=19)goto fail;
    for(size_t j=0;j<19;j++)if(out[j])goto fail;
    if(ftruncate(fd,SIZE*2))goto fail;
    if(pread(fd,out,SIZE,0)!=SIZE)goto fail;
    for(size_t j=0;j<SIZE;j++)if(out[j])goto fail;
    close(fd);unlink(aux);return 0;
fail:
    perror("G649 hole/truncate");close(fd);unlink(aux);return -1;
}
int main(void) {
    if(!mkdtemp(dir))err("G649 mkdtemp");
    if(snprintf(live,sizeof(live),"%s/live",dir)>= (int)sizeof(live)||
       snprintf(tmp,sizeof(tmp),"%s/write",dir)>= (int)sizeof(tmp))err("G649 paths");
    if(install(0))err("G649 initial file");
    pthread_t wr,rd[2];
    if(pthread_create(&wr,NULL,writer,NULL))err("G649 create writer");
    for(int i=0;i<2;i++)if(pthread_create(&rd[i],NULL,reader,NULL))err("G649 create reader");
    pthread_join(wr,NULL);
    for(int i=0;i<2;i++)pthread_join(rd[i],NULL);
    int hole=sequential_hole_truncate();
    int race=concurrent_truncate();
    printf("G649 RESULT atomic_rename=%u successful_reads=%u opens=%u mismatches=%u hole_truncate=%s same_inode_truncate=%s truncate_read_attempts=%u rcu_ref_free=OFF\n",
           atomic_load(&renames),atomic_load(&reads),atomic_load(&opens),
           atomic_load(&bad),hole?"FAIL":"PASS",race?"FAIL":"PASS",
           atomic_load(&trunc_reads));
    cleanup();
    return atomic_load(&bad)||hole||race||atomic_load(&reads)!=2*READS?2:0;
}
