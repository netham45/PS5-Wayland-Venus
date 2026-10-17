#define _GNU_SOURCE
#include "ps5wl_gpud.h"
#include "gpud_core.h"
#include "gpud_limits.h"
#include <inttypes.h>
#include <stdatomic.h>
#include "../vtest/vtest.h"
#include "../render/render.h"
#include "vkr_renderer.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#ifdef PS5WL_PS5
#include "ps5log.h"
#endif

struct resource { uint32_t id; uint64_t blob, shm_bytes; struct resource *next; };
struct context { uint32_t id; uint64_t host_bytes, device_bytes; unsigned allocations; struct resource *resources; gpud_retire_fn retire; void *opaque; struct context *next; };
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cb_lock = PTHREAD_MUTEX_INITIALIZER;
static struct context *contexts;
static uint32_t next_id = 1;
static unsigned context_count, max_contexts;
#ifdef PS5WL_PS5
#define GPUD_DEFAULT_CONTEXTS 2
#define GPUD_DEFAULT_HOST_MIB 128
#else
#define GPUD_DEFAULT_CONTEXTS 32
#define GPUD_DEFAULT_HOST_MIB 512
#endif
static bool started;
static atomic_bool stopping;
static uint64_t host_limit, device_limit;
static atomic_uint_fast64_t fd_exports;
void gpud_record_fd_export(void) { atomic_fetch_add(&fd_exports, 1); }
uint64_t gpud_fd_export_count(void) { return atomic_load(&fd_exports); }
bool gpud_memory_reserve(uint32_t id, bool host, uint64_t bytes)
{
    bool ok = false;
    pthread_mutex_lock(&cb_lock);
    for (struct context *c = contexts; c; c = c->next) if (c->id == id) {
        uint64_t *used = host ? &c->host_bytes : &c->device_bytes;
        uint64_t limit = host ? host_limit : device_limit;
        if (bytes <= limit - *used && c->allocations < 65536) {
            *used += bytes; c->allocations++; ok = true;
        }
        break;
    }
    pthread_mutex_unlock(&cb_lock);
    return ok;
}
void gpud_memory_free(uint32_t id, uint64_t blob, bool host, uint64_t bytes)
{
    pthread_mutex_lock(&cb_lock);
    for (struct context *c = contexts; c; c = c->next) if (c->id == id) {
        /* An exported resource keeps the same SHM pages alive after vkFreeMemory.
         * Transfer its charge instead of permitting allocations to bypass the quota.
         * Ignore already charged entries if the guest reuses a Vulkan object ID. */
        if (host && blob) {
            for (struct resource *r = c->resources; r; r = r->next) {
                if (r->blob == blob && !r->shm_bytes) {
                    r->shm_bytes = bytes;
                    pthread_mutex_unlock(&cb_lock);
                    return;
                }
            }
        }
        uint64_t *used = host ? &c->host_bytes : &c->device_bytes;
        *used -= bytes; c->allocations--; break;
    }
    pthread_mutex_unlock(&cb_lock);
}
void gpud_memory_release(uint32_t id, bool host, uint64_t bytes)
{
    gpud_memory_free(id, 0, host, bytes);
}
static void retire(uint32_t id, uint32_t ring, uint64_t fence)
{
    gpud_retire_fn fn = NULL;
    void *opaque = NULL;
    pthread_mutex_lock(&cb_lock);
    for (struct context *c = contexts; c; c = c->next)
        if (c->id == id) { fn = c->retire; opaque = c->opaque; break; }
    pthread_mutex_unlock(&cb_lock);
    /* vkr context destruction joins callback threads before removing the
     * registration. Never hold the shared registry lock across client code. */
    if (fn) fn(opaque, ring, fence);
}
static void logger(enum virgl_log_level_flags level, const char *message, void *opaque)
{
    (void)level; (void)opaque;
#ifdef PS5WL_PS5
    ps5log("gpud: %s", message);
#else
    fprintf(stderr, "%s\n", message);
#endif
}
static const struct vkr_renderer_callbacks callbacks = { .debug_logger = logger, .retire_fence = retire };
size_t gpud_core_capset(void *data) { return vkr_get_capset(data, 0); }
uint32_t gpud_core_context_create(const char *name, gpud_retire_fn fn, void *opaque)
{
    struct context *c = calloc(1, sizeof(*c));
    if (!c || !fn || atomic_load(&stopping)) { free(c); return 0; }
    pthread_mutex_lock(&lock);
    if (!started || context_count >= max_contexts || !next_id) goto fail;
    c->id = next_id++;
    c->retire = fn; c->opaque = opaque;
    if (!vkr_renderer_create_context(c->id, 4, strlen(name), name)) goto fail;
    pthread_mutex_lock(&cb_lock);
    c->next = contexts; contexts = c;
    pthread_mutex_unlock(&cb_lock);
    context_count++;
    pthread_mutex_unlock(&lock);
    return c->id;
fail:
    pthread_mutex_unlock(&lock); free(c); return 0;
}
void gpud_core_context_destroy(uint32_t id)
{
    pthread_mutex_lock(&lock);
    vkr_renderer_destroy_context(id);
    pthread_mutex_lock(&cb_lock);
    struct context **p = &contexts;
    while (*p && (*p)->id != id) p = &(*p)->next;
    if (*p) {
        struct context *c = *p;
        while (c->resources) {
            struct resource *r = c->resources; c->resources = r->next; free(r);
        }
        *p = c->next; free(c); context_count--;
    }
    pthread_mutex_unlock(&cb_lock);
    pthread_mutex_unlock(&lock);
}
bool gpud_core_submit(uint32_t ctx, void *data, uint32_t size)
{
    if (atomic_load(&stopping)) return false;
    pthread_mutex_lock(&lock);
    bool ok = vkr_renderer_submit_cmd(ctx, data, size);
    pthread_mutex_unlock(&lock); return ok;
}
bool gpud_core_fence(uint32_t ctx, uint32_t flags, uint32_t ring, uint64_t fence)
{
    if (ring >= 64 || (flags & ~1u)) return false;
    pthread_mutex_lock(&lock);
    bool ok = vkr_renderer_submit_fence(ctx, flags, ring, fence);
    pthread_mutex_unlock(&lock); return ok;
}
int gpud_core_blob(uint32_t ctx, uint32_t res, uint64_t blob, uint64_t size,
                   uint32_t flags, uint32_t *map_info)
{
    if (!res || !size || size > UINT64_MAX - 16383 || (flags & ~1u)) return -1;
    size = (size + 16383) & ~UINT64_C(16383);
    enum virgl_resource_fd_type type;
    struct virgl_resource_vulkan_info info;
    int fd = -1;
    pthread_mutex_lock(&lock);
    struct context *c = contexts;
    while (c && c->id != ctx) c = c->next;
    struct resource *entry = c ? c->resources : NULL;
    while (entry && entry->id != res) entry = entry->next;
    bool ok = false;
    if (!c || entry || atomic_load(&stopping)) goto out;
    entry = calloc(1, sizeof(*entry));
    if (!entry) goto out;
    if (!blob && !gpud_memory_reserve(ctx, true, size)) { free(entry); goto out; }
    ok = vkr_renderer_create_resource(ctx, res, blob, size, flags, &type, &fd, map_info, &info);
    if (ok && type != VIRGL_RESOURCE_FD_SHM) {
        close(fd); fd = -1; vkr_renderer_destroy_resource(ctx, res); ok = false;
    }
    if (!ok) {
        if (!blob) gpud_memory_release(ctx, true, size);
        free(entry);
    } else {
        entry->id = res; entry->blob = blob; entry->shm_bytes = blob ? 0 : size;
        pthread_mutex_lock(&cb_lock);
        entry->next = c->resources; c->resources = entry;
        pthread_mutex_unlock(&cb_lock);
    }
out:
    pthread_mutex_unlock(&lock);
    return ok ? fd : -1;
}
void gpud_core_unref(uint32_t ctx, uint32_t res)
{
    pthread_mutex_lock(&lock);
    vkr_renderer_destroy_resource(ctx, res);
    struct context *c = contexts;
    while (c && c->id != ctx) c = c->next;
    pthread_mutex_lock(&cb_lock);
    struct resource **p = c ? &c->resources : NULL;
    while (p && *p && (*p)->id != res) p = &(*p)->next;
    struct resource *entry = p ? *p : NULL;
    if (entry) *p = entry->next;
    pthread_mutex_unlock(&cb_lock);
    if (entry) {
        if (entry->shm_bytes) gpud_memory_release(ctx, true, entry->shm_bytes);
        free(entry);
    }
    pthread_mutex_unlock(&lock);
}
int ps5wl_gpud_start(const struct ps5wl_gpud_config *config)
{
    const struct ps5wl_gpud_config zero = {0};
    if (!config) config = &zero;
    if (started) return -EALREADY;
    max_contexts = config->max_ctx ? config->max_ctx : GPUD_DEFAULT_CONTEXTS;
    host_limit = config->host_bytes_per_ctx ? config->host_bytes_per_ctx : (uint64_t)GPUD_DEFAULT_HOST_MIB * 1024 * 1024;
    device_limit = config->device_bytes_per_ctx ? config->device_bytes_per_ctx : UINT64_C(1024) * 1024 * 1024;
    atomic_store(&fd_exports, 0);
    atomic_store(&stopping, false);
    if (!vkr_renderer_init(VKR_RENDERER_THREAD_SYNC | VKR_RENDERER_ASYNC_FENCE_CB, &callbacks)) return -ENODEV;
    started = true;
    const char *path = config->vtest_path;
    if (!path) path = getenv("VTEST_SOCKET_NAME");
    if (!path) path = PS5WL_GPUD_VTEST_PATH;
    int result = gpud_vtest_start(path);
    if (!result) result = gpud_render_start(config->render_path ? config->render_path : PS5WL_GPUD_RENDER_PATH);
    if (result) {
        int drain_result = ps5wl_gpud_stop(1000);
        if (drain_result) return drain_result;
    }
    return result;
}
int ps5wl_gpud_stop(unsigned drain_ms)
{
    if (!started) return 0;
    atomic_store(&stopping, true);
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    gpud_vtest_shutdown();
    gpud_render_shutdown();
    int result = gpud_vtest_drain(drain_ms);
    clock_gettime(CLOCK_MONOTONIC, &end);
    int64_t elapsed = (end.tv_sec - start.tv_sec) * INT64_C(1000) +
                      (end.tv_nsec - start.tv_nsec) / 1000000;
    unsigned left = elapsed < drain_ms ? drain_ms - elapsed : 0;
    int render_result = gpud_render_drain(left);
    if (result || render_result) {
        logger(VIRGL_LOG_LEVEL_ERROR, "gpud wedged: drain timed out; preserving mappings", NULL);
        return -EBUSY;
    }
    vkr_renderer_fini(); started = false;
    char message[96];
    snprintf(message, sizeof(message), "gpud stopped: Vulkan fd-export calls=%" PRIu64, gpud_fd_export_count());
    logger(VIRGL_LOG_LEVEL_INFO, message, NULL);
    return 0;
}
