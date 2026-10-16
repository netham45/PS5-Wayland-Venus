#include "window.h"
#include "font8x8.h"
#include "../../common/ps5log/ps5log.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRAIL_POINTS 8192
#define EVENT_LINES 12
struct point { int x, y; bool start; };
struct demo {
    char text[1024];
    size_t text_len;
    char events[EVENT_LINES][128];
    unsigned event_count;
    struct point trail[TRAIL_POINTS];
    unsigned points;
    bool dragging, new_stroke;
};
static void event(struct demo *d, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->events[d->event_count++ % EVENT_LINES], 128, fmt, ap);
    va_end(ap);
}
static void pixel(struct window *w, uint32_t *p, int x, int y, uint32_t color) {
    if (x >= 0 && y >= 0 && x < w->width && y < w->height) p[(size_t)y * w->width + x] = color;
}
static void line(struct window *w, uint32_t *p, int x, int y, int x1, int y1, uint32_t color) {
    int dx = abs(x1 - x), sx = x < x1 ? 1 : -1;
    int dy = -abs(y1 - y), sy = y < y1 ? 1 : -1, error = dx + dy;
    for (;;) {
        pixel(w, p, x, y, color); pixel(w, p, x + 1, y, color);
        if (x == x1 && y == y1) break;
        int twice = error * 2;
        if (twice >= dy) { error += dy; x += sx; }
        if (twice <= dx) { error += dx; y += sy; }
    }
}
static void text(struct window *w, uint32_t *p, int x, int y, const char *s, uint32_t color) {
    int left = x;
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (c == '\n' || x + 16 > w->width) { x = left; y += 20; if (c == '\n') continue; }
        if (y >= w->height) break;
        /* The embedded font is Basic Latin. Non-Latin UTF-8 codepoints render one '?'. */
        if (c >= 128) { if ((c & 0xc0) == 0x80) continue; c = '?'; }
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++)
                if (font8x8_basic[c][row] & (1u << col))
                    for (int yy = 0; yy < 2; yy++) for (int xx = 0; xx < 2; xx++)
                        pixel(w, p, x + col * 2 + xx, y + row * 2 + yy, color);
        x += 16;
    }
}
static void paint(struct window *w, uint32_t *p, uint32_t time) {
    struct demo *d = w->userdata;
    for (int y = 0; y < w->height; y++) for (int x = 0; x < w->width; x++)
        p[(size_t)y * w->width + x] = ((x / 32 + y / 32) & 1) ? 0xff182432 : 0xff1c2938;
    unsigned count = d->points < TRAIL_POINTS ? d->points : TRAIL_POINTS;
    unsigned first = d->points - count;
    for (unsigned i = first; i < d->points; i++) {
        struct point a = d->trail[i % TRAIL_POINTS];
        if (i == first || a.start) pixel(w, p, a.x, a.y, 0xffffb454);
        else {
            struct point b = d->trail[(i - 1) % TRAIL_POINTS];
            line(w, p, b.x, b.y, a.x, a.y, 0xffffb454);
        }
    }
    text(w, p, 16, 16, "PS5WL interactive shm demo", 0xff76d6ff);
    text(w, p, 16, 40, "Drag: draw | F11: fullscreen | Esc: exit", 0xffd4e5f5);
    char status[128];
    snprintf(status, sizeof(status), "Pointer %.0f,%.0f  frame %u  %dx%d  %ums", w->pointer_x, w->pointer_y,
             w->frames, w->width, w->height, time);
    text(w, p, 16, 64, status, 0xff90e090);
    text(w, p, 16, 96, "Typed text (Backspace edits):", 0xffd4e5f5);
    text(w, p, 16, 120, d->text, 0xffffffff);
    unsigned n = d->event_count < EVENT_LINES ? d->event_count : EVENT_LINES;
    int start_y = w->height > 500 ? w->height - 260 : 260;
    text(w, p, 16, start_y, "Key / button event log:", 0xff76d6ff);
    for (unsigned i = 0; i < n; i++)
        text(w, p, 16, start_y + 24 + (int)i * 18,
             d->events[(d->event_count - n + i) % EVENT_LINES], 0xffe5d2a0);
    int x = (int)w->pointer_x, y = (int)w->pointer_y;
    line(w, p, x - 10, y, x + 10, y, 0xffffffff);
    line(w, p, x, y - 10, x, y + 10, 0xffffffff);
}
static void motion(struct window *w, double x, double y) {
    struct demo *d = w->userdata;
    if (!d->dragging) return;
    /* Coordinates are surface-local; clamp retained trails to bounded geometry. */
    int px = x < 0 ? 0 : x >= w->width ? w->width - 1 : (int)x;
    int py = y < 0 ? 0 : y >= w->height ? w->height - 1 : (int)y;
    d->trail[d->points++ % TRAIL_POINTS] = (struct point){px, py, d->new_stroke};
    d->new_stroke = false;
    ps5log("demo-drag x=%d y=%d points=%u", px, py, d->points);
}
static void button(struct window *w, uint32_t button, bool pressed) {
    struct demo *d = w->userdata;
    event(d, "button %u %s at %.0f,%.0f", button, pressed ? "down" : "up", w->pointer_x, w->pointer_y);
    if (button == 0x110) {
        d->dragging = pressed; d->new_stroke = pressed;
        if (pressed) motion(w, w->pointer_x, w->pointer_y);
    }
}
static void key(struct window *w, uint32_t key, bool pressed, xkb_keysym_t sym, const char *utf8) {
    struct demo *d = w->userdata;
    char name[64];
    xkb_keysym_get_name(sym, name, sizeof(name));
    event(d, "key %u %s %s", key, pressed ? "down" : "up", name);
    if (!pressed) return;
    if (sym == XKB_KEY_Escape) { w->running = false; return; }
    if (sym == XKB_KEY_F11) { window_fullscreen(w, !w->fullscreen); return; }
    if (sym == XKB_KEY_BackSpace) {
        if (d->text_len) {
            do { d->text_len--; } while (d->text_len && ((unsigned char)d->text[d->text_len] & 0xc0) == 0x80);
            d->text[d->text_len] = 0;
        }
    } else {
        size_t length = strlen(utf8);
        if (length && ((unsigned char)utf8[0] >= 32 || utf8[0] == '\n' || utf8[0] == '\r') &&
            d->text_len + length < sizeof(d->text)) {
            if (sym == XKB_KEY_Return) { d->text[d->text_len++] = '\n'; d->text[d->text_len] = 0; }
            else { memcpy(d->text + d->text_len, utf8, length + 1); d->text_len += length; }
        }
    }
    ps5log("demo-text bytes=%zu text=%s", d->text_len, d->text);
}
int main(int argc, char **argv) {
    unsigned limit = 0;
    if (argc == 3 && !strcmp(argv[1], "--frames")) limit = (unsigned)strtoul(argv[2], NULL, 10);
    else if (argc > 1) { fprintf(stderr, "Usage: %s [--frames N]\n", argv[0]); return 2; }
    struct window w;
    struct demo demo = {0};
    const struct window_hooks hooks = {.paint = paint, .motion = motion, .button = button, .key = key};
    return window_run(&w, "ps5wl-demo", 9561, &hooks, &demo, limit);
}
