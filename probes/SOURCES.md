# Probe implementation sources

Build only through the pinned container, for example `env/run make -C probes/ipc`.
Deploy only through `tools/ps5ctl`. Ports: IPC 9540/9541, spawn parent 9542,
spawn child 9543 and callback 9544, native events 9545, epoll-shim events 9548.
The IPC server must launch before its client. Upload the spawn child without
launching before running its parent; `CHILD_PATH` records the Payload Manager
path returned by `tools/ps5ctl payloads` on the development console.

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
