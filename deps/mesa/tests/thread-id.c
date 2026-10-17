/* Exercise the actual vn_gettid body extracted from the prepared Mesa source. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include "util/detect_os.h"
#include "venus-thread-id-function.h"
#ifdef PS5WL_PS5
#include "ps5log.h"
#define report ps5log
#else
#define report(...) do { printf(__VA_ARGS__); putchar('\n'); } while (0)
#endif

struct sample { pid_t first; bool stable; };
static pthread_barrier_t barrier;
static void *sample_thread(void *opaque)
{
    struct sample *sample = opaque;
    sample->first = vn_gettid();
    sample->stable = sample->first > 0;
    for (unsigned i = 0; i < 10000; i++)
        sample->stable &= vn_gettid() == sample->first;
    /* Keep both worker identities live while comparing them with the caller. */
    pthread_barrier_wait(&barrier);
    return NULL;
}
int main(void)
{
#ifdef PS5WL_PS5
    if (ps5log_init("ps5wl-venus-thread-id", 9595)) return 1;
#endif
    pthread_t threads[2];
    struct sample samples[3] = {{0}};
    if (pthread_barrier_init(&barrier, NULL, 3)) return 1;
    for (unsigned i = 0; i < 2; i++)
        if (pthread_create(&threads[i], NULL, sample_thread, &samples[i])) return 1;
    sample_thread(&samples[2]);
    for (unsigned i = 0; i < 2; i++) pthread_join(threads[i], NULL);
    pthread_barrier_destroy(&barrier);
    bool pass = samples[0].stable && samples[1].stable && samples[2].stable &&
                samples[0].first != samples[1].first &&
                samples[0].first != samples[2].first &&
                samples[1].first != samples[2].first;
    report("vn_gettid main=%d worker0=%d worker1=%d stable=%u,%u,%u RESULT %s",
           samples[2].first, samples[0].first, samples[1].first,
           samples[2].stable, samples[0].stable, samples[1].stable, pass ? "PASS" : "FAIL");
#ifdef PS5WL_PS5
    ps5log_flush_wait(10000);
#endif
    return pass ? 0 : 1;
}
