#ifndef PS5WL_GPUD_RENDER_H
#define PS5WL_GPUD_RENDER_H
#include <stdbool.h>
/* PS5WL_RENDER_TRANSPORT=stream (default) or seqpacket, read at start. */
int gpud_render_start(const char *path);
void gpud_render_shutdown(void);
/* Call after shutdown; -EBUSY retains all still-live threads/mappings. */
int gpud_render_drain(unsigned drain_ms);
#endif
