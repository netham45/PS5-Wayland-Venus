/* Exercise the real socket capture path with a deterministic RGB image. */
#include "ps5.h"
#include <stdio.h>
static unsigned samples;
static uint32_t pixel(void *data, unsigned x, unsigned y) {
 (void)data; ++samples;
 return 0xff000000u | ((x * 11u) << 16) | ((y * 23u) << 8) | (x ^ y);
}
int main(void) {
 struct wl_display *display = wl_display_create();
 if (!display) return 1;
 struct wl_event_loop *loop = wl_display_get_event_loop(display);
 struct ps5_capture *capture = ps5_capture_create(loop, 17, 9, pixel, NULL);
 if (!capture) { wl_display_destroy(display); return 2; }
 puts("READY"); fflush(stdout);
 int rc = 0;
 while (samples < 17 * 9) if (wl_event_loop_dispatch(loop, -1) < 0) { rc = 3; break; }
 ps5_capture_destroy(capture); wl_display_destroy(display);
 return rc;
}
