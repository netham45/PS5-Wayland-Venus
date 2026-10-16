#define _POSIX_C_SOURCE 200809L
#include "window.h"
#include "../../common/ps5wl-client/ps5wl-client.h"
#include "../../common/ps5log/ps5log.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int sig) { (void)sig; stopped = 1; }
static void draw(struct window *w);
static void fail(struct window *w, const char *what) {
    ps5log("fatal %s: %s", what, strerror(errno));
    w->failed = true;
    w->running = false;
}
static void buffer_free(struct buffer *b) {
    wl_buffer_destroy(b->proxy);
    munmap(b->pixels, b->size);
    free(b);
}
static void release(void *data, struct wl_buffer *proxy) {
    (void)proxy;
    struct buffer *b = data;
    b->busy = false;
    /* A release can be the only wakeup when every buffer was busy at frame time. */
    if (!b->window->frame) draw(b->window);
}
static const struct wl_buffer_listener buffer_listener = {.release = release};
static struct buffer *next_buffer(struct window *w) {
    struct buffer **link = &w->buffers;
    unsigned count = 0;
    struct buffer *available = NULL;
    while (*link) {
        struct buffer *b = *link;
        if (!b->busy && (b->width != w->width || b->height != w->height)) {
            *link = b->next;
            buffer_free(b);
            continue;
        }
        if (b->width == w->width && b->height == w->height) {
            count++;
            if (!b->busy) available = b;
        }
        link = &b->next;
    }
    if (available || count >= 3) return available;
    struct buffer *b = calloc(1, sizeof(*b));
    if (!b) { fail(w, "allocate buffer"); return NULL; }
    b->width = w->width;
    b->height = w->height;
    b->size = (size_t)w->width * w->height * 4;
    b->window = w;
    int fd = ps5wl_client_shm(b->size);
    if (fd < 0) { free(b); fail(w, "anonymous shm"); return NULL; }
    b->pixels = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->pixels == MAP_FAILED) {
        close(fd); free(b); fail(w, "mmap buffer"); return NULL;
    }
    struct wl_shm_pool *pool = wl_shm_create_pool(w->shm, fd, (int)b->size);
    b->proxy = wl_shm_pool_create_buffer(pool, 0, w->width, w->height,
                                        w->width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(b->proxy, &buffer_listener, b);
    b->next = w->buffers;
    w->buffers = b;
    ps5log("shm-buffer width=%d height=%d bytes=%zu", w->width, w->height, b->size);
    return b;
}
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
    struct window *w = data;
    wl_callback_destroy(cb);
    w->frame = NULL;
    w->time = time;
    if (w->frames == 1 || w->frames % 120 == 0)
        ps5log("frame-done committed=%u time=%u", w->frames, time);
    if (w->frame_limit && w->frames >= w->frame_limit) w->running = false;
    else draw(w);
}
static const struct wl_callback_listener frame_listener = {.done = frame_done};
static void draw(struct window *w) {
    if (!w->running || !w->configured || w->frame) return;
    struct buffer *b = next_buffer(w);
    if (!b) return;
    w->hooks->paint(w, b->pixels, w->time);
    wl_surface_attach(w->surface, b->proxy, 0, 0);
    wl_surface_damage_buffer(w->surface, 0, 0, w->width, w->height);
    w->frame = wl_surface_frame(w->surface);
    wl_callback_add_listener(w->frame, &frame_listener, w);
    b->busy = true;
    wl_surface_commit(w->surface);
    w->frames++;
    if (w->frames == 1 || w->frames % 120 == 0)
        ps5log("buffer-commit frame=%u size=%dx%d", w->frames, w->width, w->height);
}
static void configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct window *w = data;
    xdg_surface_ack_configure(surface, serial);
    w->width = w->pending_width;
    w->height = w->pending_height;
    w->fullscreen = w->pending_fullscreen;
    w->configured = true;
    ps5log("configure serial=%u size=%dx%d fullscreen=%d", serial, w->width, w->height, w->fullscreen);
    draw(w);
}
static const struct xdg_surface_listener surface_listener = {.configure = configure};
static void top_configure(void *data, struct xdg_toplevel *top, int32_t width,
                          int32_t height, struct wl_array *states) {
    (void)top;
    struct window *w = data;
    /* Bound arithmetic and allocation without ignoring a valid compositor size. */
    if (width > 16384 || height > 16384 || width < 0 || height < 0) {
        errno = EOVERFLOW; fail(w, "configure size"); return;
    }
    w->pending_width = width ? width : w->width;
    w->pending_height = height ? height : w->height;
    w->pending_fullscreen = false;
    uint32_t *state;
    wl_array_for_each(state, states)
        if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) w->pending_fullscreen = true;
}
static void top_close(void *data, struct xdg_toplevel *top) {
    (void)top; ((struct window *)data)->running = false;
}
static const struct xdg_toplevel_listener top_listener = {.configure = top_configure, .close = top_close};
static void ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
    (void)data; xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener shell_listener = {.ping = ping};
void window_fullscreen(struct window *w, bool enabled) {
    if (enabled) xdg_toplevel_set_fullscreen(w->toplevel, NULL);
    else xdg_toplevel_unset_fullscreen(w->toplevel);
}
static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    (void)p; (void)serial; (void)surface;
    struct window *w = data;
    w->pointer_x = wl_fixed_to_double(x); w->pointer_y = wl_fixed_to_double(y);
    ps5log("pointer-enter x=%.1f y=%.1f", w->pointer_x, w->pointer_y);
    if (w->hooks->motion) w->hooks->motion(w, w->pointer_x, w->pointer_y);
}
static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial, struct wl_surface *s) {
    (void)data; (void)p; (void)serial; (void)s;
}
static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
    (void)p; (void)time;
    struct window *w = data;
    w->pointer_x = wl_fixed_to_double(x); w->pointer_y = wl_fixed_to_double(y);
    if (w->hooks->motion) w->hooks->motion(w, w->pointer_x, w->pointer_y);
}
static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
    (void)p; (void)serial; (void)time;
    struct window *w = data;
    ps5log("pointer-button button=%u state=%u", button, state);
    if (w->hooks->button) w->hooks->button(w, button, state == WL_POINTER_BUTTON_STATE_PRESSED);
}
static void pointer_axis(void *data, struct wl_pointer *p, uint32_t t, uint32_t axis, wl_fixed_t value) {
    (void)data; (void)p; (void)t; (void)axis; (void)value;
}
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
    .button = pointer_button, .axis = pointer_axis,
};
static void keymap(void *data, struct wl_keyboard *kbd, uint32_t format, int32_t fd, uint32_t size) {
    (void)kbd;
    struct window *w = data;
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || !size) { close(fd); return; }
    char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { fail(w, "mmap keymap"); return; }
    struct xkb_keymap *km = NULL;
    if (map[size - 1] == '\0')
        km = xkb_keymap_new_from_string(w->xkb, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    if (!km) { errno = EINVAL; fail(w, "parse keymap"); return; }
    struct xkb_state *state = xkb_state_new(km);
    if (!state) { xkb_keymap_unref(km); fail(w, "create xkb state"); return; }
    xkb_state_unref(w->state); xkb_keymap_unref(w->keymap);
    w->keymap = km; w->state = state;
    ps5log("keyboard-keymap bytes=%u", size);
}
static void keyboard_enter(void *data, struct wl_keyboard *kbd, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
    (void)kbd; (void)serial; (void)surface; (void)keys;
    struct window *w = data;
    ps5log("keyboard-enter");
    if (w->state) xkb_state_update_mask(w->state, 0, 0, 0, 0, 0, 0);
}
static void keyboard_leave(void *data, struct wl_keyboard *kbd, uint32_t serial, struct wl_surface *surface) {
    (void)kbd; (void)serial; (void)surface;
    struct window *w = data;
    if (w->state) xkb_state_update_mask(w->state, 0, 0, 0, 0, 0, 0);
}
static void keyboard_key(void *data, struct wl_keyboard *kbd, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
    (void)kbd; (void)serial; (void)time;
    struct window *w = data;
    char text[128] = {0};
    xkb_keysym_t sym = XKB_KEY_NoSymbol;
    if (w->state) {
        sym = xkb_state_key_get_one_sym(w->state, key + 8);
        xkb_state_key_get_utf8(w->state, key + 8, text, sizeof(text));
    }
    ps5log("keyboard-key key=%u state=%u sym=0x%x text=%s", key, state, sym, text);
    if (w->hooks->key) w->hooks->key(w, key, state == WL_KEYBOARD_KEY_STATE_PRESSED, sym, text);
}
static void keyboard_mods(void *data, struct wl_keyboard *kbd, uint32_t serial,
                          uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    (void)kbd; (void)serial;
    struct window *w = data;
    if (w->state) xkb_state_update_mask(w->state, depressed, latched, locked, 0, 0, group);
}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keymap, .enter = keyboard_enter, .leave = keyboard_leave,
    .key = keyboard_key, .modifiers = keyboard_mods,
};
static void capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct window *w = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !w->pointer) {
        w->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(w->pointer, &pointer_listener, w);
    } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && w->pointer) {
        wl_pointer_destroy(w->pointer); w->pointer = NULL;
    }
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !w->keyboard) {
        w->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(w->keyboard, &keyboard_listener, w);
    } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && w->keyboard) {
        wl_keyboard_destroy(w->keyboard); w->keyboard = NULL;
    }
    ps5log("seat-capabilities value=%u", caps);
}
static const struct wl_seat_listener seat_listener = {.capabilities = capabilities};
static void global(void *data, struct wl_registry *registry, uint32_t id, const char *iface, uint32_t version) {
    struct window *w = data;
    if (!strcmp(iface, "wl_compositor") && version >= 4)
        w->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    else if (!strcmp(iface, "wl_shm")) w->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    else if (!strcmp(iface, "xdg_wm_base")) {
        w->shell = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(w->shell, &shell_listener, w);
    } else if (!strcmp(iface, "wl_seat") && !w->seat) {
        /* v1 keeps pointer and keyboard listener requirements minimal. */
        w->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
        wl_seat_add_listener(w->seat, &seat_listener, w);
    }
}
static void global_remove(void *data, struct wl_registry *reg, uint32_t id) {
    (void)data; (void)reg; (void)id;
}
static const struct wl_registry_listener registry_listener = {.global = global, .global_remove = global_remove};
int window_run(struct window *w, const char *name, uint16_t port,
               const struct window_hooks *hooks, void *userdata, unsigned frame_limit) {
    *w = (struct window){.width = 800, .height = 600, .pending_width = 800,
        .pending_height = 600, .running = true, .hooks = hooks, .userdata = userdata, .frame_limit = frame_limit};
    signal(SIGINT, stop); signal(SIGTERM, stop);
    if (ps5wl_client_init(name, port) < 0) { perror("client init"); return 1; }
    w->display = ps5wl_client_connect(30000);
    if (!w->display) return 1;
    w->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!w->xkb) { fail(w, "create xkb context"); goto cleanup; }
    w->registry = wl_display_get_registry(w->display);
    wl_registry_add_listener(w->registry, &registry_listener, w);
    if (wl_display_roundtrip(w->display) < 0 || !w->compositor || !w->shm || !w->shell) {
        errno = ENODEV; fail(w, "required Wayland globals"); goto cleanup;
    }
    w->surface = wl_compositor_create_surface(w->compositor);
    w->xdg_surface = xdg_wm_base_get_xdg_surface(w->shell, w->surface);
    xdg_surface_add_listener(w->xdg_surface, &surface_listener, w);
    w->toplevel = xdg_surface_get_toplevel(w->xdg_surface);
    xdg_toplevel_add_listener(w->toplevel, &top_listener, w);
    xdg_toplevel_set_title(w->toplevel, name);
    xdg_toplevel_set_app_id(w->toplevel, name);
    wl_surface_commit(w->surface);
    while (w->running && !stopped) {
        if (wl_display_dispatch(w->display) < 0) {
            if (errno != EINTR) fail(w, "Wayland dispatch");
            if (stopped || w->failed) break;
        }
    }
cleanup:
    ps5log("client-exit frames=%u failed=%d", w->frames, w->failed);
    if (w->frame) wl_callback_destroy(w->frame);
    while (w->buffers) {
        struct buffer *b = w->buffers; w->buffers = b->next; buffer_free(b);
    }
    if (w->toplevel) xdg_toplevel_destroy(w->toplevel);
    if (w->xdg_surface) xdg_surface_destroy(w->xdg_surface);
    if (w->surface) wl_surface_destroy(w->surface);
    if (w->pointer) wl_pointer_destroy(w->pointer);
    if (w->keyboard) wl_keyboard_destroy(w->keyboard);
    if (w->seat) wl_seat_destroy(w->seat);
    if (w->shell) xdg_wm_base_destroy(w->shell);
    if (w->shm) wl_shm_destroy(w->shm);
    if (w->compositor) wl_compositor_destroy(w->compositor);
    if (w->registry) wl_registry_destroy(w->registry);
    xkb_state_unref(w->state); xkb_keymap_unref(w->keymap); xkb_context_unref(w->xkb);
    wl_display_flush(w->display);
    wl_display_disconnect(w->display);
#if defined(PS5WL_PS5)
    ps5log_flush_wait(1500);
#endif
    return w->failed ? 1 : 0;
}
