/* SDK sys/mman.h:168 exposes SHM_ANON only in the default BSD namespace.
 * sys/cdefs.h:715-775 disables that namespace for strict POSIX/XSI builds. */
#if defined(PS5WL_PS5) || defined(__FreeBSD__)
#undef _POSIX_C_SOURCE
#undef _XOPEN_SOURCE
#endif
#define _GNU_SOURCE
#include "ps5wl-client.h"
#include "../ps5log/ps5log.h"
#include <wayland-client.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static void wayland_log(const char *format, va_list args) {
    char line[1024];
    vsnprintf(line, sizeof(line), format, args);
    ps5log("libwayland: %s", line);
}

int ps5wl_client_init(const char *name, uint16_t port) {
    if (ps5log_init(name, port) < 0) return -1;
    wl_log_set_handler_client(wayland_log);
#if defined(PS5WL_PS5) || defined(__FreeBSD__)
    if (setenv("XDG_RUNTIME_DIR", "/system_tmp", 1) < 0 ||
        setenv("WAYLAND_DISPLAY", "/system_tmp/wayland-0", 1) < 0) return -1;
#endif
    /* wl_keyboard supplies a serialized keymap: clients need no XKB data files. */
    ps5log("client-init name=%s pid=%ld display=%s runtime=%s", name, (long)getpid(),
        getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "wayland-0",
        getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
    return 0;
}

static uint64_t now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

struct wl_display *ps5wl_client_connect(unsigned timeout_ms) {
    uint64_t deadline = now_ms() + timeout_ms;
    struct wl_display *display;
    do {
        display = wl_display_connect(NULL);
        if (display) {
            ps5log("wayland-connected fd=%d", wl_display_get_fd(display));
            return display;
        }
        if (now_ms() >= deadline) break;
        struct timespec delay = {.tv_nsec = 100000000};
        nanosleep(&delay, NULL);
    } while (1);
    ps5log("wayland-connect-failed: %s", strerror(errno));
#if defined(PS5WL_PS5) || defined(__FreeBSD__)
    /* Otherwise process exit can discard the final pull-log line. */
    ps5log_flush_wait(1500);
#endif
    return NULL;
}

int ps5wl_client_shm(size_t size) {
    if (!size || size > INT_MAX) { errno = EINVAL; return -1; }
#if defined(PS5WL_PS5) || defined(__FreeBSD__)
    int fd = shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0600);
#else
    int fd = memfd_create("ps5wl-client", MFD_CLOEXEC | MFD_ALLOW_SEALING);
#endif
    if (fd < 0) return -1;
    /* PS5 ignores some *_CLOEXEC creation flags; set descriptor flags explicitly. */
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 || ftruncate(fd, (off_t)size) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}
