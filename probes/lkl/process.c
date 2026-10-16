#include "cap.h"
#include <sys/event.h>
#include <sys/procctl.h>
#include <dlfcn.h>

static void observe_exit(pid_t pid, int child, int channel) {
    int k = kqueue(), status = 0;
    struct kevent ev;
    EV_SET(&ev, pid, EVFILT_PROC, EV_ADD | EV_ENABLE | EV_ONESHOT, NOTE_EXIT, 0, 0);
    int rc = kevent(k, &ev, 1, NULL, 0, NULL);
    CHECK(child ? "own_kqueue_PROC" : "elfldr_kqueue_PROC", rc == 0,
        "pid=%d rc=%d errno=%d", pid, rc, errno);
    if (child) kill(pid, SIGTERM);
    else { char stop = 1; write(channel, &stop, 1); close(channel); }
    struct timespec timeout = {5, 0};
    int n = kevent(k, NULL, 0, &ev, 1, &timeout);
    CHECK(child ? "own_NOTE_EXIT" : "elfldr_NOTE_EXIT", n == 1 && (ev.fflags & NOTE_EXIT),
        "pid=%d events=%d fflags=%x errno=%d", pid, n, n == 1 ? ev.fflags : 0, errno);
    close(k);
    rc = child ? reap(pid, &status, 2000) : waitpid(pid, &status, WNOHANG);
    CHECK(child ? "own_wait4" : "elfldr_wait4_parentage", child ? rc == 0 : rc == -1 && errno == ECHILD,
        "pid=%d rc=%d errno=%d status=%x expected=%s", pid, rc, errno, status,
        child ? "reaped" : "ECHILD_nonchild");
    if (child && rc) retire(pid);
    owned_child = 0;
}

int main(void) {
    begin("lklcap-process", 9602);
    int calls[] = {SYS_fork, SYS_vfork, SYS_rfork};
    const char *names[] = {"fork", "vfork", "rfork"};
    for (int i = 0; i < 3; i++) {
        int p[2];
        if (pipe(p)) { CHECK(names[i], 0, "pipe_errno=%d", errno); continue; }
        pid_t child = clone_raw(calls[i], RFPROC | RFFDG);
        int err = errno;
        if (child == 0) {
            /* No libc or function calls in the vfork branch. */
            long values[2] = {RAW(SYS_getpid,0,0,0,0,0), RAW(SYS_getppid,0,0,0,0,0)};
            RAW(SYS_write,p[1],values,sizeof(values),0,0);
            RAW(SYS_exit,23,0,0,0,0);
            __builtin_unreachable();
        }
        owned_child = child > 0 ? child : 0;
        close(p[1]);
        long values[2] = {0};
        if (child > 0 && ready(p[0], 1500)) full(p[0], values, sizeof(values), 0);
        int status = 0, rc = child > 0 ? reap(child, &status, 2500) : -1;
        CHECK(names[i], child > 0 && rc == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 23 && values[0] == child,
            "pid=%d errno=%d wait=%d status=%x child_raw_getpid=%ld ppid=%ld",
            child, child < 0 ? err : 0, rc, status, values[0], values[1]);
        if (child > 0 && rc) retire(child);
        owned_child = 0;
        close(p[0]);
    }
    int results[2];
    if (pipe(results)) { CHECK("exec_pipe", 0, "errno=%d", errno); return finish(); }
    pid_t child = clone_raw(SYS_fork, 0);
    if (child == 0) {
        alarm(5);
        int good = getpid() > 0 && getpagesize() == PAGE, p[2];
        if (pipe(p) == 0) { close(p[0]); close(p[1]); } else good = 0;
        write(results[1], &good, sizeof(good));
        char *args[] = {"ps5wl-lklcap-helper.elf", NULL}, *env[] = {NULL};
        execve("/data/pldmgr/payloads/ps5wl-lklcap-helper/ps5wl-lklcap-helper.elf", args, env);
        int saved = errno;
        write(results[1], &saved, sizeof(saved));
        _exit(90);
    }
    close(results[1]);
    owned_child = child > 0 ? child : 0;
    int out[2] = {0}, status = 0;
    if (child > 0 && ready(results[0], 2000)) read(results[0], out, sizeof(out));
    int rc = child > 0 ? reap(child, &status, 7000) : -1;
    if (ready(results[0], 100)) read(results[0], &out[1], sizeof(out[1]));
    close(results[0]);
    CHECK("fork_libkernel", out[0] == 1, "child=%d libkernel=%d status=%x", child, out[0], status);
    CHECK("execve_runner", child > 0 && rc == 0 && WIFEXITED(status) && WEXITSTATUS(status) != 90,
        "status=%x errno=%d", status, out[1]);
    if (child > 0 && rc) retire(child);
    owned_child = 0;

    child = clone_raw(SYS_fork, 0);
    if (child == 0) { alarm(10); for (;;) pause(); }
    CHECK("long_lived_child", child > 0, "pid=%d errno=%d", child, errno);
    if (child > 0) { owned_child = child; usleep(200000); observe_exit(child, 1, -1); }

    int sig = SIGTERM;
    long pr = RAW(SYS_procctl,P_PID,getpid(),11,&sig,0);
    CHECK("PROC_PDEATHSIG_CTL", pr == 0, "command=11 rc=%ld errno=%ld", pr, pr < 0 ? -pr : 0);
    if (!pr) { sig = 0; RAW(SYS_procctl,P_PID,getpid(),11,&sig,0); }
    int fd = shm_open(SHM_ANON, O_RDWR, 0600);
    if (fd >= 0) ftruncate(fd, PAGE);
    pid_t pid = 0;
    int s = fd < 0 ? -1 : helper(fd, 4, &pid);
    CHECK("elfldr_spawn", s >= 0, "pid=%d self=%d fd=%d", pid, getpid(), s);
    if (s >= 0) {
        int result[4] = {0};
        if (ready(s, 2000)) full(s, result, sizeof(result), 0);
        ps5log("VALUE elfldr_helper_pid=%d ppid=%d requester=%d", pid, result[3], getpid());
        observe_exit(pid, 0, s);
    }
    if (fd >= 0) close(fd);
    void *sym = dlsym(RTLD_DEFAULT, "posix_spawn");
    CHECK("posix_spawn_symbol", sym != NULL, "address=%p", sym);
    return finish();
}
