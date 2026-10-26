#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define W 640
#define H 480
#define NBUF 4

struct mbuf { void *p; size_t len; };

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do { r = ioctl(fd, req, arg); } while (r < 0 && errno == EINTR);
    return r;
}

static void fourcc(char out[5], uint32_t f)
{
    out[0] = f & 255;
    out[1] = (f >> 8) & 255;
    out[2] = (f >> 16) & 255;
    out[3] = (f >> 24) & 255;
    out[4] = 0;
}

static int try_capture(const char *dev, const char *out)
{
    struct mbuf bufs[NBUF] = {{0}};
    struct v4l2_capability cap = {0};
    struct v4l2_format f = {0};
    struct v4l2_requestbuffers rq = {0};
    enum v4l2_buf_type type;
    int fd = -1, good = 0, rc = 100;
    unsigned i;
    uint64_t fnv = 1469598103934665603ULL;
    int ymin = 255, ymax = 0;
    uint64_t ysum = 0, ycount = 0;

    fd = open(dev, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("V212_CAM_TRY device=%s stage=open errno=%d\n", dev, errno);
        return 101;
    }
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        printf("V212_CAM_TRY device=%s stage=QUERYCAP errno=%d\n", dev, errno);
        rc = 102; goto out;
    }
    printf("V212_CAM_NODE=%s driver=%s card=%s bus=%s caps=0x%08x device_caps=0x%08x\n",
           dev, cap.driver, cap.card, cap.bus_info, cap.capabilities, cap.device_caps);

    f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    f.fmt.pix_mp.width = W;
    f.fmt.pix_mp.height = H;
    f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_UYVY;
    f.fmt.pix_mp.field = V4L2_FIELD_NONE;
    f.fmt.pix_mp.num_planes = 1;
    if (xioctl(fd, VIDIOC_S_FMT, &f) < 0) {
        printf("V212_CAM_TRY device=%s stage=S_FMT errno=%d\n", dev, errno);
        rc = 103; goto out;
    }
    {
        char cc[5];
        fourcc(cc, f.fmt.pix_mp.pixelformat);
        printf("V212_CAM_FMT device=%s %ux%u fourcc=%s planes=%u size0=%u\n",
               dev, f.fmt.pix_mp.width, f.fmt.pix_mp.height, cc,
               f.fmt.pix_mp.num_planes, f.fmt.pix_mp.plane_fmt[0].sizeimage);
    }

    rq.count = NBUF;
    rq.type = f.type;
    rq.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &rq) < 0) {
        printf("V212_CAM_TRY device=%s stage=REQBUFS errno=%d\n", dev, errno);
        rc = 104; goto out;
    }
    if (rq.count < 2 || rq.count > NBUF) {
        printf("V212_CAM_TRY device=%s stage=BAD_BUFFER_COUNT count=%u\n", dev, rq.count);
        rc = 105; goto out;
    }

    for (i = 0; i < rq.count; i++) {
        struct v4l2_buffer b = {0};
        struct v4l2_plane p[VIDEO_MAX_PLANES] = {{0}};
        b.type = f.type; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        b.length = VIDEO_MAX_PLANES; b.m.planes = p;
        if (xioctl(fd, VIDIOC_QUERYBUF, &b) < 0) {
            printf("V212_CAM_TRY device=%s stage=QUERYBUF index=%u errno=%d\n", dev, i, errno);
            rc = 106; goto stream_cleanup;
        }
        bufs[i].len = p[0].length;
        bufs[i].p = mmap(NULL, p[0].length, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, p[0].m.mem_offset);
        if (bufs[i].p == MAP_FAILED) {
            bufs[i].p = NULL;
            printf("V212_CAM_TRY device=%s stage=MMAP index=%u errno=%d\n", dev, i, errno);
            rc = 107; goto stream_cleanup;
        }
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) {
            printf("V212_CAM_TRY device=%s stage=QBUF index=%u errno=%d\n", dev, i, errno);
            rc = 108; goto stream_cleanup;
        }
    }

    type = f.type;
    if (xioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        printf("V212_CAM_TRY device=%s stage=STREAMON errno=%d\n", dev, errno);
        rc = 109; goto stream_cleanup;
    }
    printf("V212_CAM_STREAMON device=%s PASS\n", dev);

    for (int frame = 0; frame < 3; frame++) {
        struct pollfd pf = { .fd = fd, .events = POLLIN };
        struct v4l2_buffer b = {0};
        struct v4l2_plane p[VIDEO_MAX_PLANES] = {{0}};
        int pr = poll(&pf, 1, 5000);
        size_t used, j;
        unsigned char *d;
        if (pr <= 0) {
            printf("V212_CAM_TRY device=%s stage=POLL frame=%d rc=%d errno=%d\n",
                   dev, frame, pr, errno);
            rc = 110; break;
        }
        b.type = f.type; b.memory = V4L2_MEMORY_MMAP;
        b.length = VIDEO_MAX_PLANES; b.m.planes = p;
        if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) {
            printf("V212_CAM_TRY device=%s stage=DQBUF frame=%d errno=%d\n", dev, frame, errno);
            rc = 111; break;
        }
        if (b.index >= rq.count) {
            printf("V212_CAM_TRY device=%s stage=BAD_INDEX index=%u\n", dev, b.index);
            rc = 112; break;
        }
        used = p[0].bytesused ? p[0].bytesused : bufs[b.index].len;
        if (used > bufs[b.index].len) used = bufs[b.index].len;
        d = (unsigned char *)bufs[b.index].p;
        for (j = 0; j < used; j++) { fnv ^= d[j]; fnv *= 1099511628211ULL; }
        for (j = 1; j < used; j += 2) {
            int y = d[j];
            if (y < ymin) ymin = y;
            if (y > ymax) ymax = y;
            ysum += (uint64_t)y; ycount++;
        }
        printf("V212_CAM_FRAME device=%s index=%u seq=%u bytes=%zu ts=%ld.%06ld\n",
               dev, b.index, b.sequence, used,
               (long)b.timestamp.tv_sec, (long)b.timestamp.tv_usec);
        if (frame == 0) {
            FILE *fp = fopen(out, "wb");
            if (fp) {
                fwrite(d, 1, used, fp);
                fclose(fp);
                printf("V212_CAM_FRAME_FILE=%s bytes=%zu\n", out, used);
            }
        }
        good++;
        if (xioctl(fd, VIDIOC_QBUF, &b) < 0) {
            printf("V212_CAM_TRY device=%s stage=RE_QBUF errno=%d\n", dev, errno);
            rc = 113; break;
        }
        rc = 0;
    }

    (void)xioctl(fd, VIDIOC_STREAMOFF, &type);

stream_cleanup:
    for (i = 0; i < rq.count && i < NBUF; i++)
        if (bufs[i].p) munmap(bufs[i].p, bufs[i].len);
out:
    if (fd >= 0) close(fd);
    printf("V212_CAM_STATS device=%s frames=%d y_min=%d y_max=%d y_mean=%llu fnv64=%016llx rc=%d\n",
           dev, good, ymin, ymax,
           (unsigned long long)(ycount ? ysum / ycount : 0),
           (unsigned long long)fnv, rc);
    if (good >= 2) {
        printf("V212_CAM_STREAM=PASS device=%s\n", dev);
        return 0;
    }
    return rc ? rc : 114;
}

int main(int argc, char **argv)
{
    const char *out = argc > 2 ? argv[2] : "/data/local/tmp/t311-front-v212.uyvy";
    if (argc > 1) return try_capture(argv[1], out);

    {
        int rc = try_capture("/dev/video1", out);
        if (rc == 0) return 0;
        fprintf(stderr, "V212_CAM_FAIL=FRONT_VIDEO1_NOT_STREAMING rc=%d\n", rc);
        return rc;
    }
}
