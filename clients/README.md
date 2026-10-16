# Native Wayland clients

Build against the pinned sysroots (never host distribution libraries):

```
env/run clients/build.sh host
env/run clients/build.sh ps5
env/run python3 clients/tests/smoke.py
```

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
the keyboard keymap and modifier state come from the compositor.

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
and Meson 1.12.1. Host acceptance runs completed at least 90 frames per client, exiting with
`failed=0`. Each received compositor-supplied XKB keymaps, pointer button 272,
and key 30 (`a`). Demo logs recorded drag `(400,300)` to `(500,350)`,
`demo-text bytes=1 text=a`, fullscreen `1280x720`, then restored `800x600`.
All six 1280x720 screencopies were nonuniform; before/after hashes changed for
both clients. The demo capture contained 149 orange trail pixels and 80 white
pixels in the typed `a` glyph cell; explicit assertions catch input that logs
correctly but fails to draw. Logs and screenshots are in the generated evidence directory
described above. These host results and successful PS5 linking do not establish
console rendering or physical input; console evidence must be recorded separately.

The initial console run uploaded and launched both client ELFs successfully through
`tools/ps5ctl`. Ports 9560/9561 returned `client-init` with the correct PS5 socket
and runtime paths. A diagnostic simple-shm run (PID 129) logged
`wayland-connect-failed: No such file or directory` after 30.043 seconds;
the compositor had not created its socket. No console connection or buffer commit
is claimed from that run. The helper now logs its PID and lingers after connect
failure so the final pull-log error is not lost. The diagnostic process exited;
no unrelated process was killed.
