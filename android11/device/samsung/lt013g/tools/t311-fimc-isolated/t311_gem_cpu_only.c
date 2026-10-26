#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <exynos_drm.h>
#include <drm_mode.h>

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const char *size_env = getenv("T311_GEM_SIZE");
    size_t n = size_env ? strtoul(size_env, NULL, 10) : (1280U * 720U * 3U / 2U);
    if (n < 4096 || n > 1382400 || (size_env && n % 4096 != 0)) {
        fprintf(stderr, "INVALID_GEM_SIZE=%zu\n", n);
        return 2;
    }
    const int allocations = argc > 1 ? atoi(argv[1]) : 16;
    const int passes = argc > 2 ? atoi(argv[2]) : 8;
    const unsigned flags = argc > 3 ? (unsigned)strtoul(argv[3], NULL, 0) : (1U << 3);
    if (allocations < 1 || allocations > 80 || passes < 1 || passes > 64 || flags > 31) return 2;
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("DRM_OPEN"); return 3; }
    uint8_t *src = malloc(n);
    if (!src) { perror("MALLOC"); close(fd); return 4; }
    uint8_t *ordinary = malloc(n);
    if (!ordinary) { perror("MALLOC_RAM_REFERENCE"); free(src); close(fd); return 4; }
    unsigned long long errors = 0;
    int failed_passes = 0;
    int normal_failed_passes = 0;
    unsigned idle_changes=0;
    const char *idle_env=getenv("T311_GEM_IDLE_US");
    unsigned idle_us=idle_env?(unsigned)strtoul(idle_env,NULL,10):0;
    if (idle_us>20000) return 2;
    for (int a = 0; a < allocations; ++a) {
        struct drm_exynos_gem_create cr = {0};
        cr.size = n;
        cr.flags = flags;
        if (drmIoctl(fd, DRM_IOCTL_EXYNOS_GEM_CREATE, &cr) < 0) {
            perror("GEM_CREATE"); free(src); close(fd); return 5;
        }
        struct drm_mode_map_dumb m = {0};
        m.handle = cr.handle;
        if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &m) < 0) {
            perror("GEM_MAP_DUMB"); free(src); close(fd); return 6;
        }
        int prime = -1;
        int skip_prime = getenv("T311_GEM_NO_PRIME") != NULL;
        if (!skip_prime && drmPrimeHandleToFD(fd, cr.handle, DRM_CLOEXEC | DRM_RDWR, &prime) < 0 &&
            drmPrimeHandleToFD(fd, cr.handle, DRM_CLOEXEC, &prime) < 0) {
            perror("GEM_PRIME"); free(src); close(fd); return 7;
        }
        if (skip_prime && (getenv("T311_PRIME_MMAP") || getenv("T311_GEM_DUALMAP"))) {
            fprintf(stderr,"ERROR: PRIME requested but disabled\n");close(fd);free(src);free(ordinary);return 7;
        }
        int map_fd = getenv("T311_PRIME_MMAP") ? prime : fd;
        off_t map_offset = getenv("T311_PRIME_MMAP") ? 0 : (off_t)m.offset;
        uint8_t *dst = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, map_offset);
        if (dst == MAP_FAILED) {
            perror("GEM_MMAP"); if(prime>=0)close(prime); free(src); close(fd); return 8;
        }
        // Optional second alias through the opposite GEM entry point.
        uint8_t *dual = NULL;
        if (getenv("T311_GEM_DUALMAP")) {
            const int second_fd = map_fd == fd ? prime : fd;
            const off_t second_offset = map_fd == fd ? 0 : (off_t)m.offset;
            dual = mmap(NULL,n,PROT_READ|PROT_WRITE,MAP_SHARED,second_fd,second_offset);
            if (dual == MAP_FAILED) {
                perror("GEM_DUAL_MMAP");munmap(dst,n);if(prime>=0)close(prime);free(src);free(ordinary);close(fd);return 9;
            }
        }
        for (int k = 0; k < passes; ++k) {
            uint32_t x = 0x51cde711U ^ (uint32_t)(a * 131 + k * 2659);
            for (size_t i = 0; i < n; ++i) {
                x ^= x << 13;
                x ^= x >> 17;
                x ^= x << 5;
                src[i] = (uint8_t)x;
            }
            memcpy(ordinary, src, n);
            if (memcmp(ordinary, src, n) != 0) normal_failed_passes++;
            if (getenv("T311_GEM_SCALAR")) {
                volatile uint8_t *v = (volatile uint8_t *)dst;
                for (size_t i=0; i<n; ++i) v[i] = src[i];
            } else {
                memcpy(dst, src, n);
            }
            if (getenv("T311_GEM_DSB") || dual)
                __asm__ __volatile__("dsb sy" ::: "memory");
            if (dual) {
                unsigned long long dual_wrong = 0, alias_disagree = 0;
                size_t dual_first = 0;
                for (size_t i=0;i<n;++i) {
                    if (dual[i]!=src[i]) { if (!dual_wrong) dual_first=i; ++dual_wrong; }
                    if (dual[i]!=dst[i]) ++alias_disagree;
                }
                if (dual_wrong || alias_disagree)
                    printf("GEM_DUALMAP_MISMATCH allocation=%d pass=%d dual_wrong=%llu alias_disagree=%llu first=%zu\n",
                           a,k,dual_wrong,alias_disagree,dual_first);
            }
            unsigned long long wrong = 0;
            size_t first = 0;
            for (size_t i = 0; i < n; ++i) {
                if (dst[i] != src[i]) {
                    if (wrong == 0) first = i;
                    ++wrong;
                }
            }
            if (wrong) {
                ++failed_passes;
                errors += wrong;
                uint8_t got = dst[first];
                uint8_t expected = src[first];
                __asm__ __volatile__("dsb sy" ::: "memory");
                unsigned long long second_wrong = 0, delayed_wrong = 0, alias_wrong = 0;
                for (size_t i=0; i<n; ++i) if (dst[i] != src[i]) ++second_wrong;
                usleep(2000);
                for (size_t i=0; i<n; ++i) if (dst[i] != src[i]) ++delayed_wrong;
                uint8_t *alias = mmap(NULL, n, PROT_READ | PROT_WRITE, MAP_SHARED, map_fd, map_offset);
                if (alias != MAP_FAILED) {
                    for (size_t i=0; i<n; ++i) if (alias[i] != src[i]) ++alias_wrong;
                    munmap(alias, n);
                }
                printf("GEM_CPU_MISMATCH allocation=%d pass=%d wrong=%llu first=%zu aligned32=%zu want=%u got=%u second=%llu delayed=%llu alias=%llu\n",
                       a, k, wrong, first, first & ~(size_t)31, expected, got,
                       second_wrong, delayed_wrong, alias_wrong);
            }
            if (idle_us) {
                usleep(idle_us);
                size_t after=0;
                for (size_t j=0;j<n;++j) if (dst[j]!=src[j])++after;
                if (after != wrong) {
                    ++idle_changes;
                    printf("GEM_IDLE_CHANGE allocation=%d pass=%d before=%llu after=%zu idle_us=%u source_changed=%d\n",
                           a,k,wrong,after,idle_us,memcmp(ordinary,src,n)!=0);
                }
            }
        }
        if (dual) munmap(dual, n);
        munmap(dst, n);
        if(prime>=0)close(prime);
        struct drm_gem_close cl = {0};
        cl.handle = cr.handle;
        drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &cl);
    }
    printf("GEM_CPU_ONLY_RESULT=%s bytes=%zu allocations=%d passes_each=%d flags=0x%x failed_passes=%d badbytes=%llu normal_ram_failed_passes=%d idle_changed=%u prime_export=%d\n",
           errors ? "FAIL" : "PASS", n, allocations, passes, flags, failed_passes, errors, normal_failed_passes, idle_changes, !getenv("T311_GEM_NO_PRIME"));
    free(ordinary);
    free(src);
    close(fd);
    return errors || normal_failed_passes || idle_changes ? 1 : 0;
}
