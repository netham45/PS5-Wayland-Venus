# ps5_wayland plan

A Wayland compositor that runs on a jailbroken PS5 as a payload process,
launched from the home screen, and accepts Wayland clients from separate
binaries: native PS5 payload clients first, then Linux clients over waypipe
and finally Linux apps executed by `~/lkl-freebsd`.

Every claim about the console is either cited evidence or marked
[UNVERIFIED] until a probe on hardware records it.

## Decisions

- **Process model:** the compositor is an elfldr payload (own process, unjailed,
  root, can spawn, AF_UNIX, dlopen). A home-screen entry triggers it (pldmgr-style
  launcher app or a stub title that streams the ELF to `127.0.0.1:9021`).
- **Compositor base:** wlroots with a custom `ps5` backend (VideoOut output,
  scePad/sceKeyboard/sceMouse input) and the pixman renderer. GPU composition on
  RADV (`VK_KHR_display`, `VK_EXT_external_memory_host`) later.
- **Clients:** native PS5 payload clients first (wl_shm over AF_UNIX at
  `/system_tmp/wayland-0`). Then waypipe from a Linux PC, then LKL.
- **Same code builds for host Linux** (wlroots headless/wayland backends) so the
  protocol and window-management work is developed and CI-tested off console.

## Dev console

- PS5 at `192.168.3.211`. Running homebrew: `elfldr.elf` (loopback-only
  :9021), Payload Manager v0.5.2 (`pldmgr.elf`, HTTP :8084), `pkgmgr.elf`.
  No etaHEN, kstuff, ShadowMountPlus, FTP or klogsrv.
- Deploy path: Payload Manager HTTP API (`src/http_server.c`,
  `src/ps5_launcher.c` in https://github.com/itsPLK/ps5-payload-manager):
  `POST /manage:upload`, `POST /loadpayload:<path>`, `GET /list_payloads`,
  `GET /processes_list`, `POST /process_kill`. Actions need POST plus
  `X-PLDMGR-Request: 1`. elfldr receives no argv/env from this path.
- The dev box is WSL2 behind NAT; the console cannot be assumed to reach it.
  Payload logs therefore use a **pull model**: the payload listens on TCP and
  the host connects (see Contracts).

## Repository layout and ownership

```
PLAN.md, Makefile, .gitignore      top-level; Makefile only dispatches to scripts
toolchain/                         SDK install (repo-local .sdk/), env.sh, sdk.lock.json
examples/hello/                    toolchain smoke payload (uses common/ps5log)
common/ps5log/                     payload-side TCP log server (C, libc only)
tools/ps5ctl                       host CLI for Payload Manager + log pulling (Python stdlib)
deps/versions.lock.json            pinned upstream sources (URL, tag/commit, sha256) for host and PS5
deps/ps5/                          PS5 cross builds into build/ps5/sysroot, patches in deps/ps5/patches/
deps/host/                         host builds of the same pins into build/host/sysroot
cross/                             meson/cmake cross files for PS5 (wrapping the SDK's prospero files)
compositor/                        meson project `ps5wl` (wlroots compositor), host + PS5
build/, .sdk/                      generated, git-ignored
```

## Contracts

- `PS5_PAYLOAD_SDK` defaults to `<repo>/.sdk`; `source toolchain/env.sh` exports
  it and puts `prospero-*` on `PATH`. No sudo, no writes outside the repo except
  package caches.
- `PS5_HOST` defaults to `192.168.3.211`, `PLDMGR_PORT` to `8084`.
- Log pull: `common/ps5log` exposes
  `int ps5log_init(const char *name, uint16_t port); void ps5log(const char *fmt, ...);`
  It listens on `port` (default 9530; one port per concurrently running
  payload), keeps a ring buffer of early lines, and streams buffered + live
  lines to every connected client. Also mirrors to stderr/klog.
  `tools/ps5ctl logs --port N` connects and prints.
- `deps/versions.lock.json` is the single pin list; host and PS5 builds use
  identical upstream versions.
- Install prefixes: `build/ps5/sysroot` and `build/host/sysroot`
  (`lib/pkgconfig` inside each).

## Milestones

1. **M0 environment** — SDK installed and pinned, hello payload built,
   uploaded, launched and its logs pulled via `tools/ps5ctl`; dependency
   stack (libffi, epoll-shim, wayland, pixman, xkbcommon, libdrm, wlroots)
   building for host and PS5; `compositor/` skeleton building for both.
2. **M1 probes** — cross-process AF_UNIX on `/system_tmp`, SCM_RIGHTS fd
   passing, shared memory (shm_open/SHM_ANON/MAP_SHARED) coherence and
   `sceKernelMprotect` GPU bits, VideoOut from a payload alongside
   `AgcCompositor.elf` and input focus, spawning payloads from a payload,
   kqueue + flip-equeue integration, mapping granularity, syscall cost.
3. **M2 display** — `ps5` backend: RegisterBuffers2, damage-limited linear→
   64KB_R_X tile copy, flip-paced frame callbacks, screenshot over TCP.
4. **M3 compositor core** — xdg-shell, seat, output, TV-first window policy;
   headless CI with screenshots on host.
5. **M4 native clients** — simple-shm and own test clients launched by the
   compositor over AF_UNIX.
6. **M5 input** — pad→pointer/keyboard, sceKeyboard, sceMouse, sceImeDialog→
   text-input-v3.
7. **M6 native vkcube** — RADV payload Mesa + shm WSI path [UNVERIFIED].
8. **M7 remote/LKL** — waypipe from Linux PC; LKL D1 (waypipe in guest) and
   D2 (supervisor bridge aliasing `/lkl_phys_mem` pages).
9. **M8** — GPU composition on RADV; lifecycle (rest mode, game launch);
   home-screen launcher.

## Key evidence

- Payload vs title restrictions, elfldr spawn, AF_UNIX on `/system_tmp`:
  ps5-payload-dev/elfldr `elfldr.c`, etaHEN `daemon/source/msg.cpp`,
  prospero-win `docs/WINE_PS5_BUILD.md`.
- VideoOut from payloads: ps5-payload-dev/SDL `src/video/ps5/SDL_ps5video.c`,
  `SDL_ps5tilemap.c`; br1cks4t1ts/ps5-moonlight `docs/RESEARCH.md`.
- RADV on PS5, no dmabuf, external_memory_host: mihawk-99/PS5_Vulkan
  `docs/CTS_GAPS.md`, `docs/HARDWARE_FINDINGS.md`; pacbrew-repo `mesa/`.
- libwayland FreeBSD path (epoll-shim, broken MSG_CMSG_CLOEXEC before
  FreeBSD 13.0.502) and wlroots dependency set: upstream `meson.build` files.
