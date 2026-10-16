#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

struct globals {
    bool shm;
    bool xdg_shell;
    bool compositor;
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
        const char *interface, uint32_t version) {
    (void)registry;
    struct globals *globals = data;
    printf("%s version=%u name=%u\n", interface, version, name);
    globals->shm |= strcmp(interface, "wl_shm") == 0;
    globals->xdg_shell |= strcmp(interface, "xdg_wm_base") == 0;
    globals->compositor |= strcmp(interface, "wl_compositor") == 0;
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data;
    (void)registry;
    (void)name;
}

int main(void) {
    struct wl_display *display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Cannot connect to WAYLAND_DISPLAY\n");
        return 1;
    }
    struct globals globals = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    const struct wl_registry_listener listener = {global, global_remove};
    wl_registry_add_listener(registry, &listener, &globals);
    int result = wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    if (result < 0 || !globals.shm || !globals.xdg_shell || !globals.compositor) {
        fprintf(stderr, "Registry roundtrip failed or required globals missing\n");
        return 1;
    }
    return 0;
}
