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
#include <dlfcn.h>
#endif
#if defined(PS5WL_PS5) && defined(PS5WL_VENUS) && defined(PS5WL_SYNC_TRACE)
static void venus_log(const char *message) {
    ps5log("Venus: %s", message);
}

int ps5wl_vulkan_trace_bind(void *library) {
    void (*set_handler)(void (*)(const char *)) =
        (void (*)(void (*)(const char *)))dlsym(library, "ps5wl_venus_set_log_handler");
    if (!set_handler) {
        ps5log("FAIL Venus trace log handler unavailable");
        return -1;
    }
    set_handler(venus_log);
    return 0;
}
#endif

#ifndef PS5WL_DRIVER_NAME
#define PS5WL_DRIVER_NAME "radv"
#endif

#ifdef PS5WL_PS5
#include "ps5_sce.h"
#include <stddef.h>
#include <stdint.h>
#include <ucontext.h>
#ifdef PS5WL_FAULT_TRACE
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include <ps5/kernel.h>
#endif

/* Firmware-measured prefix, reused from probes/lkl/faults.c. */
struct ps5_context_prefix { unsigned char native_prefix[64]; mcontext_t machine; };
_Static_assert(offsetof(struct ps5_context_prefix, machine) == 64, "PS5 native context prefix");
_Static_assert(offsetof(mcontext_t, mc_rip) == 160, "native amd64 GPR prefix");
static uintptr_t driver_base;

/* Handle ABI from the pinned SDK v0.44 crt/rtld.h. RTLD_LOCAL drivers are
 * absent from dladdr's root dependency search, so inspect their own handle. */
struct sdk_module;
struct sdk_module_link {
    struct sdk_module *module;
    struct sdk_module_link *next, *previous;
};
struct sdk_module {
    void (*operations[7])(void);
    char path[1024];
    void *base;
    unsigned long size;
    int references;
    struct sdk_module *parent;
    struct sdk_module_link *first, *last;
};
_Static_assert(offsetof(struct sdk_module, base) == 1080, "SDK v0.44 rtld base");
_Static_assert(offsetof(struct sdk_module, first) == 1112, "SDK v0.44 rtld dependencies");

static void log_modules(const struct sdk_module *module, void **seen, size_t *count) {
    for (size_t i = 0; i < *count; ++i)
        if (seen[i] == module->base) return;
    if (*count == 256) {
        ps5log("Vulkan module enumeration exceeds 256 mappings");
        return;
    }
    seen[(*count)++] = module->base;
    ps5log("Vulkan module base=%p size=%#lx path=%s",
           module->base, module->size, module->path);
    for (const struct sdk_module_link *link = module->first; link; link = link->next)
        log_modules(link->module, seen, count);
}

void ps5wl_vulkan_log_module(void *library, void *symbol) {
    const struct sdk_module *driver = library;
    void *seen[256];
    size_t count = 0;
    driver_base = (uintptr_t)driver->base;
    ps5log("Vulkan driver module base=%p size=%#lx symbol=%p path=%s",
           driver->base, driver->size, symbol, driver->path);
    const struct sdk_module *root = dlopen(NULL, RTLD_NOW);
    if (root) log_modules(root, seen, &count);
    log_modules(driver, seen, &count);
}
extern const unsigned char _binary_driver_so_start[], _binary_driver_so_end[];
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

void ps5wl_vulkan_dump_log(void) {
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
    ps5wl_vulkan_dump_log();
    ps5log_flush_wait(5000);
    abort();
}

static char *signal_text(char *out, const char *text) {
    while (*text) *out++ = *text++;
    return out;
}

static char *signal_hex(char *out, uint64_t value) {
    static const char hex[] = "0123456789abcdef";
    for (unsigned shift = 64; shift; shift -= 4)
        *out++ = hex[(value >> (shift - 4)) & 15];
    return out;
}

#ifdef PS5WL_FAULT_TRACE
enum { REG_RAX, REG_RBX, REG_RCX, REG_RDX, REG_RSI, REG_RDI, REG_RBP, REG_RSP,
       REG_R8, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15,
       REG_RIP, REG_FLAGS, REGISTER_COUNT };
static uint64_t fault_registers[REGISTER_COUNT];
static int fault_pipe[2] = {-1, -1};
static pthread_t fault_thread;
static atomic_int fault_claimed;
static atomic_int fault_ready;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "signal-safe fault ownership");

/* The SDK's process-read helper can use privileged debugging syscalls. Keep it
 * outside the signal handler, and split at 4 KiB boundaries so its page-table
 * fallback never crosses a page or over-copies the final partial page. */
static int fault_read(uintptr_t address, void *destination, size_t length) {
    unsigned char *out = destination;
    while (length) {
        size_t chunk = 4096 - (address & 4095);
        if (chunk > length) chunk = length;
        if (kernel_proc_copyout(getpid(), address, out, chunk)) return -1;
        address += chunk;
        out += chunk;
        length -= chunk;
    }
    return 0;
}

static void fault_line(const char *label, uint64_t first, uint64_t second) {
    char message[192], *out = signal_text(message, label);
    out = signal_hex(out, first);
    out = signal_text(out, " ");
    out = signal_hex(out, second);
    *out++ = '\n';
    ps5log_emergency_write(message, (size_t)(out - message));
}

static void fault_bytes(const char *label, uintptr_t address, size_t length) {
    static const char hex[] = "0123456789abcdef";
    unsigned char bytes[256];
    if (fault_read(address, bytes, length)) {
        fault_line("Vulkan fault unreadable address/bytes=", address, length);
        return;
    }
    for (size_t offset = 0; offset < length; offset += 16) {
        char message[192], *out = signal_text(message, label);
        out = signal_hex(out, address + offset);
        *out++ = ' ';
        for (size_t i = 0; i < 16 && offset + i < length; ++i) {
            *out++ = hex[bytes[offset + i] >> 4];
            *out++ = hex[bytes[offset + i] & 15];
        }
        *out++ = '\n';
        ps5log_emergency_write(message, (size_t)(out - message));
    }
}

static void *fault_collect(void *unused) {
    (void)unused;
    unsigned char number;
    ssize_t result;
    do { result = read(fault_pipe[0], &number, 1); } while (result < 0 && errno == EINTR);
    if (result != 1 || !number) return NULL;
    if (!atomic_load_explicit(&fault_ready, memory_order_acquire)) return NULL;
    static const char *const names[REGISTER_COUNT] = {
        "rax=", "rbx=", "rcx=", "rdx=", "rsi=", "rdi=", "rbp=", "rsp=",
        "r8=", "r9=", "r10=", "r11=", "r12=", "r13=", "r14=", "r15=",
        "rip=", "rflags="
    };
    for (size_t i = 0; i < REGISTER_COUNT; ++i)
        fault_line(names[i], fault_registers[i], 0);
    fault_bytes("Vulkan RIP bytes ", fault_registers[REG_RIP], 64);
    fault_bytes("Vulkan stack bytes ", fault_registers[REG_RSP], 256);
    uintptr_t frame = fault_registers[REG_RBP];
    for (unsigned i = 0; i < 16; ++i) {
        uintptr_t pair[2];
        if ((frame & 7) || frame < fault_registers[REG_RSP] ||
            frame - fault_registers[REG_RSP] > 16 * 1024 * 1024 ||
            fault_read(frame, pair, sizeof(pair))) {
            fault_line("Vulkan frame walk stopped index/address=", i, frame);
            break;
        }
        fault_line("Vulkan frame index/return=", i, pair[1]);
        if (pair[0] <= frame) break;
        frame = pair[0];
    }
    static const char complete[] = "Vulkan extended fault capture complete\n";
    ps5log_emergency_write(complete, sizeof(complete) - 1);
    _exit(128 + number);
}

static void fault_collector_close(void) {
    const unsigned char stop = 0;
    (void)write(fault_pipe[1], &stop, 1);
    pthread_join(fault_thread, NULL);
    close(fault_pipe[0]);
    close(fault_pipe[1]);
}

static int fault_collector_start(void) {
    if (pipe(fault_pipe)) return -1;
    int result = pthread_create(&fault_thread, NULL, fault_collect, NULL);
    if (result) {
        close(fault_pipe[0]);
        close(fault_pipe[1]);
        errno = result;
        return -1;
    }
    atexit(fault_collector_close);
    return 0;
}
#endif
static void fatal_signal(int number, siginfo_t *info, void *context) {
    const mcontext_t *mc = &((const struct ps5_context_prefix *)context)->machine;
    char message[384], *out = message;
    if (mc->mc_len < 256 || mc->mc_len > 2048) {
        static const char invalid[] = "Vulkan fatal signal: unsupported native context length\n";
        ps5log_emergency_write(invalid, sizeof(invalid) - 1);
        _exit(128 + number);
    }
    out = signal_text(out, "Vulkan fatal signal=");
    out = signal_hex(out, (unsigned)number);
    out = signal_text(out, " fault_address=");
    out = signal_hex(out, (uintptr_t)info->si_addr);
    out = signal_text(out, " rip=");
    out = signal_hex(out, mc->mc_rip);
    out = signal_text(out, " rsp=");
    out = signal_hex(out, mc->mc_rsp);
    out = signal_text(out, " trap=");
    out = signal_hex(out, mc->mc_trapno);
    out = signal_text(out, " error=");
    out = signal_hex(out, mc->mc_err);
    out = signal_text(out, " native_mc_len=");
    out = signal_hex(out, mc->mc_len);
    out = signal_text(out, " driver_base=");
    out = signal_hex(out, driver_base);
    *out++ = '\n';
    ps5log_emergency_write(message, (size_t)(out - message));
#ifdef PS5WL_FAULT_TRACE
    if (!atomic_exchange_explicit(&fault_claimed, 1, memory_order_relaxed)) {
        const uint64_t registers[REGISTER_COUNT] = {
            mc->mc_rax, mc->mc_rbx, mc->mc_rcx, mc->mc_rdx, mc->mc_rsi, mc->mc_rdi,
            mc->mc_rbp, mc->mc_rsp, mc->mc_r8, mc->mc_r9, mc->mc_r10, mc->mc_r11,
            mc->mc_r12, mc->mc_r13, mc->mc_r14, mc->mc_r15, mc->mc_rip, mc->mc_rflags
        };
        for (size_t i = 0; i < REGISTER_COUNT; ++i) fault_registers[i] = registers[i];
        atomic_store_explicit(&fault_ready, 1, memory_order_release);
        const unsigned char request = (unsigned char)number;
        if (write(fault_pipe[1], &request, 1) != 1) _exit(128 + number);
    }
    /* The collector must not take stdio/logger locks: the stopped thread may
     * own them. Bound its lifetime even if a debugging syscall fails to return. */
    struct timespec start, now;
    if (clock_gettime(CLOCK_MONOTONIC, &start)) _exit(128 + number);
    do {
        __asm__ volatile("pause");
        if (clock_gettime(CLOCK_MONOTONIC, &now)) break;
    } while (now.tv_sec - start.tv_sec < 3);
#endif
    _exit(128 + number);
}
#endif

int ps5wl_vulkan_install(void) {
    if (!getenv("PS5WL_VULKAN_LIBRARY"))
        setenv("PS5WL_VULKAN_LIBRARY", PS5WL_VULKAN_DEFAULT_LIBRARY, 0);
#ifdef PS5WL_VENUS
    if (!getenv("VTEST_SOCKET_NAME"))
        setenv("VTEST_SOCKET_NAME", "/system_tmp/ps5wl-gpu-0", 0);
#endif
#ifdef PS5WL_SYNC_TRACE
    if (setenv("VN_DEBUG", "wsi", 1)) return -1;
    if (setenv("WAYLAND_DEBUG", "client", 1)) return -1;
#endif
#ifdef PS5WL_PS5
#ifdef PS5WL_FAULT_TRACE
    if (fault_collector_start()) {
        ps5log("FAIL starting Vulkan fault collector errno=%d", errno);
        return -1;
    }
#endif
    struct sigaction action = {0};
    action.sa_sigaction = fatal_signal;
    action.sa_flags = SA_SIGINFO;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGABRT, &action, NULL) || sigaction(SIGSEGV, &action, NULL) ||
        sigaction(SIGBUS, &action, NULL) || sigaction(SIGILL, &action, NULL)) {
        ps5log("FAIL installing native Vulkan fault handler errno=%d", errno);
        return -1;
    }
    log_direct_memory("before-driver");
    /* pacbrew Mesa patch 0028 uses /data for payload-owned writable storage.
     * /system_tmp is too small for the driver (measured ENOSPC at 256 KiB).
     */
    const char *directory = "/data/ps5wl";
    const char *path = PS5WL_VULKAN_DEFAULT_LIBRARY;
    if (mkdir(directory, 0700) && errno != EEXIST) {
        ps5log("%s mkdir path=%s errno=%d %s", PS5WL_DRIVER_NAME, directory, errno, strerror(errno));
        return -1;
    }
    snprintf(driver_log, sizeof(driver_log), "%s/mesa-%d.log", directory, getpid());
    int log_fd = open(driver_log, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (log_fd < 0) {
        ps5log("%s log open errno=%d %s", PS5WL_DRIVER_NAME, errno, strerror(errno));
        return -1;
    }
    if (log_fd != STDERR_FILENO) {
        if (dup3(log_fd, STDERR_FILENO, 0) < 0) {
            int saved = errno;
            close(log_fd);
            remove_driver_log();
            ps5log("%s log redirect errno=%d %s", PS5WL_DRIVER_NAME, saved, strerror(saved));
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
        ps5log("%s fopen path=%s errno=%d %s", PS5WL_DRIVER_NAME, temporary, errno, strerror(errno));
        return -1;
    }
    size_t length = (size_t)(_binary_driver_so_end - _binary_driver_so_start);
    size_t written = fwrite(_binary_driver_so_start, 1, length, file);
    int rc = fclose(file);
    if (written != length || rc) {
        ps5log("%s write bytes=%zu expected=%zu close=%d errno=%d %s",
            PS5WL_DRIVER_NAME, written, length, rc, errno, strerror(errno));
        unlink(temporary);
        return -1;
    }
    if (rename(temporary, path)) {
        ps5log("%s rename path=%s errno=%d %s", PS5WL_DRIVER_NAME, path, errno, strerror(errno));
        unlink(temporary);
        return -1;
    }
    ps5log("%s library extracted bytes=%zu path=%s", PS5WL_DRIVER_NAME, length, path);
    return 0;
#else
    return 0;
#endif
}
