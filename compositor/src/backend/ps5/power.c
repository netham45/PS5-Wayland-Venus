#include "power.h"
#include "ps5log.h"
/* PowerTick(void): shipping PS5 call pattern, TokenStation5 f4b2af4b9fc4,
 * ps5/title.cpp keep_awake(), every 10 s. PS4 Cast 1057d0674765 calls it
 * every frame, including its idle lobby (app/src/main.c).
 * Getter no-argument ABI verified on firmware 10.60 through read-only SDK
 * mdbg_copyout: build/ps5/evidence/power-abi.log. It returns cached seconds in
 * eax, or 0xffff when its shared state is absent/negative; no output pointer. */
extern int sceSystemServicePowerTick(void);
extern int sceShellCoreUtilGetAutoPowerDownRemainingSeconds(void);
int ps5_power_remaining_seconds(void) {
 return sceShellCoreUtilGetAutoPowerDownRemainingSeconds();
}
int ps5_power_activity(void) {
 return sceSystemServicePowerTick();
}
void ps5_power_refresh(const char *reason) {
 int before = ps5_power_remaining_seconds();
 int tick = ps5_power_activity();
 int after = ps5_power_remaining_seconds();
 ps5log("power %s remaining_before=%d tick_rc=%#x remaining_after=%d",
  reason, before, tick, after);
}
