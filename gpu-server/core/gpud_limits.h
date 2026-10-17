#ifndef GPUD_LIMITS_H
#define GPUD_LIMITS_H
#include <stdbool.h>
#include <stdint.h>
bool gpud_memory_reserve(uint32_t ctx, bool host, uint64_t bytes);
void gpud_memory_release(uint32_t ctx, bool host, uint64_t bytes);
void gpud_memory_free(uint32_t ctx, uint64_t blob, bool host, uint64_t bytes);
void gpud_record_fd_export(void);
uint64_t gpud_fd_export_count(void);
bool gpud_is_wedged(void);
void gpud_mark_wedged(uint32_t ctx, const char *reason);
#endif
