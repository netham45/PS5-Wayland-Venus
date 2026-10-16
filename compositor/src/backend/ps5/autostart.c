#include "ps5.h"
#include "ps5log.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
/* elfldr socksrv.c accepts file:// URIs terminated by newline. pipe=0 keeps
 * client lifetime independent of this control socket. Never invoke a shell. */
void ps5_autostart(void) {
 FILE *list = fopen("/data/ps5wl/autostart.txt", "r");
 if (!list) { ps5log("autostart /data/ps5wl/autostart.txt not available errno=%d", errno); return; }
 char path[1024];
 while (fgets(path, sizeof(path), list)) {
  path[strcspn(path, "\r\n")] = 0;
  if (!path[0] || path[0] == '#') continue;
  if (path[0] != '/' || strpbrk(path, "?%#\t ") || !strstr(path, "/ps5wl-")) {
   ps5log("autostart rejected path=%s (absolute ps5wl-* ELF paths only)", path); continue;
  }
  int fd = socket(AF_INET, SOCK_STREAM, 0); if (fd < 0) continue;
  struct timeval timeout = { .tv_sec = 3 };
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  int yes = 1; setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
  struct sockaddr_in address = { .sin_len = sizeof(address), .sin_family = AF_INET,
   .sin_port = htons(9021), .sin_addr.s_addr = htonl(INADDR_LOOPBACK) };
  if (connect(fd, (struct sockaddr *)&address, sizeof(address))) {
   ps5log("autostart connect failed errno=%d path=%s", errno, path); close(fd); continue;
  }
  char request[1100]; int length = snprintf(request, sizeof(request), "file://%s?pipe=0\n", path);
  size_t sent = 0;
  while (sent < (size_t)length) { ssize_t n = send(fd, request + sent, length - sent, 0); if (n <= 0) break; sent += n; }
  ps5log("autostart submitted path=%s bytes=%zu/%d", path, sent, length);
  shutdown(fd, SHUT_WR); close(fd);
 }
 fclose(list);
}
