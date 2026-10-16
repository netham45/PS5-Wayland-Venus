/* VideoOut backend. Tile addressing is an altered scalar implementation of
 * SDL release-2.30.x-ps5 src/video/ps5/SDL_ps5tilemap.c:45-70,
 * Copyright (C) 2026 John Törnblom. See LICENSE.SDL. */
#include "ps5.h"
#include "ps5log.h"
#include <drm_fourcc.h>
#include <sys/event.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output_layer.h>
#define WIDTH 1920
#define HEIGHT 1080
/* SDL Tilemap_BufferSize: last 128-row band + final 512x128 tile.
 * Round each real 1080p scanout to 2 MiB; no unused 64 MiB allocation. */
#define BUFFER_BYTES (((((HEIGHT - 1) / 128) * (128 * WIDTH) + ((WIDTH - 1) / 512) * 65536 + 65536) * 4 + 0x1fffff) & ~0x1fffff)
#define MEMORY_SIZE (2 * BUFFER_BYTES)
struct ps5_backend {
 struct wlr_backend base;
 struct wlr_output output;
 struct wl_display *display;
 struct wl_event_loop *loop;
 struct ps5_input *input;
 struct ps5_capture *capture;
 int video, pipefd[2];
 intptr_t physical;
 bool allocated, thread_started, pending;
 void *mapping;
 struct ps5_video_buffer buffers[2];
 struct kevent *queue;
 struct wl_event_source *flip_source;
 pthread_t thread;
 atomic_bool stopping;
 unsigned next, pending_index, displayed, commit_seq;
 uint64_t flips, composites;
 struct timespec first_flip;
 pixman_region32_t stale[2];
 uint32_t xoff[WIDTH], yoff[128];
};
static uint32_t tile_offset(uint32_t x, uint32_t y) {
 return ((x & 1) | ((x >> 1 & 1) << 1) | ((y & 1) << 2) |
 ((y >> 1 & 1) << 3) | ((y >> 2 & 1) << 4) | ((x >> 2 & 1) << 5) |
 (((x >> 3 ^ y >> 3) & 1) << 6) | (((x >> 4 ^ y >> 4) & 1) << 7) |
 (((x >> 6 ^ y >> 5) & 1) << 8) | (((x >> 5 ^ y >> 6) & 1) << 9) |
 ((y >> 3 & 1) << 10) | ((x >> 4 & 1) << 11) | ((y >> 6 & 1) << 12) |
 ((x >> 6 & 1) << 13) | ((x >> 7 & 1) << 14) | ((x >> 8 & 1) << 15));
}
static struct ps5_backend *from_output(struct wlr_output *output) {
 struct ps5_backend *b = wl_container_of(output, b, output); return b;
}
static bool output_test(struct wlr_output *output, const struct wlr_output_state *s) {
 (void)output;
 uint32_t allowed = WLR_OUTPUT_STATE_BACKEND_OPTIONAL | WLR_OUTPUT_STATE_BUFFER |
 WLR_OUTPUT_STATE_ENABLED | WLR_OUTPUT_STATE_MODE;
 if (s->committed & ~allowed) return false;
 if ((s->committed & WLR_OUTPUT_STATE_MODE) &&
 (s->mode_type != WLR_OUTPUT_STATE_MODE_CUSTOM || s->custom_mode.width != WIDTH ||
 s->custom_mode.height != HEIGHT || s->custom_mode.refresh != 60000)) return false;
 if ((s->committed & WLR_OUTPUT_STATE_BUFFER) &&
 (s->buffer->width != WIDTH || s->buffer->height != HEIGHT)) return false;
 if (s->committed & WLR_OUTPUT_STATE_LAYERS) {
  for (size_t i = 0; i < s->layers_len; ++i) s->layers[i].accepted = false;
 }
 return true;
}
static bool output_commit(struct wlr_output *output, const struct wlr_output_state *s) {
 struct ps5_backend *b = from_output(output);
 if (!output_test(output, s)) return false;
 if (!(s->committed & WLR_OUTPUT_STATE_BUFFER)) return true;
 if (b->pending) return false;
 void *pixels; uint32_t format; size_t stride;
 if (!wlr_buffer_begin_data_ptr_access(s->buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ,
 &pixels, &format, &stride)) return false;
 if (format != DRM_FORMAT_XRGB8888 && format != DRM_FORMAT_ARGB8888) {
  ps5log("unsupported scanout source format=%08x", format);
  wlr_buffer_end_data_ptr_access(s->buffer); return false;
 }
 pixman_region32_t damage;
 pixman_region32_init(&damage);
 if (s->committed & WLR_OUTPUT_STATE_DAMAGE) pixman_region32_copy(&damage, &s->damage);
 else pixman_region32_union_rect(&damage, &damage, 0, 0, WIDTH, HEIGHT);
 pixman_region32_intersect_rect(&damage, &damage, 0, 0, WIDTH, HEIGHT);
 /* Each scanout has its own history: preserve damage from the intervening flip. */
 pixman_region32_union(&b->stale[0], &b->stale[0], &damage);
 pixman_region32_union(&b->stale[1], &b->stale[1], &damage);
 int n; pixman_box32_t *rects = pixman_region32_rectangles(&b->stale[b->next], &n);
 uint32_t *dst = b->buffers[b->next].data;
 for (int r = 0; r < n; ++r) {
  for (int y = rects[r].y1; y < rects[r].y2; ++y) {
   const uint32_t *src = (const uint32_t *)((const char *)pixels + y * stride);
   uint32_t band = (y / 128) * (128 * WIDTH), yo = b->yoff[y % 128];
   for (int x = rects[r].x1; x < rects[r].x2; ++x)
    dst[band + (b->xoff[x] ^ yo)] = src[x];
  }
 }
 /* WC scanout stores must be globally visible before the flip ioctl. */
 __asm__ volatile("sfence" ::: "memory");
 wlr_buffer_end_data_ptr_access(s->buffer);
 pixman_region32_fini(&damage);
 int rc = sceVideoOutSubmitFlip(b->video, b->next, 1, b->flips + 1);
 if (rc) { ps5log("SubmitFlip rc=%#x", rc); return false; }
 pixman_region32_clear(&b->stale[b->next]);
 b->next ^= 1;
 b->pending_index = b->next ^ 1;
 b->pending = true;
 b->commit_seq = output->commit_seq + 1;
 if (++b->composites == 1 || b->composites % 120 == 0)
  ps5log("pixman composed buffer count=%llu damaged_rects=%d stride=%zu format=%08x",
   (unsigned long long)b->composites, n, stride, format);
 return true;
}
static void output_destroy(struct wlr_output *output) {
 wlr_backend_destroy(&from_output(output)->base);
}
static const struct wlr_output_impl output_impl = {
 .test = output_test, .commit = output_commit, .destroy = output_destroy,
};
struct flip_message { struct timespec when; int error; };
static void *flip_thread(void *data) {
 struct ps5_backend *b = data;
 while (!atomic_load(&b->stopping)) {
  struct kevent event; int count = 0; unsigned timeout = 100000;
  int rc = sceKernelWaitEqueue(b->queue, &event, 1, &count, &timeout);
  if (atomic_load(&b->stopping)) break;
  if (rc || count == 0) continue;
  struct flip_message msg = {0}; clock_gettime(CLOCK_MONOTONIC, &msg.when);
  /* At most one outstanding flip, so this small blocking write cannot fill the pipe. */
  if (write(b->pipefd[1], &msg, sizeof(msg)) != sizeof(msg)) break;
 }
 return NULL;
}
static int flip_ready(int fd, uint32_t mask, void *data) {
 (void)mask;
 struct ps5_backend *b = data; struct flip_message msg;
 if (read(fd, &msg, sizeof(msg)) != sizeof(msg)) return 0;
 if (!b->pending) return 0;
 b->pending = false;
 b->displayed = b->pending_index;
 ++b->flips;
 ps5log("flip-event seq=%llu index=%u monotonic=%lld.%09ld",
  (unsigned long long)b->flips, b->displayed, (long long)msg.when.tv_sec, msg.when.tv_nsec);
 if (b->flips == 1) b->first_flip = msg.when;
 struct wlr_output_event_present event = { .commit_seq = b->commit_seq,
 .presented = true, .when = msg.when, .seq = b->flips, .refresh = 16666667,
 .flags = WLR_OUTPUT_PRESENT_VSYNC | WLR_OUTPUT_PRESENT_HW_COMPLETION };
 if (b->commit_seq) wlr_output_send_present(&b->output, &event);
 b->commit_seq = 0;
 if (b->flips == 1 || b->flips % 120 == 0) {
  double elapsed = msg.when.tv_sec - b->first_flip.tv_sec +
   (msg.when.tv_nsec - b->first_flip.tv_nsec) / 1e9;
  ps5log("VideoOut flip complete count=%llu elapsed=%.3f rate=%.2fHz", 
   (unsigned long long)b->flips, elapsed, elapsed > 0 ? (b->flips - 1) / elapsed : 0);
  uint64_t status[16] = {0};
  int rc = sceVideoOutGetFlipStatus(b->video, status);
  ps5log("VideoOut GetFlipStatus rc=%#x displayed_marker=%llu expected=%llu",
   rc, (unsigned long long)status[3], (unsigned long long)b->flips);
 }
 wlr_output_send_frame(&b->output);
 /* Keep the hardware clock alive even when the scene has no damage. Re-flip
  * the current scanout without copying pixels or inventing a presentation. */
 if (!b->pending && b->output.enabled) {
  int rc = sceVideoOutSubmitFlip(b->video, b->next ^ 1, 1, b->flips + 1);
  b->pending_index = b->next ^ 1;
  if (rc == 0) b->pending = true;
  else ps5log("VideoOut idle SubmitFlip rc=%#x", rc);
 }
 return 0;
}
static void first_frame(void *data) {
 struct ps5_backend *b = data;
 wlr_output_send_frame(&b->output);
}
static bool backend_start(struct wlr_backend *base) {
 struct ps5_backend *b = wl_container_of(base, b, base);
 if (pthread_create(&b->thread, NULL, flip_thread, b)) return false;
 b->thread_started = true;
 wl_signal_emit_mutable(&base->events.new_output, &b->output);
 ps5_input_start(b->input);
 wl_event_loop_add_idle(b->loop, first_frame, b);
 return true;
}
static void backend_destroy(struct wlr_backend *base) {
 struct ps5_backend *b = wl_container_of(base, b, base);
 atomic_store(&b->stopping, true);
 if (b->thread_started) pthread_join(b->thread, NULL);
 if (b->flip_source) wl_event_source_remove(b->flip_source);
 ps5_capture_destroy(b->capture);
 ps5_input_destroy(b->input);
 wlr_output_finish(&b->output);
 wlr_backend_finish(base);
 if (b->queue) { sceVideoOutDeleteFlipEvent(b->queue, b->video); sceKernelDeleteEqueue(b->queue); }
 if (b->video >= 0) sceVideoOutClose(b->video);
 if (b->mapping) sceKernelMunmap(b->mapping, MEMORY_SIZE);
 if (b->allocated) sceKernelReleaseDirectMemory(b->physical, MEMORY_SIZE);
 for (int i = 0; i < 2; ++i) {
  if (b->pipefd[i] >= 0) close(b->pipefd[i]);
  pixman_region32_fini(&b->stale[i]);
 }
 ps5log("PS5 backend released VideoOut flips=%llu", (unsigned long long)b->flips);
 free(b);
}
static const struct wlr_backend_impl backend_impl = { .start = backend_start, .destroy = backend_destroy };
static uint32_t scanout_pixel(void *data, unsigned x, unsigned y) {
 struct ps5_backend *b = data;
 const uint32_t *pixels = b->buffers[b->displayed].data;
 return pixels[(y / 128) * (128 * WIDTH) + (b->xoff[x] ^ b->yoff[y % 128])];
}
struct wlr_backend *ps5_backend_create(struct wl_display *display) {
 /* Reference: SDL release-2.30.x-ps5 src/video/ps5/SDL_ps5video.c:188-242
  * (PS5_VideoInit): SYSTEM user, direct memory type3/prot0x33, equeue,
  * flip rate0 and RegisterBuffers2 attribute0x8000000022000000. */
 struct ps5_backend *b = calloc(1, sizeof(*b));
 if (!b) return NULL;
 atomic_init(&b->stopping, false);
 b->display = display; b->loop = wl_display_get_event_loop(display);
 b->video = -1; b->pipefd[0] = b->pipefd[1] = -1;
 wlr_backend_init(&b->base, &backend_impl);
 b->base.buffer_caps = WLR_BUFFER_CAP_DATA_PTR | WLR_BUFFER_CAP_SHM;
 for (int i = 0; i < 2; ++i) pixman_region32_init_rect(&b->stale[i], 0, 0, WIDTH, HEIGHT);
 for (unsigned x = 0; x < WIDTH; ++x) b->xoff[x] = (x / 512) * 65536 + tile_offset(x % 512, 0);
 for (unsigned y = 0; y < 128; ++y) b->yoff[y] = tile_offset(0, y);
 struct wlr_output_state state; wlr_output_state_init(&state);
 wlr_output_state_set_custom_mode(&state, WIDTH, HEIGHT, 60000);
 wlr_output_init(&b->output, &b->base, &output_impl, b->loop, &state);
 wlr_output_state_finish(&state);
 wlr_output_set_name(&b->output, "PS5-TV-1");
 wlr_output_set_description(&b->output, "PS5 VideoOut 1920x1080 60 Hz");
 ps5log("HideSplashScreen rc=%#x", sceSystemServiceHideSplashScreen());
 b->video = sceVideoOutOpen(0xff, 0, 0, NULL);
 ps5log("VideoOut SYSTEM open=%#x", b->video);
 if (b->video < 0) goto fail;
 int64_t pool = sceKernelGetDirectMemorySize(), available_start = -1;
 size_t available = 0;
 int rc = sceKernelAvailableDirectMemorySize(0, pool, 0x200000, &available_start, &available);
 ps5log("direct pool=%lld available-query rc=%#x start=%#llx bytes=%zu need=%u",
  (long long)pool, rc, (unsigned long long)available_start, available, MEMORY_SIZE);
 /* Full pool rather than MainDirectMemory's application-budget subrange,
  * as in prospero-win pw_videoout_ps5.c and Mesa RADV's PS5 winsys. */
 rc = sceKernelAllocateDirectMemory(0, pool, MEMORY_SIZE, 0x200000, 3, &b->physical);
 ps5log("VideoOut direct allocate rc=%#x physical=%#lx bytes=%u", rc, (unsigned long)b->physical, MEMORY_SIZE);
 if (rc) goto fail;
 b->allocated = true;
 rc = sceKernelMapDirectMemory(&b->mapping, MEMORY_SIZE, 0x33, 0, b->physical, 0x200000);
 ps5log("VideoOut direct map rc=%#x address=%p", rc, b->mapping);
 if (rc) goto fail;
 memset(b->mapping, 0, MEMORY_SIZE);
 b->buffers[0].data = b->mapping;
 b->buffers[1].data = (char *)b->mapping + BUFFER_BYTES;
 rc = sceKernelCreateEqueue(&b->queue, "ps5wl flips");
 ps5log("CreateEqueue rc=%#x queue=%p", rc, (void *)b->queue);
 if (rc) goto fail;
 rc = sceVideoOutAddFlipEvent(b->queue, b->video, NULL);
 ps5log("AddFlipEvent rc=%#x", rc);
 if (rc) goto fail;
 rc = sceVideoOutSetFlipRate(b->video, 0);
 ps5log("SetFlipRate rc=%#x", rc);
 if (rc) goto fail;
 struct ps5_video_attr attr = {0};
 sceVideoOutSetBufferAttribute2(&attr, 0x8000000022000000ULL, 0, WIDTH, HEIGHT, 0, 0, 0);
 rc = sceVideoOutRegisterBuffers2(b->video, 0, 0, b->buffers, 2, &attr, 0, NULL);
 ps5log("VideoOut RegisterBuffers2 rc=%#x format=0x8000000022000000 1920x1080", rc);
 if (rc) goto fail;
 if (pipe(b->pipefd)) goto fail;
 if (fcntl(b->pipefd[0], F_SETFD, FD_CLOEXEC) ||
  fcntl(b->pipefd[1], F_SETFD, FD_CLOEXEC)) goto fail;
 b->flip_source = wl_event_loop_add_fd(b->loop, b->pipefd[0], WL_EVENT_READABLE, flip_ready, b);
 if (!b->flip_source) goto fail;
 b->input = ps5_input_create(&b->base, display);
 if (!b->input) goto fail;
 b->capture = ps5_capture_create(b->loop, WIDTH, HEIGHT, scanout_pixel, b);
 if (!b->capture) ps5log("scanout debug capture unavailable errno=%d", errno);
 return &b->base;
fail:
 backend_destroy(&b->base); return NULL;
}
