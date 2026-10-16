# PS5 dependencies

`make deps-ps5` dispatches to `deps/ps5/build.sh`. Builds run inside the pinned
rootless container through `env/run`; install the SDK and build `make deps-host`
first. The native scanner is the pinned Wayland build at
`build/host/sysroot/bin/wayland-scanner`, using source-built Expat. No target
scanner is executed. Downloads are shared with the host build in `.cache/deps`;
every invocation checks the archive SHA256 from `deps/versions.lock.json` before
using a source tree. Modified patches invalidate that dependency's source/build
directories. `--only NAME` rebuilds one dependency with existing prerequisites.

All dependency libraries are **static, position-independent** and install into
`build/ps5/sysroot/lib`; pkg-config metadata lives in `lib/pkgconfig`. No system
pkg-config directories are searched. The SDK itself still supplies the console
ABI, libc and kernel imports. Expat is needed only by the native scanner, not
by the target Wayland libraries. xkeyboard-config data comes from the shared pin;
libxkbcommon's console search root is `/system_tmp/ps5wl/share/X11/xkb` (deploy
`build/ps5/sysroot/share/X11/xkb` there before using keyboard layouts).

`cross/setup.sh` derives `build/ps5/cross.ini` from the SDK's `prospero.ini`, then
overrides prefix and pkg-config. `cross/prospero.cmake` includes the SDK CMake
toolchain. The SDK pkg-config wrapper hardcodes the SDK's homebrew directory;
`cross/pkg-config` preserves its static-link contract while isolating lookup to
our target prefix. `build/ps5/native.ini` uses a separate host-prefix wrapper.

All PS5 compiler links must use `cross/bin/prospero-clang` or its C++ counterpart.
The generated Meson and CMake files select these proxies; `toolchain/env.sh`
also puts them before the SDK tools. Direct Makefiles use that same compiler
path, not independent copies of link flags. The proxy builds the standalone
`build/ps5/runtime/libps5wl-fd-compat.a` under a lock and links it with wrappers
for `fcntl`, `dup3`, `open`, `openat`, `shm_open`, `socket`, `socketpair`,
`accept4`, `recvmsg`, `mkostemp` and `pipe2`. Each wrapper has its own archive
member, so unused APIs do not become new firmware imports.

PS5's nominal `F_DUPFD_CLOEXEC` command is incompatible: console probes showed
both failure on sockets and false success returning an unrelated fd zero for
SHM. The wrapper never sends it (or `F_DUP2FD_CLOEXEC`) to the kernel. It uses
real duplication and checked `F_SETFD(FD_CLOEXEC)`. Other `fcntl` commands are
tail-forwarded unchanged by an x86-64 ABI trampoline, preserving pointer,
integer and absent variadic arguments. Exact-target duplication uses native
`F_DUP2FD`, avoiding an additional firmware `dup2` import.
Creation/receive wrappers remove unreliable atomic flags and explicitly set
close-on-exec/nonblocking state, cleaning up acquired descriptors on failure.
`accept4` uses the real `accept`; no nonexistent firmware `accept4` import is
introduced. The native regression is `cross/test-fd-compat.sh`; console probe
regressions verify actual SHM identity and cross-process mapping coherence.

wlroots 0.20 always includes the pixman renderer, headless/Wayland backends,
shared-memory allocator and DRM-dumb allocator. Its `renderers` option only lists
optional GPU renderers, so `renderers=[]` means pixman-only. `backends=[]` and
`allocators=[]` disable all optional backends/allocators; session, Xwayland,
examples and optional features are disabled. No fake renderer/allocator stubs
are supplied. The PS5 libc check patch avoids unnecessary libm/librt when those
functions are already provided by libc/kernel imports.

epoll-shim implements epoll/eventfd/timerfd/signalfd using kqueue. Cross builds
cannot run its kernel feature tests: zero-timeout oneshot timers are configured
conservatively as unsupported so its existing workaround is enabled. The
FreeBSD SDK has no POLLRDHUP declaration, so epoll-shim's existing fallback
constant is used. No kernel feature success is invented.

Meson's FreeBSD default installs library `.pc` files under `libdata/pkgconfig`;
data-only packages use `share/pkgconfig`. The builder moves both to the single
contracted `lib/pkgconfig` directory after each install. No alternate search path
or compatibility copy is retained.

## Source patches

- `libffi/0001-prospero-sysv-only.patch`: prospero-clang rejects `ms_abi`.
  Retain SysV calls and closures; unsupported Windows CIF preparation takes the
  existing `FFI_BAD_ABI` path instead of pretending to support Windows calls.
- `epoll-shim/0001-prospero-no-capsicum-header-order.patch`: exclude Capsicum
  capability queries on PS5 (SDK has no Capsicum), and include `sys/types.h`
  before the old SDK's `sys/event.h`.
- `epoll-shim/0002-ps5-static-pkgconfig.patch`: publish the actual upstream release
  version and omit nonexistent librt from the PS5 static-link metadata.
- `epoll-shim/0003-prospero-bind-static-ppoll.patch`: console `dlsym` has no
  `ppoll` export, although SDK static libc implements it. Bind that real
  implementation directly instead of aborting during dynamic symbol lookup.
- `epoll-shim/0004-prospero-link-fcntl-compatibility.patch`: bind `fcntl` through
  the link-time wrapper instead of bypassing it through `dlsym`.
- `wayland/0001-freebsd-local-socket-level.patch`: use protocol level zero for
  local socket options when the old SDK lacks the later `SOL_LOCAL` spelling.
- `wayland/0002-growable-closure-log-buffer.patch`: use
  `shim/include/ps5wl_log_buffer.h` to preserve complete protocol diagnostics
  without `open_memstream` or `funopen`, which the SDK does not provide.
- `wayland/0003-prospero-set-socket-cloexec-explicitly.patch`: console probes
  observed successful `SOCK_CLOEXEC` socket creation without the close-on-exec
  flag. Always use Wayland's checked `fcntl(F_SETFD)` fallback on PS5.
- `wayland/0004-prospero-duplicate-and-set-cloexec.patch`: never attempt the
  incompatible atomic duplication command on PS5, even when it appears to
  succeed. Use real `F_DUPFD` and checked `F_SETFD`.
  Wayland polls the duplicate but dispatches callbacks with the original fd;
  general reads through aliases of shim-created eventfds remain unsupported.
- `libdrm/0001-growable-modifier-name-buffer.patch`: preserve complete ARM/AMD
  modifier names with the same checked growable buffer instead of unavailable
  `open_memstream`.
- `wlroots/0001-check-libc-before-libm-librt.patch`: detect actual libc math and
  clock symbols before looking for separate libraries that the SDK lacks.
- `wlroots/0002-xcursor-request-getline.patch`: request FreeBSD's `getline`
  declaration when Xcursor deliberately undefines `_POSIX_C_SOURCE`.
- `wlroots/0003-link-real-getline-compatibility.patch`: link `libps5compat.a`
  through a normal pkg-config dependency. SDK headers declare `getline` but
  the library does not implement it; `shim/getline.c` supplies the real
  stream-locking, dynamically growing POSIX line reader, including embedded
  NULs, unterminated final lines and EOF/error handling.
- `wlroots/0004-report-shm-import-descriptor-identity.patch`: preserve the
  actual mapping error and report fd flags/stat identity on failure without
  changing writable/shared mapping semantics.

libxkbcommon 1.11 has no tests-off switch. The builder explicitly compiles only
its enabled installable library, rather than unrelated test/fuzz/benchmark
executables which use APIs absent on PS5 (`open_memstream`).

`shim/include/math.h` corrects the SDK's old `_Generic` math dispatcher by
removing qualified type associations that lvalue conversion makes unreachable.
It delegates to the same SDK math functions and evaluates the argument once;
warnings are not suppressed and no math behavior is stubbed.

The input event keycode constants are a separately hashed Linux v5.8 UAPI header,
pinned to its actual Git commit, matching FreeBSD's evdev-proto header baseline.
This is protocol data, not an input backend or a Linux kernel implementation.

The log buffer regression test checks expansion, concatenation, numeric
formatting and counted embedded NULs:

```sh
env/run clang -std=c11 -Wall -Wextra -Werror deps/ps5/shim/test_log_buffer.c -o build/ps5/test-log-buffer
env/run build/ps5/test-log-buffer
```

The getline regression forces multiple buffer expansions and checks newline
retention, counted embedded NULs, an unterminated final line and EOF:

```sh
env/run clang -std=c11 -Wall -Wextra -Werror deps/ps5/shim/getline.c deps/ps5/shim/test_getline.c -o build/ps5/test-getline
env/run build/ps5/test-getline
```

## Link/runtime check

`deps/ps5/linktest/build.sh` links `build/ps5/linktest/linktest.elf` using
`prospero-clang` and static pkg-config metadata. It checks `wlr_log_init`,
environment lookup, standalone event-loop creation/destruction, creates
a Wayland display, dispatches its event loop with timeout zero, destroys it,
and logs the actual result through `common/ps5log` on TCP port 9531. It waits ten
seconds for a pull client. Deployment and console output are separate evidence;
a successful link alone does not prove console behavior.

### Observed console acceptance

An earlier freshly uploaded linktest, after the initial atomic-dup fallback
repair, completed on `192.168.3.211` through Payload Manager and TCP port 9531:

```text
deps-linktest: wlr_log_init: OK
deps-linktest: getenv: OK (unset)
deps-linktest: wl_event_loop_create: OK
deps-linktest: wl_display_create: OK
deps-linktest: wl_event_loop_dispatch(timeout=0): 0 errno=0
deps-linktest: RESULT: PASS
```

The host upload/launch/log command exited zero. Separate console probes also
passed epoll/eventfd, timerfd, signalfd and temporary-mask `epoll_pwait`, including
restoration of the original blocked signal mask. These observations establish
the dependency/linktest path, not compositor rendering or unsupported GPU APIs.

The subsequent exact fd regression exposed the false-success SHM case and
verified the systemic compiler-wrapper repair: all three 8,294,400-byte SHM
paths (original, plain duplicate, requested CLOEXEC duplicate) retained the
correct backing object after the original fd was closed, mapped successfully
with read/read-write and shared/private combinations, and passed full-byte
bidirectional coherence checks. Exact-target `F_DUP2FD_CLOEXEC` and `dup3`
also retained identity, size and close-on-exec state and passed coherence.
Socket/socketpair close-on-exec and nonblocking flags, open/shm_open
close-on-exec, and received SCM_RIGHTS close-on-exec were observed set.

### Console-stop evidence

When the coordinator reported the console unavailable after its BigApp fork
experiment, the dependency owner had no active console commands or pullers.
The final exact lines held from DeployTools' last captured dependency linktest
were:

```text
[17173120 ms] deps-linktest: creating Wayland display
[17173220 ms] deps-linktest: wl_display_create: OK
[17173320 ms] deps-linktest: wl_event_loop_dispatch(timeout=0): 0 errno=0
[17173320 ms] deps-linktest: RESULT: PASS
```

These are earlier linktest results, not output from the later fork experiment.
The separate systemic-wrapper hardware verdicts above were supplied by Probes.
