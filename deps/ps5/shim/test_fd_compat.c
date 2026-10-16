#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static void check_flags(int fd, int nonblock) {
    assert(fd >= 0);
    assert(fcntl(fd, F_GETFD) & FD_CLOEXEC);
    if (nonblock) assert(fcntl(fd, F_GETFL) & O_NONBLOCK);
}

int main(void) {
    int pipe_fds[2];
    assert(pipe2(pipe_fds, O_CLOEXEC | O_NONBLOCK) == 0);
    check_flags(pipe_fds[0], 1);
    check_flags(pipe_fds[1], 1);
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    char template[] = "build/ps5/runtime/fd-test-XXXXXX";
    int temporary = mkostemp(template, O_CLOEXEC);
    check_flags(temporary, 0);
    close(temporary);
    assert(unlink(template) == 0);
    char path[128];
    snprintf(path, sizeof(path), "build/ps5/runtime/fd-test-%ld", (long)getpid());
    int file = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    check_flags(file, 0);
    int reopened = openat(AT_FDCWD, path, O_RDWR | O_CLOEXEC);
    check_flags(reopened, 0);
    close(reopened);
    assert(unlink(path) == 0);
    assert(ftruncate(file, 16384) == 0);
    struct stat original, duplicate;
    assert(fstat(file, &original) == 0);
    int copy = fcntl(file, F_DUPFD_CLOEXEC, 0);
    check_flags(copy, 0);
    assert(copy != file);
    assert(fstat(copy, &duplicate) == 0);
    assert(original.st_dev == duplicate.st_dev && original.st_ino == duplicate.st_ino);
    struct flock lock = {.l_type = F_WRLCK, .l_whence = SEEK_SET};
    assert(fcntl(file, F_GETLK, &lock) == 0);
    assert(lock.l_type == F_UNLCK);
    assert(dup3(file, copy, O_CLOEXEC) == copy);
    check_flags(copy, 0);
    assert(dup3(file, file, O_CLOEXEC) == -1 && errno == EINVAL);
#ifdef F_DUP2FD_CLOEXEC
    assert(fcntl(file, F_DUP2FD_CLOEXEC, copy) == copy);
    check_flags(copy, 0);
#endif
    close(copy);

    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, pair) == 0);
    check_flags(pair[0], 1);
    check_flags(pair[1], 1);
    char byte;
    assert(read(pair[0], &byte, 1) == -1 && (errno == EAGAIN || errno == EWOULDBLOCK));
    char control[CMSG_SPACE(sizeof(int))] = {0};
    byte = 'D';
    struct iovec iov = {.iov_base = &byte, .iov_len = 1};
    struct msghdr message = {.msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &file, sizeof(file));
    assert(sendmsg(pair[0], &message, 0) == 1);
    memset(control, 0, sizeof(control));
    assert(recvmsg(pair[1], &message, MSG_CMSG_CLOEXEC) == 1);
    assert(byte == 'D');
    int received;
    memcpy(&received, CMSG_DATA(CMSG_FIRSTHDR(&message)), sizeof(received));
    check_flags(received, 0);
    assert(fstat(received, &duplicate) == 0 && original.st_ino == duplicate.st_ino);
    close(received);
    close(pair[0]);
    close(pair[1]);
    close(file);

    int listener = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    check_flags(listener, 1);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    socklen_t length = sizeof(address);
    assert(getsockname(listener, (struct sockaddr *)&address, &length) == 0);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    assert(client >= 0);
    assert(connect(client, (struct sockaddr *)&address, length) == 0);
    int accepted = accept4(listener, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    check_flags(accepted, 1);
    close(accepted);
    close(client);
    close(listener);

    puts("fd compatibility: dup identity/flags/variadic forwarding/open/openat/socket/socketpair/accept4/SCM_RIGHTS PASS");
    return 0;
}
