/* Plain capability and explicitly authorized elfldr pt.c credential pattern.
 * Ptrace targets ONLY children created here; credentials touch ONLY getpid(). */
#include "cap.h"
#include <sys/ptrace.h>
#include <machine/reg.h>
#include <ps5/kernel.h>

static volatile uint64_t word = 0x123456789abcdef0;

static long trace(int request, pid_t child, void *address, int data, int elevated) {
    if (!elevated) return RAW(SYS_ptrace,request,child,address,data,0);
    pid_t self = getpid();
    uint8_t saved_caps[16], all_caps[16];
    memset(all_caps, 0xff, sizeof(all_caps));
    uint64_t saved_auth = kernel_get_ucred_authid(self);
    if (!saved_auth || kernel_get_ucred_caps(self, saved_caps)) {
        CHECK("elfldr_credentials_read", 0, "self=%d auth=%lx", self, saved_auth);
        return -EIO;
    }
    int auth_rc = kernel_set_ucred_authid(self, UINT64_C(0x4800000000010003));
    int caps_rc = auth_rc ? -1 : kernel_set_ucred_caps(self, all_caps);
    long rc = auth_rc || caps_rc ? -EIO : RAW(SYS_ptrace,request,child,address,data,0);
    /* Restore both fields even after partial elevation or failed ptrace. */
    int restore_caps = kernel_set_ucred_caps(self, saved_caps);
    int restore_auth = kernel_set_ucred_authid(self, saved_auth);
    CHECK("elfldr_credentials_restore", !restore_caps && !restore_auth,
        "self=%d request=%d setauth=%d setcaps=%d restorecaps=%d restoreauth=%d",
        self, request, auth_rc, caps_rc, restore_caps, restore_auth);
    if (restore_caps || restore_auth) {
        ps5log("EXIT 2 credential_restore_failed");
        ps5log_flush_wait(1000);
        retire(child);
        _exit(2);
    }
    return rc;
}

static void registers_and_io(pid_t child, int elevated) {
    const char *mode = elevated ? "elfldr" : "plain";
    struct reg regs = {0};
    long rc = trace(PT_GETREGS, child, &regs, 0, elevated);
    CHECK("PT_GETREGS", rc == 0, "mode=%s rc=%ld errno=%ld rip=%lx rsp=%lx",
        mode, rc, rc < 0 ? -rc : 0, regs.r_rip, regs.r_rsp);
    if (!rc) {
        rc = trace(PT_SETREGS, child, &regs, 0, elevated);
        CHECK("PT_SETREGS", rc == 0, "mode=%s rc=%ld errno=%ld", mode, rc, rc < 0 ? -rc : 0);
    }
    uint64_t value = 0;
    struct ptrace_io_desc io = {.piod_op = PIOD_READ_D,
        .piod_offs = (void*)&word, .piod_addr = &value, .piod_len = sizeof(value)};
    rc = trace(PT_IO, child, &io, 0, elevated);
    CHECK("PT_IO_read", rc == 0 && value == word && io.piod_len == 8,
        "mode=%s rc=%ld errno=%ld value=%lx bytes=%zu", mode, rc, rc < 0 ? -rc : 0, value, io.piod_len);
    value = 0xfedcba9876543210;
    io.piod_op = PIOD_WRITE_D;
    rc = trace(PT_IO, child, &io, 0, elevated);
    CHECK("PT_IO_write", rc == 0, "mode=%s rc=%ld errno=%ld bytes=%zu",
        mode, rc, rc < 0 ? -rc : 0, io.piod_len);
    value = 0;
    io.piod_op = PIOD_READ_D;
    rc = trace(PT_IO, child, &io, 0, elevated);
    CHECK("PT_IO_roundtrip", rc == 0 && value == 0xfedcba9876543210,
        "mode=%s rc=%ld errno=%ld value=%lx", mode, rc, rc < 0 ? -rc : 0, value);
    rc = trace(PT_DETACH, child, (void*)1, 0, elevated);
    CHECK("PT_DETACH", rc == 0, "mode=%s rc=%ld errno=%ld", mode, rc, rc < 0 ? -rc : 0);
}

int main(void) {
    begin("lklcap-ptrace", 9607);
    int p[2];
    if (pipe(p)) { CHECK("trace_pipe", 0, "errno=%d", errno); return finish(); }
    pid_t child = clone_raw(SYS_fork, 0);
    if (child == 0) {
        alarm(8);
        long result = RAW(SYS_ptrace,PT_TRACE_ME,0,0,0,0);
        RAW(SYS_write,p[1],&result,sizeof(result),0,0);
        if (!result) {
            long self = RAW(SYS_getpid,0,0,0,0,0);
            RAW(SYS_kill,self,SIGSTOP,0,0,0);
        }
        RAW(SYS_exit,0,0,0,0,0);
        __builtin_unreachable();
    }
    close(p[1]);
    owned_child = child > 0 ? child : 0;
    long result = -999;
    if (child > 0 && ready(p[0], 2000)) full(p[0], &result, sizeof(result), 0);
    CHECK("PT_TRACE_ME_plain", result == 0, "pid=%d rc=%ld errno=%ld", child, result, result < 0 ? -result : 0);
    close(p[0]);
    if (result == 0) {
        int stopped = 0;
        if (reap(child, &stopped, 2000) == 0 && WIFSTOPPED(stopped))
            registers_and_io(child, 0);
        else CHECK("PT_TRACE_ME_stop", 0, "pid=%d status=%x errno=%d", child, stopped, errno);
    }
    if (child > 0) retire(child);

    child = clone_raw(SYS_fork, 0);
    if (child == 0) { alarm(15); for (;;) pause(); }
    if (child < 0) { CHECK("own_child", 0, "errno=%d", errno); return finish(); }
    owned_child = child;
    long rc = trace(PT_ATTACH, child, NULL, 0, 0);
    CHECK("PT_ATTACH_plain", rc == 0, "pid=%d rc=%ld errno=%ld", child, rc, rc < 0 ? -rc : 0);
    int elevated = rc != 0;
    if (elevated) {
        rc = trace(PT_ATTACH, child, NULL, 0, 1);
        CHECK("PT_ATTACH_elfldr", rc == 0, "pid=%d rc=%ld errno=%ld", child, rc, rc < 0 ? -rc : 0);
    }
    int status = 0;
    if (!rc && reap(child, &status, 3000) == 0 && WIFSTOPPED(status))
        registers_and_io(child, elevated);
    else ps5log("UNTESTED PT_GETREGS/PT_SETREGS/PT_IO/PT_DETACH attach_or_stop_failed rc=%ld status=%x", rc, status);
    retire(child);
    return finish();
}
