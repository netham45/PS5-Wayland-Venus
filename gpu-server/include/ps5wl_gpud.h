#ifndef PS5WL_GPUD_H
#define PS5WL_GPUD_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS5WL_GPUD_VTEST_PATH "/system_tmp/ps5wl-gpu-0"
#define PS5WL_GPUD_RENDER_PATH "/system_tmp/ps5wl-gpu-lkl"
struct ps5wl_gpud_config {
    const char *vtest_path, *render_path;
    uint64_t host_bytes_per_ctx, device_bytes_per_ctx;
    unsigned max_ctx;
};
/* Call before backend initialization. NULL config or NULL/zero members select
 * defaults: PS5 host quota128MiB/max_ctx2; host quota512MiB/max_ctx32;
 * device quota1GiB/context. VTEST_SOCKET_NAME overrides the compiled vtest
 * path, not an explicit path. Returns0 or negative errno; no process creation. */
int ps5wl_gpud_start(const struct ps5wl_gpud_config *config);
/* Stops accepting and disconnects clients. Returns -EBUSY if drain_ms expires;
 * in that case keep the GPU library and backing memory alive until app exit. */
int ps5wl_gpud_stop(unsigned drain_ms);
#ifdef __cplusplus
}
#endif
#endif
