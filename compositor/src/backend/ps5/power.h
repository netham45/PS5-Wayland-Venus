#ifndef PS5WL_POWER_H
#define PS5WL_POWER_H
/* Call for an actual dispatched input event, not an idle input poll. */
int ps5_power_activity(void);
/* Log cached remaining seconds before/after refreshing the native idle timers. */
void ps5_power_refresh(const char *reason);
#endif
