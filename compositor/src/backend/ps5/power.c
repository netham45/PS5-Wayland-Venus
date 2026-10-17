#include "power.h"
#include "ps5log.h"
/* PowerTick(void): shipping PS5 call pattern, TokenStation5 f4b2af4b9fc4,
 * ps5/title.cpp keep_awake(), every 10 s. PS4 Cast 1057d0674765 calls it
 * every frame, including its idle lobby (app/src/main.c).
 * Private no-argument ABIs verified on firmware 10.60 through read-only SDK
 * mdbg_copyout: evidence/power-abi.log. The getter returns cached seconds in
 * eax, or 0xffff when its shared state is absent/negative; no output pointer.
 * Reset replaces rdi/esi with internal state/0x08000000 before its tail call. */
extern int sceSystemServicePowerTick(void);
extern int sceShellCoreUtilResetAutoPowerDownTimer(void);
extern int sceShellCoreUtilGetAutoPowerDownRemainingSeconds(void);
int ps5_power_activity(void) {
 return sceSystemServicePowerTick();
}
void ps5_power_refresh(const char *reason) {
 int before = sceShellCoreUtilGetAutoPowerDownRemainingSeconds();
 int tick = ps5_power_activity();
 int reset = sceShellCoreUtilResetAutoPowerDownTimer();
 int after = sceShellCoreUtilGetAutoPowerDownRemainingSeconds();
 ps5log("power %s remaining_before=%d tick_rc=%#x reset_rc=%#x remaining_after=%d",
  reason, before, tick, reset, after);
}
