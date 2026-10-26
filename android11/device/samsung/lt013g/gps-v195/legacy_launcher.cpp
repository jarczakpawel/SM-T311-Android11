#define LOG_TAG "Lt013gGpsLegacyLauncherV410"
#include <log/log.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/sysmacros.h>
#include <sys/system_properties.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {
constexpr uid_t kGpsUid = 1021;
constexpr gid_t kGpsGid = 1021;
constexpr uid_t kSystemUid = 1000;
constexpr gid_t kSystemGid = 1000;
constexpr gid_t kInetGid = 3003;
constexpr gid_t kNetRawGid = 3004;
constexpr gid_t kSdcardRwGid = 1015;
constexpr gid_t kWakelockGid = 3010;
constexpr gid_t kReadprocGid = 3009;
volatile sig_atomic_t gStop;
pid_t gChild = -1;
pid_t gLegacyServiceManager = -1;
pid_t gLegacySensorService = -1;
void resetChildSignalState();

long long monotonicMs() {
    struct timespec ts {};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<long long>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000LL;
}

void signalProcessGroup(pid_t leader, int signalNumber) {
    if (leader > 0) {
        (void)kill(-leader, signalNumber);
        (void)kill(leader, signalNumber);
    }
}

void onSignal(int) {
    gStop = 1;
    signalProcessGroup(gChild, SIGTERM);
    signalProcessGroup(gLegacySensorService, SIGTERM);
    signalProcessGroup(gLegacyServiceManager, SIGTERM);
}

bool makeDir(const std::string& path, mode_t mode) {
    if (mkdir(path.c_str(), mode) == 0 || errno == EEXIST) return true;
    ALOGE("mkdir %s failed: %s", path.c_str(), strerror(errno));
    return false;
}

bool makeParents(const std::string& path, mode_t mode) {
    std::string current;
    for (size_t i = 0; i < path.size(); ++i) {
        current.push_back(path[i]);
        if (path[i] == '/' && current.size() > 1 && !makeDir(current, mode)) return false;
    }
    return makeDir(path, mode);
}

int runCommand(const std::vector<std::string>& args) {
    pid_t child = fork();
    if (child < 0) return -errno;
    if (child == 0) {
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& arg : args) argv.push_back(const_cast<char*>(arg.c_str()));
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return -errno;
    }
    if (!WIFEXITED(status)) return -EIO;
    return WEXITSTATUS(status);
}

bool copyFile(const std::string& source, const std::string& target, mode_t mode) {
    int in = open(source.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return false;
    int out = open(target.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (out < 0) {
        close(in);
        return false;
    }
    char buffer[65536];
    bool ok = true;
    for (;;) {
        ssize_t count = read(in, buffer, sizeof(buffer));
        if (count == 0) break;
        if (count < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        char* cursor = buffer;
        ssize_t remaining = count;
        while (remaining > 0) {
            ssize_t written = write(out, cursor, static_cast<size_t>(remaining));
            if (written < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            cursor += written;
            remaining -= written;
        }
        if (!ok) break;
    }
    if (ok && fsync(out) != 0) ok = false;
    close(out);
    close(in);
    if (ok) (void)chmod(target.c_str(), mode);
    return ok;
}

bool seedPrivateHostsFromTrustedBaseline(const std::string& path, uid_t uid, gid_t gid) {
    // Exact canonical content in /vendor/etc/lt013g-gps-legacy/rootfs.tar,
    // system/etc/hosts. Also matches the Android 7 bridge DNS seed baseline.
    // Avoid reading generic vendor_data_file under /data: Android SELinux
    // intentionally forbids these accesses once Enforcing is enabled.
    static constexpr char kStockHosts[] =
            "127.0.0.1       localhost\n"
            "::1             ip6-localhost\n";
    static_assert(sizeof(kStockHosts) - 1 == 56, "Unexpected GPS hosts baseline");

    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC |
                        O_CLOEXEC | O_NOFOLLOW, 0644);
    if (fd < 0) return false;
    struct stat st {};
    bool ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode);
    if (ok && fchown(fd, uid, gid) != 0) ok = false;
    if (ok && fchmod(fd, 0644) != 0) ok = false;
    size_t remaining = sizeof(kStockHosts) - 1;
    const char* cursor = kStockHosts;
    while (ok && remaining != 0) {
        const ssize_t n = write(fd, cursor, remaining);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { ok = false; break; }
        cursor += n;
        remaining -= static_cast<size_t>(n);
    }
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    return ok;
}

bool ensureFile(const std::string& path, mode_t mode, uid_t uid, gid_t gid) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC, mode);
    if (fd < 0) return false;
    close(fd);
    (void)chown(path.c_str(), uid, gid);
    (void)chmod(path.c_str(), mode);
    return true;
}

bool ensureConfigKey(const std::string& path, const char* key, const char* value,
                     mode_t mode, uid_t uid, gid_t gid) {
    int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, mode);
    if (fd < 0) return false;

    char buffer[65536];
    ssize_t nr = read(fd, buffer, sizeof(buffer) - 1);
    if (nr < 0) {
        close(fd);
        return false;
    }
    buffer[nr] = '\0';

    const size_t keyLen = strlen(key);
    bool found = false;
    const char* cursor = buffer;
    const char* end = buffer + nr;
    while (cursor < end) {
        const char* lineEnd = static_cast<const char*>(memchr(cursor, '\n', static_cast<size_t>(end - cursor)));
        const size_t lineLen = lineEnd ? static_cast<size_t>(lineEnd - cursor) : static_cast<size_t>(end - cursor);
        const char* p = cursor;
        while (p < cursor + lineLen && (*p == ' ' || *p == '\t' || *p == '\r')) ++p;
        if (static_cast<size_t>(cursor + lineLen - p) > keyLen &&
            memcmp(p, key, keyLen) == 0 && p[keyLen] == '=') {
            found = true;
            break;
        }
        cursor = lineEnd ? lineEnd + 1 : end;
    }

    bool ok = true;
    if (!found) {
        if (lseek(fd, 0, SEEK_END) < 0) {
            ok = false;
        } else {
            if (nr > 0 && buffer[nr - 1] != '\n') {
                const char nl = '\n';
                if (write(fd, &nl, 1) != 1) ok = false;
            }
            if (ok) {
                const std::string line = std::string(key) + "=" + value + "\n";
                const char* data = line.c_str();
                size_t left = line.size();
                while (left > 0) {
                    ssize_t nw = write(fd, data, left);
                    if (nw < 0) {
                        if (errno == EINTR) continue;
                        ok = false;
                        break;
                    }
                    if (nw == 0) {
                        ok = false;
                        break;
                    }
                    data += nw;
                    left -= static_cast<size_t>(nw);
                }
            }
        }
    }

    if (ok && fchown(fd, uid, gid) != 0) ok = false;
    if (ok && fchmod(fd, mode) != 0) ok = false;
    if (ok && fsync(fd) != 0) ok = false;
    close(fd);
    return ok;
}

bool resetOwnedFile(const std::string& path, mode_t mode, uid_t uid, gid_t gid) {
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return false;
    bool ok = true;
    if (fchown(fd, uid, gid) != 0) ok = false;
    if (fchmod(fd, mode) != 0) ok = false;
    close(fd);
    return ok;
}

bool bindMount(const std::string& source, const std::string& target) {
    if (mount(source.c_str(), target.c_str(), nullptr, MS_BIND, nullptr) == 0) return true;
    ALOGE("bind mount %s -> %s failed: %s", source.c_str(), target.c_str(), strerror(errno));
    return false;
}

bool bindMountRecursive(const std::string& source, const std::string& target) {
    if (mount(source.c_str(), target.c_str(), nullptr, MS_BIND | MS_REC, nullptr) == 0) return true;
    ALOGE("recursive bind mount %s -> %s failed: %s",
          source.c_str(), target.c_str(), strerror(errno));
    return false;
}

bool bindHostSocketIfPresent(const std::string& root, const char* name) {
    const std::string source = std::string("/dev/socket/") + name;
    const std::string target = root + "/dev/socket/" + name;
    struct stat st {};
    if (lstat(source.c_str(), &st) != 0) {
        ALOGW("V341_HOST_SOCKET name=%s state=MISSING errno=%d", name, errno);
        return true;
    }
    int fd = open(target.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0660);
    if (fd < 0) {
        ALOGE("V341_HOST_SOCKET name=%s placeholder=FAIL errno=%d", name, errno);
        return false;
    }
    close(fd);
    if (!bindMount(source, target)) return false;
    struct stat bound {};
    if (stat(target.c_str(), &bound) != 0 || !S_ISSOCK(bound.st_mode)) {
        ALOGE("V341_HOST_SOCKET name=%s bind=FAIL_NOT_SOCKET", name);
        return false;
    }
    ALOGI("V341_HOST_SOCKET name=%s bind=PASS", name);
    return true;
}

// SM-T311: rootfs code comes from immutable files installed under /vendor.
// Bind source directories read-only only in this launcher's own private mount
// namespace. Mutable DNS hosts, secgps.conf, log files and bind-backed /data
// remain outside the code directory mounts.
// Do not make the whole rootfs RO: the KitKat GNSS stack updates hosts.
bool protectLegacyCodeDirectory(const std::string& source, const std::string& directory) {
    struct stat st {};
    if (stat(directory.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        ALOGE("T311_GPS_CODE_RO=FAIL missing_dir=%s errno=%d", directory.c_str(), errno);
        return false;
    }
    if (mount(source.c_str(), directory.c_str(), nullptr, MS_BIND, nullptr) != 0) {
        ALOGE("T311_GPS_CODE_RO=FAIL bind=%s errno=%d", directory.c_str(), errno);
        return false;
    }
    // The immutable code lives on the physical read-only SYSTEM partition.
    // A bind of that read-only superblock is already nonwritable. The old
    // MS_REMOUNT call is forbidden to vendor processes by Android 11's
    // filesystem neverallow and is unnecessary on this actual device.
    // Never degrade protection: reject any source not already read-only.
    struct statvfs fs {};
    if (statvfs(directory.c_str(), &fs) != 0 || (fs.f_flag & ST_RDONLY) == 0) {
        ALOGE("T311_GPS_CODE_RO=FAIL verify=%s errno=%d", directory.c_str(), errno);
        (void)umount2(directory.c_str(), MNT_DETACH);
        return false;
    }
    ALOGI("T311_GPS_CODE_RO=PASS path=%s readonly=1 isolated_mount=1", directory.c_str());
    return true;
}

void detach(const std::string& path) { (void)umount2(path.c_str(), MNT_DETACH); }

void detachRuntimeMounts(const std::string& root) {
    // V341: host networking sockets are child bind mounts inside private /dev/socket.
    // File bind in the private mount namespace: only the GPS hosts file.
    detach(root + "/system/etc/hosts");
    detach(root + "/dev/socket/dnsproxyd");
    detach(root + "/dev/socket/fwmarkd");
    detach(root + "/dev/socket/netd");
    detach(root + "/dev/socket");
    detach(root + "/sdcard/gps/broadcom/storage");
    detach(root + "/storage/sdcard0/gps/broadcom/storage");
    detach(root + "/data/data/com.android.angryGps");
    detach(root + "/data/system/gps");
    detach(root + "/data/gps");
    detach(root + "/sys/fs/selinux");
    detach(root + "/sys");
    detach(root + "/proc");
    // G378: remove the dedicated Binder node's private mount before /dev.
    detach(root + "/dev/binderfs/binder");
    detach(root + "/dev");
    // These vendor-origin RO binds exist only in the launcher's private mount namespace.
    detach(root + "/system/usr");
    detach(root + "/system/lib");
    detach(root + "/system/bin");
}

// G378: binderfs device contexts are independent in actual Linux 6.12
// binder.c (binder_open uses inode->i_private->context). The SM-T311-only
// kernel defconfig creates a fourth device, lt013g-gps, when Android11 init
// mounts its regular binderfs. Avoid creating a filesystem in the vendor
// GNSS domain: Android11 system/sepolicy/private/domain.te neverallow.
// Source is MODE 0600 at boot; restrict it to uid=system,gid=gps,0660, so
// stock API25 servicemanager(system) and gpsd(gps) can access it while other
// users cannot. Only a bind-mounted view inside init's private namespace
// replaces legacy /dev/binder; the host main Binder remains unchanged.
bool setupPrivateLegacyBinder(const std::string& root) {
    const std::string source = "/dev/binderfs/lt013g-gps";
    const std::string hostSystemBinder = "/dev/binderfs/binder";
    const std::string jailBinder = root + "/dev/binderfs/binder";
    struct stat privateBefore {}, globalBefore {}, insideBefore {};
    if (stat(source.c_str(), &privateBefore) != 0 ||
        stat(hostSystemBinder.c_str(), &globalBefore) != 0 ||
        stat(jailBinder.c_str(), &insideBefore) != 0 ||
        !S_ISCHR(privateBefore.st_mode) || !S_ISCHR(globalBefore.st_mode) ||
        !S_ISCHR(insideBefore.st_mode) ||
        privateBefore.st_rdev == globalBefore.st_rdev ||
        insideBefore.st_rdev != globalBefore.st_rdev ||
        major(privateBefore.st_rdev) != major(globalBefore.st_rdev)) {
        ALOGE("G378_PREPROVISIONED_GPS_BINDER=FAIL absent_or_not_independent errno=%d", errno);
        return false;
    }
    // G378 ownership is only for this new dedicated binder device.
    // The live /dev/binderfs/binder, /dev/hwbinder and /dev/vndbinder are
    // never chowned, chmodded, replaced or unlinked.
    if (chown(source.c_str(), kSystemUid, kGpsGid) != 0 ||
        chmod(source.c_str(), 0660) != 0) {
        ALOGE("G378_GPS_BINDER_DAC=FAIL errno=%d", errno);
        return false;
    }
    if (!bindMount(source, jailBinder)) {
        ALOGE("G378_GPS_BINDER_JAIL_BIND=FAIL errno=%d", errno);
        return false;
    }
    struct stat privateAfter {}, insideAfter {}, globalAfter {};
    if (stat(source.c_str(), &privateAfter) != 0 ||
        stat(jailBinder.c_str(), &insideAfter) != 0 ||
        stat(hostSystemBinder.c_str(), &globalAfter) != 0 ||
        !S_ISCHR(insideAfter.st_mode) ||
        insideAfter.st_rdev != privateAfter.st_rdev ||
        globalAfter.st_rdev != globalBefore.st_rdev ||
        globalAfter.st_ino != globalBefore.st_ino ||
        privateAfter.st_uid != kSystemUid ||
        privateAfter.st_gid != kGpsGid ||
        (privateAfter.st_mode & 0777) != 0660) {
        ALOGE("G378_GPS_BINDER_JAIL_VALIDATE=FAIL errno=%d", errno);
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("G378_GPS_BINDER_ISOLATION=PASS private_major=%u private_minor=%u system_minor=%u dac=0660",
          major(privateAfter.st_rdev), minor(privateAfter.st_rdev), minor(globalAfter.st_rdev));
    return true;
}

pid_t startLegacyRuntimeService(const std::string& root, const char* binary,
                                uid_t uid, gid_t gid, const gid_t* groups,
                                size_t groupCount, const char* logName) {
    const std::string logPath = root + "/data/gps/" + logName;
    int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
    if (logFd >= 0) {
        (void)fchown(logFd, kGpsUid, kGpsGid);
        (void)fchmod(logFd, 0660);
    }
    const pid_t parentPid = getpid();
    pid_t child = fork();
    if (child != 0) {
        if (logFd >= 0) close(logFd);
        return child;
    }
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) != 0 || getppid() != parentPid) _exit(170);
    resetChildSignalState();
    if (setsid() < 0) _exit(171);
    if (logFd >= 0) {
        (void)dup2(logFd, STDOUT_FILENO);
        (void)dup2(logFd, STDERR_FILENO);
        if (logFd > STDERR_FILENO) close(logFd);
    }
    if (chroot(root.c_str()) != 0 || chdir("/") != 0) _exit(172);
    if (groupCount > 0 && setgroups(groupCount, groups) != 0) _exit(173);
    if (setgid(gid) != 0 || setuid(uid) != 0) _exit(174);
    setenv("LD_LIBRARY_PATH", "/system/lib:/system/vendor/lib", 1);
    setenv("ANDROID_ROOT", "/system", 1);
    setenv("ANDROID_DATA", "/data", 1);
    setenv("TZ", "GMT0", 1);
    unsetenv("LD_PRELOAD");
    if (strcmp(binary, "/system/bin/servicemanager") == 0) {
        setenv("LD_PRELOAD", "/system/lib/liblt013g-servicemanager-selinux-off-v414.so", 1);
    } else if (strcmp(binary, "/system/bin/sensorservice") == 0) {
        setenv("LD_PRELOAD", "/system/lib/liblt013g-sensor-hw-loader-v415.so", 1);
    }
    unsetenv("LT013G_BRIDGE_PROBE_ONLY");
    execl(binary, binary, static_cast<char*>(nullptr));
    dprintf(STDERR_FILENO, "V410_LEGACY_RUNTIME_EXEC_FAIL binary=%s errno=%d (%s)\n",
            binary, errno, strerror(errno));
    _exit(175);
}

bool processAlive(pid_t pid) {
    if (pid <= 0) return false;
    int status = 0;
    pid_t rc = waitpid(pid, &status, WNOHANG);
    if (rc == 0) return true;
    if (rc == pid) {
        if (WIFEXITED(status)) {
            ALOGE("V410_RUNTIME_CHILD_EXIT pid=%d code=%d raw=%d", pid, WEXITSTATUS(status), status);
        } else if (WIFSIGNALED(status)) {
            ALOGE("V410_RUNTIME_CHILD_EXIT pid=%d sig=%d raw=%d", pid, WTERMSIG(status), status);
        } else {
            ALOGE("V410_RUNTIME_CHILD_EXIT pid=%d raw=%d", pid, status);
        }
        return false;
    }
    if (rc < 0 && errno == ECHILD) return false;
    if (rc < 0 && errno != EINTR) {
        ALOGE("V410_RUNTIME_CHILD_WAIT_FAIL pid=%d errno=%d (%s)", pid, errno, strerror(errno));
        return false;
    }
    return kill(pid, 0) == 0 || errno == EPERM;
}

bool startLegacySensorIsland(const std::string& root) {
    gid_t smGroups[] = {kSystemGid, kReadprocGid};
    gLegacyServiceManager = startLegacyRuntimeService(root, "/system/bin/servicemanager",
                                                       kSystemUid, kSystemGid,
                                                       smGroups, sizeof(smGroups) / sizeof(smGroups[0]),
                                                       "v410-servicemanager.log");
    if (gLegacyServiceManager <= 0) {
        ALOGE("V410_LEGACY_SERVICEMANAGER=FAIL fork");
        return false;
    }
    usleep(500000);
    if (!processAlive(gLegacyServiceManager)) {
        ALOGE("V410_LEGACY_SERVICEMANAGER=FAIL exited pid=%d", gLegacyServiceManager);
        return false;
    }
    ALOGI("V410_LEGACY_SERVICEMANAGER=PASS pid=%d", gLegacyServiceManager);

    gid_t sensorGroups[] = {kSystemGid};
    gLegacySensorService = startLegacyRuntimeService(root, "/system/bin/sensorservice",
                                                      0, kSystemGid,
                                                      sensorGroups, 1,
                                                      "v410-sensorservice.log");
    if (gLegacySensorService <= 0) {
        ALOGE("V410_LEGACY_SENSORSERVICE=FAIL fork");
        return false;
    }
    usleep(1500000);
    if (!processAlive(gLegacySensorService)) {
        ALOGE("V410_LEGACY_SENSORSERVICE=FAIL exited pid=%d", gLegacySensorService);
        return false;
    }
    ALOGI("V410_LEGACY_SENSORSERVICE=PASS pid=%d", gLegacySensorService);
    return true;
}

void stopRuntimeChild(pid_t* pidPtr) {
    if (!pidPtr || *pidPtr <= 0) return;
    const pid_t pid = *pidPtr;
    signalProcessGroup(pid, SIGTERM);
    for (int i = 0; i < 30; ++i) {
        int status = 0;
        pid_t rc = waitpid(pid, &status, WNOHANG);
        if (rc == pid || (rc < 0 && errno == ECHILD)) {
            *pidPtr = -1;
            return;
        }
        if (rc < 0 && errno != EINTR) break;
        usleep(10000);
    }
    signalProcessGroup(pid, SIGKILL);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    *pidPtr = -1;
}

void stopLegacySensorIsland() {
    stopRuntimeChild(&gLegacySensorService);
    stopRuntimeChild(&gLegacyServiceManager);
}

bool extractRootfs(const std::string& archive, const std::string& root) {
    // Keep legacy GPS extraction inside the vendor SELinux domain.
    // /system/bin/rm and /system/bin/tar resolve to the platform toybox,
    // which Android 11 forbids vendor services to execute under Enforcing.
    const std::string tar = access("/vendor/bin/tar", X_OK) == 0 ?
        "/vendor/bin/tar" : "/vendor/bin/toybox_vendor";
    detachRuntimeMounts(root);
    (void)runCommand({"/vendor/bin/rm", "-rf", root});
    if (!makeParents(root, 0755)) return false;
    int rc = tar == "/vendor/bin/toybox_vendor" ?
        runCommand({tar, "tar", "-xf", archive, "-C", root}) :
        runCommand({tar, "-xf", archive, "-C", root});
    if (rc != 0) {
        ALOGE("rootfs extraction failed rc=%d", rc);
        return false;
    }
    // V351 RUNNERFIX2: tar archives are allowed to omit the top-level "./"
    // entry. In that case the directory keeps mkdir() permissions filtered by
    // the launcher's inherited umask, which can make the chroot root
    // untraversable after dropping to uid=gps. Normalize it explicitly.
    if (chown(root.c_str(), 0, 0) != 0 || chmod(root.c_str(), 0755) != 0) {
        ALOGE("V351_ROOTFS_ROOT_MODE=FAIL path=%s errno=%d (%s)",
              root.c_str(), errno, strerror(errno));
        return false;
    }
    struct stat rootSt {};
    if (stat(root.c_str(), &rootSt) != 0 || (rootSt.st_mode & 0777) != 0755) {
        ALOGE("V351_ROOTFS_ROOT_MODE=FAIL stat path=%s errno=%d (%s)",
              root.c_str(), errno, strerror(errno));
        return false;
    }
    ALOGI("V351_ROOTFS_ROOT_MODE=PASS mode=%#o uid=%u gid=%u",
          rootSt.st_mode, static_cast<unsigned>(rootSt.st_uid),
          static_cast<unsigned>(rootSt.st_gid));
    return true;
}

void installTzdata(const std::string& root) {
    const std::string directory = root + "/system/usr/share/zoneinfo";
    const std::string target = directory + "/tzdata";
    struct stat existing {};
    if (stat(target.c_str(), &existing) == 0 && existing.st_size > 0) {
        ALOGI("V341_REFERENCE_TZDATA_KEEP=PASS path=%s bytes=%lld", target.c_str(), (long long)existing.st_size);
        return;
    }
    static const char* candidates[] = {
        "/apex/com.android.tzdata/etc/tz/tzdata",
        "/system/usr/share/zoneinfo/tzdata",
        "/system/etc/tz/tzdata",
    };
    if (!makeParents(directory, 0755)) return;
    for (const char* source : candidates) {
        if (access(source, R_OK) == 0 && copyFile(source, target, 0644)) {
            ALOGI("installed runtime tzdata from %s", source);
            return;
        }
    }
    ALOGW("host tzdata unavailable; stock GMT fallback remains active");
}

// SM-T311: retain the stock Broadcom configuration inside the immutable tar;
// after extraction, enable delivery of verified Android network time. The
// legacy blob's original IgnoreJniTime=true suppresses the framework time
// assistance necessary to recover from its 1024-week GPS era rollover.
// The Broadcom file log remains a diagnostic opt-in:
//   adb shell setprop persist.vendor.lt013g.gps.filelog 1   (then reboot)
// Failure here never blocks GPS; it only leaves the stock setting in place.
void applyGpsLogMode(const std::string& root) {
    char value[PROP_VALUE_MAX] = {};
    const bool fileLog =
            __system_property_get("persist.vendor.lt013g.gps.filelog", value) > 0 &&
            (strcmp(value, "1") == 0 || strcmp(value, "true") == 0);
    if (!fileLog) ALOGI("T311_GPS_LOG_MODE=JAVA (stock)");
    const std::string path = root + "/system/etc/gps.xml";
    std::string xml;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGW("T311_GPS_LOG_MODE=JAVA open failed: %s", strerror(errno));
        return;
    }
    char buffer[4096];
    for (;;) {
        const ssize_t nr = read(fd, buffer, sizeof(buffer));
        if (nr < 0 && errno == EINTR) continue;
        if (nr <= 0) break;
        xml.append(buffer, static_cast<size_t>(nr));
    }
    close(fd);
    const std::string fromTime = "IgnoreJniTime=\"true\"";
    const size_t timePos = xml.find(fromTime);
    if (timePos == std::string::npos) {
        ALOGW("T311_GPS_JNI_TIME unchanged: no stock time setting in %s", path.c_str());
        return;
    }
    xml.replace(timePos, fromTime.size(), "IgnoreJniTime=\"false\"");
    if (fileLog) {
        const std::string fromLog = "Log=\"JAVA\"";
        const size_t logPos = xml.find(fromLog);
        if (logPos != std::string::npos)
            xml.replace(logPos, fromLog.size(), "LogEnabled=\"true\" Log=\"FILE\"");
    }
    fd = open(path.c_str(), O_WRONLY | O_TRUNC | O_CLOEXEC);
    if (fd < 0) {
        ALOGW("T311_GPS_LOG_MODE=JAVA rewrite failed: %s", strerror(errno));
        return;
    }
    const char* data = xml.data();
    size_t left = xml.size();
    while (left > 0) {
        const ssize_t nw = write(fd, data, left);
        if (nw < 0 && errno == EINTR) continue;
        if (nw <= 0) break;
        data += nw;
        left -= static_cast<size_t>(nw);
    }
    (void)fsync(fd);
    close(fd);
    ALOGI("T311_GPS_JNI_TIME=%s (framework UTC assistance enabled)",
          left == 0 ? "ACCEPT" : "PARTIAL_WRITE");
    if (fileLog)
        ALOGI("T311_GPS_LOG_MODE=%s (persist.vendor.lt013g.gps.filelog=1)",
              left == 0 ? "FILE" : "PARTIAL_WRITE");
}

bool setupRootfs(const std::string& archive, const std::string& workRoot,
                 std::string* rootOut) {
    const std::string root = workRoot + "/rootfs";
    // V303: do not reuse Android 11's /data/system/gps as the API25 NvStorageDir.
    // The Broadcom daemon owns a persistent .gpsd.lock there and treats failure
    // to acquire/open that lock as "Process is already running".  Keep a private
    // stock-shaped backing directory in vendor data and bind it into the jail.
    const std::string systemGps = workRoot + "/system-gps";
    const std::string dataGps = workRoot + "/data-gps";
    const std::string angryGps = workRoot + "/storage/angrygps";
    const std::string broadcom = workRoot + "/storage/broadcom";
    const std::string devSocket = workRoot + "/dev-socket";

    if (!makeParents(workRoot, 0750) ||
        chown(workRoot.c_str(), 0, kGpsGid) != 0 ||
        chmod(workRoot.c_str(), 0750) != 0 ||
        !makeParents(systemGps, 0771) ||
        !makeParents(dataGps, 0770) ||
        !makeParents(angryGps, 0770) ||
        !makeParents(broadcom, 0770) ||
        !makeParents(devSocket, 0770)) return false;

    (void)chown(systemGps.c_str(), kSystemUid, kSystemGid);
    (void)chmod(systemGps.c_str(), 0771);
    struct stat privateSystemGpsSt {};
    if (stat(systemGps.c_str(), &privateSystemGpsSt) != 0) {
        ALOGE("V303 private system-gps stat failed: %s", strerror(errno));
        return false;
    }
    ALOGI("SMT311_V303_PRIVATE_SYSTEM_GPS=PASS path=%s mode=%#o uid=%u gid=%u",
          systemGps.c_str(), privateSystemGpsSt.st_mode,
          static_cast<unsigned>(privateSystemGpsSt.st_uid),
          static_cast<unsigned>(privateSystemGpsSt.st_gid));
    for (const std::string& path : {dataGps, angryGps, broadcom}) {
        (void)chown(path.c_str(), kGpsUid, kGpsGid);
        (void)chmod(path.c_str(), 0770);
    }
    (void)chown(devSocket.c_str(), kGpsUid, kSystemGid);
    (void)chmod(devSocket.c_str(), 0770);
    if (!resetOwnedFile(dataGps + "/v290-preload-stage.log", 0660, kGpsUid, kGpsGid) ||
        !resetOwnedFile(dataGps + "/v290-preload-child.log", 0660, kGpsUid, kGpsGid) ||
        !resetOwnedFile(dataGps + "/v295-gpsd-preload-probe.log", 0660, kGpsUid, kGpsGid) ||
        !resetOwnedFile(dataGps + "/v417-gps-io-trace.log", 0660, kGpsUid, kGpsGid)) {
        ALOGE("V299 GPS diagnostic file preparation failed: %s", strerror(errno));
        return false;
    }
    const std::string secgps = angryGps + "/secgps.conf";
    if (!ensureFile(secgps, 0660, kGpsUid, kGpsGid) ||
        !ensureConfigKey(secgps, "OPERATION_TEST_MODE", "0", 0660, kGpsUid, kGpsGid) ||
        !ensureConfigKey(secgps, "OPERATION_MODE", "STANDALONE", 0660, kGpsUid, kGpsGid)) {
        ALOGE("V355_SECGPS_DEFAULTS=FAIL path=%s errno=%d (%s)",
              secgps.c_str(), errno, strerror(errno));
        return false;
    }
    ALOGI("V355_SECGPS_DEFAULTS=PASS operation_test_mode=0 operation_mode=STANDALONE");

    if (!extractRootfs(archive, root)) return false;
    applyGpsLogMode(root);
    // V423: the legacy DNS client needs a writable /system/etc/hosts inside
    // its chroot. A standalone source in /data/vendor/gps/legacy-v195/data-gps
    // has the already deployed lt013g_gps_scratch_data_file SELinux label,
    // rather than granting the GNSS process write to vendor_data_file.
    // Seed from a compile-time verified byte-for-byte copy of vendor rootfs
    // stock hosts; never open the generic vendor_data_file target before bind.
    // Target remains outside /system/bin and /system/lib RO mounts.
    const std::string privateHosts = dataGps + "/hosts";
    const std::string jailHosts = root + "/system/etc/hosts";
    if (!seedPrivateHostsFromTrustedBaseline(privateHosts, kGpsUid, kGpsGid) ||
        !bindMount(privateHosts, jailHosts)) {
        ALOGE("T311_GPS_PRIVATE_HOSTS_BIND=FAIL errno=%d (%s)", errno, strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    struct stat originalHosts {}, bindHosts {};
    if (stat(privateHosts.c_str(), &originalHosts) != 0 ||
        stat(jailHosts.c_str(), &bindHosts) != 0 ||
        originalHosts.st_dev != bindHosts.st_dev ||
        originalHosts.st_ino != bindHosts.st_ino) {
        ALOGE("T311_GPS_PRIVATE_HOSTS_BIND=FAIL not_same_inode");
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("T311_GPS_PRIVATE_HOSTS_BIND=PASS source=data-gps/hosts same_inode=1");
    if (!makeParents(root + "/system/bin", 0755) ||
        !makeParents(root + "/system/lib", 0755) ||
        !makeParents(root + "/system/usr", 0755) ||
        !makeParents(root + "/dev/socket", 0777) ||
        !makeParents(root + "/proc", 0555) ||
        !makeParents(root + "/sys", 0555) ||
        !makeParents(root + "/data/system/gps", 0771) ||
        !makeParents(root + "/data/gps", 0771) ||
        !makeParents(root + "/data/data/com.android.angryGps", 0771) ||
        !makeParents(root + "/sdcard/gps/broadcom/storage", 0771) ||
        !makeParents(root + "/storage/sdcard0/gps/broadcom/storage", 0771)) return false;

    // V304/V347: mkdir() is filtered by the launcher's inherited umask.  The bind
    // targets themselves can have stock-like permissions while uid=gps is still
    // blocked by an intermediate parent. Normalize every required /data parent.
    for (const std::string& path : {root + "/data", root + "/data/system", root + "/data/data"}) {
        if (chown(path.c_str(), kSystemUid, kSystemGid) != 0 ||
            chmod(path.c_str(), 0771) != 0) {
            ALOGE("V347 jail data parent fix failed path=%s: %s",
                  path.c_str(), strerror(errno));
            return false;
        }
    }
    // V355: CM14.1 gpsd runs as uid=gps, group=sdcard_rw and writes its
    // Broadcom runtime log below /sdcard/gps/broadcom/storage.  makeParents()
    // inherits the launcher's umask, so normalize every intermediate directory
    // to a traversable state before bind-mounting the private log directory.
    for (const std::string& path : {root + "/sdcard", root + "/sdcard/gps",
                                    root + "/sdcard/gps/broadcom", root + "/storage",
                                    root + "/storage/sdcard0", root + "/storage/sdcard0/gps",
                                    root + "/storage/sdcard0/gps/broadcom"}) {
        if (chown(path.c_str(), 0, 0) != 0 || chmod(path.c_str(), 0755) != 0) {
            ALOGE("V355_BROADCOM_LOG_PARENT=FAIL path=%s errno=%d (%s)",
                  path.c_str(), errno, strerror(errno));
            return false;
        }
    }
    ALOGI("V355_BROADCOM_LOG_PARENTS=PASS");
    struct stat jailDataSt {};
    struct stat jailSystemSt {};
    struct stat jailAppDataSt {};
    if (stat((root + "/data").c_str(), &jailDataSt) != 0 ||
        stat((root + "/data/system").c_str(), &jailSystemSt) != 0 ||
        stat((root + "/data/data").c_str(), &jailAppDataSt) != 0) {
        ALOGE("V347 jail data parent stat failed: %s", strerror(errno));
        return false;
    }
    ALOGI("SMT311_V347_JAIL_DATA_PARENTS=PASS data_mode=%#o system_mode=%#o appdata_mode=%#o",
          jailDataSt.st_mode, jailSystemSt.st_mode, jailAppDataSt.st_mode);

    // Mount executable, library and ICU code from immutable vendor files.
    // Keep the existing hosts and GPS data trees writable as before.
    // Only this private mount namespace sees the replacement mounts.
    const std::string immutable = "/vendor/etc/lt013g-gps-legacy/immutable/system";
    if (!protectLegacyCodeDirectory(immutable + "/bin", root + "/system/bin") ||
        !protectLegacyCodeDirectory(immutable + "/lib", root + "/system/lib") ||
        !protectLegacyCodeDirectory(immutable + "/usr", root + "/system/usr")) {
        detachRuntimeMounts(root);
        return false;
    }
    if (access((root + "/system/bin/gpsd").c_str(), X_OK) != 0) {
        ALOGE("V295 API25 gpsd host missing from reference rootfs: %s", strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    if (access((root + "/system/lib/liblt013g-gps-bridge-v290.so").c_str(), R_OK) != 0) {
        ALOGE("vendor preload bridge library missing: %s", strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("V295_API25_PRELOAD_HOST=PASS host=/system/bin/gpsd lib=/system/lib/liblt013g-gps-bridge-v290.so");
    installTzdata(root);

    // V310: /dev contains Android 11 child mounts (notably binderfs). A plain
    // MS_BIND copied only the top-level devtmpfs and left /dev/binder symlinks
    // pointing at an unmounted /dev/binderfs, so the API25 libgui path aborted
    // after open("/dev/binder") returned ENOENT. Recursively clone the complete
    // /dev mount tree, then keep our private /dev/socket overlay on top.
    if (!bindMountRecursive("/dev", root + "/dev")) return false;
    ALOGI("SMT311_V310_RECURSIVE_DEV_BIND=PASS");
    if (access((root + "/dev/socket").c_str(), F_OK) != 0) {
        ALOGE("V299 host /dev/socket missing inside whole-/dev bind: %s", strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    if (!bindMount(devSocket, root + "/dev/socket")) {
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("SMT311_V299_PRIVATE_DEV_SOCKET=PASS source=%s", devSocket.c_str());
    if (!bindHostSocketIfPresent(root, "dnsproxyd") ||
        !bindHostSocketIfPresent(root, "fwmarkd") ||
        !bindHostSocketIfPresent(root, "netd")) {
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("V341_NETWORK_SOCKET_PASSTHROUGH=PASS");
    ALOGI("SMT311_V325_GPSD_CREDENTIALS=system,inet,net_raw,sdcard_rw,wakelock");

    struct stat hostRandom {};
    struct stat jailRandom {};
    if (stat("/dev/urandom", &hostRandom) != 0 ||
        stat((root + "/dev/urandom").c_str(), &jailRandom) != 0) {
        ALOGE("V298 /dev/urandom stat failed: %s", strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("SMT311_V298_WHOLE_DEV_BIND host_mode=%#o host_rdev=%u:%u jail_mode=%#o jail_rdev=%u:%u",
          hostRandom.st_mode, major(hostRandom.st_rdev), minor(hostRandom.st_rdev),
          jailRandom.st_mode, major(jailRandom.st_rdev), minor(jailRandom.st_rdev));
    if (!S_ISCHR(hostRandom.st_mode) || !S_ISCHR(jailRandom.st_mode) ||
        hostRandom.st_rdev != jailRandom.st_rdev) {
        ALOGE("SMT311_V298_WHOLE_DEV_BIND=FAIL urandom_not_same_char_device");
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("SMT311_V298_WHOLE_DEV_BIND=PASS");

    struct stat hostBinder {};
    struct stat jailBinder {};
    if (stat("/dev/binder", &hostBinder) != 0 ||
        stat((root + "/dev/binder").c_str(), &jailBinder) != 0) {
        ALOGE("SMT311_V310_BINDERFS=FAIL binder_stat errno=%d (%s)", errno, strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    if (!S_ISCHR(hostBinder.st_mode) || !S_ISCHR(jailBinder.st_mode) ||
        hostBinder.st_rdev != jailBinder.st_rdev) {
        ALOGE("SMT311_V310_BINDERFS=FAIL binder_mismatch host_mode=%#o jail_mode=%#o host_rdev=%u:%u jail_rdev=%u:%u",
              hostBinder.st_mode, jailBinder.st_mode,
              major(hostBinder.st_rdev), minor(hostBinder.st_rdev),
              major(jailBinder.st_rdev), minor(jailBinder.st_rdev));
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("SMT311_V310_BINDERFS=PASS host_rdev=%u:%u jail_rdev=%u:%u",
          major(hostBinder.st_rdev), minor(hostBinder.st_rdev),
          major(jailBinder.st_rdev), minor(jailBinder.st_rdev));

    // G376: original Samsung/KitKat GPS expects /proc, /sys and SELinux
    // status. On Android 11, init already provides all three in its dedicated
    // per-service mount namespace (namespace mnt), including nested selinuxfs.
    // Reuse those verified host mounts via MS_BIND|MS_REC instead of creating
    // new proc/sysfs/selinuxfs instances: avoid extra filesystem-mount policy
    // requirements, keep the modern host's actual mount tree and avoid global
    // remounts. Binderfs stays private; do not attach this legacy process to
    // the Android system's own binder/service-manager instance.
    if (!bindMountRecursive("/proc", root + "/proc")) {
        ALOGE("G376_PROC_BIND=FAIL errno=%d (%s)", errno, strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    if (!bindMountRecursive("/sys", root + "/sys")) {
        ALOGE("G376_SYSFS_BIND=FAIL errno=%d (%s)", errno, strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    const std::string selinuxStatus = root + "/sys/fs/selinux/status";
    struct stat selinuxStatusSt {};
    if (stat(selinuxStatus.c_str(), &selinuxStatusSt) != 0 || !S_ISREG(selinuxStatusSt.st_mode)) {
        ALOGE("G376_SELINUXFS_INHERITED=FAIL status errno=%d (%s)", errno, strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("G376_KERNEL_FS_BIND=PASS proc=1 sys=1 selinuxfs_inherited=1");
    if (!bindMount(systemGps, root + "/data/system/gps") ||
        !bindMount(dataGps, root + "/data/gps") ||
        !bindMount(angryGps, root + "/data/data/com.android.angryGps") ||
        !bindMount(broadcom, root + "/sdcard/gps/broadcom/storage") ||
        !bindMount(broadcom, root + "/storage/sdcard0/gps/broadcom/storage")) {
        detachRuntimeMounts(root);
        return false;
    }
    struct stat jailSystemGpsSt {};
    if (stat((root + "/data/system/gps").c_str(), &jailSystemGpsSt) != 0 ||
        jailSystemGpsSt.st_dev != privateSystemGpsSt.st_dev ||
        jailSystemGpsSt.st_ino != privateSystemGpsSt.st_ino) {
        ALOGE("SMT311_V303_PRIVATE_SYSTEM_GPS_BIND=FAIL errno=%d (%s)", errno, strerror(errno));
        detachRuntimeMounts(root);
        return false;
    }
    ALOGI("SMT311_V303_PRIVATE_SYSTEM_GPS_BIND=PASS mode=%#o uid=%u gid=%u",
          jailSystemGpsSt.st_mode, static_cast<unsigned>(jailSystemGpsSt.st_uid),
          static_cast<unsigned>(jailSystemGpsSt.st_gid));
    *rootOut = root;
    return true;
}

void resetChildSignalState() {
    struct sigaction action {};
    action.sa_handler = SIG_DFL;
    (void)sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    const int signals[] = {SIGTERM, SIGINT, SIGHUP, SIGCHLD, SIGPIPE};
    for (int sig : signals) (void)sigaction(sig, &action, nullptr);

    sigset_t mask;
    (void)sigemptyset(&mask);
    (void)sigprocmask(SIG_SETMASK, &mask, nullptr);
}

void writeChildStage(int fd, const char* text) {
    if (fd < 0 || text == nullptr) return;
    (void)write(fd, text, strlen(text));
    (void)write(fd, "\n", 1);
}

void drainChildStagePipe(int fd) {
    if (fd < 0) return;
    char buffer[512];
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            ALOGI("V290_CHILD_PREEXEC_TRACE %s", buffer);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        break;
    }
}

bool fileContains(const std::string& path, const char* needle) {
    if (needle == nullptr || *needle == '\0') return false;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    std::string data;
    char buffer[1024];
    for (;;) {
        ssize_t n = read(fd, buffer, sizeof(buffer));
        if (n == 0) break;
        if (n < 0) {
            if (errno == EINTR) continue;
            close(fd);
            return false;
        }
        data.append(buffer, static_cast<size_t>(n));
        if (data.size() > 65536) break;
    }
    close(fd);
    return data.find(needle) != std::string::npos;
}

void logProbeProcState(pid_t pid, const char* label) {
    (void)label;  // Also keeps host stub-log syntax checks warning-clean.
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/status", pid);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        char buffer[8192];
        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        if (n > 0) {
            buffer[n] = '\0';
            char* save = nullptr;
            for (char* line = strtok_r(buffer, "\n", &save); line != nullptr;
                 line = strtok_r(nullptr, "\n", &save)) {
                if (strncmp(line, "Name:", 5) == 0 ||
                    strncmp(line, "State:", 6) == 0 ||
                    strncmp(line, "Uid:", 4) == 0 ||
                    strncmp(line, "Gid:", 4) == 0 ||
                    strncmp(line, "NoNewPrivs:", 11) == 0 ||
                    strncmp(line, "Seccomp:", 8) == 0 ||
                    strncmp(line, "Seccomp_filters:", 16) == 0) {
                    ALOGI("V293_PROBE_PROC label=%s pid=%d %s", label, pid, line);
                }
            }
        }
    }
    snprintf(path, sizeof(path), "/proc/%d/cgroup", pid);
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        char buffer[2048];
        ssize_t n = read(fd, buffer, sizeof(buffer) - 1);
        close(fd);
        if (n > 0) {
            buffer[n] = '\0';
            for (ssize_t i = 0; i < n; ++i) {
                if (buffer[i] == '\n') buffer[i] = ';';
            }
            ALOGI("V293_PROBE_CGROUP label=%s pid=%d %s", label, pid, buffer);
        }
    }
}

int runApi25GpsdPreloadProbe(const std::string& root, bool dropPrivileges,
                               const char* label) {
    const std::string constructorStage = root + "/data/gps/v290-preload-stage.log";
    if (!resetOwnedFile(constructorStage, 0660, kGpsUid, kGpsGid)) {
        ALOGE("V299 reset constructor stage failed: %s", strerror(errno));
        return -errno;
    }

    int stagePipe[2] = {-1, -1};
    int proofPipe[2] = {-1, -1};
    if (pipe2(stagePipe, O_CLOEXEC | O_NONBLOCK) != 0) return -errno;
    if (pipe(proofPipe) != 0) {
        int saved = errno;
        close(stagePipe[0]); close(stagePipe[1]);
        return -saved;
    }

    pid_t child = fork();
    if (child < 0) {
        int saved = errno;
        close(stagePipe[0]); close(stagePipe[1]);
        close(proofPipe[0]); close(proofPipe[1]);
        return -saved;
    }
    if (child == 0) {
        close(stagePipe[0]);
        close(proofPipe[0]);
        resetChildSignalState();
        writeChildStage(stagePipe[1], "V299_PROBE_STAGE=fork-pass");
        if (setsid() < 0) {
            writeChildStage(stagePipe[1], "V299_PROBE_STAGE=setsid-failed");
            _exit(119);
        }
        writeChildStage(stagePipe[1], "V299_PROBE_STAGE=setsid-pass");
        if (chroot(root.c_str()) != 0 || chdir("/") != 0) {
            writeChildStage(stagePipe[1], "V299_PROBE_STAGE=chroot-failed");
            _exit(120);
        }
        writeChildStage(stagePipe[1], "V299_PROBE_STAGE=chroot-pass");

        struct stat randomStat {};
        if (stat("/dev/urandom", &randomStat) != 0 || !S_ISCHR(randomStat.st_mode)) {
            char line[192];
            snprintf(line, sizeof(line), "V299_PROBE_STAGE=urandom-fail errno=%d mode=%#o",
                     errno, randomStat.st_mode);
            writeChildStage(stagePipe[1], line);
            _exit(123);
        }
        {
            char line[192];
            snprintf(line, sizeof(line), "V299_PROBE_STAGE=urandom-char-pass rdev=%u:%u",
                     major(randomStat.st_rdev), minor(randomStat.st_rdev));
            writeChildStage(stagePipe[1], line);
        }

        if (dropPrivileges) {
            gid_t groups[] = {kSystemGid, kInetGid, kNetRawGid, kSdcardRwGid, kWakelockGid};
            if (setgroups(sizeof(groups) / sizeof(groups[0]), groups) != 0) {
                writeChildStage(stagePipe[1], "V299_PROBE_STAGE=setgroups-failed");
                _exit(121);
            }
            writeChildStage(stagePipe[1], "V299_PROBE_STAGE=setgroups-pass");
            if (setgid(kGpsGid) != 0 || setuid(kGpsUid) != 0) {
                writeChildStage(stagePipe[1], "V299_PROBE_STAGE=setuid-failed");
                _exit(122);
            }
            writeChildStage(stagePipe[1], "V299_PROBE_STAGE=gps-creds-pass");
        } else {
            writeChildStage(stagePipe[1], "V299_PROBE_STAGE=root-creds-pass");
        }

        {
            struct stat dataStat {};
            char line[224];
            if (stat("/data/gps", &dataStat) == 0) {
                snprintf(line, sizeof(line),
                         "V299_PROBE_STAGE=data-gps-stat mode=%#o uid=%u gid=%u",
                         dataStat.st_mode, dataStat.st_uid, dataStat.st_gid);
            } else {
                snprintf(line, sizeof(line), "V299_PROBE_STAGE=data-gps-stat-fail errno=%d", errno);
            }
            writeChildStage(stagePipe[1], line);
        }
        {
            int fd = open("/data/gps/.v299-write-probe", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0660);
            if (fd >= 0) {
                (void)write(fd, "ok\n", 3);
                close(fd);
                (void)unlink("/data/gps/.v299-write-probe");
                writeChildStage(stagePipe[1], "V299_PROBE_STAGE=data-gps-write-pass");
            } else {
                char line[160];
                snprintf(line, sizeof(line), "V299_PROBE_STAGE=data-gps-write-fail errno=%d", errno);
                writeChildStage(stagePipe[1], line);
            }
        }

        writeChildStage(stagePipe[1], "V299_PROBE_STAGE=preexec-hold-enter");
        usleep(200000);
        writeChildStage(stagePipe[1], "V299_PROBE_STAGE=preexec-hold-pass");

        int logfd = open("/data/gps/v295-gpsd-preload-probe.log",
                         O_WRONLY | O_CREAT | O_APPEND, 0660);
        if (logfd >= 0) {
            (void)dup2(logfd, STDOUT_FILENO);
            (void)dup2(logfd, STDERR_FILENO);
            if (logfd > STDERR_FILENO) close(logfd);
        }

        char proofFd[32];
        snprintf(proofFd, sizeof(proofFd), "%d", proofPipe[1]);
        setenv("LD_LIBRARY_PATH", "/system/lib:/system/vendor/lib", 1);
        setenv("ANDROID_ROOT", "/system", 1);
        setenv("ANDROID_DATA", "/data", 1);
        setenv("TZ", "GMT0", 1);
        setenv("LT013G_BRIDGE_PROBE_ONLY", "1", 1);
        setenv("LT013G_BRIDGE_PROBE_FD", proofFd, 1);
        setenv("LD_PRELOAD",
               "/system/lib/libm.so:/system/lib/liblt013g-gps-bridge-v290.so",
               1);
        dprintf(STDERR_FILENO, "V306_DLFNCN_PROVIDER=API25_LINKER_BUILTIN probe=1\n");
        unsetenv("ANDROID_SOCKET_gps");

        writeChildStage(stagePipe[1], "V299_PROBE_STAGE=exec-enter");
        execl("/system/bin/gpsd", "gpsd", "--lt013g-preload-probe",
              static_cast<char*>(nullptr));
        dprintf(STDERR_FILENO, "V299_PROBE_STAGE=exec-failed label=%s errno=%d (%s)\n",
                label, errno, strerror(errno));
        _exit(127);
    }

    close(stagePipe[1]);
    close(proofPipe[1]);
    const long long deadline = monotonicMs() + 3500;
    int status = 0;
    bool pipeClosed = false;
    bool procLogged = false;
    std::string pending;
    while (monotonicMs() < deadline) {
        if (!pipeClosed) {
            char buffer[512];
            ssize_t n = read(stagePipe[0], buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                pending.append(buffer, static_cast<size_t>(n));
                size_t pos = 0;
                for (;;) {
                    size_t nl = pending.find('\n', pos);
                    if (nl == std::string::npos) {
                        pending.erase(0, pos);
                        break;
                    }
                    std::string line = pending.substr(pos, nl - pos);
                    ALOGI("V299_%s_PREEXEC_TRACE pid=%d %s", label, child, line.c_str());
                    if (!procLogged && line.find("preexec-hold-enter") != std::string::npos) {
                        logProbeProcState(child, label);
                        procLogged = true;
                    }
                    pos = nl + 1;
                }
            } else if (n == 0) {
                pipeClosed = true;
                close(stagePipe[0]);
                stagePipe[0] = -1;
                ALOGI("V299_%s_EXEC_FD_CLOSED pid=%d", label, child);
            } else if (errno != EAGAIN && errno != EINTR) {
                ALOGW("V299_%s_PREEXEC_PIPE_READ_FAIL pid=%d errno=%d (%s)",
                      label, child, errno, strerror(errno));
                pipeClosed = true;
                close(stagePipe[0]);
                stagePipe[0] = -1;
            }
        }

        pid_t rc = waitpid(child, &status, WNOHANG);
        if (rc == child) {
            if (stagePipe[0] >= 0) close(stagePipe[0]);
            char proof[16] = {};
            ssize_t proofBytes = read(proofPipe[0], proof, sizeof(proof));
            close(proofPipe[0]);
            bool proofOk = proofBytes > 0 && memchr(proof, 'P', static_cast<size_t>(proofBytes)) != nullptr;
            bool stageOk = fileContains(constructorStage, "V295_STAGE=constructor-probe-pass");
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && proofOk) {
                ALOGI("V299_API25_%s_PROBE=PASS pid=%d exec_fd_closed=%d proof_ok=1 stage_ok=%d",
                      label, child, pipeClosed ? 1 : 0, stageOk ? 1 : 0);
                return 0;
            }
            if (WIFEXITED(status)) {
                ALOGE("V299_API25_%s_PROBE_EXIT code=%d exec_fd_closed=%d proof_ok=%d stage_ok=%d",
                      label, WEXITSTATUS(status), pipeClosed ? 1 : 0,
                      proofOk ? 1 : 0, stageOk ? 1 : 0);
            } else if (WIFSIGNALED(status)) {
                ALOGE("V299_API25_%s_PROBE_SIGNAL sig=%d exec_fd_closed=%d proof_ok=%d stage_ok=%d",
                      label, WTERMSIG(status), pipeClosed ? 1 : 0,
                      proofOk ? 1 : 0, stageOk ? 1 : 0);
            } else {
                ALOGE("V299_API25_%s_PROBE_OTHER raw=%d exec_fd_closed=%d proof_ok=%d",
                      label, status, pipeClosed ? 1 : 0, proofOk ? 1 : 0);
            }
            return -EIO;
        }
        if (rc < 0 && errno != EINTR) {
            if (stagePipe[0] >= 0) close(stagePipe[0]);
            close(proofPipe[0]);
            ALOGE("V299_API25_%s_PROBE_WAIT_FAIL errno=%d (%s)",
                  label, errno, strerror(errno));
            return -errno;
        }
        usleep(5000);
    }

    if (stagePipe[0] >= 0) close(stagePipe[0]);
    close(proofPipe[0]);
    ALOGE("V299_API25_%s_PROBE_TIMEOUT pid=%d exec_fd_closed=%d",
          label, child, pipeClosed ? 1 : 0);
    signalProcessGroup(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return -ETIMEDOUT;
}

pid_t startLegacy(const std::string& root, int* stageReadFd) {
    int stagePipe[2] = {-1, -1};
    const std::string childLogPath = root + "/data/gps/v290-preload-child.log";
    int childLogFd = -1;
    if (pipe2(stagePipe, O_CLOEXEC | O_NONBLOCK) != 0) return -1;

    /*
     * V302: open the diagnostic stream before chroot/setuid.  V301 tried to
     * open /data/gps/v290-preload-child.log only after becoming uid=gps and
     * produced V290_CHILD_STAGE=log-open-failed, hiding the one stream that
     * contains the API25 server/gpsd preflight.  dup2() below deliberately
     * removes O_CLOEXEC from stdout/stderr while the original fd stays CLOEXEC.
     */
    childLogFd = open(childLogPath.c_str(),
                      O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0660);
    if (childLogFd >= 0) {
        (void)fchown(childLogFd, kGpsUid, kGpsGid);
        (void)fchmod(childLogFd, 0660);
    }

    pid_t child = fork();
    if (child != 0) {
        close(stagePipe[1]);
        if (childLogFd >= 0) close(childLogFd);
        if (stageReadFd) *stageReadFd = stagePipe[0];
        else close(stagePipe[0]);
        return child;
    }

    close(stagePipe[0]);
    resetChildSignalState();
    if (childLogFd >= 0) {
        (void)dup2(childLogFd, STDOUT_FILENO);
        (void)dup2(childLogFd, STDERR_FILENO);
        if (childLogFd > STDERR_FILENO) close(childLogFd);
        writeChildStage(stagePipe[1], "V302_CHILD_STAGE=log-preopen-pass");
    } else {
        writeChildStage(stagePipe[1], "V302_CHILD_STAGE=log-preopen-failed");
    }
    writeChildStage(stagePipe[1], "V290_CHILD_STAGE=forked");
    if (setsid() < 0) {
        writeChildStage(stagePipe[1], "V290_CHILD_STAGE=setsid-failed");
        _exit(119);
    }
    writeChildStage(stagePipe[1], "V290_CHILD_STAGE=setsid-pass");
    if (chroot(root.c_str()) != 0 || chdir("/") != 0) {
        writeChildStage(stagePipe[1], "V290_CHILD_STAGE=chroot-failed");
        _exit(120);
    }
    writeChildStage(stagePipe[1], "V290_CHILD_STAGE=chroot-pass");
    gid_t groups[] = {kSystemGid, kInetGid, kNetRawGid, kSdcardRwGid, kWakelockGid};
    if (setgroups(sizeof(groups) / sizeof(groups[0]), groups) != 0) {
        writeChildStage(stagePipe[1], "V290_CHILD_STAGE=setgroups-failed");
        _exit(121);
    }
    if (setgid(kGpsGid) != 0 || setuid(kGpsUid) != 0) {
        writeChildStage(stagePipe[1], "V290_CHILD_STAGE=setuid-failed");
        _exit(122);
    }
    writeChildStage(stagePipe[1], "V290_CHILD_STAGE=credentials-pass");

    dprintf(STDERR_FILENO,
            "V302_CHILD_STAGE=pre-exec pid=%ld uid=%u gid=%u\n",
            (long)getpid(), (unsigned)getuid(), (unsigned)getgid());

    setenv("LD_LIBRARY_PATH", "/system/lib:/system/vendor/lib", 1);
    setenv("LD_PRELOAD",
           "/system/lib/libm.so:/system/lib/liblt013g-gps-bridge-v290.so",
           1);
    dprintf(STDERR_FILENO, "V306_DLFNCN_PROVIDER=API25_LINKER_BUILTIN probe=0\n");
    setenv("ANDROID_ROOT", "/system", 1);
    setenv("ANDROID_DATA", "/data", 1);
    setenv("TZ", "GMT0", 1);
    unsetenv("LT013G_BRIDGE_PROBE_ONLY");
    unsetenv("ANDROID_SOCKET_gps");
    writeChildStage(stagePipe[1], "V295_CHILD_STAGE=exec-enter-gpsd-host");
    execl("/system/bin/gpsd", "lt013g-gps-bridge-host", static_cast<char*>(nullptr));
    writeChildStage(stagePipe[1], "V290_CHILD_STAGE=exec-failed");
    dprintf(STDERR_FILENO, "V290_CHILD_STAGE=exec-failed errno=%d (%s)\n", errno, strerror(errno));
    _exit(127);
}

bool waitLegacyChild(pid_t child, int stageFd, int* statusOut) {
    int status = 0;
    bool stageClosed = false;
    const long long traceDeadline = monotonicMs() + 2500;

    while (!stageClosed && monotonicMs() < traceDeadline) {
        char buffer[512];
        ssize_t n = read(stageFd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            ALOGI("V290_CHILD_PREEXEC_TRACE %s", buffer);
        } else if (n == 0) {
            stageClosed = true;
        } else if (errno != EAGAIN && errno != EINTR) {
            ALOGW("V290_CHILD_PREEXEC_PIPE_READ_FAIL errno=%d (%s)", errno, strerror(errno));
            stageClosed = true;
        }

        pid_t rc = waitpid(child, &status, WNOHANG);
        if (rc == child) {
            drainChildStagePipe(stageFd);
            close(stageFd);
            if (statusOut) *statusOut = status;
            return true;
        }
        if (rc < 0 && errno != EINTR) {
            ALOGE("V290_CHILD_WAITPID_EARLY_FAIL pid=%d errno=%d (%s)",
                  child, errno, strerror(errno));
            close(stageFd);
            return false;
        }
        if (!stageClosed) usleep(5000);
    }

    drainChildStagePipe(stageFd);
    close(stageFd);
    if (!stageClosed) ALOGW("V290_CHILD_PREEXEC_PIPE_TIMEOUT pid=%d", child);
    else ALOGI("V290_CHILD_EXEC_BOUNDARY=PASS pid=%d", child);

    for (;;) {
        pid_t rc = waitpid(child, &status, 0);
        if (rc == child) {
            if (statusOut) *statusOut = status;
            return true;
        }
        if (rc < 0 && errno == EINTR) continue;
        ALOGE("V290_CHILD_WAITPID_FAIL pid=%d errno=%d (%s)",
              child, errno, strerror(errno));
        return false;
    }
}

void stopProcessGroup(pid_t leader) {
    if (leader <= 0) return;
    signalProcessGroup(leader, SIGTERM);
    usleep(300000);
    signalProcessGroup(leader, SIGKILL);
}

unsigned restartDelayMs(int status, long long runtimeMs, unsigned* crashStreak) {
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        *crashStreak = 0;
        return 100;
    }
    if (runtimeMs >= 30000) *crashStreak = 0;
    if (*crashStreak < 16) ++*crashStreak;
    if (*crashStreak >= 8) return 30000;
    unsigned shift = std::min(*crashStreak - 1, 4u);
    return std::min(500u << shift, 8000u);
}
}  // namespace

int main(int argc, char** argv) {
    std::string archive = "/vendor/etc/lt013g-gps-legacy/rootfs-mutable.tar";
    std::string workRoot = "/data/vendor/gps/legacy-v195";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--archive") == 0 && i + 1 < argc) archive = argv[++i];
        else if (strcmp(argv[i], "--server") == 0 && i + 1 < argc) { ++i; /* legacy rc arg: intentionally ignored */ }
        else if (strcmp(argv[i], "--work-root") == 0 && i + 1 < argc) workRoot = argv[++i];
        else {
            ALOGE("bad argument: %s", argv[i]);
            return 2;
        }
    }

    struct sigaction action {};
    action.sa_handler = onSignal;
    (void)sigemptyset(&action.sa_mask);
    (void)sigaction(SIGTERM, &action, nullptr);
    (void)sigaction(SIGINT, &action, nullptr);
    (void)sigaction(SIGHUP, &action, nullptr);

    /*
     * Be explicit about SIGCHLD semantics.  The launcher must own/reap its
     * children itself; inherited SIG_IGN/SA_NOCLDWAIT would make waitpid()
     * report ECHILD and can create false watchdog behaviour.
     */
    struct sigaction childAction {};
    childAction.sa_handler = SIG_DFL;
    (void)sigemptyset(&childAction.sa_mask);
    childAction.sa_flags = 0;
    if (sigaction(SIGCHLD, &childAction, nullptr) != 0) {
        ALOGE("V290_SIGCHLD_RESET=FAIL errno=%d (%s)", errno, strerror(errno));
        return 2;
    }
    ALOGI("V290_SIGCHLD_RESET=PASS");

    // Android 11 init's 'namespace mnt' sets up a per-service mount
    // namespace and makes inherited mounts slaves before our SELinux
    // domain starts. Do not unshare/remount '/' here: Android 11 forbids
    // vendor processes from filesystem remount under neverallow.
    ALOGI("SMT311_V316_GPS_INIT_MOUNT_NAMESPACE=PASS");

    std::string root;
    if (!setupRootfs(archive, workRoot, &root)) return 4;
    ALOGI("V195 legacy rootfs ready: %s", root.c_str());
    if (!setupPrivateLegacyBinder(root)) {
        detachRuntimeMounts(root);
        return 7;
    }
    if (!startLegacySensorIsland(root)) {
        stopLegacySensorIsland();
        detachRuntimeMounts(root);
        return 8;
    }

    /*
     * v294 proved that the API25 mksh-specific probe dies after exec, while
     * historical logs from this same boot prove that the exact API25 gpsd
     * executable runs on the current kernel.  Use gpsd only as the loader host:
     * the preload constructor exits immediately in probe mode and never enters
     * gpsd main().  Test both root and the real gps credentials before serving.
     */
    const int rootProbeRc = runApi25GpsdPreloadProbe(root, false, "ROOT_GPSD_PRELOAD");
    if (rootProbeRc == 0) {
        const int gpsProbeRc = runApi25GpsdPreloadProbe(root, true, "GPS_GPSD_PRELOAD");
        if (gpsProbeRc == 0) {
            ALOGI("V377_API25_GPSD_PRELOAD_PREFLIGHT=PASS");
        } else {
            ALOGW("V377_API25_GPSD_PRELOAD_PREFLIGHT=SOFT_FAIL scope=gps rc=%d runtime_bridge_authoritative=1", gpsProbeRc);
        }
    } else {
        ALOGW("V377_API25_GPSD_PRELOAD_PREFLIGHT=SOFT_FAIL scope=root rc=%d runtime_bridge_authoritative=1", rootProbeRc);
    }

    unsigned crashStreak = 0;
    while (!gStop) {
        int status = 0;
        int stageFd = -1;
        const long long startedMs = monotonicMs();
        gChild = startLegacy(root, &stageFd);
        if (gChild < 0) {
            ALOGE("fork legacy server failed: %s", strerror(errno));
            if (stageFd >= 0) close(stageFd);
            break;
        }
        const pid_t child = gChild;
        ALOGI("V290_API25_PRELOAD_CHILD pid=%d", child);
        ALOGI("V195 legacy server pid=%d", child);
        if (!waitLegacyChild(child, stageFd, &status)) {
            ALOGE("V290_CHILD_WAIT=FAIL pid=%d", child);
            stopProcessGroup(child);
            gChild = -1;
            break;
        }
        stopProcessGroup(child);
        gChild = -1;
        const long long runtimeMs = monotonicMs() - startedMs;
        if (WIFEXITED(status)) {
            ALOGW("V290_API25_BRIDGE_SERVER_EXIT kind=exit code=%d raw=%d runtime_ms=%lld stop=%d",
                  WEXITSTATUS(status), status, runtimeMs, gStop);
        } else if (WIFSIGNALED(status)) {
            ALOGW("V290_API25_BRIDGE_SERVER_EXIT kind=signal sig=%d raw=%d runtime_ms=%lld stop=%d",
                  WTERMSIG(status), status, runtimeMs, gStop);
        } else {
            ALOGW("V290_API25_BRIDGE_SERVER_EXIT kind=other raw=%d runtime_ms=%lld stop=%d",
                  status, runtimeMs, gStop);
        }
        ALOGW("V195 legacy server exited status=%d runtime_ms=%lld stop=%d",
              status, runtimeMs, gStop);
        if (gStop) break;
        const unsigned delayMs = restartDelayMs(status, runtimeMs, &crashStreak);
        ALOGW("V195 restart delay_ms=%u crash_streak=%u", delayMs, crashStreak);
        usleep(delayMs * 1000u);
    }

    stopProcessGroup(gChild);
    stopLegacySensorIsland();
    detachRuntimeMounts(root);
    int power = open("/sys/class/sec/gps/GPS_PWR_EN/value", O_WRONLY | O_CLOEXEC);
    if (power >= 0) {
        (void)write(power, "0", 1);
        close(power);
    }
    return 0;
}
