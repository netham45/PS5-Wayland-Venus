#include "ps5log.h"

#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* SDK samples/hello_world uses this kernel notification request layout. */
struct notify_request {
    char reserved[45];
    char message[3075];
};
int sceKernelSendNotificationRequest(int, struct notify_request *, size_t, int);

static int
probe_unix(void)
{
    const char *path = "/system_tmp/ps5wl-hello.sock";
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    int error = fd < 0 ? errno : 0;
    ps5log("cap socket(AF_UNIX,SOCK_STREAM) fd=%d errno=%d", fd, error);
    if (fd < 0)
        return 0;

    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);
    addr.sun_len = offsetof(struct sockaddr_un, sun_path) + strlen(path) + 1;
    /* This is the hello payload's own disposable endpoint. */
    unlink(path);
    int rc = bind(fd, (struct sockaddr *)&addr, addr.sun_len);
    error = rc < 0 ? errno : 0;
    ps5log("cap bind path=%s rc=%d errno=%d", path, rc, error);
    int bound = rc == 0;
    close(fd);
    if (bound) {
        rc = unlink(path);
        error = rc < 0 ? errno : 0;
        ps5log("cap unlink path=%s rc=%d errno=%d", path, rc, error);
        return rc == 0;
    }
    return 0;
}

static int
probe_shm(void)
{
    const size_t length = 16384;
    int fd = shm_open(SHM_ANON, O_RDWR, 0600);
    int error = fd < 0 ? errno : 0;
    ps5log("cap shm_open(SHM_ANON) fd=%d errno=%d", fd, error);
    if (fd < 0)
        return 0;

    int rc = ftruncate(fd, length);
    error = rc < 0 ? errno : 0;
    ps5log("cap ftruncate size=%zu rc=%d errno=%d", length, rc, error);
    if (rc < 0) {
        close(fd);
        return 0;
    }
    void *mapping = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    error = mapping == MAP_FAILED ? errno : 0;
    ps5log("cap mmap(MAP_SHARED) addr=%p errno=%d", mapping, error);
    if (mapping == MAP_FAILED) {
        close(fd);
        return 0;
    }
    volatile unsigned char *byte = mapping;
    *byte = 0x5a;
    int ok = *byte == 0x5a;
    ps5log("cap shared-memory write/read value=0x%02x ok=%d pagesize=%ld",
           (unsigned)*byte, ok, sysconf(_SC_PAGESIZE));
    munmap(mapping, length);
    close(fd);
    return ok;
}

int
main(void)
{
    if (ps5log_init("hello", 9530) != 0) {
        perror("ps5log_init");
        return 1;
    }
    ps5log("started pid=%d uid=%u getppid=%d", (int)getpid(),
           (unsigned)getuid(), (int)getppid());
    struct notify_request req = {0};
    snprintf(req.message, sizeof(req.message), "ps5_wayland hello: capability probe running");
    int rc = sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
    ps5log("notification rc=%d", rc);
    int unix_ok = probe_unix();
    int shm_ok = probe_shm();
    for (int second = 0; second < 60; ++second) {
        ps5log("tick=%d pid=%d uid=%u getppid=%d af_unix=%s shm_anon=%s",
               second, (int)getpid(), (unsigned)getuid(), (int)getppid(),
               unix_ok ? "ok" : "failed", shm_ok ? "ok" : "failed");
        struct timespec delay = {.tv_sec = 1};
        while (nanosleep(&delay, &delay) < 0 && errno == EINTR)
            ;
    }
    ps5log("exiting status=0");
    ps5log_flush_wait(100);
    return 0;
}
