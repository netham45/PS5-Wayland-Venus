#ifndef PS5WL_GPUD_CORE_H
#define PS5WL_GPUD_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Callbacks run on vkr queue threads, including during context destruction. */
typedef void (*gpud_retire_fn)(void *, uint32_t ring, uint64_t fence);
size_t gpud_core_capset(void *data);
uint32_t gpud_core_context_create(const char *name, gpud_retire_fn, void *opaque);
void gpud_core_context_destroy(uint32_t ctx);
bool gpud_core_submit(uint32_t ctx, void *data, uint32_t bytes);
bool gpud_core_fence(uint32_t ctx, uint32_t flags, uint32_t ring, uint64_t fence);
/* Returns an owned ordinary SHM descriptor, or -1. flags use virgl blob bits. */
int gpud_core_blob(uint32_t ctx, uint32_t res, uint64_t blob, uint64_t size,
                   uint32_t flags, uint32_t *map_info);
void gpud_core_unref(uint32_t ctx, uint32_t res);
#endif
