#ifndef GPUD_VTEST_H
#define GPUD_VTEST_H
int gpud_vtest_start(const char *path);
void gpud_vtest_shutdown(void);
int gpud_vtest_drain(unsigned timeout_ms);
#endif
