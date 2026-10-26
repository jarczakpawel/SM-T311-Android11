#define LOG_TAG "t311_audio_ril"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <log/log.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "t311_audio_ril_io.h"

#define T311_LOCAL_SOCKET "t311_audio_ril"
#define T311_RIL_SOCKET "Multiclient"
#define T311_BRIDGE_MAGIC 0x54333131u
#define T311_BRIDGE_VERSION 1u
#define T311_RIL_REQUEST_OEM_HOOK_RAW 59
#define T311_RIL_UNSOL_WB_AMR 11017
#define T311_MAX_RIL_FRAME 8192u

#define T311_OEM_FUNC_SOUND 0x08
#define T311_OEM_SET_VOLUME 0x03
#define T311_OEM_SET_AUDIO_PATH 0x05
#define T311_OEM_SET_CLOCK 0x0a
#define T311_OEM_SET_MUTE 0x0b
#define T311_OEM_SET_TWO_MIC 0x0d

enum t311_bridge_op {
    T311_BRIDGE_HELLO = 0,
    T311_BRIDGE_SET_PATH = 1,
    T311_BRIDGE_SET_CLOCK = 2,
    T311_BRIDGE_SET_VOLUME = 3,
    T311_BRIDGE_SET_MUTE = 4,
    T311_BRIDGE_SET_TWO_MIC = 5,
    T311_BRIDGE_RELEASE = 6,
    T311_BRIDGE_EVT_WB_AMR = 100,
};

struct t311_bridge_msg {
    uint32_t magic;
    uint32_t version;
    uint32_t op;
    int32_t a;
    int32_t b;
};

struct bridge_state {
    int listener_fd;
    int client_fd;
    int ril_fd;
    int wb_valid;
    int wb_amr;
};

static void close_fd(int *fd)
{
    if (*fd >= 0) {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

static int open_abstract_socket(const char *name)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    const struct timeval limit = { .tv_sec = 0, .tv_usec = 500000 };
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit)) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t n = strlen(name);
    if (n + 1 > sizeof(addr.sun_path)) {
        close(fd);
        return -ENAMETOOLONG;
    }
    memcpy(addr.sun_path + 1, name, n);
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n);
    if (connect(fd, (struct sockaddr *)&addr, len) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }
    return fd;
}

static int create_listener(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char name[] = T311_LOCAL_SOCKET;
    memcpy(addr.sun_path + 1, name, sizeof(name) - 1);
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1);
    if (bind(fd, (struct sockaddr *)&addr, len) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }
    if (listen(fd, 2) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }
    return fd;
}

static int connect_ril(struct bridge_state *s)
{
    if (s->ril_fd >= 0)
        return 0;
    int fd = open_abstract_socket(T311_RIL_SOCKET);
    if (fd < 0)
        return fd;
    s->ril_fd = fd;
    ALOGI("V197 BRIDGE Multiclient connected on-demand");
    return 0;
}

static int send_ril_payload(struct bridge_state *s, const uint8_t *data, size_t data_size)
{
    int ret = connect_ril(s);
    if (ret != 0)
        return ret;

    size_t padded = (data_size + 3u) & ~3u;
    size_t frame_size = 12u + padded;
    if (frame_size > 64u || data_size > INT32_MAX)
        return -EINVAL;

    uint8_t frame[64];
    int32_t request = T311_RIL_REQUEST_OEM_HOOK_RAW;
    int32_t token = 1;
    int32_t length = (int32_t)data_size;
    memcpy(frame, &request, sizeof(request));
    memcpy(frame + 4, &token, sizeof(token));
    memcpy(frame + 8, &length, sizeof(length));
    memcpy(frame + 12, data, data_size);
    if (padded > data_size)
        memset(frame + 12 + data_size, 0, padded - data_size);

    uint32_t header = htonl((uint32_t)frame_size);
    int64_t start = t311_io_now_ms();
    if (start < 0)
        ret = -errno;
    else {
        int64_t deadline = start + T311_BRIDGE_IO_DEADLINE_MS;
        ret = write_full_until(s->ril_fd, &header, sizeof(header), deadline);
        if (ret == 0)
            ret = write_full_until(s->ril_fd, frame, frame_size, deadline);
    }
    if (ret != 0) {
        ALOGE("V197 BRIDGE Multiclient write failed ret=%d", ret);
        close_fd(&s->ril_fd);
    }
    return ret;
}

static int send_event(struct bridge_state *s, uint32_t op, int32_t a, int32_t b)
{
    if (s->client_fd < 0)
        return -ENOTCONN;
    const struct t311_bridge_msg msg = {
        .magic = T311_BRIDGE_MAGIC,
        .version = T311_BRIDGE_VERSION,
        .op = op,
        .a = a,
        .b = b,
    };
    int ret = write_full(s->client_fd, &msg, sizeof(msg));
    if (ret != 0) {
        ALOGW("V197 BRIDGE HAL client disconnected ret=%d", ret);
        close_fd(&s->client_fd);
    }
    return ret;
}

static int get_i32(const uint8_t *p)
{
    int32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static void process_ril_frame(struct bridge_state *s, const uint8_t *frame, uint32_t size)
{
    if (size < 8 || get_i32(frame) != 1)
        return;

    int32_t id = get_i32(frame + 4);
    if (id != T311_RIL_UNSOL_WB_AMR || size < 12)
        return;

    int32_t length = get_i32(frame + 8);
    if (length <= 0 || (uint32_t)length > size - 12)
        return;

    int wb;
    if (length >= 4)
        wb = get_i32(frame + 12) != 0;
    else
        wb = frame[12] != 0;

    s->wb_valid = 1;
    s->wb_amr = wb;
    ALOGI("V197 BRIDGE WB_AMR=%d", wb);
    (void)send_event(s, T311_BRIDGE_EVT_WB_AMR, wb, 0);
}

static int read_ril_frame(struct bridge_state *s)
{
    uint32_t header;
    int64_t start = t311_io_now_ms();
    if (start < 0)
        return -errno;
    int64_t deadline = start + T311_BRIDGE_IO_DEADLINE_MS;
    int ret = read_full_until(s->ril_fd, &header, sizeof(header), deadline);
    if (ret != 0)
        return ret;

    uint32_t size = ntohl(header);
    if (size < 4 || size > T311_MAX_RIL_FRAME)
        return -EPROTO;

    uint8_t *frame = malloc(size);
    if (frame == NULL)
        return -ENOMEM;
    ret = read_full_until(s->ril_fd, frame, size, deadline);
    if (ret == 0)
        process_ril_frame(s, frame, size);
    free(frame);
    return ret;
}

static int process_command(struct bridge_state *s, const struct t311_bridge_msg *msg)
{
    if (msg->magic != T311_BRIDGE_MAGIC || msg->version != T311_BRIDGE_VERSION)
        return -EPROTO;

    uint8_t data[6];
    size_t size = 0;
    switch (msg->op) {
    case T311_BRIDGE_HELLO:
        /* Keep Samsung Multiclient completely idle until an actual call/audio
         * command needs it.  The HAL-local control socket may stay connected
         * for the lifetime of audioserver without occupying a proprietary RIL
         * OEM-client slot. */
        ALOGI("V197 BRIDGE HELLO local-only ril_connected=%d", s->ril_fd >= 0);
        return 0;
    case T311_BRIDGE_SET_PATH:
        data[0] = T311_OEM_FUNC_SOUND;
        data[1] = T311_OEM_SET_AUDIO_PATH;
        data[2] = 0;
        data[3] = 6;
        data[4] = (uint8_t)msg->a;
        data[5] = 0;
        size = 6;
        break;
    case T311_BRIDGE_SET_CLOCK:
        data[0] = T311_OEM_FUNC_SOUND;
        data[1] = T311_OEM_SET_CLOCK;
        data[2] = 0;
        data[3] = 5;
        data[4] = msg->a ? 1 : 0;
        size = 5;
        break;
    case T311_BRIDGE_SET_VOLUME:
        data[0] = T311_OEM_FUNC_SOUND;
        data[1] = T311_OEM_SET_VOLUME;
        data[2] = 0;
        data[3] = 6;
        data[4] = (uint8_t)msg->a;
        data[5] = (uint8_t)msg->b;
        size = 6;
        break;
    case T311_BRIDGE_SET_MUTE:
        data[0] = T311_OEM_FUNC_SOUND;
        data[1] = T311_OEM_SET_MUTE;
        data[2] = 0;
        data[3] = 5;
        data[4] = msg->a ? 1 : 0;
        size = 5;
        break;
    case T311_BRIDGE_SET_TWO_MIC:
        data[0] = T311_OEM_FUNC_SOUND;
        data[1] = T311_OEM_SET_TWO_MIC;
        data[2] = 0;
        data[3] = 6;
        data[4] = 0;
        data[5] = msg->a ? 1 : 0;
        size = 6;
        break;
    case T311_BRIDGE_RELEASE:
        /* The request frames already written to the Unix socket are owned by
         * rild.  Release only our OEM-client connection after the call so GPS
         * and other legacy Samsung clients do not contend with an idle audio
         * connection. */
        ALOGI("V197 BRIDGE RELEASE ril_connected=%d", s->ril_fd >= 0);
        close_fd(&s->ril_fd);
        s->wb_valid = 0;
        s->wb_amr = 0;
        return 0;
    default:
        return -EINVAL;
    }

    int ret = send_ril_payload(s, data, size);
    if (ret != 0)
        ALOGW("V197 BRIDGE command op=%u failed ret=%d", msg->op, ret);
    return ret;
}

static int accept_hal_client(struct bridge_state *s)
{
    int fd = accept(s->listener_fd, NULL, NULL);
    if (fd < 0)
        return -errno;
    (void)fcntl(fd, F_SETFD, FD_CLOEXEC);
    const struct timeval limit = { .tv_sec = 0, .tv_usec = 500000 };
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof(limit)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof(limit)) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }

    close_fd(&s->client_fd);
    /* A new HAL instance owns the local control channel.  Do not implicitly
     * keep a previous call's Multiclient connection alive. */
    close_fd(&s->ril_fd);
    s->wb_valid = 0;
    s->wb_amr = 0;
    s->client_fd = fd;
    ALOGI("V197 BRIDGE HAL client connected ril=DEFERRED");
    return 0;
}

static int use_stock_audio_identity(int argc, char **argv)
{
    static const char stock_cmdline[] = "/system/bin/mediaserver";

    if (argc < 1 || argv == NULL || argv[0] == NULL)
        return -EINVAL;

    const size_t old_len = strlen(argv[0]);
    const size_t new_len = sizeof(stock_cmdline) - 1;
    if (old_len < new_len)
        return -ENOSPC;

    /* Samsung's XMM6262 libsec-ril authorizes Multiclient peers with the
     * peer uid plus /proc/<pid>/cmdline.  The stock audio HAL lived in
     * mediaserver (uid media).  Keep the real executable/SELinux domain,
     * but present the same stock argv[0] to that legacy authorization gate.
     * Do not change PR_SET_NAME: pidof/comm must remain t311-audio-ril. */
    memset(argv[0], 0, old_len);
    memcpy(argv[0], stock_cmdline, new_len);
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);

    int identity_ret = use_stock_audio_identity(argc, argv);
    if (identity_ret != 0) {
        ALOGE("V197 BRIDGE stock identity failed ret=%d", identity_ret);
        return 1;
    }
    ALOGI("V197 BRIDGE stock identity uid=%u cmdline=/system/bin/mediaserver",
          (unsigned)getuid());

    struct bridge_state s = {
        .listener_fd = -1,
        .client_fd = -1,
        .ril_fd = -1,
        .wb_valid = 0,
        .wb_amr = 0,
    };

    s.listener_fd = create_listener();
    if (s.listener_fd < 0) {
        ALOGE("V197 BRIDGE listener failed ret=%d", s.listener_fd);
        return 1;
    }

    int ret = 0;
    ALOGI("V197 BRIDGE ready event-driven Multiclient=ON_DEMAND");

    for (;;) {
        struct pollfd pfds[3] = {
            { .fd = s.listener_fd, .events = POLLIN },
            { .fd = s.client_fd, .events = POLLIN },
            { .fd = s.ril_fd, .events = POLLIN },
        };
        int n = poll(pfds, 3, -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            ALOGE("V197 BRIDGE poll failed: %s", strerror(errno));
            break;
        }

        if (pfds[0].revents & POLLIN)
            (void)accept_hal_client(&s);

        if (s.client_fd >= 0 && pfds[1].fd == s.client_fd &&
            (pfds[1].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
            if (pfds[1].revents & POLLIN) {
                struct t311_bridge_msg msg;
                ret = read_full(s.client_fd, &msg, sizeof(msg));
                if (ret == 0)
                    ret = process_command(&s, &msg);
            } else {
                ret = -ECONNRESET;
            }
            if (ret != 0) {
                ALOGI("V197 BRIDGE HAL client closed ret=%d release-ril=1", ret);
                close_fd(&s.client_fd);
                close_fd(&s.ril_fd);
                s.wb_valid = 0;
                s.wb_amr = 0;
            }
        }

        if (s.ril_fd >= 0 && pfds[2].fd == s.ril_fd &&
            (pfds[2].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
            if (pfds[2].revents & POLLIN)
                ret = read_ril_frame(&s);
            else
                ret = -ECONNRESET;
            if (ret != 0) {
                ALOGW("V197 BRIDGE Multiclient disconnected ret=%d", ret);
                close_fd(&s.ril_fd);
            }
        }
    }

    close_fd(&s.ril_fd);
    close_fd(&s.client_fd);
    close_fd(&s.listener_fd);
    return 1;
}
