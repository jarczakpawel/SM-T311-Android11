#define LOG_TAG "audio_ril_lt013g"

#include "t311_ril_audio.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <log/log.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define T311_BRIDGE_SOCKET "t311_audio_ril"
#define T311_BRIDGE_MAGIC 0x54333131u
#define T311_BRIDGE_VERSION 1u

enum t311_bridge_op {
    T311_BRIDGE_HELLO = 0,
    T311_BRIDGE_SET_PATH = 1,
    T311_BRIDGE_SET_CLOCK = 2,
    T311_BRIDGE_SET_VOLUME = 3,
    T311_BRIDGE_SET_MUTE = 4,
    T311_BRIDGE_SET_TWO_MIC = 5,
    T311_BRIDGE_EVT_WB_AMR = 100,
};

struct t311_bridge_msg {
    uint32_t magic;
    uint32_t version;
    uint32_t op;
    int32_t a;
    int32_t b;
};

static int read_full(int fd, void *buffer, size_t size)
{
    uint8_t *p = buffer;
    while (size != 0) {
        ssize_t n = read(fd, p, size);
        if (n == 0)
            return -ECONNRESET;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static int write_full(int fd, const void *buffer, size_t size)
{
    const uint8_t *p = buffer;
    while (size != 0) {
        ssize_t n = send(fd, p, size, MSG_NOSIGNAL);
        if (n == 0)
            return -EPIPE;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }
        p += n;
        size -= (size_t)n;
    }
    return 0;
}

static void disconnect_locked(struct t311_ril_audio *ril)
{
    if (ril->fd >= 0) {
        shutdown(ril->fd, SHUT_RDWR);
        close(ril->fd);
        ril->fd = -1;
        ++ril->generation;
    }
}

static int send_msg_locked(struct t311_ril_audio *ril, uint32_t op, int32_t a, int32_t b);

static int connect_locked(struct t311_ril_audio *ril)
{
    if (ril->fd >= 0)
        return 0;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0)
        return -errno;

    const struct timeval limit = { .tv_sec = 0, .tv_usec = 500000 };
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit)) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char name[] = T311_BRIDGE_SOCKET;
    memcpy(addr.sun_path + 1, name, sizeof(name) - 1);
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1);

    if (connect(fd, (struct sockaddr *)&addr, len) != 0) {
        int ret = -errno;
        if (errno != EINPROGRESS) {
            close(fd);
            return ret;
        }

        struct pollfd ready = { .fd = fd, .events = POLLOUT };
        int count = poll(&ready, 1, 250);
        if (count <= 0) {
            ret = count == 0 ? -ETIMEDOUT : -errno;
            close(fd);
            return ret;
        }
        int socket_error = 0;
        socklen_t error_size = sizeof(socket_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_size) != 0 ||
            error_size != sizeof(socket_error)) {
            ret = -errno;
            close(fd);
            return ret != 0 ? ret : -EIO;
        }
        if (socket_error != 0) {
            close(fd);
            return -socket_error;
        }
    }

    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }

    ril->fd = fd;
    ++ril->generation;
    pthread_cond_broadcast(&ril->cond);
    ALOGI("V191 BRIDGE connected generation=%u", ril->generation);
    return 0;
}

static int send_msg_locked(struct t311_ril_audio *ril, uint32_t op, int32_t a, int32_t b)
{
    int ret = connect_locked(ril);
    if (ret != 0)
        return ret;

    const struct t311_bridge_msg msg = {
        .magic = T311_BRIDGE_MAGIC,
        .version = T311_BRIDGE_VERSION,
        .op = op,
        .a = a,
        .b = b,
    };
    ret = write_full(ril->fd, &msg, sizeof(msg));
    if (ret != 0)
        disconnect_locked(ril);
    return ret;
}

static void dispatch_msg(struct t311_ril_audio *ril, const struct t311_bridge_msg *msg)
{
    if (msg->magic != T311_BRIDGE_MAGIC || msg->version != T311_BRIDGE_VERSION ||
        msg->op != T311_BRIDGE_EVT_WB_AMR)
        return;

    t311_wb_callback callback;
    void *data;
    pthread_mutex_lock(&ril->lock);
    callback = ril->wb_callback;
    data = ril->wb_data;
    pthread_mutex_unlock(&ril->lock);

    ALOGI("V191 BRIDGE WB_AMR=%d", msg->a != 0);
    if (callback != NULL)
        callback(data, msg->a != 0);
}

static void *reader_thread(void *arg)
{
    struct t311_ril_audio *ril = arg;

    for (;;) {
        pthread_mutex_lock(&ril->lock);
        while (!ril->stop && ril->fd < 0)
            pthread_cond_wait(&ril->cond, &ril->lock);
        if (ril->stop) {
            pthread_mutex_unlock(&ril->lock);
            break;
        }

        uint32_t generation = ril->generation;
        int fd = dup(ril->fd);
        pthread_mutex_unlock(&ril->lock);
        if (fd < 0)
            continue;

        for (;;) {
            struct t311_bridge_msg msg;
            int ret = read_full(fd, &msg, sizeof(msg));
            if (ret != 0)
                break;
            dispatch_msg(ril, &msg);
        }

        close(fd);
        pthread_mutex_lock(&ril->lock);
        if (ril->generation == generation) {
            ALOGW("V191 BRIDGE disconnected generation=%u", generation);
            disconnect_locked(ril);
        }
        pthread_mutex_unlock(&ril->lock);
    }
    return NULL;
}

int t311_ril_audio_init(struct t311_ril_audio *ril, t311_wb_callback callback, void *data)
{
    memset(ril, 0, sizeof(*ril));
    ril->fd = -1;
    ril->wb_callback = callback;
    ril->wb_data = data;

    int ret = pthread_mutex_init(&ril->lock, NULL);
    if (ret != 0)
        return -ret;
    ret = pthread_cond_init(&ril->cond, NULL);
    if (ret != 0) {
        pthread_mutex_destroy(&ril->lock);
        return -ret;
    }
    ret = pthread_create(&ril->thread, NULL, reader_thread, ril);
    if (ret != 0) {
        pthread_cond_destroy(&ril->cond);
        pthread_mutex_destroy(&ril->lock);
        return -ret;
    }
    ril->thread_started = true;

    pthread_mutex_lock(&ril->lock);
    ret = send_msg_locked(ril, T311_BRIDGE_HELLO, 0, 0);
    pthread_mutex_unlock(&ril->lock);
    if (ret != 0)
        ALOGI("V191 BRIDGE init deferred ret=%d", ret);
    else
        ALOGI("V191 BRIDGE client initialized");
    return 0;
}

void t311_ril_audio_close(struct t311_ril_audio *ril)
{
    pthread_mutex_lock(&ril->lock);
    ril->stop = true;
    disconnect_locked(ril);
    pthread_cond_broadcast(&ril->cond);
    pthread_mutex_unlock(&ril->lock);

    if (ril->thread_started)
        pthread_join(ril->thread, NULL);
    pthread_cond_destroy(&ril->cond);
    pthread_mutex_destroy(&ril->lock);
}

static int send_command(struct t311_ril_audio *ril, uint32_t op, int32_t a, int32_t b)
{
    pthread_mutex_lock(&ril->lock);
    int ret = send_msg_locked(ril, op, a, b);
    pthread_mutex_unlock(&ril->lock);
    return ret;
}

int t311_ril_audio_set_path(struct t311_ril_audio *ril, int path)
{
    return send_command(ril, T311_BRIDGE_SET_PATH, path, 0);
}

int t311_ril_audio_set_clock(struct t311_ril_audio *ril, bool start)
{
    return send_command(ril, T311_BRIDGE_SET_CLOCK, start ? 1 : 0, 0);
}

int t311_ril_audio_set_volume(struct t311_ril_audio *ril, int type, int level)
{
    return send_command(ril, T311_BRIDGE_SET_VOLUME, type, level);
}

int t311_ril_audio_set_mute(struct t311_ril_audio *ril, bool mute)
{
    return send_command(ril, T311_BRIDGE_SET_MUTE, mute ? 1 : 0, 0);
}

int t311_ril_audio_set_two_mic(struct t311_ril_audio *ril, bool enabled)
{
    return send_command(ril, T311_BRIDGE_SET_TWO_MIC, enabled ? 1 : 0, 0);
}
