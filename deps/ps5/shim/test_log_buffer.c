#include "include/ps5wl_log_buffer.h"
#include <assert.h>
#include <string.h>

int main(void) {
    struct ps5wl_log_buffer buffer = {0};
    char large[1025];
    memset(large, 'x', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';
    ps5wl_log_append(&buffer, "%s:%d ", "event", 42);
    ps5wl_log_append(&buffer, "%s", large);
    ps5wl_log_append(&buffer, " %08x\n", 0x1234);
    assert(!buffer.failed);
    assert(buffer.length == 9 + 1024 + 10);
    assert(memcmp(buffer.data, "event:42 ", 9) == 0);
    assert(memcmp(buffer.data + 9, large, 1024) == 0);
    assert(strcmp(buffer.data + 1033, " 00001234\n") == 0);
    assert(buffer.data[buffer.length] == '\0');
    free(buffer.data);

    /* A format containing an embedded NUL must retain its counted byte. */
    buffer = (struct ps5wl_log_buffer){0};
    ps5wl_log_append(&buffer, "%c%s", 0, "tail");
    assert(!buffer.failed && buffer.length == 5);
    assert(memcmp(buffer.data, "\0tail\0", 6) == 0);
    free(buffer.data);
    puts("log buffer expansion/content/NUL: PASS");
    return 0;
}
