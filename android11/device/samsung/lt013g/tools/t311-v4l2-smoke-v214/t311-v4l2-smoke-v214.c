#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long req, void *arg)
{
    int ret;
    do {
        ret = ioctl(fd, req, arg);
    } while (ret < 0 && errno == EINTR);
    return ret;
}

static int dqbuf_timeout(int fd, struct v4l2_buffer *buffer, int timeout_ms)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN | POLLPRI };
    int loops = (timeout_ms + 499) / 500;
    int i;

    for (i = 0; i < loops; i++) {
        if (xioctl(fd, VIDIOC_DQBUF, buffer) == 0)
            return 0;
        if (errno != EAGAIN)
            return -1;
        for (;;) {
            int r = poll(&pfd, 1, 500);
            if (r >= 0)
                break;
            if (errno != EINTR)
                return -1;
        }
    }
    errno = ETIMEDOUT;
    return -1;
}

static int one(const char *path, unsigned int width, unsigned int height, unsigned int frames)
{
    struct v4l2_capability cap = {0};
    struct v4l2_format fmt = {0};
    struct v4l2_requestbuffers req = {0};
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    void **maps = NULL;
    size_t *lengths = NULL;
    unsigned int i;
    uint32_t caps;
    int fd = -1;
    int ret = 1;

    fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror(path);
        return 2;
    }
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("QUERYCAP");
        ret = 3;
        goto out;
    }
    caps = cap.capabilities;
    if (caps & V4L2_CAP_DEVICE_CAPS)
        caps = cap.device_caps;
    if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE)
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    else if (caps & V4L2_CAP_VIDEO_CAPTURE)
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    else {
        fprintf(stderr, "%s: no capture capability\n", path);
        ret = 3;
        goto out;
    }

    fmt.type = type;
    if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        fmt.fmt.pix_mp.width = width;
        fmt.fmt.pix_mp.height = height;
        fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_UYVY;
        fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
        fmt.fmt.pix_mp.num_planes = 1;
    } else {
        fmt.fmt.pix.width = width;
        fmt.fmt.pix.height = height;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_UYVY;
        fmt.fmt.pix.field = V4L2_FIELD_NONE;
    }
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("S_FMT");
        ret = 4;
        goto out;
    }
    {
        unsigned int got_w = type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? fmt.fmt.pix_mp.width : fmt.fmt.pix.width;
        unsigned int got_h = type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? fmt.fmt.pix_mp.height : fmt.fmt.pix.height;
        uint32_t got_fmt = type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? fmt.fmt.pix_mp.pixelformat : fmt.fmt.pix.pixelformat;
        printf("V214_FMT path=%s requested=%ux%u actual=%ux%u fourcc=%c%c%c%c\n",
               path, width, height, got_w, got_h,
               got_fmt & 0xff, (got_fmt >> 8) & 0xff, (got_fmt >> 16) & 0xff, (got_fmt >> 24) & 0xff);
        if (got_w != width || got_h != height || got_fmt != V4L2_PIX_FMT_UYVY) {
            fprintf(stderr, "S_FMT adjusted unsupported request %ux%u to %ux%u\n", width, height, got_w, got_h);
            ret = 12;
            goto out;
        }
    }

    req.count = 4;
    req.type = type;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count == 0) {
        perror("REQBUFS");
        ret = 5;
        goto out;
    }
    maps = calloc(req.count, sizeof(*maps));
    lengths = calloc(req.count, sizeof(*lengths));
    if (!maps || !lengths) {
        ret = 5;
        goto out;
    }

    for (i = 0; i < req.count; i++) {
        struct v4l2_buffer buffer = {0};
        struct v4l2_plane plane = {0};
        off_t offset;

        buffer.index = i;
        buffer.type = type;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            buffer.m.planes = &plane;
            buffer.length = 1;
        }
        if (xioctl(fd, VIDIOC_QUERYBUF, &buffer) < 0) {
            perror("QUERYBUF");
            ret = 6;
            goto out;
        }
        if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            lengths[i] = plane.length;
            offset = plane.m.mem_offset;
        } else {
            lengths[i] = buffer.length;
            offset = buffer.m.offset;
        }
        if (lengths[i] == 0) {
            fprintf(stderr, "QUERYBUF returned zero length for %u\n", i);
            ret = 6;
            goto out;
        }
        maps[i] = mmap(NULL, lengths[i], PROT_READ, MAP_SHARED, fd, offset);
        if (maps[i] == MAP_FAILED) {
            maps[i] = NULL;
            perror("mmap");
            ret = 7;
            goto out;
        }
        if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            perror("QBUF");
            ret = 8;
            goto out;
        }
    }

    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        perror("STREAMON");
        ret = 9;
        goto out;
    }
    for (i = 0; i < frames; i++) {
        struct v4l2_buffer buffer = {0};
        struct v4l2_plane plane = {0};
        unsigned int used;

        buffer.type = type;
        buffer.memory = V4L2_MEMORY_MMAP;
        if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
            buffer.m.planes = &plane;
            buffer.length = 1;
        }
        if (dqbuf_timeout(fd, &buffer, 5000) < 0) {
            perror("DQBUF");
            ret = 10;
            goto stream_off;
        }
        if (buffer.index >= req.count) {
            fprintf(stderr, "DQBUF invalid index %u\n", buffer.index);
            ret = 10;
            goto stream_off;
        }
        used = type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? plane.bytesused : buffer.bytesused;
        printf("V214_FRAME path=%s seq=%u bytes=%u index=%u\n",
               path, buffer.sequence, used, buffer.index);
        {
            const uint8_t *data = maps[buffer.index];
            size_t limit = used < lengths[buffer.index] ? used : lengths[buffer.index];
            unsigned int min = 255, max = 0;
            uint64_t sum = 0;
            size_t samples = 0, j;
            for (j = 1; j < limit; j += 64) {
                unsigned int y = data[j];
                if (y < min) min = y;
                if (y > max) max = y;
                sum += y;
                samples++;
            }
            printf("V214_LUMA path=%s seq=%u min=%u max=%u mean=%llu samples=%zu\n",
                   path, buffer.sequence, min, max,
                   (unsigned long long)(samples ? sum / samples : 0), samples);
        }
        if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
            plane.length = lengths[buffer.index];
        if (xioctl(fd, VIDIOC_QBUF, &buffer) < 0) {
            perror("QBUF2");
            ret = 11;
            goto stream_off;
        }
    }
    ret = 0;

stream_off:
    xioctl(fd, VIDIOC_STREAMOFF, &type);
out:
    if (maps && lengths) {
        for (i = 0; i < req.count; i++)
            if (maps[i])
                munmap(maps[i], lengths[i]);
    }
    free(maps);
    free(lengths);
    if (fd >= 0)
        close(fd);
    if (!ret)
        printf("V214_CAM_STREAM=PASS path=%s %ux%u UYVY type=%s\n",
               path, width, height,
               type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ? "MPLANE" : "SPLANE");
    return ret;
}

int main(int argc, char **argv)
{
    unsigned int width = argc > 2 ? (unsigned int)atoi(argv[2]) : 640;
    unsigned int height = argc > 3 ? (unsigned int)atoi(argv[3]) : 480;

    unsigned int frames = argc > 4 ? (unsigned int)atoi(argv[4]) : 3;

    if (argc < 2 || frames < 1 || frames > 40) {
        fprintf(stderr, "usage: %s /dev/videoN [w h [frames]]\n", argv[0]);
        return 1;
    }
    return one(argv[1], width, height, frames);
}
