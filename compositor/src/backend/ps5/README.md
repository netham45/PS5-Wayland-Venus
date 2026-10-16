# PS5 wlroots backend

`backend.c` implements wlroots 0.20 backend/output interfaces. It opens VideoOut
as SYSTEM (`0xff`), registers two direct-memory scanouts with RegisterBuffers2,
and exposes 1920x1080 at 60 Hz. Pixman buffers are read through wlroots data-ptr
access and copied into the SDL 64KB_R_X tiling layout. Damage is accumulated per
scanout, including damage from intervening flips; no full-frame copy is needed
when only a small region changes. Scanout format is the SDL attribute
`0x8000000022000000`. Source words use wlroots XRGB/ARGB (B,G,R,A bytes);
prospero-win's matching VideoOut presenter documents its scanout as B,G,R.

Two 10 MiB buffers hold the actual tiled 1080p extent, rounded to 2 MiB
boundaries. Allocation searches the full direct-memory pool, as in the PS5
RADV winsys and prospero-win, rather than SDL's application-only
`AllocateMainDirectMemory` subrange. On firmware 10.60 a plain elfldr payload
returned `0x80020023` for SDL's 64 MiB main allocation, but the full-pool
20 MiB allocation, mapping and RegisterBuffers2 all returned zero. The optional
availability query returned `0x8002000c` despite the successful allocation and
is therefore diagnostic, not an allocation gate.

Hardware validation is not yet complete. The earlier plain elfldr PID 147
submitted one flip, after which the console's system actions stopped working
and the user hard-rebooted it. No cause was proven. The backend now refuses
VideoOut unless its own process app ID matches the running foreground BigApp.
Do not run VideoOut through a background payload. The next stage must use the
foreground launcher with `PS5WL_VIDEOOUT_ONLY=true PS5WL_TEST_SECONDS=15`.

The exact SDL tile extent is 8,912,896 bytes. The native `tiling-bounds` test
enumerates all 2,073,600 scanout pixels, proves unique addresses, and measures
maximum byte end 8,830,976, below both SDL's extent and each 10 MiB allocation.
This rules out an ordinary out-of-bounds CPU tile write, not a VideoOut fault.

`safety.c` installs alternate-stack fatal handlers and an independent watchdog:
3 seconds without a main-loop heartbeat or the validation hard deadline closes
VideoOut and exits. Fatal text is sent without allocation or locks to stderr
and connected nonblocking log sockets. Normal teardown unregisters buffers,
closes VideoOut, joins the bounded waiter, deletes the equeue, unmaps and releases
memory, following the source presenters' close-before-equeue-delete order.
These are best-effort userspace safeguards, not recovery from a kernel deadlock.
Owned foreground applications terminate through shipping SDL's
`sceSystemServiceLoadExec("exit", NULL)`, not raw `_exit`: PS5_RetroArch's native
entry point documents raw kernel exit raising SIGSYS. SIGSYS is also contained
and logged. If LoadExec returns, its result is logged and the process waits for
app-manager termination instead of executing the forbidden raw BigApp exit.

A thread blocks in the Sce flip equeue with a bounded shutdown timeout and
writes completion timestamps to a pipe watched by the Wayland event loop.
Only that event-loop thread sends wlroots present/frame events. Unchanged scenes
re-flip the existing buffer, keeping the flip clock alive without rendering or
copying. Backend teardown joins the thread, removes fd/timer sources, closes
input/VideoOut/equeue and releases mapped/direct memory.

`input.c` maps USB HID usages to evdev keys and reports relative mouse and
DualSense events through wlroots keyboard/pointer interfaces. Mouse is resolved
from sysmodule `0xa9` because the SDK has no mouse import library. The ABI
declarations follow upstream SDL and prospero-win; observed initialization
results and real events are logged, not inferred from device enumeration.

`capture.c` serves a raw PNG on TCP 9531 when a host connects. It reads the
completed tiled scanout back into RGB, not a pre-composition client buffer.
This proves the pixels submitted to VideoOut; it does **not** capture the PS5
system overlay. The SDK has no ScreenShot/Capture library stubs, and the
public cursor exports lack a verified PS5 argument ABI, so capture uses our
own readable direct memory and cursors use wlroots' built-in software fallback.
The debug encoder uses bounded per-row storage and stored DEFLATE blocks.
The native `capture-png` Meson test drives the real capture socket and verifies
PNG chunk CRCs, DEFLATE/Adler32, dimensions and every RGB pixel of an image.


Sources:
- https://github.com/ps5-payload-dev/SDL/tree/release-2.30.x-ps5/src/video/ps5
- https://github.com/ps5-payload-dev/SDL/tree/release-2.30.x-ps5/src/joystick/ps5
- https://github.com/mpereiraesaa/prospero-win/blob/main/native/pw_hid_ps5.h
- https://github.com/ps5-payload-dev/elfldr/blob/master/socksrv.c

The tile implementation is altered from SDL; its license is in `LICENSE.SDL`.
GPU rendering is a client capability, not a claim about this pixman compositor.
Build/link success alone is not evidence of hardware output or input.
