#ifndef T311_AUDIO_RIL_IO_H
#define T311_AUDIO_RIL_IO_H

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/socket.h>
#include <time.h>

#ifndef T311_BRIDGE_IO_DEADLINE_MS
#define T311_BRIDGE_IO_DEADLINE_MS 500
#endif

static int64_t t311_io_now_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int t311_io_ready(int fd, short events, int64_t deadline)
{
    for (;;) {
        int64_t now = t311_io_now_ms();
        if (now < 0)
            return -errno;
        int64_t remaining = deadline - now;
        if (remaining <= 0)
            return -ETIMEDOUT;
        struct pollfd pfd = { .fd = fd, .events = events, .revents = 0 };
        int ret = poll(&pfd, 1, (int)remaining);
        if (ret > 0)
            return (pfd.revents & POLLNVAL) ? -EBADF : 0;
        if (ret == 0)
            return -ETIMEDOUT;
        if (errno != EINTR)
            return -errno;
    }
}

static int read_full_until(int fd, void *buffer, size_t size, int64_t deadline)
{
    uint8_t *p = buffer;
    while (size != 0) {
        int ret = t311_io_ready(fd, POLLIN, deadline);
        if (ret != 0)
            return ret;
        ssize_t n = recv(fd, p, size, MSG_DONTWAIT);
        if (n == 0)
            return -ECONNRESET;
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -errno;
        }
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static int write_full_until(int fd, const void *buffer, size_t size, int64_t deadline)
{
    const uint8_t *p = buffer;
    while (size != 0) {
        int ret = t311_io_ready(fd, POLLOUT, deadline);
        if (ret != 0)
            return ret;
        ssize_t n = send(fd, p, size, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n == 0)
            return -EPIPE;
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return -errno;
        }
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static int read_full(int fd, void *buffer, size_t size)
{
    int64_t start = t311_io_now_ms();
    if (start < 0)
        return -errno;
    return read_full_until(fd, buffer, size, start + T311_BRIDGE_IO_DEADLINE_MS);
}

static int write_full(int fd, const void *buffer, size_t size)
{
    int64_t start = t311_io_now_ms();
    if (start < 0)
        return -errno;
    return write_full_until(fd, buffer, size, start + T311_BRIDGE_IO_DEADLINE_MS);
}

#endif
