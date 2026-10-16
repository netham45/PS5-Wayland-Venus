# Probe implementation sources

Build only through the pinned container, for example `env/run make -C probes/ipc`.
All probe Makefiles use `cross/bin/prospero-clang`, the repository's canonical
compiler proxy, so PS5 descriptor compatibility wrappers apply to every link.
Historical raw-SDK results are labeled in RESULTS.md; they are not retained
as an unwrapped build path.
Deploy only through `tools/ps5ctl`. Ports: IPC 9540/9541, spawn parent 9542,
spawn child 9543 and callback 9544, native events 9545, epoll-shim events 9548.
The exact-size duplicated-shm IPC variants use server 9549 and client 9550.
The IPC server must launch before its client. Upload the spawn child without
launching before running its parent; `CHILD_PATH` records the Payload Manager
path returned by `tools/ps5ctl payloads` on the development console.

`ipc-exact` variants use 8,294,400-byte SHM_ANON pools, matching a 1920x1080
wl_shm frame. Kinds 0/1/2 send the original descriptor, an F_DUPFD duplicate,
or an F_DUPFD_CLOEXEC attempt with explicit fallback. Duplicated cases close
the original descriptor before SCM_RIGHTS, retain the original mapping, and
log fstat/flags plus read/read-write shared/private mappings of the original,
duplicate and received descriptor. This isolates fd generation/duplication
from foreground compositor permissions; both probe processes remain payloads.
The exact variants also log creation-time CLOEXEC/nonblocking state for
socket/socketpair and CLOEXEC state for open/shm_open before any manual
flag repair, plus descriptor flags after recvmsg. These are regression
controls for the canonical compatibility wrapper.
Exact-target controls replace an owned empty-file descriptor with a 16 KiB
SHM descriptor using F_DUP2FD_CLOEXEC and dup3(O_CLOEXEC). A PASS requires the
requested target number, matching fstat identity and size, CLOEXEC, and
bidirectional coherence between source and target mappings.
The live IPC connection also calls the canonical accept4 wrapper with
CLOEXEC|NONBLOCK, logs both accepted-descriptor flags and the native symbol,
then restores blocking mode for the byte-coherence exchange. The events
probe covers all five alias modes (original, F_DUPFD, F_DUPFD_CLOEXEC,
F_DUP2FD_CLOEXEC, dup3), including readiness on an alias followed by a read
on the original eventfd when the shim cannot read duplicated metadata.

- [etaHEN Unix IPC](https://github.com/etaHEN/etaHEN/blob/main/Source%20Code/daemon/source/msg.cpp#L477-L495)
  already demonstrates AF_UNIX `socket`/`bind`/`listen`. It does not establish
  this probe's separate-elfldr-payload 32 MiB shared-memory coherence or
  SCM_RIGHTS/CLOEXEC behavior; those are measured here.
- [elfldr socksrv.c](https://github.com/ps5-payload-dev/elfldr/blob/master/socksrv.c):
  raw ELF and newline-terminated `file:` URI on loopback port 9021.
- [SDL PS5 VideoOut](https://github.com/ps5-payload-dev/SDL/blob/release-2.30.x-ps5/src/video/ps5/SDL_ps5video.c)
  `PS5_UpdateWindowFramebuffer`: `sceKernelWaitEqueue` for flip events, after
  `sceKernelCreateEqueue` and `sceVideoOutAddFlipEvent` in `PS5_VideoInit`.
  The events probe tests bridging a native equeue thread into a POSIX socketpair
  without taking over VideoOut. No SDL implementation code is copied.
- [websrv hbldr.c](https://github.com/ps5-payload-dev/websrv/blob/master/src/ps5/hbldr.c)
  `bigapp_launch` already uses kqueue/kevent for process events while an
  application-launch thread runs. This probe exercises the different
  socket-read/timer/user filters and equeue-thread-to-socketpair bridge.

VideoOut, tiling and input are established shipping-homebrew interfaces and are
owned by the compositor backend, not repeated as human-observation probes here.
