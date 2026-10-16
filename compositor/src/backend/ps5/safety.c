/* Best-effort userspace containment; no handler can recover a kernel deadlock. */
#include "ps5.h"
#include "ps5log.h"
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
_Static_assert(ATOMIC_INT_LOCK_FREE == 2 && ATOMIC_LONG_LOCK_FREE == 2,
 "watchdog and fatal handlers require lock-free atomics");
static _Atomic int video = -1;
static _Atomic long heartbeat, deadline;
static atomic_bool armed;
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
 (void)context;
 char text[128]; size_t n = 0;
 const char prefix[] = "ps5wl FATAL signal=0x";
 for (size_t i = 0; i < sizeof(prefix) - 1; ++i) text[n++] = prefix[i];
 static const char digits[] = "0123456789abcdef";
 text[n++] = digits[(sig >> 4) & 15]; text[n++] = digits[sig & 15];
 const char middle[] = " address=0x";
 for (size_t i = 0; i < sizeof(middle) - 1; ++i) text[n++] = middle[i];
 uintptr_t address = (uintptr_t)info->si_addr;
 for (int shift = 60; shift >= 0; shift -= 4) text[n++] = digits[(address >> shift) & 15];
 text[n++] = '\n'; ps5log_emergency_write(text, n);
 close_video(); _exit(128 + sig);
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
  int stalled = now - atomic_load_explicit(&heartbeat, memory_order_relaxed) > 3000;
  if (!stalled && (!end || now < end)) continue;
  static const char stall[] = "ps5wl WATCHDOG main-loop heartbeat stalled >3s; closing VideoOut\n";
  static const char limit[] = "ps5wl WATCHDOG validation hard deadline; closing VideoOut\n";
  ps5log_emergency_write(stalled ? stall : limit, stalled ? sizeof(stall)-1 : sizeof(limit)-1);
  close_video(); _exit(stalled ? 124 : 0);
 }
}
static int tick(void *data) {
 atomic_store_explicit(&heartbeat, milliseconds(), memory_order_relaxed);
 (void)data; wl_event_source_timer_update(heartbeat_timer, 100); return 0;
}
int ps5_safety_init(struct wl_event_loop *loop) {
 if (ps5_safety_thread_stack(0)) return -1;
 struct sigaction action = { .sa_sigaction = fatal_signal, .sa_flags = SA_SIGINFO | SA_ONSTACK };
 sigemptyset(&action.sa_mask);
 const int signals[] = { SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGFPE };
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
void ps5_safety_video(int handle) {
 atomic_store(&heartbeat, milliseconds());
 if (PS5WL_TEST_SECONDS > 0) atomic_store(&deadline, milliseconds() + (PS5WL_TEST_SECONDS + 3) * 1000L);
 atomic_store(&video, handle); atomic_store(&armed, true);
}
void ps5_safety_close(void) { close_video(); }
