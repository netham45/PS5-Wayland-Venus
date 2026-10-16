#include <errno.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include "ps5log.h"

int main(void) {
    if (ps5log_init("deps-linktest", 9531) < 0) {
        fprintf(stderr, "ps5log_init failed: %d\n", errno);
        return 1;
    }
    ps5log("starting dependency calls");
    ps5log_flush_wait(2000);
    wlr_log_init(WLR_DEBUG, NULL);
    ps5log("wlr_log_init: OK");
    ps5log_flush_wait(100);
    ps5log("checking getenv(WAYLAND_DEBUG)");
    ps5log_flush_wait(100);
    const char *debug = getenv("WAYLAND_DEBUG");
    ps5log("getenv: OK (%s)", debug == NULL ? "unset" : "present");
    ps5log_flush_wait(100);
    ps5log("creating standalone Wayland event loop");
    ps5log_flush_wait(100);
    struct wl_event_loop *loop = wl_event_loop_create();
    if (loop == NULL) {
        ps5log("wl_event_loop_create: FAILED errno=%d", errno);
        ps5log_flush_wait(10000);
        return 1;
    }
    ps5log("wl_event_loop_create: OK");
    ps5log_flush_wait(100);
    wl_event_loop_destroy(loop);
    ps5log("creating Wayland display");
    ps5log_flush_wait(100);
    struct wl_display *display = wl_display_create();
    if (display == NULL) {
        ps5log("wl_display_create: FAILED errno=%d", errno);
        ps5log_flush_wait(10000);
        return 1;
    }
    ps5log("wl_display_create: OK");
    ps5log_flush_wait(100);
    int ret = wl_event_loop_dispatch(wl_display_get_event_loop(display), 0);
    ps5log("wl_event_loop_dispatch(timeout=0): %d errno=%d", ret, ret < 0 ? errno : 0);
    wl_display_destroy(display);
    ps5log("RESULT: %s", ret == 0 ? "PASS" : "FAIL");
    ps5log_flush_wait(10000);
    return ret == 0 ? 0 : 1;
}
