# ps5wl

`ps5wl` starts from `tinywl/tinywl.c` in the locked wlroots 0.20.2 release.
The upstream MIT license and attribution are preserved in `LICENSE`. The source
retains tinywl's scene graph, xdg-shell, seat, input and move/resize handling.
Alt+Escape exits; Alt+F1 cycles windows.

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

```sh
make deps-ps5
make compositor-ps5
```

The PS5 build requires `build/ps5/sysroot/lib/pkgconfig/wlroots-0.20.pc` and
`build/ps5/cross.ini`, produced by the PS5 dependency build. It uses the SDK
prospero compiler/cross configuration and static dependency linking, producing
`build/ps5/compositor/ps5wl`. Missing dependencies fail with the required next
command, not by falling back to host packages. This is a cross-build milestone,
not a functioning VideoOut/pad backend: see `src/backend/ps5/README.md`.
