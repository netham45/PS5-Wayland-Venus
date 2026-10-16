#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* POSIX getline for the payload SDK, whose stdio declares but does not export
 * it. Use libc's buffered input and stream lock, not private FILE fields.
 */
ssize_t getline(char **restrict line, size_t *restrict capacity,
        FILE *restrict stream) {
    if (line == NULL || capacity == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (*line == NULL) {
        *capacity = 0;
    }
    size_t length = 0;
    ssize_t result = -1;
    flockfile(stream);
    for (;;) {
        if (length >= (size_t)SSIZE_MAX) {
            errno = EOVERFLOW;
            break;
        }
        if (length + 1 >= *capacity) {
            size_t next = *capacity <= SIZE_MAX / 2 ? *capacity * 2 : SIZE_MAX;
            if (next < 128) {
                next = 128;
            }
            if (next <= length + 1) {
                errno = EOVERFLOW;
                break;
            }
            char *grown = realloc(*line, next);
            if (grown == NULL) {
                break;
            }
            *line = grown;
            *capacity = next;
        }
        int ch = getc_unlocked(stream);
        if (ch == EOF) {
            (*line)[length] = '\0';
            if (length != 0 && !ferror(stream)) {
                result = (ssize_t)length;
            }
            break;
        }
        (*line)[length++] = (char)ch;
        (*line)[length] = '\0';
        if (ch == '\n') {
            result = (ssize_t)length;
            break;
        }
    }
    funlockfile(stream);
    return result;
}
