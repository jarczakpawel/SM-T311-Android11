#define _POSIX_C_SOURCE 200809L
#define T311_BRIDGE_IO_DEADLINE_MS 160
#include "../t311_audio_ril_io.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static void wait_ms(long ms)
{
    struct timespec pause = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000 };
    nanosleep(&pause, NULL);
}

static void *trickle(void *arg)
{
    int fd = *(int *)arg;
    for (int i = 0; i < 4; ++i) {
        wait_ms(65);
        if (send(fd, "A", 1, MSG_NOSIGNAL) != 1)
            break;
    }
    return NULL;
}

int main(void)
{
    int fds[2], rc;
    char data[64] = {};
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(write_full(fds[0], "ABCD", 4) == 0);
    assert(read_full(fds[1], data, 4) == 0);
    assert(!memcmp(data, "ABCD", 4));
    close(fds[0]);
    assert(read_full(fds[1], data, 1) == -ECONNRESET);
    close(fds[1]);
    puts("G612 immediate transfer and peer closure PASS");

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    int64_t start = t311_io_now_ms();
    rc = read_full(fds[0], data, 4);
    int64_t elapsed = t311_io_now_ms() - start;
    assert(rc == -ETIMEDOUT && elapsed >= 100 && elapsed < 600);
    close(fds[0]);
    close(fds[1]);
    puts("G612 silent peer hard read deadline PASS");

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    pthread_t tid;
    assert(pthread_create(&tid, NULL, trickle, &fds[1]) == 0);
    start = t311_io_now_ms();
    rc = read_full(fds[0], data, 4);
    elapsed = t311_io_now_ms() - start;
    assert(rc == -ETIMEDOUT && elapsed >= 100 && elapsed < 600);
    assert(pthread_join(tid, NULL) == 0);
    close(fds[0]);
    close(fds[1]);
    puts("G612 trickle input cannot extend read deadline PASS");

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    int sndbuf = 4096;
    assert(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf)) == 0);
    char fill[512] = {};
    for (int i = 0; i < 10000; ++i) {
        ssize_t written = send(fds[0], fill, sizeof(fill), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (written < 0) {
            assert(errno == EAGAIN || errno == EWOULDBLOCK);
            break;
        }
        assert(i != 9999);
    }
    start = t311_io_now_ms();
    rc = write_full(fds[0], "XYZ", 3);
    elapsed = t311_io_now_ms() - start;
    assert(rc == -ETIMEDOUT && elapsed >= 100 && elapsed < 600);
    close(fds[0]);
    close(fds[1]);
    puts("G612 full send buffer hard write deadline PASS");

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    int64_t deadline = t311_io_now_ms() + T311_BRIDGE_IO_DEADLINE_MS;
    assert(write_full_until(fds[0], "HEAD", 4, deadline) == 0);
    assert(read_full_until(fds[1], data, 4, deadline) == 0);
    wait_ms(T311_BRIDGE_IO_DEADLINE_MS + 20);
    start = t311_io_now_ms();
    assert(read_full_until(fds[1], data, 4, deadline) == -ETIMEDOUT);
    assert(write_full_until(fds[0], "BODY", 4, deadline) == -ETIMEDOUT);
    assert(t311_io_now_ms() - start < 80);
    close(fds[0]);
    close(fds[1]);
    puts("G612 one shared header and body deadline PASS");
    return 0;
}
