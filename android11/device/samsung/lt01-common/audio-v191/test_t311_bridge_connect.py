import subprocess
import tempfile
from pathlib import Path

source = Path(__file__).resolve().parent
program = r'''
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include "t311_ril_audio.c"

static long elapsed_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec-start.tv_sec)*1000 + (end.tv_nsec-start.tv_nsec)/1000000;
}

static socklen_t abstract_address(struct sockaddr_un *addr) {
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    memcpy(addr->sun_path + 1, T311_BRIDGE_SOCKET, strlen(T311_BRIDGE_SOCKET));
    return offsetof(struct sockaddr_un, sun_path) + 1 + strlen(T311_BRIDGE_SOCKET);
}

static int check_connection(struct t311_ril_audio *ril, int must_succeed) {
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    int result = connect_locked(ril);
    clock_gettime(CLOCK_MONOTONIC, &end);
    long ms = elapsed_ms(start,end);
    if (ms < 0 || ms > 350) {
        fprintf(stderr, "BLOCKED: unbounded connection %ldms\n",ms);
        return 1;
    }
    if (must_succeed) {
        if (result != 0 || ril->fd < 0 ||
                (fcntl(ril->fd, F_GETFL) & O_NONBLOCK)) return 2;
        disconnect_locked(ril);
    } else {
        if (result >= 0) return 3;
    }
    return 0;
}

int main(void) {
    struct t311_ril_audio ril;
    memset(&ril, 0, sizeof(ril));
    ril.fd = -1;
    if (pthread_mutex_init(&ril.lock, NULL) ||
        pthread_cond_init(&ril.cond, NULL)) return 11;
    struct sockaddr_un addr;
    socklen_t len = abstract_address(&addr);
    if (check_connection(&ril, 0)) return 12;
    puts("G566_BRIDGE_MISSING_SERVER_FAILS_FAST_PASS");

    int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0 || bind(server, (struct sockaddr *)&addr, len) ||
        listen(server, 8)) return 13;
    if (check_connection(&ril, 1)) return 14;
    puts("G566_BRIDGE_SUCCESS_RESTORES_BLOCKING_MODE_PASS");
    close(server);

    server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0 || bind(server, (struct sockaddr *)&addr, len) ||
        listen(server, 0)) return 15;
    int backlog[32], held=0, saturated=0;
    for (int i=0;i<32;i++) {
        int fd=socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,0);
        if (fd<0) return 16;
        if (connect(fd,(struct sockaddr*)&addr,len) == 0) {
            backlog[held++]=fd;
        } else {
            if (errno == EAGAIN) saturated=1;
            close(fd);
            if (saturated) break;
        }
    }
    if (!saturated) return 17;
    if (check_connection(&ril, 0)) return 18;
    puts("G566_BRIDGE_FULL_LISTEN_BACKLOG_FAILS_FAST_PASS");
    for(int i=0;i<held;i++) close(backlog[i]);
    close(server);
    pthread_cond_destroy(&ril.cond);
    pthread_mutex_destroy(&ril.lock);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="g566_audio_connect_") as d:
    root = Path(d)
    logdir = root/"log"
    logdir.mkdir()
    (logdir/"log.h").write_text('#ifndef G566_STUB_LOG_H\n#define G566_STUB_LOG_H\n#define ALOGI(...) ((void)0)\n#define ALOGW(...) ((void)0)\n#endif\n')
    (root/"test.c").write_text(program)
    binary = root/"bridge_test"
    subprocess.run(["gcc","-O2","-Wall","-Wextra","-Werror","-Wno-unused-parameter",
                    "-std=gnu11","-pthread","-I",str(root),"-I",str(source),
                    str(root/"test.c"),"-o",str(binary)],check=True)
    result=subprocess.run([str(binary)],check=True,capture_output=True,text=True)
    print(result.stdout)
