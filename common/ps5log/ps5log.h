#ifndef PS5WL_PS5LOG_H
#define PS5WL_PS5LOG_H

#include <stdint.h>

/*
 * One server per process; lifetime ends at process exit. port == 0 selects
 * 9530. Listens on INADDR_ANY, retains 256 lines of up to 1024 bytes and
 * serves up to 16 clients. Slow clients overtaken by ring wrap disconnect;
 * logging never waits on network clients. Returns 0, or -1 with errno.
 */
int ps5log_init(const char *name, uint16_t port);
/*
 * Thread safe, mirrors to stderr. Formats up to 4095 message bytes; every
 * physical line (and 1024-byte chunk of a long line) gets a monotonic-ms/name
 * prefix. Calls before init retain backlog under the default name "ps5wl".
 */
void ps5log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/* Keep the process alive for ms, allowing late clients to pull its backlog. */
void ps5log_flush_wait(int ms);

#endif
