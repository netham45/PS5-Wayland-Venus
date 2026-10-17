#include "power.h"
#include "ps5log.h"
#include <errno.h>
#include <time.h>
#include <unistd.h>
/* Firmware 10.60 no-argument ABI: build/ps5/evidence/power-abi.log.
 * Keep this private API out of the foreground compositor entirely. */
extern int sceShellCoreUtilResetAutoPowerDownTimer(void);
static void refresh_power(const char *reason) {
 int before = ps5_power_remaining_seconds();
 int tick = ps5_power_activity();
 int reset = sceShellCoreUtilResetAutoPowerDownTimer();
 int after = ps5_power_remaining_seconds();
 ps5log("power %s remaining_before=%d tick_rc=%#x reset_rc=%#x remaining_after=%d",
  reason, before, tick, reset, after);
}
int main(void) {
 if (ps5log_init("ps5wl-keepalive", 9539)) return 1;
 ps5log("session keepalive pid=%d uid=%u interval=30s; no Settings/autoload changes", getpid(), getuid());
 refresh_power("startup");
 for (;;) {
  struct timespec remaining = {.tv_sec = 30};
  while (nanosleep(&remaining, &remaining)) {
   if (errno == EINTR) continue;
   ps5log("keepalive nanosleep failed errno=%d", errno);
   ps5log_flush_wait(1000);
   return 1;
  }
  refresh_power("periodic");
 }
}
