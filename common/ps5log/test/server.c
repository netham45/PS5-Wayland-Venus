#include "ps5log.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    if (ps5log_init("host-test", (uint16_t)atoi(argv[1])) != 0) {
        perror("ps5log_init");
        return 1;
    }
    if (ps5log_init("duplicate", 0) != -1 || errno != EALREADY) return 3;
    for (int i = 0; i < 300; ++i) ps5log("backlog-%03d", i);
    puts("ready");
    fflush(stdout);
    char command[32];
    while (fgets(command, sizeof(command), stdin)) {
        if (!strcmp(command, "live\n")) ps5log("live-one\nlive-two\n");
        else if (!strcmp(command, "quit\n")) break;
    }
    ps5log("goodbye");
    ps5log_flush_wait(200);
    return 0;
}
