#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <linux/fs.h>
#include "sha256_local.h"

#define BLOCK_DEVICE "/dev/block/mmcblk0p20"
#define CHUNK_DIR "/data/local/tmp/SM-T311-G563-IO64-CHUNK"
#define SYSTEM_SIZE INT64_C(2235564032)
#define PART_SIZE INT64_C(536870912)
#define BUFFER_SIZE 1048576

static void fail(const char *message) {
    fprintf(stderr, "G563_IO64_FAIL %s errno=%d\n", message, errno);
    exit(2);
}

static void require_recovery(void) {
    struct stat st;
    if (stat("/sbin/recovery", &st) || !S_ISREG(st.st_mode))
        fail("RECOVERY_REQUIRED");
}

static void check_unmounted(dev_t device) {
    FILE *in = fopen("/proc/self/mountinfo", "r");
    if (!in) fail("MOUNTINFO");
    char line[4096];
    while (fgets(line, sizeof(line), in)) {
        unsigned maj, min;
        if (sscanf(line, "%*u %*u %u:%u", &maj, &min) == 2 &&
                maj == major(device) && min == minor(device)) {
            fclose(in);
            fail("SYSTEM_MOUNTED");
        }
    }
    if (ferror(in)) fail("MOUNTINFO_READ");
    fclose(in);
}

static int open_system(int write_access) {
    require_recovery();
    struct stat st;
    if (stat(BLOCK_DEVICE, &st) || !S_ISBLK(st.st_mode))
        fail("NOT_BLOCK_SYSTEM");
    check_unmounted(st.st_rdev);
    int fd = open(BLOCK_DEVICE, (write_access ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_LARGEFILE);
    if (fd < 0) fail("OPEN_SYSTEM");
    uint64_t bytes = 0;
    if (ioctl(fd, BLKGETSIZE64, &bytes) || bytes != (uint64_t)SYSTEM_SIZE)
        fail("SYSTEM_GEOMETRY");
    return fd;
}

static int64_t length_of_part(int part) {
    int64_t left = SYSTEM_SIZE - (int64_t)part * PART_SIZE;
    return left > PART_SIZE ? PART_SIZE : left;
}

static void read_exact(int fd, void *p, size_t n, int64_t offset) {
    size_t done = 0;
    while (done < n) {
        ssize_t k = pread64(fd, (char *)p + done, n - done, (off64_t)(offset + done));
        if (k <= 0) fail("PREAD");
        done += (size_t)k;
    }
}

static void write_exact(int fd, const void *p, size_t n, int64_t offset) {
    size_t done = 0;
    while (done < n) {
        ssize_t k = pwrite64(fd, (const char *)p + done, n - done,
                             (off64_t)(offset + done));
        if (k <= 0) fail("PWRITE");
        done += (size_t)k;
    }
}

static void hash_region(int fd, int64_t base, int64_t length, char digest[65]) {
    G563Sha256 hash;
    g563_sha256_init(&hash);
    unsigned char *buffer = malloc(BUFFER_SIZE);
    if (!buffer) fail("ALLOCATE");
    for (int64_t n = 0; n < length; n += BUFFER_SIZE) {
        size_t size = (size_t)(length - n > BUFFER_SIZE ? BUFFER_SIZE : length - n);
        read_exact(fd, buffer, size, base + n);
        g563_sha256_update(&hash, buffer, size);
    }
    free(buffer);
    unsigned char h[32];
    g563_sha256_final(&hash, h);
    for (int i = 0; i < 32; i++)
        snprintf(digest + i * 2, 3, "%02x", h[i]);
    digest[64] = 0;
}

static void require_digest(const char *actual, const char *expected) {
    if (strlen(expected) != 64) fail("EXPECTED_SHA_LENGTH");
    for (int i = 0; i < 64; i++)
        if (!isxdigit((unsigned char)expected[i])) fail("EXPECTED_SHA_SYNTAX");
    if (strcasecmp(actual, expected)) {
        fprintf(stderr, "G563_IO64_FAIL SHA_MISMATCH actual=%s expected=%s\n", actual, expected);
        exit(7);
    }
}

static void preflight_data(void) {
    require_recovery();
    struct stat mountpoint, physical;
    if (stat("/data", &mountpoint) || !S_ISDIR(mountpoint.st_mode))
        fail("DATA_DIRECTORY");
    if (stat("/dev/block/mmcblk0p21", &physical) || !S_ISBLK(physical.st_mode))
        fail("DATA_BLOCK_DEVICE");
    if (mountpoint.st_dev != physical.st_rdev)
        fail("DATA_NOT_MOUNTED_FROM_P21");
    struct statvfs vfs;
    if (statvfs("/data", &vfs)) fail("DATA_STATVFS");
    if (vfs.f_flag & ST_RDONLY) fail("DATA_READ_ONLY");
    uint64_t free_bytes = (uint64_t)vfs.f_bavail * (uint64_t)vfs.f_frsize;
    printf("G570_DATA_MOUNT_VERIFIED free_bytes=%llu\n",
           (unsigned long long)free_bytes);
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "preflight-data")) {
        preflight_data();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "preflight")) {
        int fd = open_system(0);
        close(fd);
        puts("G563_IO64_PREFLIGHT_PASS");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "verify-system")) {
        int fd = open_system(0);
        char digest[65];
        hash_region(fd, 0, SYSTEM_SIZE, digest);
        require_digest(digest, argv[2]);
        close(fd);
        printf("G569_IO64_FULL_SYSTEM_VERIFIED sha256=%s\n", digest);
        return 0;
    }
    if (argc == 4 && !strcmp(argv[1], "verify-installed")) {
        if (strlen(argv[2]) != 1 || argv[2][0] < '0' || argv[2][0] > '4')
            fail("INSTALLED_PART_INDEX");
        int part = argv[2][0] - '0';
        int fd = open_system(0);
        char digest[65];
        hash_region(fd, (int64_t)part * PART_SIZE, length_of_part(part), digest);
        require_digest(digest, argv[3]);
        close(fd);
        printf("G564_IO64_INSTALLED_PART_VERIFIED index=%d sha256=%s\n", part, digest);
        return 0;
    }
    if (argc != 4 && argc != 5) fail("ARGUMENT_COUNT");
    int writing = !strcmp(argv[1], "copy");
    if (!writing && strcmp(argv[1], "verify")) fail("ACTION");
    if (strlen(argv[2]) != 1 || argv[2][0] < '0' || argv[2][0] > '4')
        fail("PART_INDEX");
    if (writing && (argc != 5 || strcmp(argv[4], "G563_FLASH_SYSTEM_P20")))
        fail("EXPLICIT_FLASH_TOKEN");
    if (!writing && argc != 4) fail("VERIFY_ARGUMENTS");
    int part = argv[2][0] - '0';
    int64_t length = length_of_part(part);
    int64_t offset = (int64_t)part * PART_SIZE;
    char path[256];
    if (snprintf(path, sizeof(path), CHUNK_DIR "/chunk%d.raw", part) >= (int)sizeof(path))
        fail("PATH_LENGTH");
    int in = open(path, O_RDONLY | O_CLOEXEC | O_LARGEFILE | O_NOFOLLOW);
    if (in < 0) fail("SOURCE_OPEN");
    struct stat st;
    if (fstat(in, &st) || !S_ISREG(st.st_mode) || st.st_size != length)
        fail("SOURCE_GEOMETRY");
    char digest[65];
    hash_region(in, 0, length, digest);
    require_digest(digest, argv[3]);
    int out = open_system(writing);
    if (writing) {
        unsigned char *buffer = malloc(BUFFER_SIZE);
        if (!buffer) fail("WRITE_ALLOC");
        for (int64_t n = 0; n < length; n += BUFFER_SIZE) {
            size_t amt = (size_t)(length - n > BUFFER_SIZE ? BUFFER_SIZE : length - n);
            read_exact(in, buffer, amt, n);
            write_exact(out, buffer, amt, offset + n);
        }
        free(buffer);
        if (fsync(out)) fail("SYSTEM_FSYNC");
    }
    hash_region(out, offset, length, digest);
    require_digest(digest, argv[3]);
    close(out);
    close(in);
    printf("G563_IO64_PART_%s_PASS index=%d offset=%lld bytes=%lld sha256=%s\n",
           writing ? "WRITE_VERIFY" : "VERIFY", part, (long long)offset,
           (long long)length, digest);
    return 0;
}
