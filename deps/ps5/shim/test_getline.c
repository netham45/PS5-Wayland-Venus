#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    FILE *stream = tmpfile();
    assert(stream != NULL);
    char large[2048];
    memset(large, 'x', sizeof(large));
    assert(fwrite(large, 1, sizeof(large), stream) == sizeof(large));
    assert(fwrite("\n\0tail", 1, 6, stream) == 6);
    rewind(stream);
    char *line = NULL;
    size_t capacity = 0;
    assert(getline(&line, &capacity, stream) == 2049);
    assert(memcmp(line, large, sizeof(large)) == 0);
    assert(line[2048] == '\n' && line[2049] == '\0');
    assert(getline(&line, &capacity, stream) == 5);
    assert(memcmp(line, "\0tail\0", 6) == 0);
    assert(getline(&line, &capacity, stream) == -1 && feof(stream));
    free(line);
    fclose(stream);
    puts("getline expansion/newline/embedded-NUL/EOF: PASS");
    return 0;
}
