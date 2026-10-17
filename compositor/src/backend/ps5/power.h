#ifndef PS5WL_POWER_H
#define PS5WL_POWER_H
/* Native cached seconds, or 65535 when the shared state is absent/invalid. */
int ps5_power_remaining_seconds(void);
/* Call for an actual dispatched input event, not an idle input poll. */
int ps5_power_activity(void);
/* Log cached remaining seconds before/after PowerTick; never call private reset. */
void ps5_power_refresh(const char *reason);
#endif
