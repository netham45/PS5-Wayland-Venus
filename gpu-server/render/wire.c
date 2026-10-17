#include "wire.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int gpud_wire_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD);
    return flags < 0 ? -1 : fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static bool transfer(int fd, void *data, size_t size, bool sending)
{
    char *p = data;
    while (size) {
        ssize_t n = sending ? send(fd, p, size, MSG_NOSIGNAL) : recv(fd, p, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        size -= (size_t)n;
    }
    return true;
}

bool gpud_wire_send(struct gpud_wire *w, const void *data, size_t size,
                    const int *fds, unsigned count)
{
    if (!size || size > GPUD_RENDER_MAX_FRAME || count > GPUD_RENDER_MAX_FDS)
        return false;
    uint8_t header[4] = { size, size >> 8, size >> 16, size >> 24 };
    struct iovec iov = { .iov_base = w->stream ? (void *)header : (void *)data,
                         .iov_len = w->stream ? sizeof(header) : size };
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int) * GPUD_RENDER_MAX_FDS)]; } control;
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };
    if (count) {
        memset(&control, 0, sizeof(control));
        msg.msg_control = control.bytes;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * count);
        struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * count);
        memcpy(CMSG_DATA(c), fds, sizeof(int) * count);
    }
    ssize_t n;
    do { n = sendmsg(w->fd, &msg, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    if (n <= 0) return false;
    if (!w->stream) return (size_t)n == size;
    if ((size_t)n < sizeof(header) &&
        !transfer(w->fd, header + n, sizeof(header) - (size_t)n, true)) return false;
    return transfer(w->fd, (void *)data, size, true);
}

/* Every receive uses recvmsg: unexpected rights in fragmented headers or
 * payload are rejected and closed rather than silently discarded/leaked. */
static ssize_t receive_piece(int fd, void *data, size_t size, int *fds,
                             unsigned *count, bool allow_fds)
{
    struct iovec iov = { .iov_base = data, .iov_len = size };
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int) * 16)]; } control;
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1,
                         .msg_control = control.bytes, .msg_controllen = sizeof(control) };
    ssize_t n;
    do { n = recvmsg(fd, &msg, 0); } while (n < 0 && errno == EINTR);
    if (n <= 0) return -1;
    bool valid = !(msg.msg_flags & (MSG_TRUNC | MSG_CTRUNC));
    for (struct cmsghdr *c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c)) {
        if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS ||
            c->cmsg_len < CMSG_LEN(0)) { valid = false; continue; }
        size_t bytes = c->cmsg_len - CMSG_LEN(0);
        if (bytes % sizeof(int)) valid = false;
        int *got = (int *)CMSG_DATA(c);
        for (size_t i = 0; i < bytes / sizeof(int); i++) {
            if (!allow_fds || *count == GPUD_RENDER_MAX_FDS ||
                gpud_wire_cloexec(got[i]) < 0) {
                close(got[i]);
                valid = false;
            } else fds[(*count)++] = got[i];
        }
    }
    return valid ? n : -1;
}

ssize_t gpud_wire_recv(struct gpud_wire *w, void *data, size_t capacity,
                       int *fds, unsigned *count)
{
    *count = 0;
    uint8_t header[4];
    /* Reading only byte zero prevents coalescing from hiding misplaced rights. */
    size_t expected = w->stream ? 1 : capacity;
    void *dest = w->stream ? (void *)header : data;
    ssize_t n = receive_piece(w->fd, dest, expected, fds, count, true);
    if (n < 0) goto fail;
    if (!w->stream) return n;
    size_t done = (size_t)n;
    while (done < sizeof(header)) {
        n = receive_piece(w->fd, header + done, sizeof(header) - done, fds, count, false);
        if (n < 0) goto fail;
        done += (size_t)n;
    }
    expected = (uint32_t)header[0] | (uint32_t)header[1] << 8 |
               (uint32_t)header[2] << 16 | (uint32_t)header[3] << 24;
    if (!expected || expected > capacity || expected > GPUD_RENDER_MAX_FRAME) goto fail;
    done = 0;
    while (done < expected) {
        n = receive_piece(w->fd, (char *)data + done, expected - done, fds, count, false);
        if (n < 0) goto fail;
        done += (size_t)n;
    }
    return (ssize_t)expected;
fail:
    for (unsigned i = 0; i < *count; i++) close(fds[i]);
    *count = 0;
    return -1;
}
