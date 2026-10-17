#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_ns(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

static void delay(long ns)
{
    struct timespec remaining = { .tv_nsec = ns };
    while (nanosleep(&remaining, &remaining) < 0)
        assert(errno == EINTR);
}

struct action { int fd, close_fd; pthread_t target; int signal, other_thread; };
static void *act(void *argument)
{
    struct action *action = argument;
    delay(5000000);
    if (action->signal) {
        if (action->other_thread) {
            sigset_t one;
            sigemptyset(&one);
            sigaddset(&one, SIGUSR1);
            assert(pthread_sigmask(SIG_UNBLOCK, &one, NULL) == 0);
            assert(pthread_kill(pthread_self(), SIGUSR1) == 0);
        } else
            assert(pthread_kill(action->target, SIGUSR1) == 0);
    } else if (action->close_fd)
        assert(close(action->fd) == 0);
    else
        assert(write(action->fd, "x", 1) == 1);
    return NULL;
}

static void basic(const sigset_t *mask)
{
    int pipefd[2];
    assert(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    struct pollfd fds[3] = {{ .fd = pipefd[0], .events = POLLIN, .revents = SHRT_MAX }};
    struct timespec zero = {0, 0}, timeout = {0, 25000000};
    assert(ppoll(fds, 1, &zero, mask) == 0 && fds[0].revents == 0);
    int64_t start = monotonic_ns();
    assert(ppoll(fds, 1, &timeout, mask) == 0 && fds[0].revents == 0);
    int64_t elapsed = monotonic_ns() - start;
    assert(elapsed >= 20000000 && elapsed < 1000000000);
    assert(timeout.tv_sec == 0 && timeout.tv_nsec == 25000000);
    assert(write(pipefd[1], "x", 1) == 1);
    assert(ppoll(fds, 1, &zero, mask) == 1 && (fds[0].revents & POLLIN));
    struct timespec huge = { .tv_sec = (time_t)INT_MAX / 1000 + 1 };
    assert(ppoll(fds, 1, &huge, mask) == 1);
    char byte;
    assert(read(pipefd[0], &byte, 1) == 1);
    fds[1] = fds[0];
    fds[2] = (struct pollfd){ .fd = -1, .events = POLLIN, .revents = SHRT_MAX };
    struct action action = { .fd = pipefd[1] };
    pthread_t sender;
    assert(pthread_create(&sender, NULL, act, &action) == 0);
    assert(ppoll(fds, 3, NULL, mask) == 2);
    assert((fds[0].revents & POLLIN) && (fds[1].revents & POLLIN) && !fds[2].revents);
    assert(pthread_join(sender, NULL) == 0);
    assert(read(pipefd[0], &byte, 1) == 1);
    int invalid = dup(pipefd[0]);
    assert(invalid >= 0 && close(invalid) == 0);
    fds[1] = (struct pollfd){ .fd = invalid, .events = 0 };
    assert(ppoll(fds, 2, &zero, mask) == 1 && fds[1].revents == POLLNVAL);
    assert(ppoll(NULL, 0, &zero, mask) == 0);
    assert(ppoll(NULL, 0, &timeout, mask) == 0);
    struct timespec bad[] = {{-1, 0}, {0, -1}, {0, 1000000000}};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        errno = 0;
        assert(ppoll(fds, 1, &bad[i], mask) == -1 && errno == EINVAL);
    }
    assert(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
}

static void hup_only(const sigset_t *mask)
{
    int pipefd[2];
    assert(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    assert(write(pipefd[1], "x", 1) == 1);
    struct pollfd fd = { .fd = pipefd[0], .events = 0 };
    struct timespec timeout = {0, 20000000};
    int64_t start = monotonic_ns();
    assert(ppoll(&fd, 1, &timeout, mask) == 0 && !fd.revents);
    assert(monotonic_ns() - start >= 15000000);
    struct action action = { .fd = pipefd[1], .close_fd = 1 };
    pthread_t sender;
    assert(pthread_create(&sender, NULL, act, &action) == 0);
    assert(ppoll(&fd, 1, NULL, mask) == 1);
    assert((fd.revents & POLLHUP) && !(fd.revents & POLLIN));
    assert(pthread_join(sender, NULL) == 0);
    assert(close(pipefd[0]) == 0);
    assert(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    assert(close(pipefd[0]) == 0);
    fd = (struct pollfd){ .fd = pipefd[1], .events = 0 };
    assert(ppoll(&fd, 1, &timeout, mask) == 1 && (fd.revents & POLLERR));
    assert(close(pipefd[1]) == 0);
}

static void maximum_vector(const sigset_t *mask)
{
    struct rlimit saved, limited;
    assert(getrlimit(RLIMIT_NOFILE, &saved) == 0);
    limited = saved;
    limited.rlim_cur = 64;
    assert(setrlimit(RLIMIT_NOFILE, &limited) == 0);
    int pipefd[2];
    assert(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    struct pollfd fds[64];
    for (int i = 0; i < 64; ++i)
        fds[i] = (struct pollfd){ .fd = (i % 2) ? -1 : pipefd[0], .events = POLLIN };
    struct action action = { .fd = pipefd[1] };
    pthread_t sender;
    assert(pthread_create(&sender, NULL, act, &action) == 0);
    assert(ppoll(fds, 64, NULL, mask) == 32);
    for (int i = 0; i < 64; ++i)
        assert(fds[i].revents == ((i % 2) ? 0 : POLLIN));
    assert(pthread_join(sender, NULL) == 0);
    assert(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
    assert(setrlimit(RLIMIT_NOFILE, &saved) == 0);
}

static void *high_descriptors(void *unused)
{
    (void)unused;
    sigset_t mask;
    sigemptyset(&mask);
    int pipefd[2];
    assert(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    int duplicates[FD_SETSIZE + 16], count = 0;
    /* Force both the caller fd and the private pselect notification above 1024. */
    do {
        assert(count < (int)(sizeof(duplicates) / sizeof(duplicates[0])));
        duplicates[count] = dup(pipefd[0]);
        assert(duplicates[count] >= 0);
    } while (duplicates[count++] < FD_SETSIZE + 8);
    struct pollfd fd = { .fd = duplicates[count - 1], .events = POLLIN };
    struct action action = { .fd = pipefd[1] };
    pthread_t sender;
    assert(pthread_create(&sender, NULL, act, &action) == 0);
    assert(ppoll(&fd, 1, NULL, &mask) == 1 && (fd.revents & POLLIN));
    assert(pthread_join(sender, NULL) == 0);
    for (int i = 0; i < count; ++i)
        assert(close(duplicates[i]) == 0);
    assert(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
    return NULL;
}

static volatile sig_atomic_t caught, handler_blocks_usr2;
static void handler(int signal)
{
    (void)signal;
    ++caught;
    sigset_t current;
    if (sigprocmask(SIG_SETMASK, NULL, &current) == 0)
        handler_blocks_usr2 = sigismember(&current, SIGUSR2);
}

static void signal_masks(void)
{
    struct sigaction action = { .sa_handler = handler, .sa_flags = SA_RESTART }, saved_action;
    sigemptyset(&action.sa_mask);
    assert(sigaction(SIGUSR1, &action, &saved_action) == 0);
    sigset_t one, saved_mask, temporary, current;
    sigemptyset(&one);
    sigaddset(&one, SIGUSR1);
    assert(pthread_sigmask(SIG_BLOCK, &one, &saved_mask) == 0);
    sigemptyset(&temporary);
    sigaddset(&temporary, SIGUSR2);
    int pipefd[2];
    assert(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    struct pollfd fd = { .fd = pipefd[0], .events = POLLIN };
    struct timespec zero = {0, 0}, finite = {0, 200000000};
    for (int i = 0; i < 2; ++i) {
        assert(pthread_kill(pthread_self(), SIGUSR1) == 0);
        errno = 0;
        assert(ppoll(&fd, 1, i ? &finite : &zero, &temporary) == -1 && errno == EINTR);
        assert(caught == i + 1 && handler_blocks_usr2 == 1);
        assert(pthread_sigmask(SIG_SETMASK, NULL, &current) == 0);
        assert(sigismember(&current, SIGUSR1) == 1);
        assert(sigismember(&current, SIGUSR2) == sigismember(&saved_mask, SIGUSR2));
    }
    /* Repeated targeted races catch the lost wake in sigmask()+poll(). */
    for (int i = 0; i < 64; ++i) {
        struct action signal = { .signal = 1, .target = pthread_self() };
        pthread_t sender;
        assert(pthread_create(&sender, NULL, act, &signal) == 0);
        errno = 0;
        assert(ppoll(&fd, 1, &finite, &temporary) == -1 && errno == EINTR);
        assert(pthread_join(sender, NULL) == 0);
        assert(caught == i + 3);
        assert(pthread_sigmask(SIG_SETMASK, NULL, &current) == 0);
        assert(sigismember(&current, SIGUSR1) == 1);
    }
    /* Process-wide kqueue signal filters must not interrupt the wrong thread. */
    struct action other = { .signal = 1, .other_thread = 1 };
    pthread_t sender;
    assert(pthread_create(&sender, NULL, act, &other) == 0);
    struct timespec timeout = {0, 25000000};
    assert(ppoll(&fd, 1, &timeout, &temporary) == 0);
    assert(pthread_join(sender, NULL) == 0 && caught == 67);
    assert(pthread_sigmask(SIG_SETMASK, &saved_mask, NULL) == 0);
    assert(sigaction(SIGUSR1, &saved_action, NULL) == 0);
    assert(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
}

static _Atomic int cancel_ready;
static void *cancel_wait(void *argument)
{
    int fd = *(int *)argument;
    sigset_t mask;
    sigemptyset(&mask);
    struct pollfd item = { .fd = fd, .events = POLLIN };
    struct timespec warmup = {0, 1000000};
    assert(ppoll(&item, 1, &warmup, &mask) == 0);
    atomic_store_explicit(&cancel_ready, 1, memory_order_release);
    ppoll(&item, 1, NULL, &mask);
    abort();
}

static int descriptor_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    assert(directory);
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)))
        if (entry->d_name[0] != '.')
            ++count;
    assert(closedir(directory) == 0);
    return count;
}

int main(void)
{
    alarm(15);
    sigset_t mask;
    sigemptyset(&mask);
    basic(NULL);
    basic(&mask);
    hup_only(NULL);
    hup_only(&mask);
    maximum_vector(&mask);
    int before = descriptor_count();
    pthread_t high;
    assert(pthread_create(&high, NULL, high_descriptors, NULL) == 0);
    assert(pthread_join(high, NULL) == 0 && descriptor_count() == before);
    signal_masks();
    int pipefd[2];
    assert(pipe2(pipefd, O_CLOEXEC | O_NONBLOCK) == 0);
    before = descriptor_count();
    pthread_t canceled;
    assert(pthread_create(&canceled, NULL, cancel_wait, &pipefd[0]) == 0);
    while (!atomic_load_explicit(&cancel_ready, memory_order_acquire))
        sched_yield();
    while (descriptor_count() < before + 4)
        sched_yield();
    assert(pthread_cancel(canceled) == 0);
    void *result;
    assert(pthread_join(canceled, &result) == 0 && result == PTHREAD_CANCELED);
    assert(descriptor_count() == before);
    assert(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
    puts("ppoll compatibility: PASS (zero/finite/infinite/ready/invalid/HUP/ERR/duplicates/limits/high-fd/atomic-mask/SA_RESTART/other-thread/cancellation)");
    return 0;
}
