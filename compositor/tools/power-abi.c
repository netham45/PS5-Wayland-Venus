/* Temporary read-only ABI investigation: never invokes private power APIs. */
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include "ps5log.h"
extern void sceShellCoreUtilGetAutoPowerDownRemainingSeconds(void);
extern void sceShellCoreUtilResetAutoPowerDownTimer(void);
extern void sceSystemServicePowerTick(void);
extern void sceSystemServiceTurnOffScreenSaver(void);
extern int mdbg_copyout(int, unsigned long, void *, unsigned long);
int main(void) {
 if (ps5log_init("ps5wl-power-abi", 9539)) return 1;
 ps5log("read-only inspector entered pid=%d", getpid());
 ps5log_flush_wait(1000);
 struct { const char *name; void (*symbol)(void); } symbols[] = {
  {"remaining", sceShellCoreUtilGetAutoPowerDownRemainingSeconds},
  {"reset", sceShellCoreUtilResetAutoPowerDownTimer},
  {"tick", sceSystemServicePowerTick},
  {"screensaver", sceSystemServiceTurnOffScreenSaver},
 };
 long page = sysconf(_SC_PAGESIZE);
 if (page <= 0) return 1;
 for (unsigned i = 0; i < sizeof(symbols) / sizeof(symbols[0]); i++) {
  const unsigned char *code = (const unsigned char *)symbols[i].symbol;
  size_t bytes = (size_t)page - (uintptr_t)code % (size_t)page;
  if (bytes > 512) bytes = 512;
  ps5log("symbol=%s address=%p bytes=%zu", symbols[i].name, code, bytes);
  ps5log_flush_wait(1000);
  if (!code) continue;
  unsigned char copy[512] = {0};
  int rc = mdbg_copyout(getpid(), (unsigned long)code, copy, bytes);
  ps5log("SDK mdbg_copyout %s rc=%d", symbols[i].name, rc);
  if (rc) continue;
  code = copy;
  for (size_t offset = 0; offset < bytes; offset += 32) {
   char hex[65];
   size_t count = bytes - offset;
   if (count > 32) count = 32;
   for (size_t j = 0; j < count; j++) snprintf(hex + j * 2, 3, "%02x", code[offset + j]);
   ps5log("code %s +%zu %s", symbols[i].name, offset, hex);
  }
 }
 ps5log("read-only ABI inspection complete; no power calls made");
 ps5log_flush_wait(3000);
 return 0;
}
