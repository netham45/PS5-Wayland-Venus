# PS5 wlroots backend

`backend.c` implements wlroots 0.20 backend/output interfaces. It opens VideoOut
as SYSTEM (`0xff`), registers two direct-memory scanouts with RegisterBuffers2,
and exposes 1920x1080 at 60 Hz. Pixman buffers are read through wlroots data-ptr
access and copied into the SDL 64KB_R_X tiling layout. Damage is accumulated per
scanout, including damage from intervening flips; no full-frame copy is needed
when only a small region changes. Scanout format is the SDL attribute
`0x8000000022000000`. Source words use wlroots XRGB/ARGB (B,G,R,A bytes);
prospero-win's matching VideoOut presenter documents its scanout as B,G,R.

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


Sources:
- https://github.com/ps5-payload-dev/SDL/tree/release-2.30.x-ps5/src/video/ps5
- https://github.com/ps5-payload-dev/SDL/tree/release-2.30.x-ps5/src/joystick/ps5
- https://github.com/mpereiraesaa/prospero-win/blob/main/native/pw_hid_ps5.h
- https://github.com/ps5-payload-dev/elfldr/blob/master/socksrv.c

The tile implementation is altered from SDL; its license is in `LICENSE.SDL`.
GPU rendering is a client capability, not a claim about this pixman compositor.
Build/link success alone is not evidence of hardware output or input.
