#define _GNU_SOURCE
#include "ps5wl_gpud.h"
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
static volatile sig_atomic_t stop;
static void handler(int sig) { (void)sig; stop = 1; }
int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: gpud-host VTEST_PATH RENDER_PATH\n"); return 2; }
    signal(SIGTERM, handler); signal(SIGINT, handler);
    struct ps5wl_gpud_config config = {.vtest_path = argv[1], .render_path = argv[2]};
    int result = ps5wl_gpud_start(&config);
    if (result) { fprintf(stderr, "gpud start: %d\n", result); return 1; }
    fprintf(stderr, "gpud ready pid=%ld in-process threads\n", (long)getpid());
    while (!stop) usleep(10000);
    result = ps5wl_gpud_stop(5000);
    return result ? 1 : 0;
}
