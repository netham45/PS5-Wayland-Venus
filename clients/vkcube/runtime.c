#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "ps5log.h"

#ifdef PS5WL_PS5
#include "ps5_sce.h"
extern const unsigned char _binary_radv_so_start[], _binary_radv_so_end[];
static char driver_log[128];

static void remove_driver_log(void) {
    if (driver_log[0]) unlink(driver_log);
}

static void log_direct_memory(const char *stage) {
    const int64_t pool = sceKernelGetDirectMemorySize();
    int64_t start = -1;
    size_t available = 0;
    int32_t rc = sceKernelAvailableDirectMemorySize(0, pool, 0x200000, &start, &available);
    ps5log("direct stage=%s uid=%u euid=%u gid=%u egid=%u pool=%lld available rc=%#x start=%lld bytes=%zu",
        stage, (unsigned)getuid(), (unsigned)geteuid(), (unsigned)getgid(), (unsigned)getegid(),
        (long long)pool, (unsigned)rc, (long long)start, available);
}

static void dump_driver_log(void) {
    FILE *log = fopen(driver_log, "r");
    if (!log) {
        ps5log("Mesa log unavailable path=%s errno=%d %s", driver_log, errno, strerror(errno));
        return;
    }
    if (!fseek(log, 0, SEEK_END)) {
        long length = ftell(log);
        fseek(log, length > 4096 ? length - 4096 : 0, SEEK_SET);
    }
    char snapshot[4097];
    size_t length = fread(snapshot, 1, sizeof(snapshot) - 1, log);
    snapshot[length] = '\0';
    fclose(log);
    remove_driver_log();
    /* Snapshot before logging: ps5log also writes to the captured stderr. */
    char *line = snapshot;
    char *end;
    while ((end = strchr(line, '\n'))) {
        *end = '\0';
        ps5log("Mesa/stderr: %s", line);
        line = end + 1;
    }
    if (*line) ps5log("Mesa/stderr: %s", line);
}

__attribute__((noreturn)) void __wrap___assert(const char *function, const char *file,
                                             int line, const char *expression) {
    ps5log("ASSERT %s:%d %s: %s", file, line, function, expression);
    log_direct_memory("assert");
    dump_driver_log();
    ps5log_flush_wait(5000);
    abort();
}

static void fatal_signal(int number) {
    char message[] = "Vulkan fatal signal: 00\n";
    const size_t digit = sizeof("Vulkan fatal signal: ") - 1;
    message[digit] = '0' + number / 10;
    message[digit + 1] = '0' + number % 10;
    ps5log_emergency_write(message, sizeof(message) - 1);
    ps5log_flush_wait(5000);
    _exit(128 + number);
}
#endif

int ps5wl_vulkan_install(void) {
#ifdef PS5WL_PS5
    signal(SIGABRT, fatal_signal);
    signal(SIGSEGV, fatal_signal);
    signal(SIGBUS, fatal_signal);
    signal(SIGILL, fatal_signal);
    log_direct_memory("before-driver");
    /* pacbrew Mesa patch 0028 uses /data for payload-owned writable storage.
     * /system_tmp is too small for the driver (measured ENOSPC at 256 KiB).
     */
    const char *directory = "/data/ps5wl";
    const char *path = "/data/ps5wl/libvulkan.so.1";
    if (mkdir(directory, 0700) && errno != EEXIST) {
        ps5log("RADV mkdir path=%s errno=%d %s", directory, errno, strerror(errno));
        return -1;
    }
    snprintf(driver_log, sizeof(driver_log), "%s/mesa-%d.log", directory, getpid());
    int log_fd = open(driver_log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log_fd < 0) {
        ps5log("RADV log open errno=%d %s", errno, strerror(errno));
        return -1;
    }
    if (log_fd != STDERR_FILENO) {
        if (dup3(log_fd, STDERR_FILENO, 0) < 0) {
            int saved = errno;
            close(log_fd);
            remove_driver_log();
            ps5log("RADV log redirect errno=%d %s", saved, strerror(saved));
            return -1;
        }
        close(log_fd);
    }
    atexit(remove_driver_log);
    /* Never truncate an inode another Vulkan client may still have mapped. */
    char temporary[128];
    snprintf(temporary, sizeof(temporary), "%s.tmp.%d", path, getpid());
    FILE *file = fopen(temporary, "wb");
    if (!file) {
        ps5log("RADV fopen path=%s errno=%d %s", temporary, errno, strerror(errno));
        return -1;
    }
    size_t length = (size_t)(_binary_radv_so_end - _binary_radv_so_start);
    size_t written = fwrite(_binary_radv_so_start, 1, length, file);
    int rc = fclose(file);
    if (written != length || rc) {
        ps5log("RADV write bytes=%zu expected=%zu close=%d errno=%d %s",
            written, length, rc, errno, strerror(errno));
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, path)) {
        ps5log("RADV rename path=%s errno=%d %s", path, errno, strerror(errno));
        unlink(temporary);
        return -1;
    }
    ps5log("RADV library extracted bytes=%zu path=%s", length, path);
    return 0;
#else
    return 0;
#endif
}
