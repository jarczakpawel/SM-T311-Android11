#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r < 0 && errno == EINTR);
    return r;
}

static unsigned int align_u32(unsigned int v, unsigned int a)
{
    return (v + a - 1U) & ~(a - 1U);
}

int main(int argc, char **argv)
{
    struct v4l2_capability cap;
    struct v4l2_format capfmt;
    struct v4l2_format outfmt;
    struct v4l2_format got;
    unsigned int w, h, stride, y_h, uv_h, y_size, uv_size;
    const char *path;
    int fd;

    if (argc != 4) {
        fprintf(stderr, "usage: %s /dev/video7 WIDTH HEIGHT\n", argv[0]);
        return 2;
    }
    path = argv[1];
    w = (unsigned int)strtoul(argv[2], NULL, 10);
    h = (unsigned int)strtoul(argv[3], NULL, 10);
    if (!w || !h)
        return 2;

    fd = open(path, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return 3;
    }

    memset(&cap, 0, sizeof(cap));
    if (xioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("VIDIOC_QUERYCAP");
        close(fd);
        return 4;
    }

    memset(&capfmt, 0, sizeof(capfmt));
    capfmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    capfmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
    capfmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    capfmt.fmt.pix_mp.num_planes = 1;
    capfmt.fmt.pix_mp.plane_fmt[0].sizeimage = 2U * 1024U * 1024U;
    if (xioctl(fd, VIDIOC_S_FMT, &capfmt) < 0) {
        perror("VIDIOC_S_FMT(CAPTURE/H264)");
        close(fd);
        return 5;
    }

    memset(&outfmt, 0, sizeof(outfmt));
    outfmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    outfmt.fmt.pix_mp.width = w;
    outfmt.fmt.pix_mp.height = h;
    outfmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
    outfmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
    outfmt.fmt.pix_mp.num_planes = 2;
    if (xioctl(fd, VIDIOC_S_FMT, &outfmt) < 0) {
        perror("VIDIOC_S_FMT(OUTPUT/NV12M)");
        close(fd);
        return 6;
    }

    memset(&got, 0, sizeof(got));
    got.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    if (xioctl(fd, VIDIOC_G_FMT, &got) < 0) {
        perror("VIDIOC_G_FMT(OUTPUT/NV12M)");
        close(fd);
        return 7;
    }

    stride = align_u32(w, 16);
    y_h = align_u32(h, 16);
    uv_h = align_u32(h >> 1, 8);
    y_size = align_u32(stride * y_h, 2048);
    uv_size = align_u32(stride * uv_h, 2048);

    printf("Q10_MFC_STRIDE_SFMT node=%s req=%ux%u got=%ux%u planes=%u p0_bpl=%u p0_size=%u p1_bpl=%u p1_size=%u\n",
           path, w, h, outfmt.fmt.pix_mp.width, outfmt.fmt.pix_mp.height,
           outfmt.fmt.pix_mp.num_planes,
           outfmt.fmt.pix_mp.plane_fmt[0].bytesperline,
           outfmt.fmt.pix_mp.plane_fmt[0].sizeimage,
           outfmt.fmt.pix_mp.plane_fmt[1].bytesperline,
           outfmt.fmt.pix_mp.plane_fmt[1].sizeimage);
    printf("Q10_MFC_STRIDE_GFMT node=%s req=%ux%u got=%ux%u planes=%u p0_bpl=%u p0_size=%u p1_bpl=%u p1_size=%u\n",
           path, w, h, got.fmt.pix_mp.width, got.fmt.pix_mp.height,
           got.fmt.pix_mp.num_planes,
           got.fmt.pix_mp.plane_fmt[0].bytesperline,
           got.fmt.pix_mp.plane_fmt[0].sizeimage,
           got.fmt.pix_mp.plane_fmt[1].bytesperline,
           got.fmt.pix_mp.plane_fmt[1].sizeimage);
    printf("Q10_MFC_STRIDE_EXPECT stride=%u y_size=%u uv_size=%u\n",
           stride, y_size, uv_size);

    if (got.fmt.pix_mp.width != w || got.fmt.pix_mp.height != h ||
        got.fmt.pix_mp.num_planes != 2 ||
        got.fmt.pix_mp.plane_fmt[0].bytesperline != stride ||
        got.fmt.pix_mp.plane_fmt[1].bytesperline != stride ||
        got.fmt.pix_mp.plane_fmt[0].sizeimage != y_size ||
        got.fmt.pix_mp.plane_fmt[1].sizeimage != uv_size) {
        printf("Q10_MFC_STRIDE=FAIL\n");
        close(fd);
        return 10;
    }

    printf("Q10_MFC_STRIDE=PASS size=%ux%u stride=%u\n", w, h, stride);
    close(fd);
    return 0;
}
