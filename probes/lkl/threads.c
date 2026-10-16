#include "cap.h"
#include <sys/thr.h>
#include <machine/sysarch.h>

struct state {
    volatile long tid, done, release;
    long fs, gs, setfs, setgs;
    uintptr_t seenfs, seengs;
};
static struct state state;
static uint64_t tls[64] __attribute__((aligned(64)));

/* No libc, errno, pthread machinery, or compiler TLS after changing FS. */
static void raw_thread(void *arg) {
    struct state *s = arg;
    uintptr_t fs = 0, gs = 0;
    RAW(SYS_sysarch,AMD64_GET_FSBASE,&fs,0,0,0);
    RAW(SYS_sysarch,AMD64_GET_GSBASE,&gs,0,0,0);
    s->fs = fs;
    s->gs = gs;
    uintptr_t f = (uintptr_t)tls, g = (uintptr_t)&tls[32];
    s->setfs = RAW(SYS_sysarch,AMD64_SET_FSBASE,&f,0,0,0);
    s->setgs = RAW(SYS_sysarch,AMD64_SET_GSBASE,&g,0,0,0);
    if (!s->setfs && !s->setgs)
        __asm__ volatile("movq %%fs:0,%0;movq %%gs:0,%1" : "=r"(s->seenfs), "=r"(s->seengs));
    __atomic_store_n(&s->done, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&s->release, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
    RAW(SYS_thr_exit,0,0,0,0,0);
    __builtin_unreachable();
}

int main(void) {
    begin("lklcap-threads", 9603);
    uintptr_t parent_fs = 0, after_fs = 0;
    RAW(SYS_sysarch,AMD64_GET_FSBASE,&parent_fs,0,0,0);
    void *stack = mmap(NULL, 1024 * 1024, 3, MAP_PRIVATE | MAP_ANON, -1, 0);
    tls[0] = 0x12345678;
    tls[32] = 0x87654321;
    struct thr_param param = {
        .start_func = raw_thread, .arg = &state,
        .stack_base = stack, .stack_size = 1024 * 1024,
        .tls_base = (char*)tls, .tls_size = sizeof(tls),
        .child_tid = (long*)&state.tid, .parent_tid = (long*)&state.tid
    };
    long rc = stack == MAP_FAILED ? -ENOMEM : RAW(SYS_thr_new,&param,sizeof(param),0,0,0);
    CHECK("thr_new", rc == 0, "rc=%ld errno=%ld tid=%ld stack=%p tls=%p",
        rc, rc < 0 ? -rc : 0, state.tid, stack, tls);
    if (!rc) {
        double end = now() + 2;
        while (!__atomic_load_n(&state.done, __ATOMIC_ACQUIRE) && now() < end) usleep(1000);
        CHECK("per_thread_FS_GS", state.done && state.fs == (long)tls && !state.setfs && !state.setgs && state.seenfs == tls[0] && state.seengs == tls[32],
            "done=%ld initial_fs=%lx initial_gs=%lx setfs=%ld setgs=%ld fs_word=%lx gs_word=%lx",
            state.done, state.fs, state.gs, state.setfs, state.setgs, state.seenfs, state.seengs);
        RAW(SYS_sysarch,AMD64_GET_FSBASE,&after_fs,0,0,0);
        CHECK("parent_FS_unchanged", parent_fs == after_fs,
            "before=%lx after=%lx", parent_fs, after_fs);
        long r = RAW(SYS_thr_kill2,getpid(),state.tid,0,0,0);
        CHECK("thr_kill2_specific", r == 0, "tid=%ld signal=0 rc=%ld errno=%ld",
            state.tid, r, r < 0 ? -r : 0);
        __atomic_store_n(&state.release, 1, __ATOMIC_RELEASE);
        end = now() + 2;
        while (now() < end && RAW(SYS_thr_kill2,getpid(),state.tid,0,0,0) == 0) usleep(1000);
    }
    /* Exit reclaims the custom stack; never free it under a retiring raw LWP. */
    return finish();
}
