/* Debug-only pull capture of the completed VideoOut scanout, not a screenshot
 * of system overlays. PNG uses lossless uncompressed DEFLATE (RFC1950/1951). */
#include "ps5.h"
#include "ps5log.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
struct ps5_capture {
 int fd;
 struct wl_event_source *source;
 uint32_t (*pixel)(void *, unsigned, unsigned);
 void *data;
 unsigned width, height;
};
static int send_all(int fd, const void *data, size_t size) {
 const unsigned char *p = data;
 while (size) { ssize_t n = send(fd, p, size, 0); if (n < 0 && errno == EINTR) continue; if (n <= 0) return -1; p += n; size -= n; }
 return 0;
}
static uint32_t crc32(const unsigned char *p, size_t size, uint32_t crc) {
 while (size--) { crc ^= *p++; for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
 return crc;
}
static void be32(unsigned char *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static int chunk(int fd, const char type[4], const unsigned char *data, size_t size) {
 unsigned char header[8], footer[4]; be32(header, size); memcpy(header + 4, type, 4);
 uint32_t crc = crc32((const unsigned char *)type, 4, 0xffffffffu);
 be32(footer, crc32(data, size, crc) ^ 0xffffffffu);
 return send_all(fd, header, sizeof(header)) || send_all(fd, data, size) || send_all(fd, footer, sizeof(footer));
}
static int capture_ready(int fd, uint32_t mask, void *data) {
 (void)mask; struct ps5_capture *capture = data;
 int client = accept(fd, NULL, NULL); if (client < 0) return 0;
 fcntl(client, F_SETFD, FD_CLOEXEC);
 int flags = fcntl(client, F_GETFL);
 fcntl(client, F_SETFL, flags & ~O_NONBLOCK);
 struct timeval timeout = { .tv_sec = 2 }; int yes = 1;
 setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
 setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
 static const unsigned char signature[] = {137,80,78,71,13,10,26,10};
 unsigned char ihdr[13] = {0}; be32(ihdr, capture->width); be32(ihdr + 4, capture->height); ihdr[8] = 8; ihdr[9] = 2;
 int rc = send_all(client, signature, sizeof(signature)) || chunk(client, "IHDR", ihdr, sizeof(ihdr));
 /* At 1080p one RGB row + filter fits a single stored DEFLATE block. The
  * event loop does not mutate either scanout during this synchronous read. */
 unsigned char row[2 + 5 + 1 + 1920 * 3 + 4]; uint32_t a = 1, b = 0;
 for (unsigned y = 0; !rc && y < capture->height; ++y) {
  size_t at = 0;
  if (y == 0) { row[at++] = 0x78; row[at++] = 0x01; }
  unsigned bytes = 1 + capture->width * 3;
  row[at++] = y + 1 == capture->height ? 1 : 0;
  row[at++] = bytes; row[at++] = bytes >> 8;
  row[at++] = ~bytes; row[at++] = ~bytes >> 8;
  size_t start = at; row[at++] = 0;
  for (unsigned x = 0; x < capture->width; ++x) {
   uint32_t pixel = capture->pixel(capture->data, x, y);
   row[at++] = pixel >> 16; row[at++] = pixel >> 8; row[at++] = pixel;
  }
  for (size_t i = start; i < at; ++i) { a = (a + row[i]) % 65521; b = (b + a) % 65521; }
  if (y + 1 == capture->height) { be32(row + at, (b << 16) | a); at += 4; }
  rc = chunk(client, "IDAT", row, at);
 }
 if (!rc) rc = chunk(client, "IEND", NULL, 0);
 ps5log("scanout PNG capture %ux%u rc=%d errno=%d (system overlays excluded)", capture->width, capture->height, rc, rc ? errno : 0);
 close(client); return 0;
}
struct ps5_capture *ps5_capture_create(struct wl_event_loop *loop,
 unsigned width, unsigned height, uint32_t (*pixel)(void *, unsigned, unsigned), void *data) {
 if (width > 1920) return NULL;
 struct ps5_capture *capture = calloc(1, sizeof(*capture)); if (!capture) return NULL;
 capture->fd = socket(AF_INET, SOCK_STREAM, 0);
 if (capture->fd < 0) { free(capture); return NULL; }
 fcntl(capture->fd, F_SETFD, FD_CLOEXEC);
 int flags = fcntl(capture->fd, F_GETFL); fcntl(capture->fd, F_SETFL, flags | O_NONBLOCK);
 int yes = 1; setsockopt(capture->fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
 struct sockaddr_in address = { .sin_len = sizeof(address), .sin_family = AF_INET, .sin_port = htons(9531), .sin_addr.s_addr = htonl(INADDR_ANY) };
 if (bind(capture->fd, (struct sockaddr *)&address, sizeof(address)) || listen(capture->fd, 2)) { close(capture->fd); free(capture); return NULL; }
 capture->width = width; capture->height = height; capture->pixel = pixel; capture->data = data;
 capture->source = wl_event_loop_add_fd(loop, capture->fd, WL_EVENT_READABLE, capture_ready, capture);
 if (!capture->source) { close(capture->fd); free(capture); return NULL; }
 ps5log("scanout debug PNG pull listening TCP 9531");
 return capture;
}
void ps5_capture_destroy(struct ps5_capture *capture) {
 if (!capture) return;
 wl_event_source_remove(capture->source); close(capture->fd); free(capture);
}
