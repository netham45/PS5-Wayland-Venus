#define _POSIX_C_SOURCE 200809L
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"
#include "../../common/ps5wl-client/ps5wl-client.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

struct test {
    struct wl_display *display;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_output *output;
    struct zwlr_screencopy_manager_v1 *capture;
    struct zwlr_virtual_pointer_manager_v1 *pointers;
    struct zwp_virtual_keyboard_manager_v1 *keyboards;
    struct wl_buffer *buffer;
    void *pixels;
    size_t size;
    uint32_t width, height, stride, flags;
    bool done, failed, verify_demo;
};
static void global(void *data, struct wl_registry *registry, uint32_t id, const char *iface, uint32_t version) {
    (void)version;
    struct test *t = data;
#define BIND(name, field, type) if (!strcmp(iface, name) && !t->field) t->field = wl_registry_bind(registry, id, &type##_interface, 1)
    BIND("wl_shm", shm, wl_shm);
    else BIND("wl_seat", seat, wl_seat);
    else BIND("wl_output", output, wl_output);
    else BIND("zwlr_screencopy_manager_v1", capture, zwlr_screencopy_manager_v1);
    else BIND("zwlr_virtual_pointer_manager_v1", pointers, zwlr_virtual_pointer_manager_v1);
    else BIND("zwp_virtual_keyboard_manager_v1", keyboards, zwp_virtual_keyboard_manager_v1);
#undef BIND
}
static void remove_global(void *data, struct wl_registry *registry, uint32_t id) {
    (void)data; (void)registry; (void)id;
}
static const struct wl_registry_listener registry_listener = {.global = global, .global_remove = remove_global};
static void capture_buffer(void *data, struct zwlr_screencopy_frame_v1 *frame,
                           uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    struct test *t = data;
    assert(format == WL_SHM_FORMAT_XRGB8888 || format == WL_SHM_FORMAT_ARGB8888);
    t->width = width; t->height = height; t->stride = stride;
    t->size = (size_t)stride * height;
    int fd = ps5wl_client_shm(t->size);
    assert(fd >= 0);
    t->pixels = mmap(NULL, t->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    assert(t->pixels != MAP_FAILED);
    struct wl_shm_pool *pool = wl_shm_create_pool(t->shm, fd, (int)t->size);
    t->buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, format);
    wl_shm_pool_destroy(pool); close(fd);
    zwlr_screencopy_frame_v1_copy(frame, t->buffer);
}
static void capture_flags(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t flags) {
    (void)frame; ((struct test *)data)->flags = flags;
}
static void ready(void *data, struct zwlr_screencopy_frame_v1 *frame, uint32_t hi, uint32_t lo, uint32_t ns) {
    (void)frame; (void)hi; (void)lo; (void)ns; ((struct test *)data)->done = true;
}
static void failed(void *data, struct zwlr_screencopy_frame_v1 *frame) {
    (void)frame; ((struct test *)data)->failed = true;
}
static const struct zwlr_screencopy_frame_v1_listener capture_listener = {
    .buffer = capture_buffer, .flags = capture_flags, .ready = ready, .failed = failed,
};
static uint64_t screenshot(struct test *t, const char *path) {
    t->done = t->failed = false;
    struct zwlr_screencopy_frame_v1 *frame = zwlr_screencopy_manager_v1_capture_output(t->capture, 0, t->output);
    zwlr_screencopy_frame_v1_add_listener(frame, &capture_listener, t);
    while (!t->done && !t->failed) assert(wl_display_dispatch(t->display) >= 0);
    assert(t->done && !t->failed);
    uint64_t hash = UINT64_C(14695981039346656037);
    unsigned varied = 0, trail_pixels = 0, glyph_pixels = 0;
    uint32_t first = *(uint32_t *)t->pixels & 0xffffff;
    FILE *file = fopen(path, "wb"); assert(file);
    fprintf(file, "P6\n%u %u\n255\n", t->width, t->height);
    for (uint32_t y = 0; y < t->height; y++) {
        uint32_t row = (t->flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) ? t->height - 1 - y : y;
        const uint32_t *pixels = (const uint32_t *)((char *)t->pixels + row * t->stride);
        for (uint32_t x = 0; x < t->width; x++) {
            uint32_t pixel = pixels[x] & 0xffffff;
            if (pixel != first) varied++;
            if (pixel == 0xffb454) trail_pixels++;
            if (x >= 16 && x < 32 && y >= 120 && y < 136 && pixel == 0xffffff) glyph_pixels++;
            hash = (hash ^ pixel) * UINT64_C(1099511628211);
            unsigned char rgb[] = {pixel >> 16, pixel >> 8, pixel};
            assert(fwrite(rgb, 1, 3, file) == 3);
        }
    }
    assert(fclose(file) == 0);
    assert(varied > 1000);
    if (t->verify_demo) {
        /* Fail if input only logs successfully but never reaches the rendered app. */
        assert(trail_pixels > 50);
        assert(glyph_pixels > 40);
        printf("demo-render trail_pixels=%u typed_glyph_pixels=%u\n", trail_pixels, glyph_pixels);
    }
    printf("screencopy path=%s size=%ux%u varied=%u hash=%016" PRIx64 "\n", path, t->width, t->height, varied, hash);
    zwlr_screencopy_frame_v1_destroy(frame);
    wl_buffer_destroy(t->buffer); munmap(t->pixels, t->size);
    return hash;
}
static void settle(struct test *t) {
    assert(wl_display_roundtrip(t->display) >= 0);
    struct timespec delay = {.tv_nsec = 150000000}; nanosleep(&delay, NULL);
}
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "Usage: %s capture|input|demo OUTPUT_PREFIX\n", argv[0]); return 2; }
    alarm(15);
    struct test t = {.display = wl_display_connect(NULL)};
    assert(t.display);
    struct wl_registry *registry = wl_display_get_registry(t.display);
    wl_registry_add_listener(registry, &registry_listener, &t);
    assert(wl_display_roundtrip(t.display) >= 0);
    assert(t.shm && t.output && t.capture);
    char path[4096];
    snprintf(path, sizeof(path), "%s-before.ppm", argv[2]);
    uint64_t before = screenshot(&t, path);
    if (!strcmp(argv[1], "input") || !strcmp(argv[1], "demo")) {
        assert(t.seat && t.pointers && t.keyboards);
        struct zwlr_virtual_pointer_v1 *pointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(t.pointers, t.seat);
        struct zwp_virtual_keyboard_v1 *keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(t.keyboards, t.seat);
        struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS); assert(context);
        struct xkb_rule_names rules = {.layout = "us"};
        struct xkb_keymap *km = xkb_keymap_new_from_names(context, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS); assert(km);
        char *map = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1); assert(map);
        size_t size = strlen(map) + 1;
        int fd = ps5wl_client_shm(size); assert(fd >= 0);
        assert(write(fd, map, size) == (ssize_t)size);
        zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, size);
        close(fd); free(map); xkb_keymap_unref(km); xkb_context_unref(context);
        zwlr_virtual_pointer_v1_motion_absolute(pointer, 10, 400, 300, 1280, 720);
        zwlr_virtual_pointer_v1_frame(pointer); settle(&t);
        zwlr_virtual_pointer_v1_button(pointer, 20, 0x110, WL_POINTER_BUTTON_STATE_PRESSED);
        zwlr_virtual_pointer_v1_frame(pointer); settle(&t);
        zwlr_virtual_pointer_v1_motion(pointer, 30, wl_fixed_from_int(100), wl_fixed_from_int(50));
        zwlr_virtual_pointer_v1_frame(pointer); settle(&t);
        zwlr_virtual_pointer_v1_button(pointer, 40, 0x110, WL_POINTER_BUTTON_STATE_RELEASED);
        zwlr_virtual_pointer_v1_frame(pointer);
        zwp_virtual_keyboard_v1_key(keyboard, 50, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(keyboard, 60, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
        settle(&t);
        t.verify_demo = !strcmp(argv[1], "demo");
        snprintf(path, sizeof(path), "%s-input.ppm", argv[2]);
        screenshot(&t, path);
        t.verify_demo = false;
        /* F11 twice exercises fullscreen resize and restoration. */
        for (unsigned i = 0; i < 2; i++) {
            zwp_virtual_keyboard_v1_key(keyboard, 70 + i * 20, 87, WL_KEYBOARD_KEY_STATE_PRESSED);
            zwp_virtual_keyboard_v1_key(keyboard, 80 + i * 20, 87, WL_KEYBOARD_KEY_STATE_RELEASED);
            settle(&t);
        }
        zwp_virtual_keyboard_v1_destroy(keyboard);
        zwlr_virtual_pointer_v1_destroy(pointer);
    } else assert(!strcmp(argv[1], "capture"));
    settle(&t);
    snprintf(path, sizeof(path), "%s-after.ppm", argv[2]);
    uint64_t after = screenshot(&t, path);
    assert(before != after);
    printf("exercise passed mode=%s\n", argv[1]);
    if (t.pointers) zwlr_virtual_pointer_manager_v1_destroy(t.pointers);
    if (t.keyboards) zwp_virtual_keyboard_manager_v1_destroy(t.keyboards);
    if (t.seat) wl_seat_destroy(t.seat);
    zwlr_screencopy_manager_v1_destroy(t.capture);
    wl_output_destroy(t.output); wl_shm_destroy(t.shm); wl_registry_destroy(registry);
    wl_display_flush(t.display); wl_display_disconnect(t.display);
    return 0;
}
