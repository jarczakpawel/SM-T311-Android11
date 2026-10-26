#!/usr/bin/env python3
"""Compile the exact GNSS SUPL probe and detect high-FD select stack OOB.

Zero actual network use: mocked socket/connect/select/poll/readiness/getsockopt.
Pre-fix ASan reproducer reads exactly original G396 Git commit; final gate
must work on high fds including 2048 without ASan error and without silent
success after a failed connect. Never run on Android or access cellular/GPS.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

gps=Path(__file__).resolve().parents[1]/"gps-v195/gps_proxy.c"
s=gps.read_text()
beg=s.index("static int v398_probe_default_supl(")
end=s.index("struct v398_local_close_work {",beg)
actual=s[beg:end]
header=r'''
#define _GNU_SOURCE
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <poll.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <stdarg.h>
static int g_fd,g_close_count,g_probe_count,g_connect_rc,g_so_error,g_poll_result,g_socket_rc;
static int fake_socket(int family,int type,int protocol) {
 assert(family==AF_INET&&type==(SOCK_STREAM|SOCK_CLOEXEC)&&protocol==0);
 ++g_probe_count; if(g_socket_rc){errno=EINTR;return -1;}return g_fd;
}
static int fake_close(int fd) {
 assert(fd==g_fd);++g_close_count;return 0;
}
static int fake_fcntl(int fd,int cmd, ...) {
 assert(fd==g_fd);
 if(cmd==F_GETFL)return O_NONBLOCK;
 assert(cmd==F_SETFL);return 0;
}
static int fake_connect(int fd,const struct sockaddr *addr,socklen_t len) {
 assert(fd==g_fd&&len==sizeof(struct sockaddr_in));
 assert(addr->sa_family==AF_INET);
 if(g_connect_rc==1)return 0;
 if(g_connect_rc==2){errno=ENETUNREACH;return -1;}
 errno=EINPROGRESS;return -1;
}
static int fake_poll(struct pollfd *fds,nfds_t n,int timeout){
 assert(n==1&&fds[0].fd==g_fd&&fds[0].events==POLLOUT);
 assert(timeout==700);
 if(g_poll_result<0){errno=EINTR;return -1;}
 if(g_poll_result==0)return 0;
 fds[0].revents=g_so_error ? POLLERR : POLLOUT;
 return 1;
}
static int fake_select(int maxfd,fd_set *r,fd_set *w,fd_set *e,struct timeval *t){
 (void)r;(void)e;
 assert(maxfd==g_fd+1&&w&&t->tv_sec==0&&t->tv_usec==700000);
 return 0;
}
static int fake_getsockopt(int fd,int level,int name,void *val,socklen_t *len) {
 assert(fd==g_fd&&level==SOL_SOCKET&&name==SO_ERROR&&*len==sizeof(int));
 *(int*)val=g_so_error;return 0;
}
#define socket fake_socket
#define close fake_close
#define fcntl fake_fcntl
#define connect fake_connect
#define poll fake_poll
#define select fake_select
#define getsockopt fake_getsockopt
'''
tail=r'''
static void reset(int fd) {
 g_fd=fd;g_close_count=g_probe_count=g_connect_rc=g_so_error=g_socket_rc=0;
 g_poll_result=1;
}
int main(void){
 reset(2048);
 assert(v398_probe_default_supl(htonl(0x7f000001),7275)==0);
 assert(g_probe_count==1&&g_close_count==1);
 puts("PASS high FD2048 ready and successful connect");
 reset(3000);g_so_error=ECONNREFUSED;
 assert(v398_probe_default_supl(htonl(0x7f000001),7275)==-ECONNREFUSED);
 assert(g_close_count==1);
 puts("PASS high FD3000 actual asynchronous socket error");
 reset(6550);g_poll_result=0;
 assert(v398_probe_default_supl(htonl(0x7f000001),7275)==-ETIMEDOUT);
 assert(g_close_count==1);
 puts("PASS high FD6550 timeout and close");
 reset(2500);g_poll_result=-1;
 assert(v398_probe_default_supl(htonl(0x7f000001),7275)==-EINTR);
 assert(g_close_count==1);
 puts("PASS high FD2500 poll interruption propagates error");
 reset(2050);g_connect_rc=2;
 assert(v398_probe_default_supl(htonl(0x7f000001),7275)==-ENETUNREACH);
 assert(g_close_count==1);
 puts("PASS high FD2050 immediate route failure closes socket");
 reset(2048);g_connect_rc=1;
 assert(v398_probe_default_supl(htonl(0x7f000001),7275)==0);
 assert(g_close_count==1);
 puts("PASS high FD immediate TCP connection closes socket");
 reset(2048);
 assert(v398_probe_default_supl(0,7275)==-EINVAL);
 assert(g_probe_count==0&&g_close_count==0);
 puts("PASS invalid zero SUPL IP does not create socket");
 reset(2048);
 assert(v398_probe_default_supl(htonl(0x7f000001),0)==-EINVAL);
 assert(g_probe_count==0&&g_close_count==0);
 puts("PASS zero port does not create socket");
 reset(2048);g_socket_rc=-1;
 assert(v398_probe_default_supl(htonl(0x7f000001),7275)==-EINTR);
 assert(g_probe_count==1&&g_close_count==0);
 puts("PASS failed socket creation propagates errno without close");
 return 0;
}
'''
def main():
  pa=argparse.ArgumentParser()
  pa.add_argument('--reproduce-actual-g396-unsafe',action='store_true')
  a=pa.parse_args()
  real_actual=actual
  if a.reproduce_actual_g396_unsafe:
    historical=subprocess.run(['git','-C',str(gps.parent.parent),
       'show','89cfc09:gps-v195/gps_proxy.c'],capture_output=True,text=True,check=True).stdout
    left=historical.index('static int v398_probe_default_supl(')
    right=historical.index('struct v398_local_close_work {',left)
    real_actual=historical[left:right]
    if 'FD_SET(fd, &wfds)' not in real_actual or 'poll(&write_ready' in real_actual:
      raise SystemExit('G396 source is not real known FD_SET baseline')
  if not a.reproduce_actual_g396_unsafe:
    if 'poll(&write_ready, 1, 700)' not in real_actual or 'FD_SET(' in real_actual:
      raise SystemExit('Current SUPL source still has unsafe fd_set')
  with tempfile.TemporaryDirectory(prefix='lt013g-g397-highfd-') as d:
    c=Path(d)/'probe.c';p=Path(d)/'probe'
    body=header+real_actual+tail
    c.write_text(body)
    args=['clang','-O0','-g','-std=gnu11','-Wall','-Wextra',
          '-fsanitize=address','-fno-omit-frame-pointer','-fno-pie','-no-pie',
          str(c),'-o',str(p)]
    compiled=subprocess.run(args,capture_output=True,text=True)
    if compiled.returncode:raise SystemExit('G397 C compile failed: '+compiled.stderr[:2000])
    executed=subprocess.run([str(p)],capture_output=True,text=True,env={'ASAN_OPTIONS':'detect_leaks=0:abort_on_error=1'})
    if a.reproduce_actual_g396_unsafe:
      if executed.returncode==0 or 'AddressSanitizer: stack-buffer-overflow' not in executed.stderr:
        raise SystemExit('FAIL: old high FD source did not reproduce real stack overflow ASan!\n'+executed.stderr[:1600])
      print('G397 UNSAFE OLD SOURCE ACTUAL C+ASAN REPRODUCED: FD_SET(fd2048) stack-buffer-overflow (host mocked TCP; NO network)')
    else:
      if executed.returncode:
        raise SystemExit('G397 actual C+ASan new implementation fail '+str(executed.returncode)+'\n'+executed.stderr[:2700])
      print(executed.stdout)
      print('G397 GNSS SUPL HIGH FD ACTUAL C/ASan corrected branch PASS, no real network or device touched')

if __name__=='__main__':main()
