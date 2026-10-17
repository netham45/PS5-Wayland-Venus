#define _GNU_SOURCE
#include "wire.h"
#include <assert.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

static void pair(struct gpud_wire *a, struct gpud_wire *b, bool stream)
{
    int fds[2];
    assert(socketpair(AF_UNIX, stream ? SOCK_STREAM : SOCK_SEQPACKET, 0, fds) == 0);
    *a = (struct gpud_wire){ fds[0], stream };
    *b = (struct gpud_wire){ fds[1], stream };
}
static void roundtrip(bool stream)
{
    struct gpud_wire a, b; pair(&a, &b, stream);
    int fd = memfd_create("wire-test", 0);
    assert(fd >= 0);
    const char payload[] = "ordinary SHM descriptor";
    assert(gpud_wire_send(&a, payload, sizeof(payload), &fd, 1));
    assert(gpud_wire_send(&a, payload, sizeof(payload), NULL, 0));
    int fds[GPUD_RENDER_MAX_FDS]; unsigned count; char data[256];
    assert(gpud_wire_recv(&b, data, sizeof(data), fds, &count) == sizeof(payload));
    assert(count == 1 && !memcmp(data, payload, sizeof(payload)));
    assert(fcntl(fds[0], F_GETFD) & FD_CLOEXEC);
    close(fds[0]); close(fd);
    assert(gpud_wire_recv(&b, data, sizeof(data), fds, &count) == sizeof(payload) && count == 0);
    close(a.fd); close(b.fd);
}
static void *fragmented_writer(void *opaque)
{
    int fd = *(int *)opaque;
    const unsigned char bytes[] = { 4, 0, 0, 0, 1, 2, 3, 4 };
    for (size_t i = 0; i < sizeof(bytes); i++) assert(send(fd, bytes + i, 1, 0) == 1);
    return NULL;
}
static void misplaced_rights(void)
{
    struct gpud_wire a, b; pair(&a, &b, true);
    int p[2]; assert(pipe(p) == 0);
    /* Rights on header byte two must fail even when all bytes are coalesced. */
    assert(send(a.fd, "\004", 1, 0) == 1);
    unsigned char bytes[] = { 0, 0, 0, 1, 2, 3, 4 };
    struct iovec iov = { bytes, sizeof(bytes) };
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control;
    memset(&control, 0, sizeof(control));
    struct msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof(control) };
    struct cmsghdr *c = CMSG_FIRSTHDR(&msg);
    c->cmsg_level = SOL_SOCKET; c->cmsg_type = SCM_RIGHTS; c->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(c), &p[1], sizeof(int));
    assert(sendmsg(a.fd, &msg, 0) == sizeof(bytes));
    close(p[1]);
    unsigned char data[8]; int fds[GPUD_RENDER_MAX_FDS]; unsigned count;
    assert(gpud_wire_recv(&b, data, sizeof(data), fds, &count) == -1 && count == 0);
    struct pollfd check = { .fd = p[0], .events = POLLIN };
    assert(poll(&check, 1, 100) == 1);
    assert(read(p[0], data, 1) == 0); /* proves the received writer was closed */
    close(p[0]); close(a.fd); close(b.fd);
}
int main(void)
{
    roundtrip(false); roundtrip(true);
    misplaced_rights();
    struct gpud_wire a, b; pair(&a, &b, true);
    pthread_t writer; assert(pthread_create(&writer, NULL, fragmented_writer, &a.fd) == 0);
    unsigned char data[8]; int fds[GPUD_RENDER_MAX_FDS]; unsigned count;
    assert(gpud_wire_recv(&b, data, sizeof(data), fds, &count) == 4 && count == 0);
    assert(data[0] == 1 && data[3] == 4); pthread_join(writer, NULL);
    const unsigned char oversize[] = { 9, 0, 0, 0 };
    assert(send(a.fd, oversize, sizeof(oversize), 0) == sizeof(oversize));
    assert(gpud_wire_recv(&b, data, sizeof(data), fds, &count) == -1 && count == 0);
    close(a.fd); close(b.fd);
    pair(&a, &b, true);
    assert(send(a.fd, "\004\000", 2, 0) == 2); close(a.fd);
    assert(gpud_wire_recv(&b, data, sizeof(data), fds, &count) == -1); close(b.fd);
    pair(&a, &b, false);
    assert(send(a.fd, "0123456789", 10, 0) == 10);
    assert(gpud_wire_recv(&b, data, sizeof(data), fds, &count) == -1); close(a.fd); close(b.fd);
    puts("render wire: seqpacket/stream, fd passing, fragmentation, coalescing, bounds PASS");
}
