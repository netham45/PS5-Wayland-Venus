#ifndef PS5WL_LOG_BUFFER_H
#define PS5WL_LOG_BUFFER_H
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Preserve Wayland's complete, single-write diagnostic without open_memstream
 * or libc-private FILE internals. Allocation/format failures remain failures.
 */
struct ps5wl_log_buffer {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
};

static inline void ps5wl_log_append(struct ps5wl_log_buffer *buffer,
        const char *format, ...) __attribute__((format(printf, 2, 3)));

static inline void ps5wl_log_append(struct ps5wl_log_buffer *buffer,
        const char *format, ...) {
    if (buffer->failed) {
        return;
    }
    if (buffer->capacity == 0) {
        buffer->capacity = 256;
        buffer->data = malloc(buffer->capacity);
        if (buffer->data == NULL) {
            buffer->failed = true;
            return;
        }
    }
    for (;;) {
        va_list args;
        va_start(args, format);
        size_t available = buffer->capacity - buffer->length;
        int count = vsnprintf(buffer->data + buffer->length, available, format, args);
        va_end(args);
        if (count < 0 || (size_t)count > SIZE_MAX - buffer->length - 1) {
            buffer->failed = true;
            return;
        }
        if ((size_t)count < available) {
            buffer->length += (size_t)count;
            return;
        }
        size_t required = buffer->length + (size_t)count + 1;
        size_t capacity = buffer->capacity <= SIZE_MAX / 2 ? buffer->capacity * 2 : SIZE_MAX;
        if (capacity < required) {
            capacity = required;
        }
        char *data = realloc(buffer->data, capacity);
        if (data == NULL) {
            buffer->failed = true;
            return;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
}
#endif
