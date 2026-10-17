#define _GNU_SOURCE
#include "vtest.h"
#include "gpud_core.h"
#include "vtest_protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LIMIT 4096u
#define MAX_WORDS (4u * 1024u * 1024u)
struct sync { uint64_t value; bool live; };
struct point { uint32_t id; uint64_t value; };
struct pending { struct pending *next; uint64_t fence; unsigned count; struct point points[]; };
struct waiting { struct waiting *next; int readfd, writefd; uint64_t deadline; bool any; unsigned count; struct point points[]; };
struct client {
    struct client *next;
    int fd;
    uint32_t ctx, resource, sync_count;
    uint64_t fence;
    char name[256];
    bool created, version;
    pthread_mutex_t mutex;
    struct sync syncs[LIMIT];
    struct pending *pending;
    struct waiting *waiting;
    unsigned pending_count, waiting_count;
};
static pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t clients_cond = PTHREAD_COND_INITIALIZER;
static struct client *clients;
static int listener = -1;
static pthread_t accept_thread;
static bool accepting;
static char socket_path[sizeof(((struct sockaddr_un *)0)->sun_path)];
static uint64_t now_ms(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
static bool output(int fd, const void *buf, size_t size)
{
    while (size) {
        ssize_t n = send(fd, buf, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        buf = (const char *)buf + n; size -= n;
    }
    return true;
}
static bool reply(struct client *c, uint32_t cmd, const void *data, uint32_t words)
{
    uint32_t h[] = {words, cmd};
    return output(c->fd, h, sizeof(h)) && (!words || output(c->fd, data, words * 4));
}
static bool send_fd(struct client *c, int fd)
{
    char byte = 0, control[CMSG_SPACE(sizeof(int))];
    struct iovec iov = { &byte, 1 };
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1, .msg_control = control, .msg_controllen = sizeof(control) };
    memset(control, 0, sizeof(control));
    struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level = SOL_SOCKET; cm->cmsg_type = SCM_RIGHTS; cm->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cm), &fd, sizeof(fd));
    ssize_t n;
    do { n = sendmsg(c->fd, &msg, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    return n == 1;
}
static uint64_t u64(const uint32_t *p) { return p[0] | ((uint64_t)p[1] << 32); }
static bool valid_sync(struct client *c, uint32_t id) { return id && id < c->sync_count && c->syncs[id].live; }
static void check_waits(struct client *c)
{
    struct waiting **p = &c->waiting;
    uint64_t now = now_ms();
    while (*p) {
        struct waiting *w = *p;
        bool done = !w->any;
        for (unsigned i = 0; i < w->count; i++) {
            bool ready = c->syncs[w->points[i].id].value >= w->points[i].value;
            if (w->any) done |= ready; else done &= ready;
        }
        if (!done && now < w->deadline) { p = &w->next; continue; }
        /* Keep our read end until after writing, so a dead client cannot raise
         * SIGPIPE in the compositor. Exactly one byte is ever written. */
        if (done) { char byte = 1; ssize_t n; do { n = write(w->writefd, &byte, 1); } while (n < 0 && errno == EINTR); }
        close(w->writefd); close(w->readfd);
        *p = w->next; free(w); c->waiting_count--;
    }
}
static void retire(void *opaque, uint32_t ring, uint64_t fence)
{
    (void)ring;
    struct client *c = opaque;
    pthread_mutex_lock(&c->mutex);
    struct pending **p = &c->pending;
    while (*p && (*p)->fence != fence) p = &(*p)->next;
    if (*p) {
        struct pending *b = *p;
        for (unsigned i = 0; i < b->count; i++)
            c->syncs[b->points[i].id].value = b->points[i].value;
        *p = b->next; free(b); c->pending_count--;
        check_waits(c);
    }
    pthread_mutex_unlock(&c->mutex);
}
static bool submit(struct client *c, uint32_t *p, uint32_t n)
{
    if (!c->ctx || !n || p[0] > (n - 1) / 8) return false;
    for (uint32_t i = 0; i < p[0]; i++) {
        uint32_t *b = &p[1 + i * 8];
        if (b[0] != VCMD_SUBMIT_CMD2_FLAG_RING_IDX || b[5] >= 64 || b[6] || b[7] ||
            b[1] > n || b[2] > n - b[1] || b[3] > n || b[4] > (n - b[3]) / 3 || b[4] > LIMIT) return false;
        if (b[2] && !gpud_core_submit(c->ctx, &p[b[1]], b[2] * 4)) return false;
        if (!b[4]) continue;
        struct pending *pending = calloc(1, sizeof(*pending) + b[4] * sizeof(struct point));
        if (!pending) return false;
        pthread_mutex_lock(&c->mutex);
        bool ok = c->pending_count < LIMIT && c->fence != UINT64_MAX;
        pending->count = b[4]; pending->fence = ++c->fence;
        for (unsigned j = 0; j < b[4]; j++) {
            uint32_t *s = &p[b[3] + j * 3];
            if (!valid_sync(c, s[0])) { ok = false; break; }
            pending->points[j] = (struct point){ s[0], u64(&s[1]) };
        }
        if (ok) { pending->next = c->pending; c->pending = pending; c->pending_count++; }
        pthread_mutex_unlock(&c->mutex);
        if (!ok) { free(pending); return false; }
        if (!gpud_core_fence(c->ctx, 0, b[5], pending->fence)) return false;
    }
    return true;
}
static bool command(struct client *c, uint32_t cmd, uint32_t *p, uint32_t n)
{
    uint32_t r[2] = {0};
    if (cmd == VCMD_CREATE_RENDERER) {
        if (c->created || !n || n > sizeof(c->name) || ((char *)p)[n-1]) return false;
        memcpy(c->name, p, n); c->created = true; return true;
    }
    if (!c->created) return false;
    switch (cmd) {
    case VCMD_PING_PROTOCOL_VERSION: return !n && reply(c, cmd, NULL, 0);
    case VCMD_RESOURCE_BUSY_WAIT: return n == 2 && !p[0] && !p[1] && reply(c, cmd, r, 1);
    case VCMD_PROTOCOL_VERSION:
        if (n != 1 || p[0] != 4) return false;
        c->version = true; return reply(c, cmd, p, 1);
    case VCMD_GET_PARAM:
        if (n != 1 || !c->version) return false;
        r[0] = p[0] == VCMD_PARAM_MAX_TIMELINE_COUNT;
        r[1] = p[0] == VCMD_PARAM_MAX_TIMELINE_COUNT ? 64 : 0;
        return reply(c, cmd, r, 2);
    case VCMD_GET_CAPSET: {
        if (n != 2) return false;
        size_t bytes = gpud_core_capset(NULL);
        uint32_t *caps = calloc(1, bytes + 4);
        if (!caps) return false;
        caps[0] = p[0] == 4 && p[1] == 0;
        if (caps[0]) gpud_core_capset(caps + 1);
        bool ok = reply(c, cmd, caps, caps[0] ? 1 + bytes / 4 : 1);
        free(caps); return ok;
    }
    case VCMD_CONTEXT_INIT:
        if (n != 1 || p[0] != 4 || c->ctx || !c->version) return false;
        c->ctx = gpud_core_context_create(c->name, retire, c); return c->ctx != 0;
    case VCMD_RESOURCE_CREATE_BLOB: {
        if (n != 6 || !c->ctx || p[0] != VCMD_BLOB_TYPE_HOST3D || c->resource == LIMIT) return false;
        uint32_t id = ++c->resource, map;
        int fd = gpud_core_blob(c->ctx, id, u64(p+4), u64(p+2), p[1], &map);
        if (fd < 0) return false;
        bool ok = reply(c, cmd, &id, 1) && send_fd(c, fd);
        close(fd); return ok;
    }
    case VCMD_RESOURCE_UNREF:
        if (n != 1 || !c->ctx || !p[0] || p[0] > c->resource) return false;
        gpud_core_unref(c->ctx, p[0]); return true;
    case VCMD_SUBMIT_CMD2: return submit(c, p, n);
    case VCMD_SYNC_CREATE:
        if (n != 2 || c->sync_count == LIMIT) return false;
        pthread_mutex_lock(&c->mutex);
        r[0] = c->sync_count++; c->syncs[r[0]] = (struct sync){u64(p), true};
        pthread_mutex_unlock(&c->mutex); return reply(c, cmd, r, 1);
    case VCMD_SYNC_UNREF: case VCMD_SYNC_READ: case VCMD_SYNC_WRITE: {
        if (n != (cmd == VCMD_SYNC_WRITE ? 3u : 1u)) return false;
        pthread_mutex_lock(&c->mutex);
        bool ok = valid_sync(c, p[0]);
        uint64_t value = 0;
        if (ok) {
            if (cmd == VCMD_SYNC_WRITE) { c->syncs[p[0]].value = u64(p+1); check_waits(c); }
            if (cmd == VCMD_SYNC_UNREF) c->syncs[p[0]].live = false;
            value = c->syncs[p[0]].value;
        }
        pthread_mutex_unlock(&c->mutex);
        return ok && (cmd != VCMD_SYNC_READ || reply(c, cmd, &value, 2));
    }
    case VCMD_SYNC_WAIT: {
        if (n < 2 || (n-2)%3 || (n-2)/3 > LIMIT || p[0] & ~VCMD_SYNC_WAIT_FLAG_ANY) return false;
        unsigned count = (n-2)/3;
        struct waiting *w = calloc(1, sizeof(*w) + count * sizeof(struct point));
        if (!w) return false;
        int fds[2];
        if (pipe(fds)) { free(w); return false; }
        w->readfd = fds[0]; w->writefd = fds[1];
        fcntl(fds[0], F_SETFD, FD_CLOEXEC); fcntl(fds[1], F_SETFD, FD_CLOEXEC);
        w->any = p[0] & VCMD_SYNC_WAIT_FLAG_ANY; w->count = count;
        w->deadline = p[1] == UINT32_MAX ? UINT64_MAX : now_ms() + p[1];
        pthread_mutex_lock(&c->mutex);
        bool ok = c->waiting_count < LIMIT;
        for (unsigned i = 0; i < count; i++) {
            uint32_t *s = &p[2+i*3];
            if (!valid_sync(c, s[0])) { ok = false; break; }
            w->points[i] = (struct point){s[0], u64(s+1)};
        }
        if (ok) {
            /* Send before publishing: an immediate callback must not close the fd. */
            ok = reply(c, cmd, NULL, 0) && send_fd(c, fds[0]);
            if (ok) { w->next = c->waiting; c->waiting = w; c->waiting_count++; check_waits(c); }
        }
        pthread_mutex_unlock(&c->mutex);
        if (!ok) { close(fds[0]); close(fds[1]); free(w); }
        return ok;
    }
    default: return false;
    }
}
static void *client_main(void *opaque)
{
    struct client *c = opaque;
    uint32_t h[2], *payload = NULL;
    size_t got = 0, needed = sizeof(h);
    bool header = true;
    for (;;) {
        struct pollfd pfd = {c->fd, POLLIN, 0};
        int ready = poll(&pfd, 1, 20);
        pthread_mutex_lock(&c->mutex); check_waits(c); pthread_mutex_unlock(&c->mutex);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0 || (pfd.revents & (POLLERR|POLLHUP|POLLNVAL))) break;
        if (!ready) continue;
        char *dst = header ? (char *)h : (char *)payload;
        ssize_t size = recv(c->fd, dst + got, needed - got, 0);
        if (size < 0 && errno == EINTR) continue;
        if (size <= 0) break;
        got += size;
        if (got < needed) continue;
        if (header) {
            if (h[0] > MAX_WORDS) break;
            needed = h[1] == VCMD_CREATE_RENDERER ? h[0] : (size_t)h[0] * 4;
            payload = needed ? malloc(needed) : NULL;
            if (needed && !payload) break;
            got = 0; header = false;
            if (needed) continue;
        }
        if (!command(c, h[1], payload, h[0])) { fprintf(stderr, "gpud vtest disconnect: command=%u length=%u\n", h[1], h[0]); break; }
        free(payload); payload = NULL; header = true; got = 0; needed = sizeof(h);
    }
    free(payload);
    if (c->ctx) gpud_core_context_destroy(c->ctx);
    while (c->pending) { struct pending *p = c->pending; c->pending = p->next; free(p); }
    while (c->waiting) { struct waiting *w = c->waiting; c->waiting = w->next; close(w->readfd); close(w->writefd); free(w); }
    pthread_mutex_lock(&clients_lock);
    struct client **p = &clients; while (*p != c) p = &(*p)->next;
    *p = c->next; close(c->fd); pthread_cond_broadcast(&clients_cond);
    pthread_mutex_unlock(&clients_lock);
    pthread_mutex_destroy(&c->mutex); free(c); return NULL;
}
static void *accept_main(void *unused)
{
    (void)unused;
    for (;;) {
        int fd = accept(listener, NULL, NULL);
        if (fd < 0) { if (errno == EINTR) continue; break; }
        fcntl(fd, F_SETFD, FD_CLOEXEC);
        struct client *c = calloc(1, sizeof(*c));
        if (!c) { close(fd); continue; }
        c->fd = fd; c->sync_count = 1; pthread_mutex_init(&c->mutex, NULL);
        pthread_mutex_lock(&clients_lock);
        c->next = clients; clients = c;
        pthread_t thread;
        int error = pthread_create(&thread, NULL, client_main, c);
        if (error) { clients = c->next; close(fd); pthread_mutex_destroy(&c->mutex); free(c); }
        else pthread_detach(thread);
        pthread_mutex_unlock(&clients_lock);
    }
    return NULL;
}
int gpud_vtest_start(const char *path)
{
    if (strlen(path) >= sizeof(socket_path)) return -ENAMETOOLONG;
    strcpy(socket_path, path);
    listener = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return -errno;
    fcntl(listener, F_SETFD, FD_CLOEXEC);
    struct sockaddr_un addr = {.sun_family = AF_UNIX}; strcpy(addr.sun_path, path);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) || listen(listener, 32)) {
        int error = errno; close(listener); listener = -1; return -error;
    }
    int error = pthread_create(&accept_thread, NULL, accept_main, NULL);
    if (error) { close(listener); listener = -1; unlink(path); return -error; }
    accepting = true; return 0;
}
void gpud_vtest_shutdown(void)
{
    if (accepting) {
        shutdown(listener, SHUT_RDWR); pthread_join(accept_thread, NULL);
        close(listener); listener = -1; accepting = false; unlink(socket_path);
    }
    pthread_mutex_lock(&clients_lock);
    for (struct client *c = clients; c; c = c->next) shutdown(c->fd, SHUT_RDWR);
    pthread_mutex_unlock(&clients_lock);
}
int gpud_vtest_drain(unsigned ms)
{
    struct timespec until; clock_gettime(CLOCK_REALTIME, &until);
    until.tv_sec += ms / 1000; until.tv_nsec += (ms % 1000) * 1000000L;
    if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&clients_lock);
    int error = 0;
    while (clients && !error) error = pthread_cond_timedwait(&clients_cond, &clients_lock, &until);
    bool done = !clients; pthread_mutex_unlock(&clients_lock);
    return done ? 0 : -EBUSY;
}
