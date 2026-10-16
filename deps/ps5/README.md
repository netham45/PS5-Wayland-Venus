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
- `wayland/0001-freebsd-local-socket-level.patch`: use protocol level zero for
  local socket options when the old SDK lacks the later `SOL_LOCAL` spelling.
- `wayland/0002-growable-closure-log-buffer.patch`: use
  `shim/include/ps5wl_log_buffer.h` to preserve complete protocol diagnostics
  without `open_memstream` or `funopen`, which the SDK does not provide.
- `wayland/0003-prospero-set-socket-cloexec-explicitly.patch`: console probes
  observed successful `SOCK_CLOEXEC` socket creation without the close-on-exec
  flag. Always use Wayland's checked `fcntl(F_SETFD)` fallback on PS5.
- `wayland/0004-unsupported-atomic-dup-cloexec-fallback.patch`: console
  `F_DUPFD_CLOEXEC` fails with `EOPNOTSUPP`; use the existing checked
  `F_DUPFD` plus `F_SETFD` path for that unsupported atomic operation.
  Wayland polls the duplicate but dispatches callbacks with the original fd.
  General reads through aliases of shim-created eventfds remain unsupported.
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

After the unsupported atomic-dup fallback patch, a freshly uploaded linktest
on `192.168.3.211` completed through Payload Manager and TCP port 9531:

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
