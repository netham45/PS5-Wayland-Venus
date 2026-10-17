/* Best-effort userspace containment; no handler can recover a kernel deadlock. */
#define _XOPEN_SOURCE 700
#include "ps5.h"
#include "ps5log.h"
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <ucontext.h>
#include <ps5/kernel.h>
#include <unistd.h>
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2,
 "watchdog and fatal handlers require lock-free atomics");
static _Atomic int video = -1;
static _Atomic long heartbeat, deadline, grace_until;
static atomic_bool armed;
static atomic_bool foreground_owned;
static unsigned char signal_stacks[3][65536];
static struct wl_event_source *heartbeat_timer;
static long milliseconds(void) {
 struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
 return now.tv_sec * 1000L + now.tv_nsec / 1000000L;
}
static void close_video(void) {
 int handle = atomic_exchange_explicit(&video, -1, memory_order_relaxed);
 if (handle >= 0) sceVideoOutClose(handle);
}
static void fatal_signal(int sig, siginfo_t *info, void *context) {
 char text[384]; size_t n = 0;
 const char prefix[] = "ps5wl FATAL signal=0x";
 for (size_t i = 0; i < sizeof(prefix) - 1; ++i) text[n++] = prefix[i];
 static const char digits[] = "0123456789abcdef";
 text[n++] = digits[(sig >> 4) & 15]; text[n++] = digits[sig & 15];
 const char middle[] = " address=0x";
 for (size_t i = 0; i < sizeof(middle) - 1; ++i) text[n++] = middle[i];
 uintptr_t address = (uintptr_t)info->si_addr;
 for (int shift = 60; shift >= 0; shift -= 4) text[n++] = digits[(address >> shift) & 15];
 /* Firmware-measured prefix, also used by the Vulkan fault collector. */
 const mcontext_t *mc = &((const struct {
  unsigned char native_prefix[64]; mcontext_t machine;
 } *)context)->machine;
 if (mc->mc_len >= 256 && mc->mc_len <= 2048) {
  const char *labels[] = { " rip=0x", " rsp=0x", " rbp=0x", " rax=0x", " rdi=0x", " rsi=0x" };
  const uintptr_t values[] = { mc->mc_rip, mc->mc_rsp, mc->mc_rbp,
   mc->mc_rax, mc->mc_rdi, mc->mc_rsi };
  for (size_t r = 0; r < sizeof(values) / sizeof(values[0]); ++r) {
   for (const char *label = labels[r]; *label; ++label) text[n++] = *label;
   for (int shift = 60; shift >= 0; shift -= 4)
    text[n++] = digits[(values[r] >> shift) & 15];
  }
 }
 text[n++] = '\n'; ps5log_emergency_write(text, n);
 close_video(); ps5_process_exit(128 + sig);
}
int ps5_safety_thread_stack(unsigned slot) {
 if (slot >= 3) return -1;
 stack_t stack = { .ss_sp = signal_stacks[slot], .ss_size = sizeof(signal_stacks[slot]) };
 return sigaltstack(&stack, NULL);
}
static void *watchdog(void *unused) {
 (void)unused; ps5_safety_thread_stack(2);
 for (;;) {
  struct timespec pause = { .tv_nsec = 100000000 }; nanosleep(&pause, NULL);
  if (!atomic_load_explicit(&armed, memory_order_relaxed)) continue;
  long now = milliseconds(), end = atomic_load_explicit(&deadline, memory_order_relaxed);
  if (now < atomic_load_explicit(&grace_until, memory_order_relaxed)) continue;
  int stalled = now - atomic_load_explicit(&heartbeat, memory_order_relaxed) > 3000;
  if (!stalled && (!end || now < end)) continue;
  static const char stall[] = "ps5wl WATCHDOG main-loop heartbeat stalled >3s; closing VideoOut\n";
  static const char limit[] = "ps5wl WATCHDOG phase/validation hard deadline; closing VideoOut\n";
  ps5log_emergency_write(stalled ? stall : limit, stalled ? sizeof(stall)-1 : sizeof(limit)-1);
  close_video(); ps5_process_exit(stalled ? 124 : 0);
 }
}
static int tick(void *data) {
 atomic_store_explicit(&heartbeat, milliseconds(), memory_order_relaxed);
 (void)data; wl_event_source_timer_update(heartbeat_timer, 100); return 0;
}
int ps5_safety_init(struct wl_event_loop *loop) {
 if (ps5_safety_thread_stack(0)) return -1;
 extern const unsigned char __image_start[];
 ps5log("native image base=%p fatal_handler=%p", __image_start, fatal_signal);
 intptr_t kernel_base = kernel_dynlib_mapbase_addr(-1, 1);
 if (!kernel_base) kernel_base = kernel_dynlib_mapbase_addr(-1, 0x2001);
 ps5log("native module bases kernel=%p libc=%p",
  (void *)kernel_base,
  (void *)kernel_dynlib_mapbase_addr(-1, 2));
 struct sigaction action = { .sa_sigaction = fatal_signal, .sa_flags = SA_SIGINFO | SA_ONSTACK };
 sigemptyset(&action.sa_mask);
 const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE, SIGSYS };
 for (unsigned i = 0; i < sizeof(signals)/sizeof(signals[0]); ++i)
  if (sigaction(signals[i], &action, NULL)) return -1;
 heartbeat_timer = wl_event_loop_add_timer(loop, tick, NULL);
 if (!heartbeat_timer) return -1;
 atomic_store(&heartbeat, milliseconds());
 pthread_t thread;
 if (pthread_create(&thread, NULL, watchdog, NULL)) return -1;
 pthread_detach(thread); wl_event_source_timer_update(heartbeat_timer, 100);
 return 0;
}
void ps5_safety_grace(unsigned timeout_ms) {
 long end = milliseconds() + timeout_ms;
 atomic_store(&grace_until, end);
 atomic_store(&deadline, end);
 atomic_store(&armed, true);
}
void ps5_safety_video(int handle) {
 atomic_store(&heartbeat, milliseconds());
 atomic_store(&deadline, PS5WL_TEST_SECONDS > 0 ?
  milliseconds() + (PS5WL_TEST_SECONDS + 3) * 1000L : 0);
 atomic_store(&grace_until, 0);
 atomic_store(&video, handle); atomic_store(&armed, true);
}
void ps5_safety_close(void) { close_video(); }
void ps5_safety_identify(void) {
 struct ps5_app_info app = {0};
 int big = sceSystemServiceGetAppIdOfRunningBigApp();
 atomic_store(&foreground_owned, sceKernelGetAppInfo(getpid(), &app) == 0 &&
  big > 0 && app.app_id == (uint32_t)big);
}
_Noreturn void ps5_process_exit(int status) {
 if (atomic_load_explicit(&foreground_owned, memory_order_relaxed)) {
  /* Shipping SDL src/main/ps5/SDL_ps5_main.c exits a BigApp through this
   * lifecycle request, not raw process _exit. Best effort in fatal context. */
  int rc = sceSystemServiceLoadExec("exit", NULL);
  char result[] = "ps5wl SystemServiceLoadExec(exit) rc=0x00000000\n";
  size_t at = sizeof("ps5wl SystemServiceLoadExec(exit) rc=0x") - 1;
  for (unsigned i = 0; i < 8; ++i) result[at + i] = "0123456789abcdef"[((unsigned)rc >> (28 - 4 * i)) & 15];
  ps5log_emergency_write(result, sizeof(result) - 1);
  static const char returned[] = "ps5wl SystemServiceLoadExec(exit) returned; awaiting app-manager termination (no raw BigApp exit)\n";
  ps5log_emergency_write(returned, sizeof(returned) - 1);
  /* Raw exit raises SIGSYS in a native title (PS5_RetroArch main.cpp).
   * Never execute it while the application-manager exit is pending. */
  for (;;) pause();
 }
 _exit(status);
}
