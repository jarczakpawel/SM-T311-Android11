#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char *fourcc(uint32_t f, char s[5]) {
    s[0] = (char)(f & 0xff);
    s[1] = (char)((f >> 8) & 0xff);
    s[2] = (char)((f >> 16) & 0xff);
    s[3] = (char)((f >> 24) & 0xff);
    s[4] = 0;
    for (int i = 0; i < 4; ++i) {
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126) s[i] = '.';
    }
    return s;
}

static const char *type_name(enum v4l2_buf_type t) {
    switch (t) {
    case V4L2_BUF_TYPE_VIDEO_CAPTURE: return "CAPTURE";
    case V4L2_BUF_TYPE_VIDEO_OUTPUT: return "OUTPUT";
    case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE: return "CAPTURE_MPLANE";
    case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE: return "OUTPUT_MPLANE";
    default: return "OTHER";
    }
}

static int enum_formats(int fd, const char *node, enum v4l2_buf_type type) {
    struct v4l2_fmtdesc d;
    int n = 0;
    memset(&d, 0, sizeof(d));
    d.type = type;
    for (d.index = 0; ioctl(fd, VIDIOC_ENUM_FMT, &d) == 0; ++d.index) {
        char s[5];
        printf("FMT node=%s type=%s index=%u fourcc=%s hex=0x%08x flags=0x%x desc=%s\n",
               node, type_name(type), d.index, fourcc(d.pixelformat, s), d.pixelformat,
               d.flags, d.description);
        ++n;

        struct v4l2_frmsizeenum fs;
        memset(&fs, 0, sizeof(fs));
        fs.pixel_format = d.pixelformat;
        for (fs.index = 0; fs.index < 32 && ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) == 0; ++fs.index) {
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
                printf("FRMSIZE node=%s type=%s fourcc=%s discrete=%ux%u\n",
                       node, type_name(type), fourcc(d.pixelformat, s),
                       fs.discrete.width, fs.discrete.height);
            } else if (fs.type == V4L2_FRMSIZE_TYPE_STEPWISE || fs.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
                printf("FRMSIZE node=%s type=%s fourcc=%s stepwise=%ux%u..%ux%u step=%ux%u\n",
                       node, type_name(type), fourcc(d.pixelformat, s),
                       fs.stepwise.min_width, fs.stepwise.min_height,
                       fs.stepwise.max_width, fs.stepwise.max_height,
                       fs.stepwise.step_width, fs.stepwise.step_height);
                break;
            }
        }
    }
    return n;
}

static void query_controls(int fd, const char *node) {
    struct v4l2_queryctrl q;
    memset(&q, 0, sizeof(q));
    q.id = V4L2_CTRL_FLAG_NEXT_CTRL;
    int count = 0;
    while (count < 512 && ioctl(fd, VIDIOC_QUERYCTRL, &q) == 0) {
        printf("CTRL node=%s id=0x%08x type=%u min=%d max=%d step=%d default=%d flags=0x%x name=%s\n",
               node, q.id, q.type, q.minimum, q.maximum, q.step,
               q.default_value, q.flags, q.name);
        q.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
        ++count;
    }
    printf("CTRL_COUNT node=%s count=%d\n", node, count);
}

static int try_fmt_mplane(int fd, const char *node, enum v4l2_buf_type type,
                          uint32_t pixfmt, unsigned int w, unsigned int h) {
    struct v4l2_format f;
    char s[5];
    memset(&f, 0, sizeof(f));
    f.type = type;
    f.fmt.pix_mp.width = w;
    f.fmt.pix_mp.height = h;
    f.fmt.pix_mp.pixelformat = pixfmt;
    f.fmt.pix_mp.field = V4L2_FIELD_NONE;
    int r = ioctl(fd, VIDIOC_TRY_FMT, &f);
    int e = errno;
    if (r == 0) {
        printf("TRY_FMT_PASS node=%s type=%s req=%s_%ux%u got=%s_%ux%u planes=%u",
               node, type_name(type), fourcc(pixfmt, s), w, h,
               fourcc(f.fmt.pix_mp.pixelformat, s), f.fmt.pix_mp.width,
               f.fmt.pix_mp.height, f.fmt.pix_mp.num_planes);
        for (unsigned int i = 0; i < f.fmt.pix_mp.num_planes && i < VIDEO_MAX_PLANES; ++i)
            printf(" p%u_size=%u p%u_bpl=%u", i, f.fmt.pix_mp.plane_fmt[i].sizeimage,
                   i, f.fmt.pix_mp.plane_fmt[i].bytesperline);
        putchar('\n');
        return 1;
    }
    printf("TRY_FMT_FAIL node=%s type=%s req=%s_%ux%u errno=%d(%s)\n",
           node, type_name(type), fourcc(pixfmt, s), w, h, e, strerror(e));
    return 0;
}

static void probe_one(const char *node) {
    int fd = open(node, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        printf("OPEN_FAIL node=%s errno=%d(%s)\n", node, errno, strerror(errno));
        return;
    }

    struct v4l2_capability c;
    memset(&c, 0, sizeof(c));
    if (ioctl(fd, VIDIOC_QUERYCAP, &c) != 0) {
        printf("QUERYCAP_FAIL node=%s errno=%d(%s)\n", node, errno, strerror(errno));
        close(fd);
        return;
    }
    uint32_t caps = (c.capabilities & V4L2_CAP_DEVICE_CAPS) ? c.device_caps : c.capabilities;
    printf("CAP node=%s driver=%s card=%s bus=%s version=0x%x caps=0x%08x device_caps=0x%08x effective=0x%08x\n",
           node, c.driver, c.card, c.bus_info, c.version, c.capabilities, c.device_caps, caps);

    enum_formats(fd, node, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
    enum_formats(fd, node, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
    enum_formats(fd, node, V4L2_BUF_TYPE_VIDEO_OUTPUT);
    enum_formats(fd, node, V4L2_BUF_TYPE_VIDEO_CAPTURE);
    query_controls(fd, node);

    if (strstr((char *)c.card, "s5p-mfc-enc") || strstr((char *)c.driver, "s5p-mfc")) {
        printf("MFC_NODE node=%s card=%s driver=%s\n", node, c.card, c.driver);
        const unsigned int sizes[][2] = {{640,480}, {1280,720}, {1920,1080}};
        for (unsigned int i = 0; i < sizeof(sizes)/sizeof(sizes[0]); ++i) {
            try_fmt_mplane(fd, node, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_NV12M, sizes[i][0], sizes[i][1]);
#ifdef V4L2_PIX_FMT_NV12MT
            try_fmt_mplane(fd, node, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
                           V4L2_PIX_FMT_NV12MT, sizes[i][0], sizes[i][1]);
#endif
            try_fmt_mplane(fd, node, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                           V4L2_PIX_FMT_H264, sizes[i][0], sizes[i][1]);
            try_fmt_mplane(fd, node, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                           V4L2_PIX_FMT_MPEG4, sizes[i][0], sizes[i][1]);
            try_fmt_mplane(fd, node, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
                           V4L2_PIX_FMT_H263, sizes[i][0], sizes[i][1]);
        }
    }
    close(fd);
}

int main(int argc, char **argv) {
    printf("T311_V4L2_PROBE_VERSION=1\n");
    if (argc < 2) {
        fprintf(stderr, "usage: %s /dev/video0 [/dev/video1 ...]\n", argv[0]);
        return 2;
    }
    for (int i = 1; i < argc; ++i) probe_one(argv[i]);
    return 0;
}
