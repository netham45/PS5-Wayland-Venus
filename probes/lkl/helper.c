#include "cap.h"
#include <sys/umtx.h>
#include <setjmp.h>

static sigjmp_buf jump;
static volatile int caught;
static void trap(int s) { caught = s; siglongjmp(jump, 1); }

int main(void) {
    begin("lklcap-helper", 9618);
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {.sun_len = sizeof(a), .sun_family = AF_UNIX};
    strcpy(a.sun_path, SOCK);
    if (s < 0 || connect(s, (void*)&a, sizeof(a))) {
        CHECK("helper_connect", 0, "errno=%d", errno);
        if (s >= 0) close(s);
        return finish();
    }
    pid_t me = getpid();
    full(s, &me, sizeof(me), 1);
    char control[CMSG_SPACE(sizeof(int))] = {0};
    int mode = 0, fd = -1;
    struct iovec i = {&mode, sizeof(mode)};
    struct msghdr m = {.msg_iov = &i, .msg_iovlen = 1,
        .msg_control = control, .msg_controllen = sizeof(control)};
    ssize_t n = ready(s, 5000) ? recvmsg(s, &m, 0) : -1;
    struct cmsghdr *c = n == sizeof(mode) ? CMSG_FIRSTHDR(&m) : NULL;
    if (c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS &&
        c->cmsg_len >= CMSG_LEN(sizeof(int)) && !(m.msg_flags & MSG_CTRUNC))
        memcpy(&fd, CMSG_DATA(c), sizeof(fd));
    CHECK("SCM_RIGHTS_receive", fd >= 0, "fd=%d mode=%d bytes=%zd errno=%d", fd, mode, n, errno);
    int result[4] = {-1, 0, 0, getppid()};
    void *p = mmap(NULL, PAGE, mode == 1 ? 5 : 3, MAP_SHARED, fd, 0);
    result[1] = p == MAP_FAILED ? errno : 0;
    if (p != MAP_FAILED) {
        if (mode == 1) {
            signal(SIGSEGV, trap); signal(SIGBUS, trap); signal(SIGILL, trap);
            if (sigsetjmp(jump, 1) == 0) result[0] = ((int(*)(void))p)();
            else result[2] = caught;
            full(s, result, sizeof(result), 1);
        } else if (mode == 2 || mode == 3) {
            volatile unsigned *word = p;
            __atomic_store_n(word + 1, 1, __ATOMIC_RELEASE);
            struct timespec t = {2, 0};
            long rc = RAW(SYS__umtx_op,word,mode == 2 ? UMTX_OP_WAIT_UINT : UMTX_OP_WAIT_UINT_PRIVATE,0,0,&t);
            result[0] = rc;
            result[1] = rc < 0 ? -rc : 0;
            result[2] = *word;
            full(s, result, sizeof(result), 1);
        } else {
            result[0] = 0;
            full(s, result, sizeof(result), 1);
            char stop;
            if (ready(s, 10000)) read(s, &stop, 1);
        }
        munmap(p, PAGE);
    } else full(s, result, sizeof(result), 1);
    CHECK("helper_operation", mode == 3 ? result[0] == -ETIMEDOUT : mode == 1 ? result[0] == 42 : result[0] == 0,
        "mode=%d result=%d errno=%d signal_or_word=%d", mode, result[0], result[1], result[2]);
    if (fd >= 0) close(fd);
    close(s);
    return finish();
}
