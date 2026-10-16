# Native Wayland clients

Build against the pinned sysroots (never host distribution libraries):

```
env/run clients/build.sh host
env/run clients/build.sh ps5
env/run python3 clients/tests/smoke.py
```

The PS5 wrapper bootstraps the shared descriptor compatibility runtime and
fingerprints the cross/native files, compiler proxy, and injected compatibility
archive. A changed toolchain identity wipes only the generated client Meson
configuration: ordinary `--reconfigure` would retain the old compiler path and
could silently bypass the PS5 descriptor fixes.

The host test requires `env/run compositor/build.sh host` and the host dependency
sources in `build/host/sources/wlroots-0.20.2` (the protocol XMLs come from the
same pinned wlroots source as the compositor). It starts ps5wl headless, launches
each separate client, captures real composited output with wlr-screencopy, checks
nonuniform and changing images, and injects virtual pointer/keyboard input into
both clients (including drag, text entry, and fullscreen checks in the demo).
Logs and PPM screenshots remain in `build/host/clients/evidence/`.
Host ps5wl must expose the virtual pointer/keyboard and screencopy managers;
the virtual input managers are deliberately host-only.

Outputs:

| Client | Host | PS5 | Pull log port |
| --- | --- | --- | --- |
| Weston animation | `build/host/clients/ps5wl-simple-shm` | `build/ps5/clients/ps5wl-simple-shm.elf` | 9560 |
| Interactive demo | `build/host/clients/ps5wl-demo` | `build/ps5/clients/ps5wl-demo.elf` | 9561 |

`simple-shm` adapts [Weston's `clients/simple-shm.c`](https://gitlab.freedesktop.org/wayland/weston/-/blob/main/clients/simple-shm.c)
XRGB8888 animated pattern; its upstream MIT license is retained in the source. Both clients use xdg-shell,
frame callbacks, and released-buffer reuse. Resize allocates correctly sized
buffers and retires old buffers only after the compositor releases them.
`--frames N` exits after N committed frames have received frame callbacks (host
or argument-capable launchers only; Payload Manager supplies no arguments).

Demo controls: left-click/drag draws persistent trails, typing appends UTF-8,
Backspace removes a full UTF-8 codepoint, F11 toggles fullscreen, and Escape exits.
The screen shows coordinates, frame/time, typed text, and the last 12 key/button
events. The embedded [8x8 Basic Latin bitmap font](https://github.com/dhepper/font8x8)
is public domain (Daniel Hepper, Marcel Sondaar, IBM); non-Latin codepoints are retained in the text buffer and
rendered as `?`. No fontconfig, installed fonts, or client-side XKB data is needed:
the keyboard keymap and modifier state come from the compositor. The client XKB
context explicitly uses `XKB_CONTEXT_NO_DEFAULT_INCLUDES`, so missing installation
paths cannot prevent a payload from parsing the self-contained serialized keymap.

PS5 deployment uses only `tools/ps5ctl run <ELF>` after the compositor is running.
The common helper initializes pull logging and sets `XDG_RUNTIME_DIR=/system_tmp`
and `WAYLAND_DISPLAY=/system_tmp/wayland-0`, then retries actual libwayland
connections for 30 seconds. Host environment values are left unchanged. Shared
storage is `memfd_create` on Linux and `shm_open(SHM_ANON)` on PS5, with
`ftruncate`, `MAP_SHARED`, and normal libwayland SCM_RIGHTS descriptor transport.
Connection, configure, buffer commits, frame completions, keymap, keyboard, and
button events are logged. A successful connection alone is not evidence of a
rendered frame or working input: require compositor-side commits, frame callbacks,
and the physical TV/input check as well.

## Recorded verification

On 2026-10-16, `env/run clients/build.sh host`, `env/run clients/build.sh ps5`,
and `env/run python3 clients/tests/smoke.py` each exited 0 using LLVM 23.1.1
and Meson 1.12.1. The final PS5 configure used
`cross/bin/prospero-clang`; inspection of both actual ELFs confirmed the linked
descriptor wrappers, including `__wrap_fcntl`, `__wrap_shm_open`, and `__wrap_recvmsg`.

Host acceptance completed 90 frames per client with `failed=0`, while client
`XKB_CONFIG_ROOT` deliberately pointed to a nonexistent directory. Both received
compositor-supplied XKB keymaps, pointer button 272, and key 30 (`a`). The demo
recorded drag `(400,300)` to `(500,350)`, typed `a`, fullscreen `1280x720`, and
restoration to `800x600`. Six real 1280x720 screencopies were nonuniform and
before/after hashes changed. The input screenshot contained 149 trail pixels and
80 pixels in the typed glyph cell, so logging alone cannot satisfy the test.

On PS5, foreground compositor PID 132 accepted separate simple-shm PID 133 and
demo PID 134. Each mapped 8,294,400-byte SHM pools, configured `1920x1080`, received
a 68,214-byte keyboard keymap and keyboard focus, and repeatedly committed buffers.
Client logs prove at least 600 simple-shm and 720 demo frame callbacks. The
compositor independently recorded the corresponding surface commits in
`build/ps5/evidence/compositor-fd-fixed.log`.

Actual completed-scanout PNGs, client logs, and measured results are saved under
`build/ps5/evidence/clients/` (`simple-shm.png`, `demo.png`, both `.log` files,
and `acceptance.json`). Both 1920x1080 RGB PNGs passed chunk-CRC/DEFLATE checks;
the simple-shm image shows the colored animated pattern and the demo image shows
its checkerboard and readable bitmap text. Pixel checks verified the demo's two
background colors and 2,000 title-colored glyph pixels. These are compositor
scanout captures, not captures of system overlays or the physical HDMI output.
Both receipt-launched clients were stopped using their exact logged PIDs;
both cleanup operations returned success.

The captured regressions also established why clients require a no-default-includes
XKB context and why PS5 links require the systemic descriptor wrapper:
`F_DUPFD_CLOEXEC` could falsely return socket FD 0 instead of duplicating a SHM
FD. The corrected wrapper plus the explicit Wayland compatibility patch preserve
normal SCM_RIGHTS/SHM transport; no private-memory or fake-success fallback is used.
Physical mouse/keyboard interaction and final on-TV usability remain pending the
coordinator's single end-to-end human session.

