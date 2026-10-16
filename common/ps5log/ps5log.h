#ifndef PS5WL_PS5LOG_H
#define PS5WL_PS5LOG_H

#include <stdint.h>

/* One server per process. port == 0 selects 9530. Returns 0, or -1 with errno. */
int ps5log_init(const char *name, uint16_t port);
/* Thread safe. Each physical line gets a CLOCK_MONOTONIC millisecond prefix. */
void ps5log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Keep the process alive for ms, allowing late clients to pull its backlog. */
void ps5log_flush_wait(int ms);

#endif
