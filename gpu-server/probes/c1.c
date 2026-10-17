/* C1: no GPU submission, VideoOut, process creation or budget changes. */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include "ps5_sce.h"
#include "ps5log.h"
#include "app.h"

int sceKernelAvailableFlexibleMemorySize(size_t *available);
int sceKernelAllocateMainDirectMemory(size_t, size_t, int, int64_t *);
#ifdef PROBE_APP
extern const unsigned char _binary_radv_so_start[], _binary_radv_so_end[];
static void shell_exit(void) {
    int rc = sceSystemServiceLoadExec("exit", NULL);
    ps5log("sceSystemServiceLoadExec(exit) rc=0x%x", rc);
    for (;;) sleep(1);
}
static void *deadline(void *unused) {
    (void)unused;
    sleep(30);
    ps5log("STOP unexpected diagnostic deadline=30s");
    shell_exit();
    return NULL;
}
#else
static void deadline(int signal_number) {
    (void)signal_number;
    static const char message[] = "STOP C1 plain deadline=30s code=124\n";
    ps5log_emergency_write(message, sizeof(message) - 1);
    _exit(124);
}
#endif

static int flexible(const char *stage, size_t mapping) {
    size_t available = 0;
    int rc = sceKernelAvailableFlexibleMemorySize(&available);
    ps5log("flexible stage=%s mapping=%zu rc=0x%x available=%zu", stage, mapping, rc, available);
    return rc;
}

static int shared_mapping(size_t bytes, int gpu) {
    if (bytes > 16384 && flexible("before", bytes)) return 1;
    errno = 0;
    int fd = shm_open(SHM_ANON, O_RDWR | O_CREAT, 0600);
    ps5log("shm_open bytes=%zu fd=%d errno=%d", bytes, fd, errno);
    if (fd < 0) return 1;
    errno = 0;
    int rc = ftruncate(fd, bytes);
    ps5log("ftruncate bytes=%zu rc=%d errno=%d", bytes, rc, errno);
    if (rc) { close(fd); return 1; }
    errno = 0;
    void *map = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ps5log("mmap MAP_SHARED bytes=%zu ptr=%p errno=%d", bytes, map, errno);
    if (map == MAP_FAILED) { close(fd); return 1; }
    /* Commit and verify every 16 KiB page, not merely a virtual reservation. */
    volatile unsigned char *pages = map;
    for (size_t i = 0; i < bytes; i += 16384) pages[i] = 0x5a;
    for (size_t i = 0; i < bytes; i += 16384)
        if (pages[i] != 0x5a) { munmap(map, bytes); close(fd); return 1; }
    int failed = bytes > 16384 && flexible("mapped-touched", bytes) != 0;
    if (gpu) {
        rc = sceKernelMprotect(map, bytes, PS5_KERNEL_PROT_CPU_READ | PS5_KERNEL_PROT_CPU_WRITE |
                               PS5_KERNEL_PROT_GPU_READ | PS5_KERNEL_PROT_GPU_WRITE);
        ps5log("sceKernelMprotect SHM_ANON MAP_SHARED bytes=%zu protection=0x33 rc=0x%x", bytes, rc);
        failed |= rc != 0;
    }
    rc = munmap(map, bytes);
    ps5log("munmap bytes=%zu rc=%d", bytes, rc);
    failed |= rc != 0;
    rc = close(fd);
    ps5log("close SHM rc=%d", rc);
    failed |= rc != 0;
    if (bytes > 16384) failed |= flexible("released", bytes) != 0;
    return failed;
}

#ifdef PROBE_APP
static int direct_ladder(void) {
    int64_t pool = sceKernelGetDirectMemorySize();
    ps5log("direct pool=%lld independent released allocations step=2MiB ceiling=256MiB", (long long)pool);
    for (int main = 0; main < 2; main++) {
        for (size_t bytes = 2u << 20; bytes <= (256u << 20); bytes += 2u << 20) {
            int64_t physical = -1;
            int rc = main ? sceKernelAllocateMainDirectMemory(bytes, 0x20000, 3, &physical) :
                sceKernelAllocateDirectMemory(0, pool, bytes, 0x20000, 12, &physical);
            ps5log("direct api=%s type=%d bytes=%zu rc=0x%x physical=0x%llx", main ? "main" : "full",
                   main ? 3 : 12, bytes, rc, (unsigned long long)physical);
            /* The first allocation refusal is the measured ceiling, not permission to retry. */
            if (rc) { ps5log("STOP direct allocation ceiling"); return 1; }
            rc = sceKernelReleaseDirectMemory(physical, bytes);
            ps5log("direct release bytes=%zu rc=0x%x", bytes, rc);
            if (rc) return 1;
            /* Keep the initial pull from losing the start to the 256-line log ring. */
            usleep(10000);
        }
    }
    return 0;
}

static int instance_only(void) {
    const char *path = "/data/ps5wl/probe-radv.so.1";
    FILE *file = fopen(path, "wb");
    if (!file) { ps5log("RADV fopen errno=%d", errno); return 1; }
    size_t bytes = _binary_radv_so_end - _binary_radv_so_start;
    size_t written = fwrite(_binary_radv_so_start, 1, bytes, file);
    int rc = fclose(file);
    ps5log("RADV extracted path=%s bytes=%zu written=%zu fclose=%d", path, bytes, written, rc);
    if (written != bytes || rc) return 1;
    void *library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    ps5log("RADV dlopen handle=%p error=%s", library, library ? "none" : dlerror());
    if (!library) return 1;
    PFN_vkGetInstanceProcAddr get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vkGetInstanceProcAddr");
    if (!get) get = (PFN_vkGetInstanceProcAddr)dlsym(library, "vk_icdGetInstanceProcAddr");
    if (!get) { ps5log("STOP RADV resolver missing"); dlclose(library); return 1; }
    PFN_vkCreateInstance create = (PFN_vkCreateInstance)get(NULL, "vkCreateInstance");
    if (!create) { dlclose(library); return 1; }
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ps5wl-gpu-c1", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo info = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = create(&info, NULL, &instance);
    ps5log("RADV vkCreateInstance result=%d instance=%p (no physical-device enumeration or GPU work)", result, (void *)instance);
    if (result == VK_SUCCESS) {
        PFN_vkDestroyInstance destroy = (PFN_vkDestroyInstance)get(instance, "vkDestroyInstance");
        destroy(instance, NULL);
    }
    dlclose(library);
    unlink(path);
    return result != VK_SUCCESS;
}
#endif

int main(void) {
#ifdef PROBE_APP
    ps5log_init("ps5wl-gpu-c1-app", 9530);
    pthread_t timer;
    int rc = pthread_create(&timer, NULL, deadline, NULL);
    if (rc) { ps5log("STOP deadline thread rc=%d", rc); shell_exit(); }
    pthread_detach(timer);
#else
    ps5log_init("ps5wl-gpu-c1-plain", 9592);
    signal(SIGALRM, deadline);
    alarm(30);
#endif
    app_info_t app = {0};
    int app_rc = sceKernelGetAppInfo(getpid(), &app);
    int foreground = sceSystemServiceGetAppIdOfRunningBigApp();
    ps5log("context pid=%d uid=%u app_rc=0x%x own_app=0x%x foreground=0x%x page=%d", getpid(),
           getuid(), app_rc, app.app_id, foreground, getpagesize());
#ifdef PROBE_APP
    if (app_rc || !app.app_id || app.app_id != (uint32_t)foreground) {
        ps5log("STOP not foreground BigApp"); shell_exit();
    }
#endif
    int failed = shared_mapping(16384, 1);
    const size_t sizes[] = {64u << 20, 256u << 20, 1024u << 20};
    for (unsigned i = 0; !failed && i < sizeof(sizes) / sizeof(sizes[0]); i++)
        failed = shared_mapping(sizes[i], 0);
    if (!failed) {
        int pair[2];
        errno = 0;
        int rc = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair);
        ps5log("AF_UNIX SOCK_SEQPACKET socketpair rc=%d errno=%d", rc, errno);
        failed = rc != 0;
        if (!rc) {
            char byte = 0;
            ssize_t sent = send(pair[0], "Q", 1, 0);
            ssize_t received = recv(pair[1], &byte, 1, MSG_DONTWAIT);
            ps5log("SEQPACKET send=%zd recv=%zd byte=0x%x", sent, received, byte);
            failed = sent != 1 || received != 1 || byte != 'Q';
            close(pair[0]); close(pair[1]);
        }
    }
#ifdef PROBE_APP
    if (!failed) failed = direct_ladder();
    if (!failed) failed = instance_only();
#endif
    ps5log("C1 RESULT %s code=%d", failed ? "STOP" : "PASS", failed);
    ps5log_flush_wait(5000);
#ifdef PROBE_APP
    shell_exit();
#else
    alarm(0);
#endif
    return failed;
}
