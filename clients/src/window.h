#ifndef PS5WL_WINDOW_H
#define PS5WL_WINDOW_H
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "xdg-shell-client-protocol.h"

struct window;
struct window_hooks {
    void (*paint)(struct window *, uint32_t *pixels, uint32_t time);
    void (*motion)(struct window *, double x, double y);
    void (*button)(struct window *, uint32_t button, bool pressed);
    void (*key)(struct window *, uint32_t key, bool pressed, xkb_keysym_t sym, const char *utf8);
};
struct buffer {
    struct wl_buffer *proxy;
    uint32_t *pixels;
    size_t size;
    int width, height;
    bool busy;
    struct window *window;
    struct buffer *next;
};
struct window {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *shell;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_keyboard *keyboard;
    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *state;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_callback *frame;
    struct buffer *buffers;
    const struct window_hooks *hooks;
    void *userdata;
    int width, height, pending_width, pending_height;
    unsigned frames, frame_limit;
    uint32_t time;
    double pointer_x, pointer_y;
    bool configured, running, fullscreen, pending_fullscreen, failed;
};
int window_run(struct window *w, const char *name, uint16_t port,
               const struct window_hooks *hooks, void *userdata, unsigned frame_limit);
void window_fullscreen(struct window *w, bool enabled);
#endif
