#include "power.h"
#include "ps5log.h"
#include <errno.h>
#include <time.h>
#include <unistd.h>
int main(void) {
 if (ps5log_init("ps5wl-keepalive", 9539)) return 1;
 ps5log("session keepalive pid=%d uid=%u interval=30s; no Settings/autoload changes", getpid(), getuid());
 ps5_power_refresh("startup");
 for (;;) {
  struct timespec remaining = {.tv_sec = 30};
  while (nanosleep(&remaining, &remaining)) {
   if (errno == EINTR) continue;
   ps5log("keepalive nanosleep failed errno=%d", errno);
   ps5log_flush_wait(1000);
   return 1;
  }
  ps5_power_refresh("periodic");
 }
}
