#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* One archive member per wrapper avoids importing APIs an executable never uses. */
extern int __real_fcntl(int fd, int command, ...);

static inline int set_descriptor_flags(int fd, int cloexec, int nonblock) {
    if (cloexec) {
        int flags = __real_fcntl(fd, F_GETFD);
        if (flags < 0 || __real_fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) return -1;
    }
    if (nonblock) {
        int flags = __real_fcntl(fd, F_GETFL);
        if (flags < 0 || __real_fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    }
    return 0;
}

#if !defined(PS5WL_WRAP_RECVMSG) && !defined(PS5WL_WRAP_SOCKETPAIR) && !defined(PS5WL_WRAP_PIPE2)
static inline int finish_descriptor(int fd, int cloexec, int nonblock) {
    if (fd >= 0 && set_descriptor_flags(fd, cloexec, nonblock) < 0) {
        int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}
#endif

#ifdef PS5WL_WRAP_FCNTL
__attribute__((used)) static int duplicate_cloexec(int fd, int command, int target) {
    int result;
#ifdef F_DUP2FD_CLOEXEC
    if (command == F_DUP2FD_CLOEXEC) {
        result = __real_fcntl(fd, F_DUP2FD, target);
    } else
#endif
    {
        (void)command;
        result = __real_fcntl(fd, F_DUPFD, target);
    }
    return finish_descriptor(result, 1, 0);
}

/* fcntl's other commands have heterogeneous/no variadic arguments. The x86-64
 * tail trampoline preserves the entire original call instead of guessing their
 * C types or reading a nonexistent argument. Only our two known int commands
 * enter the typed helper, with the original fd/command/target registers intact.
 */
#define STRINGIFY_INNER(value) #value
#define STRINGIFY(value) STRINGIFY_INNER(value)
__asm__(".text\n.globl __wrap_fcntl\n.hidden __wrap_fcntl\n"
        ".type __wrap_fcntl,@function\n__wrap_fcntl:\n"
        "cmpl $" STRINGIFY(F_DUPFD_CLOEXEC) ", %esi\n"
        "je duplicate_cloexec\n"
#ifdef F_DUP2FD_CLOEXEC
        "cmpl $" STRINGIFY(F_DUP2FD_CLOEXEC) ", %esi\n"
        "je duplicate_cloexec\n"
#endif
        "jmp __real_fcntl\n.size __wrap_fcntl, .-__wrap_fcntl\n");
#endif

#ifdef PS5WL_WRAP_DUP3
int __wrap_dup3(int oldfd, int newfd, int flags) {
    if (oldfd == newfd || (flags & ~O_CLOEXEC)) { errno = EINVAL; return -1; }
#ifdef F_DUP2FD
    return finish_descriptor(__real_fcntl(oldfd, F_DUP2FD, newfd), flags & O_CLOEXEC, 0);
#else
    return finish_descriptor(dup2(oldfd, newfd), flags & O_CLOEXEC, 0);
#endif
}
#endif

#if defined(PS5WL_WRAP_OPEN) || defined(PS5WL_WRAP_OPENAT)
static inline mode_t open_mode(int flags, va_list args) {
    return flags & O_CREAT ? (mode_t)va_arg(args, int) : 0;
}
#endif

#ifdef PS5WL_WRAP_OPEN
extern int __real_open(const char *path, int flags, ...);
int __wrap_open(const char *path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    mode_t mode = open_mode(flags, args);
    va_end(args);
    return finish_descriptor(__real_open(path, flags & ~O_CLOEXEC, mode), flags & O_CLOEXEC, 0);
}
#endif

#ifdef PS5WL_WRAP_OPENAT
extern int __real_openat(int dirfd, const char *path, int flags, ...);
int __wrap_openat(int dirfd, const char *path, int flags, ...) {
    va_list args;
    va_start(args, flags);
    mode_t mode = open_mode(flags, args);
    va_end(args);
    return finish_descriptor(__real_openat(dirfd, path, flags & ~O_CLOEXEC, mode), flags & O_CLOEXEC, 0);
}
#endif

#ifdef PS5WL_WRAP_SHM_OPEN
extern int __real_shm_open(const char *path, int flags, mode_t mode);
int __wrap_shm_open(const char *path, int flags, mode_t mode) {
    /* POSIX shm_open descriptors are close-on-exec even without O_CLOEXEC. */
    return finish_descriptor(__real_shm_open(path, flags & ~O_CLOEXEC, mode), 1, 0);
}
#endif

#ifdef PS5WL_WRAP_SOCKET
extern int __real_socket(int domain, int type, int protocol);
int __wrap_socket(int domain, int type, int protocol) {
    return finish_descriptor(__real_socket(domain, type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK), protocol),
        type & SOCK_CLOEXEC, type & SOCK_NONBLOCK);
}
#endif

#ifdef PS5WL_WRAP_SOCKETPAIR
extern int __real_socketpair(int domain, int type, int protocol, int pair[2]);
int __wrap_socketpair(int domain, int type, int protocol, int pair[2]) {
    if (__real_socketpair(domain, type & ~(SOCK_CLOEXEC | SOCK_NONBLOCK), protocol, pair) < 0) return -1;
    if (set_descriptor_flags(pair[0], type & SOCK_CLOEXEC, type & SOCK_NONBLOCK) < 0 ||
            set_descriptor_flags(pair[1], type & SOCK_CLOEXEC, type & SOCK_NONBLOCK) < 0) {
        int saved = errno;
        close(pair[0]);
        close(pair[1]);
        errno = saved;
        return -1;
    }
    return 0;
}
#endif

#ifdef PS5WL_WRAP_ACCEPT4
int __wrap_accept4(int fd, struct sockaddr *address, socklen_t *length, int flags) {
    if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK)) { errno = EINVAL; return -1; }
    return finish_descriptor(accept(fd, address, length), flags & SOCK_CLOEXEC, flags & SOCK_NONBLOCK);
}
#endif

#ifdef PS5WL_WRAP_RECVMSG
extern ssize_t __real_recvmsg(int fd, struct msghdr *message, int flags);
ssize_t __wrap_recvmsg(int fd, struct msghdr *message, int flags) {
    ssize_t result = __real_recvmsg(fd, message, flags & ~MSG_CMSG_CLOEXEC);
    if (result < 0 || !(flags & MSG_CMSG_CLOEXEC)) return result;
    int saved = 0;
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(message); cmsg != NULL; cmsg = CMSG_NXTHDR(message, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len < CMSG_LEN(0)) continue;
        size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int *fds = (int *)CMSG_DATA(cmsg);
        for (size_t i = 0; i < count; ++i) {
            if (set_descriptor_flags(fds[i], 1, 0) < 0 && saved == 0) saved = errno;
        }
    }
    if (saved == 0) return result;
    /* Ownership of ancillary fds cannot be handed out with the requested
     * invariant unmet. Close all of them before returning the actual error. */
    for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(message); cmsg != NULL; cmsg = CMSG_NXTHDR(message, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS || cmsg->cmsg_len < CMSG_LEN(0)) continue;
        size_t count = (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int *fds = (int *)CMSG_DATA(cmsg);
        for (size_t i = 0; i < count; ++i) close(fds[i]);
    }
    errno = saved;
    return -1;
}
#endif

#ifdef PS5WL_WRAP_MKOSTEMP
extern int __real_mkostemp(char *template, int flags);
int __wrap_mkostemp(char *template, int flags) {
    return finish_descriptor(__real_mkostemp(template, flags & ~O_CLOEXEC), flags & O_CLOEXEC, 0);
}
#endif

#ifdef PS5WL_WRAP_PIPE2
int __wrap_pipe2(int pair[2], int flags) {
    if (flags & ~(O_CLOEXEC | O_NONBLOCK)) { errno = EINVAL; return -1; }
    if (pipe(pair) < 0) return -1;
    if (set_descriptor_flags(pair[0], flags & O_CLOEXEC, flags & O_NONBLOCK) < 0 ||
            set_descriptor_flags(pair[1], flags & O_CLOEXEC, flags & O_NONBLOCK) < 0) {
        int saved = errno;
        close(pair[0]);
        close(pair[1]);
        errno = saved;
        return -1;
    }
    return 0;
}
#endif
