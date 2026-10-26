#define LOG_TAG "audio_ril_lt013g"

#include "t311_ril_audio.h"

#include <arpa/inet.h>
#include <errno.h>
#include <log/log.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define T311_RIL_REQUEST_OEM_HOOK_RAW 59
#define T311_RIL_UNSOL_WB_AMR 11017
#define T311_OEM_FUNC_SOUND 0x08
#define T311_OEM_SET_VOLUME 0x03
#define T311_OEM_SET_AUDIO_PATH 0x05
#define T311_OEM_SET_CLOCK 0x0a
#define T311_OEM_SET_MUTE 0x0b

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
        ril->generation++;
    }
}

static int send_payload_locked(struct t311_ril_audio *ril, const uint8_t *data, size_t data_size)
{
    uint8_t frame[64];
    size_t padded = (data_size + 3u) & ~3u;
    size_t frame_size = 12u + padded;
    if (ril->fd < 0)
        return -ENOTCONN;
    if (frame_size > sizeof(frame) || data_size > INT32_MAX)
        return -EINVAL;

    int32_t request = T311_RIL_REQUEST_OEM_HOOK_RAW;
    int32_t token = ++ril->token;
    int32_t length = (int32_t)data_size;
    memcpy(frame, &request, sizeof(request));
    memcpy(frame + 4, &token, sizeof(token));
    memcpy(frame + 8, &length, sizeof(length));
    memcpy(frame + 12, data, data_size);
    if (padded > data_size)
        memset(frame + 12 + data_size, 0, padded - data_size);

    uint32_t header = htonl((uint32_t)frame_size);
    int ret = write_full(ril->fd, &header, sizeof(header));
    if (ret == 0)
        ret = write_full(ril->fd, frame, frame_size);
    return ret;
}

static int replay_state_locked(struct t311_ril_audio *ril)
{
    int ret;
    if (ril->path_valid) {
        uint8_t data[6] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_AUDIO_PATH, 0, 6,
                            (uint8_t)ril->path, 0 };
        ret = send_payload_locked(ril, data, sizeof(data));
        if (ret != 0)
            return ret;
    }
    if (ril->clock_valid) {
        uint8_t data[5] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_CLOCK, 0, 5,
                            ril->clock_started ? 1 : 0 };
        ret = send_payload_locked(ril, data, sizeof(data));
        if (ret != 0)
            return ret;
    }
    if (ril->volume_valid) {
        uint8_t data[6] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_VOLUME, 0, 6,
                            (uint8_t)ril->volume_type, (uint8_t)ril->volume_level };
        ret = send_payload_locked(ril, data, sizeof(data));
        if (ret != 0)
            return ret;
    }
    if (ril->mute_valid) {
        uint8_t data[5] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_MUTE, 0, 5,
                            ril->muted ? 1 : 0 };
        ret = send_payload_locked(ril, data, sizeof(data));
        if (ret != 0)
            return ret;
    }
    return 0;
}

static int connect_locked(struct t311_ril_audio *ril, bool *replayed)
{
    if (replayed != NULL)
        *replayed = false;
    if (ril->fd >= 0)
        return 0;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -errno;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    const char name[] = "Multiclient";
    memcpy(addr.sun_path + 1, name, sizeof(name) - 1);
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1);

    if (connect(fd, (struct sockaddr *)&addr, len) != 0) {
        int ret = -errno;
        close(fd);
        return ret;
    }

    ril->fd = fd;
    ril->generation++;
    int ret = replay_state_locked(ril);
    if (ret != 0) {
        disconnect_locked(ril);
        return ret;
    }
    if (replayed != NULL)
        *replayed = true;
    ALOGI("V189.1 RIL connected generation=%u", ril->generation);
    return 0;
}

static int send_raw_locked(struct t311_ril_audio *ril, const uint8_t *data, size_t data_size)
{
    bool replayed = false;
    int ret = connect_locked(ril, &replayed);
    if (ret != 0)
        return ret;
    if (!replayed)
        ret = send_payload_locked(ril, data, data_size);
    if (ret != 0)
        disconnect_locked(ril);
    return ret;
}

static int32_t get_i32(const uint8_t *p)
{
    int32_t value;
    memcpy(&value, p, sizeof(value));
    return value;
}

static void *reader_thread(void *arg)
{
    struct t311_ril_audio *ril = arg;
    for (;;) {
        pthread_mutex_lock(&ril->lock);
        bool stop = ril->stop;
        int ret = stop ? -ECANCELED : connect_locked(ril, NULL);
        int fd = -1;
        uint32_t generation = ril->generation;
        if (!stop && ret == 0 && ril->fd >= 0)
            fd = dup(ril->fd);
        pthread_mutex_unlock(&ril->lock);

        if (stop)
            break;
        if (ret != 0 || fd < 0) {
            if (fd >= 0)
                close(fd);
            usleep(250000);
            continue;
        }

        uint32_t header;
        ret = read_full(fd, &header, sizeof(header));
        if (ret != 0) {
            close(fd);
            pthread_mutex_lock(&ril->lock);
            if (ril->generation == generation)
                disconnect_locked(ril);
            pthread_mutex_unlock(&ril->lock);
            usleep(250000);
            continue;
        }

        uint32_t size = ntohl(header);
        if (size < 4 || size > 8192) {
            close(fd);
            pthread_mutex_lock(&ril->lock);
            if (ril->generation == generation)
                disconnect_locked(ril);
            pthread_mutex_unlock(&ril->lock);
            continue;
        }

        uint8_t *frame = malloc(size);
        if (frame == NULL) {
            close(fd);
            usleep(250000);
            continue;
        }
        ret = read_full(fd, frame, size);
        close(fd);
        if (ret == 0 && size >= 12 && get_i32(frame) == 1) {
            int32_t id = get_i32(frame + 4);
            int32_t length = get_i32(frame + 8);
            if (id == T311_RIL_UNSOL_WB_AMR && length >= 4 &&
                    (uint32_t)length <= size - 12 && size >= 16) {
                int enabled = get_i32(frame + 12) != 0;
                t311_wb_callback callback;
                void *data;
                pthread_mutex_lock(&ril->lock);
                callback = ril->wb_callback;
                data = ril->wb_data;
                pthread_mutex_unlock(&ril->lock);
                ALOGI("V189.1 RIL WB_AMR=%d", enabled);
                if (callback != NULL)
                    callback(data, enabled);
            }
        }
        free(frame);
        if (ret != 0) {
            pthread_mutex_lock(&ril->lock);
            if (ril->generation == generation)
                disconnect_locked(ril);
            pthread_mutex_unlock(&ril->lock);
        }
    }
    return NULL;
}

int t311_ril_audio_init(struct t311_ril_audio *ril, t311_wb_callback callback, void *data)
{
    memset(ril, 0, sizeof(*ril));
    ril->fd = -1;
    ril->token = 1;
    ril->wb_callback = callback;
    ril->wb_data = data;
    int ret = pthread_mutex_init(&ril->lock, NULL);
    if (ret != 0)
        return -ret;
    ret = pthread_create(&ril->thread, NULL, reader_thread, ril);
    if (ret != 0) {
        pthread_mutex_destroy(&ril->lock);
        return -ret;
    }
    ril->thread_started = true;
    return 0;
}

void t311_ril_audio_close(struct t311_ril_audio *ril)
{
    pthread_mutex_lock(&ril->lock);
    ril->stop = true;
    disconnect_locked(ril);
    pthread_mutex_unlock(&ril->lock);
    if (ril->thread_started)
        pthread_join(ril->thread, NULL);
    pthread_mutex_destroy(&ril->lock);
}

int t311_ril_audio_set_path(struct t311_ril_audio *ril, int path)
{
    uint8_t data[6] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_AUDIO_PATH, 0, 6,
                        (uint8_t)path, 0 };
    pthread_mutex_lock(&ril->lock);
    ril->path = path;
    ril->path_valid = true;
    int ret = send_raw_locked(ril, data, sizeof(data));
    pthread_mutex_unlock(&ril->lock);
    return ret;
}

int t311_ril_audio_set_clock(struct t311_ril_audio *ril, bool start)
{
    uint8_t data[5] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_CLOCK, 0, 5, start ? 1 : 0 };
    pthread_mutex_lock(&ril->lock);
    ril->clock_started = start;
    ril->clock_valid = true;
    int ret = send_raw_locked(ril, data, sizeof(data));
    pthread_mutex_unlock(&ril->lock);
    return ret;
}

int t311_ril_audio_set_volume(struct t311_ril_audio *ril, int type, int level)
{
    uint8_t data[6] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_VOLUME, 0, 6,
                        (uint8_t)type, (uint8_t)level };
    pthread_mutex_lock(&ril->lock);
    ril->volume_type = type;
    ril->volume_level = level;
    ril->volume_valid = true;
    int ret = send_raw_locked(ril, data, sizeof(data));
    pthread_mutex_unlock(&ril->lock);
    return ret;
}

int t311_ril_audio_set_mute(struct t311_ril_audio *ril, bool mute)
{
    uint8_t data[5] = { T311_OEM_FUNC_SOUND, T311_OEM_SET_MUTE, 0, 5, mute ? 1 : 0 };
    pthread_mutex_lock(&ril->lock);
    ril->muted = mute;
    ril->mute_valid = true;
    int ret = send_raw_locked(ril, data, sizeof(data));
    pthread_mutex_unlock(&ril->lock);
    return ret;
}
