#ifndef PS5WL_GPUD_RENDER_H
#define PS5WL_GPUD_RENDER_H
#include <stdbool.h>
/* PS5WL_RENDER_TRANSPORT=stream (default) or seqpacket, read at start. */
int gpud_render_start(const char *path);
/* Stops listeners and joins all connection/context threads before returning. */
void gpud_render_stop(void);
#endif
