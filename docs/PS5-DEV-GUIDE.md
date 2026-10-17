# PS5 development: getting started and what we know

For the LKL-on-PS5 work. Everything here was measured on our dev console unless
it is marked **[UNTESTED]**, **[EXTERNAL]** (from public homebrew sources) or
**[INFERENCE]**. Paths are relative to `~/ps5_wayland`. Raw evidence is under
`build/ps5/evidence/`. The detailed tables are in `probes/RESULTS.md`,
`probes/lkl/RESULTS.md` and `gpu-server/probes/RESULTS.md`.

## 1. The console

| Item | Value |
|---|---|
| Address | `192.168.3.211` |
| Firmware | 10.60 (`kernel_get_fw_version()` = `0x10600006`), FreeBSD 11 based (`__FreeBSD_version` 1104001 in SDK headers) |
| Homebrew running after jailbreak | `elfldr.elf` (listens on **127.0.0.1:9021 only**), Payload Manager v0.5.2 (`pldmgr.elf`, HTTP **:8084**) |
| Not present | etaHEN, kstuff, ShadowMountPlus, FTP, klogsrv |
| After a reboot | Jailbreak + autoloader must be rerun by the user. The home-screen icon survives, but its resident launcher (`ps5wl-install.elf`) does not. |
| Auto power-down | Can't be disabled in Settings outside a game/movie. Long idle sessions end with the console powered off. Run the keepalive (§3.4). |

A console crash looks like this: Payload Manager stops answering, then ping
dies, then the user finds it off and has to hard power-cycle. Every crash costs
the user a manual recovery, so §5 is mandatory reading.

## 2. Build environment

The WSL2 Ubuntu host is only a launcher. **Nothing builds against host packages.**

- `env/run <cmd...>` runs a command inside the pinned rootless podman image
  `ps5wl-build`. The image is Arch at a digest-pinned base, with packages from
  the 2026/10/15 Arch Archive snapshot, giving LLVM/clang 23.1.1 and meson
  1.12.1. It mounts the repo at the same absolute path, runs as your uid, and
  sets `PS5WL_BUILD_ENV=1` and `LLVM_CONFIG=/usr/bin/llvm-config`.
  Definition: `env/Containerfile`, `env/env.lock.json`.
- `make sdk` installs ps5-payload-dev/sdk **v0.44** into `.sdk/`, with its
  checksum in `toolchain/sdk.lock.json`. libc++/libc++abi/libunwind are bundled.
- `source toolchain/env.sh` (inside `env/run`) puts **`cross/bin`** ahead of the
  SDK's `bin`. Always compile PS5 code with `cross/bin/prospero-clang`, never the
  raw SDK compiler. It links the PS5 compat shim and the required `--wrap`
  flags (§6).
- Meson/CMake: `cross/setup.sh` generates `build/ps5/cross.ini` and
  `native.ini`; `cross/prospero.cmake` covers CMake. Static target libraries go
  in `build/ps5/sysroot`, with pkg-config confined to it through
  `cross/pkg-config`.
- Pins: every third-party source is in `deps/versions.lock.json` (URL, tag,
  sha256). Patches live under `deps/ps5/patches/<dep>/` (and `deps/mesa/`,
  `deps/llvm/`).

Minimal payload template: `examples/hello/` (Makefile plus `main.c`). Build it
with `make hello`; the Makefile re-enters `env/run` itself.

```make
CC := $(REPO)/cross/bin/prospero-clang
CPPFLAGS += -I$(REPO)/common/ps5log
CFLAGS += -O2 -g -Wall -Wextra -pthread
hello.elf: main.c $(REPO)/common/ps5log/ps5log.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -pthread -o $@
```

## 3. Deploying, running and logging

### 3.1 `tools/ps5ctl` (Python stdlib, runs on host or in `env/run`)

| Command | What it does |
|---|---|
| `ps5ctl run FILE` | Uploads through Payload Manager (`POST /manage:upload?filename=`, raw ELF body), then launches (`POST /loadpayload:<path>`, which Payload Manager streams to elfldr on loopback 9021). |
| `ps5ctl upload` / `launch` / `delete` | Individual steps. |
| `ps5ctl ps` | Process list (`/processes_list`): pid, name, MiB, daemon. |
| `ps5ctl kill PID [--force]` | Uses Payload Manager's `/process_kill` (SIGKILL). |
| `ps5ctl logs --port N [--retry S]` | Pulls a payload's log stream. |
| `ps5ctl screenshot --port 9531 --output f.png` | Grabs the compositor's scanout. |
| `ps5ctl pmlog` | Payload Manager's own log. |

Rules built into the tool:
- Upload names get a `ps5wl-` prefix.
- Receipts in `.cache/ps5ctl/uploads.json` limit launch/delete/kill to payloads
  this checkout uploaded.
- Payload Manager actions need POST plus the header `X-PLDMGR-Request: 1`;
  the tool sends it.

**Payloads receive no argv and no environment.** Bake configuration in at
compile time, or read a file under `/data`.

### 3.2 Logging (pull model)

The console can't reach the WSL host (NAT), so payloads **listen** and the host
connects. Use `common/ps5log`:

```c
ps5log_init("name", 9600);   /* listens on TCP 9600; 0 = ephemeral port */
ps5log("x=%d", x);           /* ring buffer of 256 lines, replayed to late clients */
ps5log_flush_wait(2500);     /* linger before exit so a late pull still gets the log */
ps5log_emergency_write(buf, len); /* async-signal-safe, for fatal handlers */
```

Then on the host: `tools/ps5ctl logs --port 9600 --retry 30`. Always log an
explicit final `EXIT <code>` line: the host never sees a payload's exit status.

Port usage so far:
- 9530: compositor
- 9531: screenshots
- 9539: keepalive
- 9540–9559: probes
- 9560–9579: clients
- 9580–9589: launcher
- 9590–9599: Vulkan clients
- 9600–9619: LKL capability probes

### 3.3 Spawning one payload from another

Connect to `127.0.0.1:9021` and send either a raw ELF or a line
`file:/data/path.elf?args=...&pipe=0` (elfldr `socksrv.c`). The new process's
**parent is elfldr, not you**:
- `wait4` on it returns ECHILD;
- `kqueue EVFILT_PROC NOTE_EXIT` on its pid works.

Payload Manager stores uploads in `/data/pldmgr/payloads/<name>/<name>.elf`.
Find the real path with `ps5ctl payloads`.

### 3.4 Keepalive

`build/ps5/keepalive/ps5wl-keepalive.elf` (source `compositor/tools/keepalive.c`)
calls `sceShellCoreUtilResetAutoPowerDownTimer` and `sceSystemServicePowerTick`
every 30 s and logs on 9539. In `ps` it shows up as `payload.elf`. Start it
at the beginning of a console session.

The foreground app should call **only** `sceSystemServicePowerTick`. The one
console loss during a startup that called Reset from the foreground app went
away when Reset was removed. Causality isn't proven.

## 4. Execution contexts

| | Plain payload (elfldr) | Foreground app ("BigApp", via hbldr) |
|---|---|---|
| How it starts | `ps5ctl run` | `launcher/` (websrv-hbldr technique, title `PSWL00002`): home icon or `launcher/test.sh --diagnostic-elf` |
| Credentials | uid 1 / euid 0 (logged) | uid/euid 0 |
| GPU direct memory | **None usable.** elfldr sets `sys_budget_set(0)`; 8 MiB `AllocateDirectMemory` fails `0x80020023`. Small (64 KiB) allocations passed only while an app was foreground. | 12 GiB pool; 2 MiB–256 MiB allocation ladders all pass. |
| Flexible (general) memory | Large (see §7) | **~409 MiB available.** 256 MiB SHM maps OK, 1 GiB mmap fails ENOMEM. |
| VideoOut / input focus | Don't. A background payload with VideoOut broke the shell (restart failed). | Works (59.94 Hz, scePad/sceKeyboard/sceMouse). |
| fork | Works (§7) | **Crashes the console.** Never. |
| Exit | `_exit` is fine | Only `sceSystemServiceLoadExec("exit", NULL)`, or the external shipping close (`launcher/close.c`: SuspendApp → SIGKILL → `sceLncUtilKillApp`). Raw `_exit`/SIGKILL leaves an unkillable zombie that blocks restart. |

For LKL [INFERENCE]: supervisor and workers fit plain payloads, which have the
big memory, fork and ptrace (with the credential pattern below). Anything needing
the GPU must go through the foreground compositor's GPU server (§8).

## 5. Safety rules (each one is from a real incident)

1. **No fork/rfork/vfork in the foreground app.** One raw fork crashed the console.
2. **No VideoOut from a plain payload.** It left the PS menu up but every system
   action, including restart, failed.
3. **Foreground apps exit only via LoadExec("exit") or the shipping close.**
4. **Tear down GPU users before the GPU owner exits.** Two console losses
   followed compositor teardown with a stuck Vulkan client still attached.
5. **Remounting `/system_ex` read-only after writing it can hang the process
   unkillably.** Mirror websrv: remount rw and never restore.
6. **ptrace only your own children,** and only with the elfldr credential
   pattern (§7).
7. **Bound every probe.** Use an alarm or watchdog under 60 s and log `EXIT`.
   Check `ps5ctl ps` before and after each run, and **stop all console actions
   the moment it fails**.
8. **Watch for user activity.** The user may launch the app from the icon. If a
   foreground app you didn't start appears, don't close it; ask.

## 6. libc / ABI gotchas (all observed)

| Issue | Handling |
|---|---|
| `fcntl(F_DUPFD_CLOEXEC)` can **silently return an unrelated fd** (fd 0, a socket) instead of a duplicate. It returned EOPNOTSUPP in other cases. | The shim never passes it to the kernel; it does F_DUPFD + F_SETFD. |
| `SOCK_CLOEXEC`/`SOCK_NONBLOCK`, `MSG_CMSG_CLOEXEC`, `O_CLOEXEC` (open/shm_open) are silently ignored. | The shim applies the flags with explicit fcntl. libwayland's broken-`MSG_CMSG_CLOEXEC` path is auto-selected for FreeBSD < 13. |
| `accept4`, `getpeereid` absent at runtime. `LOCAL_PEERCRED` works (xucred size 88). | The shim implements accept4. Use LOCAL_PEERCRED. |
| SDK `ppoll` is a naked syscall 545 with no errno handling. On an empty fd with timeout `{0,0}` it returned **+3**. `dlsym("ppoll")` = NULL. | The shim's `--wrap=ppoll` goes to native `poll`/`pselect`, resolved via `dlopen("/system/common/lib/libkernel_sys.sprx")` + `dlsym`, **and the handle is never dlclosed**: an immediate dlclose crashed a later startup. |
| SDK raw-syscall wrappers (`libc/syscalls.c`) don't translate carry→errno or fork's second return. | Use your own inline asm (`probes/lkl/cap.h` `raw6()`), checking carry. |
| **PS5 reuses the FreeBSD 532+ syscall range**: 533/534 are JIT create/alias, not `cap_rights_limit`. PDEATHSIG via 544 returns ESRCH. | Never guess modern syscall numbers. |
| SDK C11 threads return `thrd_success = 4`; Mesa/most code expects 0. | Use Mesa's own C11 implementation (`C11_THREADS_RENAME`) or pthreads. |
| `__thread` works in payloads via the SDK's emutls. No native PT_TLS in DSOs. | Fine for payloads. Raw-created threads must not touch libc TLS. |
| Shared libraries: SDK rtld `dlclose` doesn't really unload. `dladdr` fails for custom-loaded DSOs. `__dlsym` rejects `RTLD_NEXT` (ENOSYS). A DSO linked `-Bsymbolic` with a version script hides `dlopen`/`mmap`/`mprotect`; it then calls SDK-private stubs with NULL hooks (lavapipe crashed in `strlen(NULL)` and `kernel_mprotect` = NULL). | Every PS5 shared link uses `cross/public-loader.dyn`: `dlopen dlsym dladdr dlclose dlerror mmap mprotect` stay global. |
| **Signal frame differs from SDK headers:** mcontext starts at `ucontext+64` (the SDK assumes +16), RIP is at `uc+224`, and native mcontext length is 1152. | Use the adapter in `probes/lkl/faults.c`. With it, RIP-skip resume works. |
| A truncated shared backing raises **SIGSEGV** (si_code 2), not SIGBUS. Trap 12 = `T_PAGEFLT`, err 4 = user read. | Classify faults by your own VMA/backing knowledge. |
| `/system_tmp` is tiny: writes hit ENOSPC after ~256 KiB. File-backed MAP_SHARED there failed at 32 MiB. | Use it for sockets only. Big files go to `/data/...`. |
| No eventfd/timerfd/signalfd natively. epoll-shim provides them on kqueue, after binding ppoll statically. | Reading a **dup** of a shim eventfd fails; read the original fd. |
| `execve` of an unsigned ELF from `/data` returns **EACCES**. | Spawn through elfldr (§3.3), or load code yourself. |
| Payload default thread affinity was CPUs 9, 11, 13, 14, 15 [EXTERNAL, PS5LM]. Busy-spinning all threads starved the system and shut the console down. | Don't spin-wait on every core. |

## 7. Capability results relevant to LKL (`probes/lkl/RESULTS.md`)

Context: plain payload, firmware 10.60. **PASS** = executed on hardware.

### Memory and execution
- **PASS** Anonymous RW→RX and RWX execute.
- **PASS** `SHM_ANON` mapped RW in process A, sent by SCM_RIGHTS, mapped RX
  and executed in process B (an elfldr-spawned helper).
- **PASS** Raw JIT syscalls 533 (create) / 534 (alias) give an RX object plus
  RW alias that executes.
- **FAIL** Executing through `sceKernelJitMapSharedMemory`'s returned
  address faults (cause unknown). Use the raw path or SHM_ANON.
- **Page size is 16 KiB.** 4 KiB-aligned `MAP_FIXED` addresses **and** file
  offsets fail EINVAL through libc. This is the main design-level gap for
  LKL's 4 KiB PTE projection. Raw `SYS_mmap` bypass probes are built but
  **[UNTESTED]**.
- **VA range:**
  - `MAP_FIXED` map/protect/unmap cycles **PASS** at 0x400000, 0x10000000,
    0x100000000, 0x1000000000 and 0x8000000000 (4 MiB to 512 GiB samples).
  - Reservations at 0xff00000000 and above **FAIL** ENOMEM, up to
    0x7ffffff00000.
  - Plan for a lower guest VA layout.
- **Large shared memory:**
  - 1, 2 and 4 GiB SHM_ANON contiguous maps **PASS**.
  - An **8 GiB object works** through 16 KiB windows (64 windows, including the
    last page).
  - An 8 GiB contiguous map fails ENOMEM.
- Flexible memory budget: payload-measured ~6 GiB malloc ceiling [EXTERNAL,
  PS5LM]. Holding ~6 GiB froze the home screen in their tests. We only probed
  to a 128 MiB resident safety cap (PASS).
- Limits: `RLIMIT_DATA` 32 GiB, `RLIMIT_AS` infinite, `NPROC` 1604, `NOFILE`
  13952. Thread creation hit EAGAIN after 510 new threads.

### Processes and threads
- **PASS** raw fork/vfork/rfork from a plain payload. The forked child keeps
  a working libkernel.
- **PASS** own-child kqueue NOTE_EXIT and wait4.
- **FAIL** `execve` of an unsigned runner (EACCES).
- **PASS** elfldr-spawned workers (non-child, kqueue observable).
- **FAIL** `PROC_PDEATHSIG_CTL`. Use control-channel EOF/deadlines instead.
- **PASS** `thr_new` with custom stack + TLS, per-thread `amd64_set_fsbase`/
  gsbase (sysarch), `thr_kill2` to a specific LWP, SIGUSR2 interrupting a
  spinning worker.

### Synchronisation
- **PASS** cross-process `_umtx_op` `WAIT_UINT`/`WAKE` on shared memory.
- `*_PRIVATE` variants don't rendezvous across processes (expected).

### Signals
- **PASS** SIGSEGV with si_addr/si_code/trap/err, full GPRs, RIP-skip resume,
  and sigaltstack, all using the PS5 frame adapter (§6).

### CPU state
- **PASS** XSAVE/XSAVEC/XRSTOR (XCR0 = 7, area 832 bytes).
- CPUID: AVX/AVX2/BMI1/BMI2/FMA/AES/SHA present, no AVX-512.
- **PASS** RDTSC/RDTSCP, invariant TSC, ~1.596 GHz (short sample).

### ptrace
- **PASS** PT_TRACE_ME (plain).
- **FAIL** plain PT_ATTACH (EPERM).
- **PASS** with elfldr's credential pattern (`launcher/vendor/pt.c`): set the
  caller's authid to `0x4800000000010003` plus all caps around the call, then
  restore immediately. PT_ATTACH, GETREGS/SETREGS, PT_IO and detach on our own
  child all worked.

### IPC
- **PASS** AF_UNIX at `/system_tmp/...`, SCM_RIGHTS between payloads, 32 MiB
  shared-memory coherence both ways, `SOCK_SEQPACKET` socketpair.
- **PASS** TCP listen from payloads.

### [UNTESTED] (built in `probes/lkl/`, never run)
- Unprivileged TCP/UDP/ICMP (`network`).
- Storage throughput on `/data` (`storage`).
- `KERN_PROC_VMMAP`/RSS (`introspection`).
- Full ABI census: memfd_create, posix_spawn, copy_file_range, pdfork,
  getrandom, cap_*, native eventfd/timerfd (`abi`).
- Raw-syscall 4 KiB / high-VA bypass (`backing`).
- Plain-trace GETREGS/PT_IO (`ptrace`).

Run them with:

```sh
env/run make -C probes/lkl
python3 probes/lkl/run.py network storage introspection   # low-risk batch
python3 probes/lkl/run.py --ack-window backing backing      # risky: one at a time
```

Evidence lands in `build/ps5/evidence/lkl/`.

### Design implications [INFERENCE]
- 16 KiB host pages versus 4 KiB guest PTEs needs a projection strategy:
  coalescing, software enforcement, or a raw bypass if the untested probe
  passes.
- The guest VA layout must stay below ~0xff00000000 unless a raw path proves
  otherwise.
- Workers can't `execve`. Use fork (proven) or elfldr spawn plus a bootstrap
  handshake. Watch exits with kqueue rather than PDEATHSIG.
- Use the PS5 signal-frame layout when resuming faults.
- Link everything with `cross/bin`, so the shim, the ppoll fix and the public
  loader symbols are always in.

## 8. GPU and display facts (if the LKL desktop needs them)

- The compositor (foreground app) hosts **`ps5wl-gpud`**: virglrenderer 1.2.0
  vkr on RADV, running as threads. Upstream render-server fork/exec is removed.
- It has two front ends:
  - vtest at `/system_tmp/ps5wl-gpu-0`, used by native Venus clients
    (proven: vkcube, 600+ frames on the TV);
  - the virglrenderer render protocol at `/system_tmp/ps5wl-gpu-lkl` (stream
    or seqpacket), **built and host-tested for the LKL virtio-gpu device**.
- The exact contract for an LKL virtio-gpu device model is in
  `gpu-server/render/LKL-DEVICE.md`:
  - capset discovery extension;
  - host-visible blobs as server-created SHM fds over SCM_RIGHTS, 16 KiB aligned;
  - fences via shmem seqnos (no eventfd);
  - a virtio-mmio shm-region gap in `tools/lkl/lib/virtio.c`;
  - a new device-PFN mapping class.
- Memory for GPU-visible host memory: RADV imports `SHM_ANON` pages via
  `VK_EXT_external_memory_host` (proven coherent cross-process at 16 KiB,
  8 MiB and 64 MiB). There's no dma-buf or opaque-fd export on PS5.
- Per-context quota in the app is 128 MiB, at most 2 contexts, because of the
  ~409 MiB flexible headroom.
- Wayland: the compositor socket is `/system_tmp/wayland-0`, and clients use
  wl_shm.

## 9. References

**In this repo**
- `PLAN.md`: decisions and console facts.
- `probes/RESULTS.md`: IPC, events, spawn.
- `probes/lkl/RESULTS.md`: LKL capabilities.
- `gpu-server/probes/RESULTS.md`: GPU, power, ppoll, teardown history.
- `deps/ps5/README.md`: patches, shim.
- `launcher/README.md`: BigApp lifecycle.
- `compositor/README.md`: backend, power.

**External**
- [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) (v0.44)
- [ps5-payload-dev/elfldr](https://github.com/ps5-payload-dev/elfldr)
- [websrv `hbldr.c`](https://github.com/ps5-payload-dev/websrv/blob/master/src/ps5/hbldr.c)
- [itsPLK/ps5-payload-manager](https://github.com/itsPLK/ps5-payload-manager) (v0.5.2)
- [PS5LM `docs/CONSOLE.md`](https://raw.githubusercontent.com/abhi245y/PS5LM/main/docs/CONSOLE.md): payload memory/CPU limits
- [prospero-win `docs/WINE_PS5_BUILD.md`](https://github.com/mpereiraesaa/prospero-win/blob/main/docs/WINE_PS5_BUILD.md): title restrictions
- [mihawk-99/PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) `docs/HARDWARE_FINDINGS.md`
- [ps5-payload-dev/SDL](https://github.com/ps5-payload-dev/SDL) PS5 video/input backend
