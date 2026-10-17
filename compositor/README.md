# ps5wl

`ps5wl` starts from `tinywl/tinywl.c` in the locked wlroots 0.20.2 release.
The upstream MIT license and attribution are preserved in `LICENSE`. The source
retains tinywl's scene graph, xdg-shell, seat, input and move/resize handling.
Super+Tab (or DualSense Options) cycles windows. Ctrl+Alt+Backspace exits;
Alt+Escape and Alt+F1 remain available on the host.

All builds enter the pinned rootless build container through `env/run`; Ubuntu
compiler tools, development libraries, scanner and XKB data are not used.

```sh
make deps-host
make compositor-host
compositor/tests/smoke.sh
```

The host executable is `build/host/compositor/ps5wl`; its runtime library search
path points to the pinned host sysroot. Run it in the container (the executable
is linked for the container's libc, not the Ubuntu host's):

```sh
# Use a private mode-0700 runtime directory under the mounted repository.
mkdir -p build/host/runtime && chmod 700 build/host/runtime
env/run env XDG_RUNTIME_DIR="$PWD/build/host/runtime" build/host/compositor/ps5wl -b headless
# To nest, the parent socket must be accessible inside the same repo mount:
env/run env XDG_RUNTIME_DIR="$PWD/build/host/runtime" WAYLAND_DISPLAY=wayland-0 \
  build/host/compositor/ps5wl -b wayland
```

`-b headless|wayland` overrides `PS5WL_BACKEND`, then `WLR_BACKENDS`; the default
is headless. Only those two backends are accepted. Pixman is the default renderer.
`-d name` sets the listening Wayland socket name; otherwise libwayland selects an
available `wayland-N` socket. `-s command` runs a startup command with the selected
`WAYLAND_DISPLAY`. Nested mode connects to the incoming `WAYLAND_DISPLAY` before
replacing it with the compositor's listening socket for child clients.

The smoke script creates a private runtime directory under `build/host`, starts
the headless compositor for three seconds and connects the in-tree
`registry-dump` client, linked against the same pinned libwayland. It lists all
globals and fails unless `wl_shm`, `xdg_wm_base` and `wl_compositor` are present;
then it requests graceful SIGTERM shutdown. Neither `wayland-info` nor any system
Wayland client is needed.
The PNG readback encoder has a separate host regression test:
`env/run meson test -C build/host/compositor capture-png --print-errorlogs`.


```sh
make deps-ps5
env/run python3 gpu-server/build.py ps5
make compositor-ps5
```

The PS5 build requires `build/ps5/sysroot/lib/pkgconfig/wlroots-0.20.pc`,
`build/ps5/sysroot/lib/pkgconfig/ps5wl-gpud.pc` and `build/ps5/cross.ini`.
The dependency build produces wlroots/cross configuration; the GPU-server build
installs the in-process GPU daemon archive and package metadata.
The PS5 build uses the SDK prospero compiler/cross configuration and static
dependency linking, producing
`build/ps5/compositor/ps5wl`. Missing dependencies fail with the required next
command, not by falling back to host packages. PS5 selects the custom VideoOut
backend automatically, logs on TCP 9530, and listens on `/system_tmp/wayland-0`.
The PS5 compiler is the repository compatibility proxy. Changes to the cross
configuration, proxy or injected runtime archive wipe Meson's cached compiler
identity before rebuilding; incremental reconfigure cannot retain an old SDK
compiler that bypasses the PS5 descriptor compatibility layer.
The pinned evdev/pc105/us XKB keymap is compiled on the build host and embedded;
no console XKB files or shell are required. New PS5 windows are maximized to
1920x1080. Keyboard/mouse and DualSense poll at 250 Hz. Left stick/touchpad moves
the pointer, Cross/R2 and Circle/L2 click, right stick scrolls, and Options
cycles focus. Ctrl+Alt+Backspace or Options+Create (Share) requests clean shutdown.

Regular PS5 builds start the in-process GPU server before creating the VideoOut
backend or Wayland socket. It uses the GPU-server defaults (128 MiB of host
memory per context, at most two contexts); pixman remains the compositor
renderer. Shutdown drains the GPU server for up to five seconds before backend
teardown. A busy drain retains GPU mappings until native application exit.
GPU startup and shutdown have bounded 30-second/eight-second watchdog phases;
arming VideoOut restores the ordinary three-second heartbeat. The isolated
`PS5WL_VIDEOOUT_ONLY=true` build does not link or start the GPU server.
Hardware Vulkan acceptance is gated by `../gpu-server/PLAN.md`; linking this
library alone does not establish successful Vulkan client presentation.

For unattended hardware validation, build with
`PS5WL_TEST_SECONDS=60 make compositor-ps5` (accepted range 0–90, default 0).
This timer requests full teardown and then process exit, including when running
inside a signed foreground launcher. Normal interactive builds leave it off.
VideoOut requires the foreground BigApp launcher; the backend rejects a plain
background elfldr process before opening VideoOut. First validate the isolated
output with `PS5WL_VIDEOOUT_ONLY=true PS5WL_TEST_SECONDS=15 make compositor-ps5`
and launch that artifact through the foreground launcher without rebuilding it.
Do not use `ps5ctl run` directly for the compositor.

TCP 9531 returns a raw PNG of the completed, de-tiled scanout on connection;
use `tools/ps5ctl screenshot --port 9531 --output capture.png` to pull it.
System UI overlays are not included in this readback.


The home-screen launcher bundle installs the separate interactive demo at
`/data/ps5wl/bin/ps5wl-demo.elf` and writes a demo-only autostart manifest.
See `../launcher/README.md` for the final build/install command. Demo-only
startup avoids focus ambiguity during the keyboard/mouse/controller check.

`/data/ps5wl/autostart.txt`, when readable, contains one absolute client ELF path
per line (`#` comments allowed). Only `ps5wl-*` payload paths are sent as
`file://...?pipe=0` requests to the loopback elfldr on 9021. This does not change
Payload Manager autoload/configuration. To add other clients manually, upload
them through `tools/ps5ctl` and use its returned console paths in this list;
reinstalling the launcher restores its bundled demo-only manifest. Install and
start the compositor itself through the foreground launcher, not a background
payload upload.

Host builds also expose screencopy and virtual keyboard/pointer protocols for
the pinned client integration tests. These injection protocols are not exposed
on the console. See `src/backend/ps5/README.md` for backend design and hardware
validation boundaries.
