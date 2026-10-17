#ifndef PS5WL_RENDER_WIRE_H
#define PS5WL_RENDER_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
/* Include prerequisites omitted by upstream's internal protocol header. */
#include "render_protocol.h"
#define GPUD_RENDER_MAX_FRAME (1024u * 1024u)
#define GPUD_RENDER_MAX_FDS 2
/* Upstream has no capset request: its proxy obtains it locally. */
#define GPUD_RENDER_GET_CAPSET RENDER_CLIENT_OP_COUNT
struct gpud_render_capset_request { uint32_t op, capset_id, version; };
struct gpud_render_capset_reply { uint32_t capset_id, version, size; };
struct gpud_wire { int fd; bool stream; };
/* Frame: uint32 little-endian payload length, then payload. SCM_RIGHTS is
 * attached to the first header byte, never to later payload bytes. */
bool gpud_wire_send(struct gpud_wire *, const void *, size_t, const int *, unsigned);
/* Returns payload length, or -1 for EOF/error. Received fds owned by caller. */
ssize_t gpud_wire_recv(struct gpud_wire *, void *, size_t, int *, unsigned *);
int gpud_wire_cloexec(int fd);
#endif
