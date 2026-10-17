#define _POSIX_C_SOURCE 200809L
#include "ps5log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>

#define RING_LINES 256
#define LINE_BYTES 1024
#define MAX_CLIENTS 16
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "fatal logger requires lock-free fd slots");
static _Atomic int emergency_fds[MAX_CLIENTS]; /* descriptor+1; zero is empty */
static struct client *active_clients;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char ring[RING_LINES][LINE_BYTES];
static size_t lengths[RING_LINES];
static uint64_t next_line;
static char log_name[64] = "ps5wl";
static int listener = -1;

struct client {
    int fd;
    uint64_t next;
    size_t offset;
};

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static int nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int suppress_sigpipe(int fd) {
#if defined(SO_NOSIGPIPE)
    int one = 1;
    return setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#else
    (void)fd;
    return 0;
#endif
}

static void disconnect_client(struct client *client) {
    atomic_store_explicit(&emergency_fds[client - active_clients], 0, memory_order_relaxed);
    close(client->fd);
    client->fd = -1;
}

static void *serve(void *unused) {
    (void)unused;
    struct client clients[MAX_CLIENTS];
    active_clients = clients;
    for (size_t i = 0; i < MAX_CLIENTS; ++i)
        clients[i].fd = -1;
    for (;;) {
        struct pollfd incoming = { .fd = listener, .events = POLLIN };
        int ready = poll(&incoming, 1, 10);
        if (ready > 0 && (incoming.revents & POLLIN)) {
            /* Bounded acceptance keeps a busy listener from starving delivery. */
            for (size_t accepted = 0; accepted < MAX_CLIENTS; ++accepted) {
                int fd = accept(listener, NULL, NULL);
                if (fd < 0)
                    break;
                if (nonblocking(fd) < 0 || suppress_sigpipe(fd) < 0) {
                    close(fd);
                    continue;
                }
                size_t i;
                for (i = 0; i < MAX_CLIENTS && clients[i].fd >= 0; ++i) {}
                if (i == MAX_CLIENTS) {
                    close(fd);
                    continue;
                }
                pthread_mutex_lock(&lock);
                clients[i].next = next_line > RING_LINES ? next_line - RING_LINES : 0;
                clients[i].offset = 0;
                clients[i].fd = fd;
                atomic_store_explicit(&emergency_fds[i], fd + 1, memory_order_relaxed);
                pthread_mutex_unlock(&lock);
            }
        }
        for (size_t i = 0; i < MAX_CLIENTS; ++i) {
            struct client *client = &clients[i];
            if (client->fd < 0)
                continue;
            pthread_mutex_lock(&lock);
            /* Never splice a partially sent overwritten line into a new line. */
            if (next_line - client->next > RING_LINES) {
                disconnect_client(client);
                pthread_mutex_unlock(&lock);
                continue;
            }
            while (client->next < next_line) {
                size_t slot = client->next % RING_LINES;
                int flags = 0;
#ifdef MSG_NOSIGNAL
                flags |= MSG_NOSIGNAL;
#endif
                ssize_t sent = send(client->fd, ring[slot] + client->offset,
                                    lengths[slot] - client->offset, flags);
                if (sent < 0 && errno == EINTR)
                    continue;
                if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
                    break;
                if (sent <= 0) {
                    disconnect_client(client);
                    break;
                }
                client->offset += (size_t)sent;
                if (client->offset == lengths[slot]) {
                    client->next++;
                    client->offset = 0;
                }
            }
            pthread_mutex_unlock(&lock);
            if (client->fd >= 0) {
                char byte;
                ssize_t n = recv(client->fd, &byte, 1, 0);
                if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                    disconnect_client(client);
            }
        }
    }
    return NULL;
}

int ps5log_init(const char *name, uint16_t port) {
    pthread_mutex_lock(&lock);
    if (listener >= 0) {
        pthread_mutex_unlock(&lock);
        errno = EALREADY;
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        goto fail;
    int one = 1;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0 ||
        nonblocking(fd) < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(fd, MAX_CLIENTS) < 0)
        goto fail;
#if !defined(MSG_NOSIGNAL) && !defined(SO_NOSIGPIPE)
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        goto fail;
#endif
    listener = fd;
    pthread_attr_t attr;
    int rc = pthread_attr_init(&attr);
    if (rc) {
        errno = rc;
        listener = -1;
        goto fail;
    }
    rc = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    if (!rc)
        rc = pthread_create(&thread, &attr, serve, NULL);
    pthread_attr_destroy(&attr);
    if (rc) {
        errno = rc;
        listener = -1;
        goto fail;
    }
    snprintf(log_name, sizeof(log_name), "%s", name && *name ? name : "ps5wl");
    for (char *p = log_name; *p; ++p)
        if (*p == '\n' || *p == '\r') *p = '_';
    pthread_mutex_unlock(&lock);
    return 0;
fail:;
    int saved = errno;
    if (fd >= 0)
        close(fd);
    pthread_mutex_unlock(&lock);
    errno = saved;
    return -1;
}

void ps5log(const char *fmt, ...) {
    char message[4096];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    if (n < 0)
        return;
    pthread_mutex_lock(&lock);
    const char *start = message;
    do {
        const char *end = strchr(start, '\n');
        size_t remaining = end ? (size_t)(end - start) : strlen(start);
        /* Split oversized physical lines rather than losing their suffixes. */
        do {
            size_t slot = next_line % RING_LINES;
            int prefix = snprintf(ring[slot], LINE_BYTES, "[%" PRIu64 " ms] %s: ",
                                  monotonic_ms(), log_name);
            size_t count = LINE_BYTES - (size_t)prefix - 2;
            if (count > remaining)
                count = remaining;
            memcpy(ring[slot] + prefix, start, count);
            lengths[slot] = (size_t)prefix + count + 1;
            ring[slot][lengths[slot] - 1] = '\n';
            ring[slot][lengths[slot]] = '\0';
            fwrite(ring[slot], 1, lengths[slot], stderr);
            next_line++;
            start += count;
            remaining -= count;
        } while (remaining);
        if (!end)
            break;
        start = end + 1;
    } while (*start);
    fflush(stderr);
    pthread_mutex_unlock(&lock);
}

void ps5log_flush_wait(int ms) {
    if (ms <= 0)
        return;
    struct timespec delay = { .tv_sec = ms / 1000, .tv_nsec = (ms % 1000) * 1000000L };
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {}
}

void ps5log_emergency_write(const char *message, size_t length) {
    (void)write(STDERR_FILENO, message, length);
    for (size_t i = 0; i < MAX_CLIENTS; ++i) {
        int fd = atomic_load_explicit(&emergency_fds[i], memory_order_relaxed) - 1;
        if (fd < 0) continue;
        int flags = 0; /* Every published client descriptor is O_NONBLOCK. */
#ifdef MSG_NOSIGNAL
        flags |= MSG_NOSIGNAL;
#endif
        (void)send(fd, message, length, flags);
    }
}