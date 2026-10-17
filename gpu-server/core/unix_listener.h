#ifndef GPUD_UNIX_LISTENER_H
#define GPUD_UNIX_LISTENER_H

/* Bind without replacing a live listener or a non-socket filesystem entry. */
int gpud_unix_bind(int fd, const char *path, int type);

#endif
