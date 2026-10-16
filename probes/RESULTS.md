# PS5 capability probe results

Date: **2026-10-16**. Console: `192.168.3.211`, Payload Manager v0.5.2,
elfldr PID 81. SDK `kernel_get_fw_version()` returned **`0x10600006`** in
probe PID 114. This is the exact raw firmware value; no guessed marketing
version. `kern.osrelease=0.0-prototype` is not the firmware version.

All payloads were compiled inside `env/run` with the pinned SDK and uploaded
and launched exclusively using `tools/ps5ctl`. Commands shown below returned
exit 0 unless explicitly stated. Payload exit status is not exposed by elfldr;
`probe complete` and log TCP EOF establish reaching the final probe code, not
an invented remote exit status. Source/protocol references: [SOURCES.md](SOURCES.md).

## 1. Cross-process Unix IPC, shared memory, descriptors and credentials

**Verdict: PASS** for separate-payload AF_UNIX stream data exchange and
SCM_RIGHTS transfers with full, bidirectional 32 MiB shared-memory coherence
for both `SHM_ANON` and named `shm_open`. **PASS** for file-backed `MAP_SHARED`
at 16 KiB; **FAIL / capacity constraint** for 32 MiB file backing in
`/system_tmp` (`ENOSPC`, errno 28). **CLOEXEC flags are silently ignored** and
must be implemented with explicit `fcntl(F_SETFD, FD_CLOEXEC)` by consumers.
`LOCAL_PEERCRED` works; `getpeereid` and `accept4` are absent runtime symbols.

Build/run/pull:

```sh
env/run make -C probes/ipc
tools/ps5ctl run probes/ipc/ps5wl-probe-ipc-server.elf
tools/ps5ctl run probes/ipc/ps5wl-probe-ipc-client.elf
tools/ps5ctl logs --port 9540
tools/ps5ctl logs --port 9541
```

Server PID 103 and client PID 104 are separate processes, each parented by
elfldr. They exchange a mapping-length header, SCM_RIGHTS message, validation
acknowledgement and reverse-direction validation result. Every byte of each
mapping is initialized, verified, overwritten by the server and verified by
the client, not merely first/last-page sampling. Exact excerpts:

```text
[16321520 ms] probe-ipc-server: started pid=103 ppid=81 uid=1 pagesize=16384
[16321520 ms] probe-ipc-server: socket SOCK_CLOEXEC fd=8 errno=0 FD_CLOEXEC=0
[16321520 ms] probe-ipc-server: bind /system_tmp/ps5wl-probe.sock rc=0 errno=0
[16321726 ms] probe-ipc-server: accept4 symbol=0 fd=-1 errno=32
[16321726 ms] probe-ipc-server: accept fallback fd=9 errno=0
[16321726 ms] probe-ipc-server: getpeereid symbol=0 rc=-1 errno=0 uid=4294967295 gid=4294967295
[16321726 ms] probe-ipc-server: LOCAL_PEERCRED rc=0 errno=0 size=88 version=0 uid=0 ngroups=1
[16321759 ms] probe-ipc-server: recvmsg MSG_CMSG_CLOEXEC rc=1 errno=0 flags=40000 kind=0 fd=8 FD_CLOEXEC=0
[16321821 ms] probe-ipc-server: server coherence client->server kind=0 all_33554432_bytes=PASS
[16321845 ms] probe-ipc-server: server coherence server->client kind=0 verdict=PASS
[16321881 ms] probe-ipc-server: recvmsg MSG_CMSG_CLOEXEC rc=1 errno=0 flags=40000 kind=1 fd=8 FD_CLOEXEC=0
[16321944 ms] probe-ipc-server: server coherence client->server kind=1 all_33554432_bytes=PASS
[16321969 ms] probe-ipc-server: server coherence server->client kind=1 verdict=PASS
[16321970 ms] probe-ipc-server: server coherence client->server kind=2 all_16384_bytes=PASS
[16321970 ms] probe-ipc-server: server coherence server->client kind=2 verdict=PASS
[16321970 ms] probe-ipc-server: probe complete
[16321725 ms] probe-ipc-client: started pid=104 ppid=81 uid=1 pagesize=16384
[16321726 ms] probe-ipc-client: create kind=0 (SHM_ANON) fd=9 errno=0
[16321726 ms] probe-ipc-client: ftruncate kind=0 length=33554432 rc=0 errno=0
[16321845 ms] probe-ipc-client: client coherence server->client kind=0 all_33554432_bytes=PASS server_ack=1
[16321846 ms] probe-ipc-client: create kind=1 (/ps5wl-probe-shm) fd=9 errno=0
[16321846 ms] probe-ipc-client: ftruncate kind=1 length=33554432 rc=0 errno=0
[16321968 ms] probe-ipc-client: client coherence server->client kind=1 all_33554432_bytes=PASS server_ack=1
[16321970 ms] probe-ipc-client: ftruncate kind=2 length=33554432 rc=-1 errno=28
[16321970 ms] probe-ipc-client: ftruncate smaller-file kind=2 length=16384 rc=0 errno=0
[16321970 ms] probe-ipc-client: client coherence server->client kind=2 all_16384_bytes=PASS server_ack=1
[16321970 ms] probe-ipc-client: probe complete
```

`accept4`'s logged errno 32 is stale from symbol lookup, not a syscall result:
its symbol is null and it was not called. `getpeereid` is likewise not callable.
`LOCAL_PEERCRED` reports uid 0 although libc `getuid()` reports 1; do not infer
peer identity from the latter without accounting for this measured mismatch.
The native `xucred` layout is 88 bytes, `cr_version=0`, `cr_ngroups=1`.

An initial, discarded experiment used the SDK FreeBSD `SYS_accept4=541`
constant. It returned integer 3 which was not an accepted socket
(`LOCAL_PEERCRED` errno 22); subsequent fd operations stalled. **FreeBSD
syscall numbers must not be assumed to describe PS5.** This raw syscall was
removed. The stale server was identified by its own PID-100 start log,
rechecked against `tools/ps5ctl ps`, then killed with the coordinator-approved
`tools/ps5ctl kill 100 --force` (response `{"ok":true,"message":"Killed"}`).
The corrected pair exited normally after unlinking its socket, named shm and
file; both pulls reached EOF. Neither shared-memory success depends on that
initial invalid experiment. Clients, Ps5Backend and Ps5Deps received the
corrected results immediately; Ps5Deps confirmed explicit CLOEXEC fallbacks.

## 2. Payload-to-payload spawning

**Verdict: PASS** for both `file:` URI and raw ELF bytes sent by a payload to
loopback `127.0.0.1:9021`. This is elfldr's already-documented transport;
our additional console evidence proves separate child PIDs and read access
to Payload Manager's uploaded files. Children are parented by elfldr, not by
the sending payload. Each child self-exits after its callback and one-second
log linger; the parent has a 60-second hard timeout.

```sh
env/run make -C probes/spawn
tools/ps5ctl upload probes/spawn/ps5wl-probe-spawn-child.elf
tools/ps5ctl payloads
tools/ps5ctl run probes/spawn/ps5wl-probe-spawn-parent.elf
tools/ps5ctl logs --port 9542
```

`payloads` returned the exact path embedded in `CHILD_PATH`:
`/data/pldmgr/payloads/ps5wl-probe-spawn-child/ps5wl-probe-spawn-child.elf`.
The URI is newline-terminated `file:<absolute-path>?pipe=0`; raw mode opens
that file and sends the ELF bytes, then shuts down its socket write side.
Children report their own PID over loopback port 9544, logged by the parent:

```text
[16345145 ms] probe-spawn-parent: parent started pid=108 ppid=81 uid=1 child_path=/data/pldmgr/payloads/ps5wl-probe-spawn-child/ps5wl-probe-spawn-child.elf
[16345145 ms] probe-spawn-parent: child file open mode=file URI fd=9 errno=0
[16345145 ms] probe-spawn-parent: elfldr send mode=file URI bytes=86 rc=0 errno=0
[16345326 ms] probe-spawn-parent: spawn mode=file URI callback bytes=27 parent_pid=108 child pid=109 ppid=81 uid=1
[16347326 ms] probe-spawn-parent: child file open mode=raw fd=9 errno=0
[16347327 ms] probe-spawn-parent: elfldr send mode=raw bytes=122472 rc=0 errno=0
[16347545 ms] probe-spawn-parent: spawn mode=raw callback bytes=27 parent_pid=108 child pid=110 ppid=81 uid=1
[16349545 ms] probe-spawn-parent: spawn probe complete
```

## 3. Native events and equeue-to-POSIX integration

**Native verdict: PASS** for `socketpair`, `pipe`, `poll`, kqueue/kevent
`EVFILT_READ` on AF_UNIX/TCP/pipe, `EVFILT_TIMER`, `EVFILT_USER`, and a
`sceKernelWaitEqueue` worker thread signalling a socketpair polled by the
main thread. No VideoOut ownership or human observation is involved.

```sh
env/run make -C probes/events
tools/ps5ctl run probes/events/ps5wl-probe-events.elf
tools/ps5ctl logs --port 9545
```

Exact native-only payload log excerpts:

```text
[16542263 ms] probe-events-native: preamble pid=121 firmware_raw=0x10600006
[16545264 ms] probe-events-native: socketpair rc=0 errno=0
[16545264 ms] probe-events-native: kqueue EVFILT_READ AF_UNIX add rc=0 errno=0
[16545264 ms] probe-events-native: kqueue EVFILT_READ AF_UNIX wait rc=1 errno=0 ident=9 filter=-1 flags=10 data=1
[16545264 ms] probe-events-native: poll AF_UNIX rc=1 revents=1 read=1 byte=R
[16545264 ms] probe-events-native: pipe rc=0 errno=0
[16545264 ms] probe-events-native: kqueue EVFILT_READ pipe wait rc=1 errno=0 ident=9 filter=-1 flags=10 data=1
[16545264 ms] probe-events-native: poll pipe rc=1 revents=1 read=1 byte=R
[16545264 ms] probe-events-native: TCP setup rc=0 errno=0
[16545264 ms] probe-events-native: kqueue EVFILT_READ TCP wait rc=1 errno=0 ident=11 filter=-1 flags=10 data=1
[16545264 ms] probe-events-native: poll TCP rc=1 revents=1 read=1 byte=R
[16545264 ms] probe-events-native: kqueue TIMER add rc=0 errno=0
[16545314 ms] probe-events-native: kqueue TIMER wait rc=1 errno=0 filter=-7 flags=30 data=1
[16545314 ms] probe-events-native: kqueue USER add rc=0 errno=0
[16545314 ms] probe-events-native: kqueue USER trigger rc=0 errno=0
[16545314 ms] probe-events-native: kqueue USER wait rc=1 errno=0 filter=-11 flags=10 data=0
[16545314 ms] probe-events-native: sceKernelCreateEqueue rc=0 queue=200000000d
[16545314 ms] probe-events-native: sceKernelAddReadEvent rc=0
[16545314 ms] probe-events-native: pthread_create rc=0
[16545314 ms] probe-events-native: sceKernelWaitEqueue thread rc=0 count=1 ident=9 filter=-1 data=1
[16545314 ms] probe-events-native: sce equeue -> thread -> socketpair poll rc=1 revents=1
[16545314 ms] probe-events-native: sceKernelDeleteEqueue rc=0
[16545314 ms] probe-events-native: events probe complete
```

The 50 ms timer elapsed from monotonic 16545264 to 16545314 ms.
`struct kevent` and equeue declarations follow shipping SDL's PS5 ABI,
as cited in `events/main.c`. The queue value is opaque; it must not be
dereferenced as a normal userspace pointer.

### epoll-shim runtime discovery

The original combined native+shim payload terminated before its early logs
could all stream. Instrumentation and isolation established that native APIs
work, whereas shim interposition initialization looked up an absent dynamic
`ppoll`. Exact symbol evidence:

```text
[16473744 ms] probe-events: runtime symbol socketpair=81a838f30
[16473744 ms] probe-events: runtime symbol pipe=81a838120
[16473744 ms] probe-events: runtime symbol kqueue=81a839890
[16473744 ms] probe-events: runtime symbol kevent=81a8398b0
[16473744 ms] probe-events: runtime symbol ppoll=0
[16473744 ms] probe-events: runtime symbol poll=81a846cc0
```

Ps5Deps patched epoll-shim to bind the SDK's static `ppoll` implementation
directly instead of failing its runtime `dlsym` lookup. The events build now
produces native-only and shim-only payloads so a shim failure cannot invalidate
native results. All process-directed SIGUSR1 signals are blocked before
starting the logger thread, as required for a valid `signalfd` test.

**Repaired epoll-shim verdict: PASS on the real console.** The shim-only
payload was relinked against the repaired static archive, uploaded and run:

```sh
env/run make -C probes/events
tools/ps5ctl run probes/events/ps5wl-probe-events-shim.elf
tools/ps5ctl logs --port 9548
```

Exact excerpts establish eventfd counter delivery, a 51 ms timerfd expiry and
SIGUSR1 signalfd delivery through `epoll_wait`:

```text
[16603362 ms] probe-events-shim: preamble pid=123 firmware_raw=0x10600006
[16607362 ms] probe-events-shim: epoll_create1 fd=9 errno=0
[16607362 ms] probe-events-shim: shim eventfd create fd=10 errno=0
[16607362 ms] probe-events-shim: shim kind=0 epoll_ctl rc=0 errno=0
[16607362 ms] probe-events-shim: shim kind=0 signal rc=8 errno=0
[16607362 ms] probe-events-shim: shim kind=0 epoll_wait rc=1 errno=0 events=1
[16607362 ms] probe-events-shim: shim kind=0 read=8 first_u64=7
[16607362 ms] probe-events-shim: shim timerfd create fd=10 errno=0
[16607362 ms] probe-events-shim: shim kind=1 signal rc=0 errno=0
[16607413 ms] probe-events-shim: shim kind=1 epoll_wait rc=1 errno=0 events=1
[16607413 ms] probe-events-shim: shim kind=1 read=8 first_u64=1
[16607413 ms] probe-events-shim: shim signalfd create fd=10 errno=0
[16607413 ms] probe-events-shim: shim kind=2 signal rc=0 errno=0
[16607413 ms] probe-events-shim: shim kind=2 epoll_wait rc=1 errno=0 events=1
[16607413 ms] probe-events-shim: shim kind=2 read=128 first_u64=30
[16607413 ms] probe-events-shim: events probe complete
```

Here signalfd's low 32-bit signal number is 30, the SDK's `SIGUSR1`;
`read=128` is one complete emulated `signalfd_siginfo` record. Native and
shim pulls reached EOF after their bounded log linger. The shim results
were immediately sent to Ps5Deps, Ps5Backend and Clients.

A final relink/run (PID 126) also tested the temporary signal-mask path
through `epoll_pwait`, with no remaining events. It returned zero and restored
the caller's original blocked SIGUSR1 mask; the same run again passed all
three emulated descriptor tests:

```text
[16659462 ms] probe-events-shim: shim kind=2 read=128 first_u64=30
[16659462 ms] probe-events-shim: epoll_pwait temporary empty mask rc=0 errno=0 query_rc=0 SIGUSR1_restored_blocked=1
[16659462 ms] probe-events-shim: events probe complete
```

### Descriptor duplication: compositor initialization regression

The compositor subsequently exposed `wl_display_create()` returning NULL
with errno 45. An additional real-console probe (PID 140) compared the
original emulated eventfd against both duplication commands.

**Verdict:** `F_DUPFD_CLOEXEC` is unsupported (`EOPNOTSUPP`, not `EINVAL`).
`F_DUPFD` succeeds and its alias can be watched with epoll, but the alias
cannot be read through epoll-shim because duplication does not copy its
userspace descriptor metadata. The original descriptor still works.

```text
[16992435 ms] probe-events-shim: eventfd alias mode=original original=10 alias=10 dup_errno=0
[16992435 ms] probe-events-shim: eventfd alias mode=0 epoll_ctl ADD rc=0 errno=0
[16992435 ms] probe-events-shim: eventfd alias mode=0 write_original=8 errno=0
[16992435 ms] probe-events-shim: eventfd alias mode=0 epoll_wait rc=1 errno=0 events=1
[16992435 ms] probe-events-shim: eventfd alias mode=0 read_alias=8 errno=0 value=11
[16992435 ms] probe-events-shim: eventfd alias mode=F_DUPFD original=10 alias=11 dup_errno=0
[16992435 ms] probe-events-shim: eventfd alias mode=1 epoll_ctl ADD rc=0 errno=0
[16992435 ms] probe-events-shim: eventfd alias mode=1 write_original=8 errno=0
[16992435 ms] probe-events-shim: eventfd alias mode=1 epoll_wait rc=1 errno=0 events=1
[16992435 ms] probe-events-shim: eventfd alias mode=1 read_alias=-1 errno=45 value=0
[16992435 ms] probe-events-shim: eventfd alias mode=F_DUPFD_CLOEXEC original=10 alias=-1 dup_errno=45
```

Ps5Deps confirmed that Wayland watches the duplicate but invokes its callback
with the original fd. Therefore this tested alias-readiness behavior suffices
for Wayland; supporting arbitrary reads through shim aliases would be extra
scope. Ps5Deps owns the measured `EOPNOTSUPP` fallback to checked
`F_DUPFD` plus `F_SETFD(FD_CLOEXEC)`. The descriptor tests remain as a
regression probe, with the general alias-read limitation explicitly recorded.

A follow-on control run (PID 141) proves the unsupported command is not
specific to emulated eventfds. It also verifies explicit CLOEXEC setting:

```text
[17044529 ms] probe-events-shim: eventfd alias mode=1 CLOEXEC before=0 setrc=0 errno=0 after=1
[17044529 ms] probe-events-shim: eventfd alias mode=F_DUPFD_CLOEXEC original=10 alias=-1 dup_errno=45
[17044529 ms] probe-events-shim: socketpair alias mode=0 CLOEXEC before=0 setrc=0 errno=0 after=1
[17044529 ms] probe-events-shim: socketpair alias mode=0 epoll_ctl ADD rc=0 errno=0
[17044529 ms] probe-events-shim: socketpair alias mode=0 epoll_wait rc=1 errno=0 events=1
[17044529 ms] probe-events-shim: socketpair alias mode=0 read_alias=1 errno=0 byte=D
[17044529 ms] probe-events-shim: socketpair alias mode=1 original=10 alias=12 dup_errno=0
[17044529 ms] probe-events-shim: socketpair alias mode=1 CLOEXEC before=0 setrc=0 errno=0 after=1
[17044529 ms] probe-events-shim: socketpair alias mode=1 epoll_ctl ADD rc=0 errno=0
[17044529 ms] probe-events-shim: socketpair alias mode=1 write_peer=1 errno=0
[17044529 ms] probe-events-shim: socketpair alias mode=1 epoll_wait rc=1 errno=0 events=1
[17044529 ms] probe-events-shim: socketpair alias mode=1 read_alias=1 errno=0 byte=D
[17044529 ms] probe-events-shim: socketpair alias mode=2 original=10 alias=-1 dup_errno=45
[17044529 ms] probe-events-shim: events probe complete
```

Both control runs built successfully inside `env/run`, used the same
`ps5wl-probe-events-shim.elf` deployment and port 9548, completed their
bounded log linger and reached EOF. No probe payload remains running.

## 4–5. VideoOut and input

**Verdict: removed from probe scope by coordinator directive.** Shipping SDL
and prospero-win are the reference implementations, not grounds for asking
the user to verify basics. The drafted VideoOut/input observation payloads
were deleted before deployment, and no such hardware result is claimed.
Their human-observation requests are absent from `local://need-user.md`.
The compositor backend owns on-console API execution and screen capture.

## 6. Optional GPU protection on shared memory

**Not exercised.** No zero-copy/GPU-access capability is inferred from CPU
shared-memory coherence.
