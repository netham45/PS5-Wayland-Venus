/* Native fault collection / RIP redirection from freebsd_fault_frame_context.
 * Firmware 10.60 starts mcontext at ucontext+64, versus SDK FreeBSD's +16.
 * Keep that native prefix opaque: its mask/padding semantics are not established.
 * The GPR prefix matches mcontext_t; retain the longer FP tail as raw words. */
#include "cap.h"
#include <ucontext.h>
#include <sys/thr.h>
#include <machine/trap.h>
#include <stddef.h>

struct ps5_context_prefix { unsigned char native_prefix[64]; mcontext_t machine; };
_Static_assert(offsetof(struct ps5_context_prefix, machine) == 64, "PS5 native context prefix");
_Static_assert(offsetof(mcontext_t, mc_rip) == 160, "native amd64 GPR prefix");
static mcontext_t context;
static uint64_t raw_context[256];
static size_t raw_bytes;
static siginfo_t info;
static volatile int received, alt, usr;
static uintptr_t altstart, altend;
static volatile int stop, working;
static long tid;
extern void fault_load(void *);
extern char fault_resume[];
__asm__(".text\n.globl fault_load\nfault_load: movb (%rdi),%al\n.globl fault_resume\nfault_resume: ret\n");

static void collect(int s, siginfo_t *i, void *v) {
    mcontext_t *mc = &((struct ps5_context_prefix*)v)->machine;
    if (received || (uintptr_t)mc->mc_rip != (uintptr_t)fault_load ||
        mc->mc_len < 256 || mc->mc_len > (long)sizeof(raw_context)) {
        static const char text[] = "FAIL native_signal_ABI_or_repeated_fault\nEXIT 2\n";
        ps5log_emergency_write(text, sizeof(text) - 1);
        _exit(2);
    }
    context = *mc;
    raw_bytes = mc->mc_len;
    memcpy(raw_context, mc, raw_bytes);
    info = *i;
    received = s;
    char local;
    alt = (uintptr_t)&local >= altstart && (uintptr_t)&local < altend;
    mc->mc_rip = (uintptr_t)fault_resume;
}

static void interrupt(int s) { (void)s; usr++; }
static void dump(void) {
    for (size_t i = 0; i < raw_bytes / 8; i += 4)
        ps5log("REG native_mcontext_qword[%zu..] %016lx %016lx %016lx %016lx", i,
            raw_context[i], i+1 < raw_bytes/8 ? raw_context[i+1] : 0,
            i+2 < raw_bytes/8 ? raw_context[i+2] : 0, i+3 < raw_bytes/8 ? raw_context[i+3] : 0);
    ps5log("FRAME signal=%d si_code=%d si_addr=%p trap=%u err=%lx rip=%lx rsp=%lx native_mc_len=%zu alt=%d",
        received, info.si_code, info.si_addr, context.mc_trapno, context.mc_err,
        context.mc_rip, context.mc_rsp, raw_bytes, alt);
    ps5log("GPR rax=%lx rbx=%lx rcx=%lx rdx=%lx rsi=%lx rdi=%lx rbp=%lx r8=%lx r9=%lx r10=%lx r11=%lx r12=%lx r13=%lx r14=%lx r15=%lx flags=%lx cs=%lx ss=%lx fs=%x gs=%x",
        context.mc_rax, context.mc_rbx, context.mc_rcx, context.mc_rdx,
        context.mc_rsi, context.mc_rdi, context.mc_rbp, context.mc_r8,
        context.mc_r9, context.mc_r10, context.mc_r11, context.mc_r12,
        context.mc_r13, context.mc_r14, context.mc_r15, context.mc_rflags,
        context.mc_cs, context.mc_ss, context.mc_fs, context.mc_gs);
}
static void *worker(void *arg) {
    (void)arg;
    RAW(SYS_thr_self,&tid,0,0,0,0);
    __atomic_store_n(&working, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&stop, __ATOMIC_ACQUIRE)) __asm__ volatile("pause");
    return NULL;
}

int main(void) {
    begin("lklcap-faults", 9605);
    stack_t stack = {.ss_sp = mmap(NULL,65536,3,MAP_ANON|MAP_PRIVATE,-1,0), .ss_size = 65536};
    altstart = (uintptr_t)stack.ss_sp;
    altend = altstart + stack.ss_size;
    int r = sigaltstack(&stack, NULL);
    CHECK("sigaltstack", r == 0, "rc=%d errno=%d address=%p", r, r ? errno : 0, stack.ss_sp);
    struct sigaction sa = {.sa_sigaction = collect, .sa_flags = SA_SIGINFO | SA_ONSTACK};
    sigemptyset(&sa.sa_mask);
    r = sigaction(SIGSEGV, &sa, NULL);
    CHECK("SIGSEGV_handler", r == 0, "rc=%d errno=%d", r, r ? errno : 0);
    r = sigaction(SIGBUS, &sa, NULL);
    CHECK("SIGBUS_handler", r == 0, "rc=%d errno=%d", r, r ? errno : 0);
    if (failures) return finish();
    usleep(500000); /* Emergency diagnostics require an attached pull client. */
    void *p = mmap(NULL,PAGE,PROT_NONE,MAP_ANON|MAP_PRIVATE,-1,0);
    received = 0;
    if (p != MAP_FAILED) fault_load(p);
    dump();
    CHECK("SIGSEGV_frame_RIP_resume", received == SIGSEGV && info.si_addr == p && context.mc_trapno == T_PAGEFLT && alt,
        "signal=%d address=%p expected=%p trap=%u alt=%d", received, info.si_addr, p, context.mc_trapno, alt);
    if (p != MAP_FAILED) munmap(p, PAGE);
    int fd = shm_open(SHM_ANON, O_RDWR, 0600);
    if (fd >= 0 && ftruncate(fd, PAGE) == 0) {
        p = mmap(NULL,PAGE,PROT_READ,MAP_SHARED,fd,0);
        r = ftruncate(fd, 0);
        received = 0;
        if (!r && p != MAP_FAILED) fault_load(p);
        dump();
        CHECK("SIGBUS_truncated_shm", received == SIGBUS && info.si_addr == p && alt,
            "signal=%d code=%d address=%p", received, info.si_code, info.si_addr);
        if (p != MAP_FAILED) munmap(p, PAGE);
    } else CHECK("SIGBUS_setup", 0, "fd=%d errno=%d", fd, errno);
    if (fd >= 0) close(fd);
    signal(SIGUSR2, interrupt);
    pthread_t t;
    r = pthread_create(&t, NULL, worker, NULL);
    if (!r) {
        double end = now() + 2;
        while (!working && now() < end) usleep(1000);
        long rc = RAW(SYS_thr_kill2,getpid(),tid,SIGUSR2,0,0);
        end = now() + 2;
        while (!usr && now() < end) usleep(1000);
        CHECK("SIGUSR2_running_thread", rc == 0 && usr, "tid=%ld rc=%ld delivered=%d", tid, rc, usr);
        __atomic_store_n(&stop, 1, __ATOMIC_RELEASE);
        pthread_join(t, NULL);
    } else CHECK("worker_thread", 0, "rc=%d", r);
    stack.ss_flags = SS_DISABLE;
    sigaltstack(&stack, NULL);
    munmap(stack.ss_sp, stack.ss_size);
    return finish();
}
