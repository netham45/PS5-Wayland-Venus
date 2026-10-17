#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

/* Do not call the interposable poll symbol: epoll-shim's poll calls ppoll. */
static pthread_once_t poll_once = PTHREAD_ONCE_INIT;
static int (*native_poll)(struct pollfd *, nfds_t, int);
#ifdef __PROSPERO__
static void *native_poll_module;
#endif

static void resolve_poll(void)
{
#ifdef __PROSPERO__
    /* RTLD_NEXT is unsupported in the SDK; RTLD_DEFAULT can hit interposers.
     * An explicit resident-kernel handle works in both payloads and ICD DSOs. */
    /* The lookup owner has the same process lifetime as the cached function.
     * Do not close it while the function pointer remains callable. */
    native_poll_module = dlopen("/system/common/lib/libkernel_sys.sprx", RTLD_NOW | RTLD_LOCAL);
    if (native_poll_module)
        native_poll = dlsym(native_poll_module, "poll");
#else
    native_poll = dlsym(RTLD_NEXT, "poll");
#endif
}

static int timeout_ms(const struct timespec *timeout)
{
    if (timeout->tv_sec > INT_MAX / 1000)
        return INT_MAX;
    int64_t ms = (int64_t)timeout->tv_sec * 1000 +
                 (timeout->tv_nsec + 999999) / 1000000;
    return ms > INT_MAX ? INT_MAX : (int)ms;
}

static int unmasked_poll(struct pollfd *fds, nfds_t nfds,
                         const struct timespec *timeout)
{
    if (!timeout)
        return native_poll(fds, nfds, -1);
    int ms = timeout_ms(timeout);
    if (ms < INT_MAX)
        return native_poll(fds, nfds, ms);

    /* A large finite timeout must not become an infinite wait or overflow. */
    struct timespec start, now, remaining = *timeout;
    if (clock_gettime(CLOCK_MONOTONIC, &start) < 0)
        return -1;
    for (;;) {
        int result = native_poll(fds, nfds, timeout_ms(&remaining));
        if (result != 0)
            return result;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
            return -1;
        time_t sec = now.tv_sec - start.tv_sec;
        long ns = now.tv_nsec - start.tv_nsec;
        if (ns < 0) { --sec; ns += 1000000000; }
        if (sec > timeout->tv_sec ||
            (sec == timeout->tv_sec && ns >= timeout->tv_nsec))
            return 0;
        remaining.tv_sec = timeout->tv_sec - sec;
        remaining.tv_nsec = timeout->tv_nsec - ns;
        if (remaining.tv_nsec < 0) {
            --remaining.tv_sec;
            remaining.tv_nsec += 1000000000;
        }
    }
}

/*
 * Native pselect supplies the atomic thread-mask swap, but fd_sets cannot
 * express poll's HUP/ERR-only subscriptions, POLLNVAL or arbitrary fd numbers.
 * A helper polls the complete vector and wakes pselect through a pipe. It is
 * always joined before return, including timeout, signal and cancellation:
 * neither threads nor TLS destructors may survive an ICD dlclose.
 * The common unmasked path never creates threads, copies fds or allocates.
 */
struct poll_worker {
    pthread_t thread;
    int started, cancel[2], notify[2], result, error;
    struct pollfd *fds;
    nfds_t count;
    unsigned long *select_bits;
    fd_set small_select;
    struct pollfd small[17];
};

static void wake(int fd)
{
    char byte = 1;
    ssize_t result;
    do { result = write(fd, &byte, 1); } while (result < 0 && errno == EINTR);
    /* At most one cancellation byte is outstanding; EAGAIN is already awake. */
}

static void *worker_main(void *argument)
{
    struct poll_worker *worker = argument;
    worker->result = native_poll(worker->fds, worker->count, -1);
    worker->error = errno;
    wake(worker->notify[1]);
    return NULL;
}

static void cleanup_worker(void *argument)
{
    struct poll_worker *worker = argument;
    int cancel_state;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cancel_state);
    if (worker->started) {
        wake(worker->cancel[1]);
        pthread_join(worker->thread, NULL);
    }
    for (int i = 0; i != 2; ++i) {
        if (worker->cancel[i] >= 0) close(worker->cancel[i]);
        if (worker->notify[i] >= 0) close(worker->notify[i]);
    }
    if (worker->fds != worker->small)
        free(worker->fds);
    if (worker->select_bits != (void *)&worker->small_select)
        free(worker->select_bits);
    pthread_setcancelstate(cancel_state, NULL);
}

static int compare_fd(const void *left, const void *right)
{
    int a = ((const struct pollfd *)left)->fd;
    int b = ((const struct pollfd *)right)->fd;
    return (a > b) - (a < b);
}

static int prepare_worker(struct poll_worker *worker, struct pollfd *fds, nfds_t nfds,
                           int *compressed)
{
    if ((uintmax_t)nfds >= SIZE_MAX / sizeof(struct pollfd)) {
        errno = ENOMEM;
        return -1;
    }
    size_t needed = (size_t)nfds + 1;
    if (needed > sizeof(worker->small) / sizeof(worker->small[0])) {
        worker->fds = malloc(needed * sizeof(*worker->fds));
        if (!worker->fds)
            return -1;
    }
    if (pipe2(worker->cancel, O_CLOEXEC | O_NONBLOCK) < 0 ||
        pipe2(worker->notify, O_CLOEXEC | O_NONBLOCK) < 0)
        return -1;
    /* The native syscall accepts a larger bitmap than the C fd_set typedef. */
    unsigned bits_per_word = CHAR_BIT * sizeof(unsigned long);
    size_t word = (size_t)worker->notify[0] / bits_per_word;
    if (worker->notify[0] >= FD_SETSIZE) {
        worker->select_bits = calloc(word + 1, sizeof(*worker->select_bits));
        if (!worker->select_bits)
            return -1;
    }
    worker->select_bits[word] = 1UL << (worker->notify[0] % bits_per_word);
    memcpy(worker->fds, fds, (size_t)nfds * sizeof(*fds));
    worker->fds[nfds] = (struct pollfd){ .fd = worker->cancel[0], .events = POLLIN };
    worker->count = (nfds_t)needed;
    struct rlimit limit;
    if (getrlimit(RLIMIT_NOFILE, &limit) < 0)
        return -1;
    *compressed = (uintmax_t)needed > limit.rlim_cur;
    if (*compressed) {
        /* Adding the private wake fd must not invalidate a maximum-size vector. */
        qsort(worker->fds, needed, sizeof(*worker->fds), compare_fd);
        worker->count = 0;
        for (size_t i = 0; i < needed; ++i) {
            struct pollfd current = worker->fds[i];
            if (current.fd < 0)
                continue;
            if (worker->count && worker->fds[worker->count - 1].fd == current.fd)
                worker->fds[worker->count - 1].events |= current.events;
            else
                worker->fds[worker->count++] = current;
        }
    }
    return 0;
}

static int masked_poll(struct pollfd *fds, nfds_t nfds,
                       const struct timespec *timeout, const sigset_t *mask)
{
    if (!nfds)
        return pselect(0, NULL, NULL, NULL, timeout, mask);
    int result = native_poll(fds, nfds, 0);
    if (result < 0)
        return result;
    if (result || (timeout && !timeout->tv_sec && !timeout->tv_nsec)) {
        const struct timespec zero = {0, 0};
        if (pselect(0, NULL, NULL, NULL, &zero, mask) < 0)
            return -1;
        return result;
    }
    struct poll_worker worker = { .cancel = {-1, -1}, .notify = {-1, -1} };
    worker.fds = worker.small;
    worker.select_bits = (void *)&worker.small_select;
    int compressed, error;
    if (prepare_worker(&worker, fds, nfds, &compressed) < 0) {
        error = errno;
        cleanup_worker(&worker);
        errno = error;
        return -1;
    }
    pthread_attr_t attributes;
    error = pthread_attr_init(&attributes);
    if (error)
        goto failed;
    error = pthread_attr_setstacksize(&attributes, 64 * 1024);
    if (error) {
        pthread_attr_destroy(&attributes);
        goto failed;
    }
    /* Helpers must never steal a process-directed user signal. */
    sigset_t all, saved;
    sigfillset(&all);
    error = pthread_sigmask(SIG_SETMASK, &all, &saved);
    if (error) {
        pthread_attr_destroy(&attributes);
        goto failed;
    }
    error = pthread_create(&worker.thread, &attributes, worker_main, &worker);
    worker.started = !error;
    pthread_attr_destroy(&attributes);
    pthread_cleanup_push(cleanup_worker, &worker);
    int restore_error = pthread_sigmask(SIG_SETMASK, &saved, NULL);
    if (!error)
        error = restore_error;
    if (error) {
        result = -1;
    } else {
        result = pselect(worker.notify[0] + 1, (void *)worker.select_bits,
                         NULL, NULL, timeout, mask);
        error = errno;
        if (result <= 0)
            wake(worker.cancel[1]);
        pthread_join(worker.thread, NULL);
        worker.started = 0;
        if (result > 0) {
            result = worker.result;
            error = worker.error;
            if (result >= 0) {
                result = 0;
                for (nfds_t i = 0; i < nfds; ++i) {
                    short events = 0;
                    if (!compressed)
                        events = worker.fds[i].revents;
                    else if (fds[i].fd >= 0) {
                        struct pollfd key = { .fd = fds[i].fd };
                        struct pollfd *entry = bsearch(&key, worker.fds, worker.count,
                                                       sizeof(*entry), compare_fd);
                        if (entry)
                            events = entry->revents &
                                (fds[i].events | POLLERR | POLLHUP | POLLNVAL);
                    }
                    fds[i].revents = events;
                    result += events != 0;
                }
            }
        }
    }
    pthread_cleanup_pop(1);
    errno = error;
    return result;

failed:
    cleanup_worker(&worker);
    errno = error;
    return -1;
}

int __wrap_ppoll(struct pollfd *fds, nfds_t nfds,
                 const struct timespec *timeout, const sigset_t *mask)
{
    if (timeout && (timeout->tv_sec < 0 || timeout->tv_nsec < 0 ||
                    timeout->tv_nsec >= 1000000000)) {
        errno = EINVAL;
        return -1;
    }
    pthread_once(&poll_once, resolve_poll);
    if (!native_poll) { errno = ENOSYS; return -1; }
    return mask ? masked_poll(fds, nfds, timeout, mask) : unmasked_poll(fds, nfds, timeout);
}
