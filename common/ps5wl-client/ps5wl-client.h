#ifndef PS5WL_CLIENT_H
#define PS5WL_CLIENT_H
#include <stddef.h>
#include <stdint.h>
struct wl_display;
/* Initializes payload logging and the PS5 socket environment; leaves host env alone. */
int ps5wl_client_init(const char *name, uint16_t log_port);
/* Retry the real Wayland connection until timeout_ms elapses. */
struct wl_display *ps5wl_client_connect(unsigned timeout_ms);
/* Anonymous, close-on-exec, MAP_SHARED-capable storage; caller owns the fd. */
int ps5wl_client_shm(size_t size);
#endif
