#include <errno.h>
#include <stdio.h>
#include <wayland-server-core.h>
#include <wlr/util/log.h>
#include "ps5log.h"

int main(void) {
    if (ps5log_init("deps-linktest", 9531) < 0) {
        fprintf(stderr, "ps5log_init failed: %d\n", errno);
        return 1;
    }
    wlr_log_init(WLR_DEBUG, NULL);
    ps5log("wlr_log_init: OK");
    struct wl_display *display = wl_display_create();
    if (display == NULL) {
        ps5log("wl_display_create: FAILED errno=%d", errno);
        ps5log_flush_wait(10000);
        return 1;
    }
    ps5log("wl_display_create: OK");
    int ret = wl_event_loop_dispatch(wl_display_get_event_loop(display), 0);
    ps5log("wl_event_loop_dispatch(timeout=0): %d errno=%d", ret, ret < 0 ? errno : 0);
    wl_display_destroy(display);
    ps5log("RESULT: %s", ret == 0 ? "PASS" : "FAIL");
    ps5log_flush_wait(10000);
    return ret == 0 ? 0 : 1;
}
