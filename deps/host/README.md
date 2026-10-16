# Pinned host sysroot

`make deps-host` enters `env/run` and builds the source archives in
`deps/versions.lock.json` into `build/host/sysroot`. The builder validates every
archive's SHA256 before extraction, reuses `.cache/deps/<archive>` with the PS5
build, and keeps extracted sources/build trees under `build/host`.

Build order: libffi, expat, Wayland (libraries plus scanner), wayland-protocols,
pixman, xkeyboard-config, libxkbcommon, libdrm, wlroots. Linux uses native epoll;
the lock's epoll-shim entry is only needed for the FreeBSD-derived PS5 target.
No alternate version list or system development `.pc` directory is used.

`PKG_CONFIG_LIBDIR` contains only this prefix's `lib/pkgconfig` and
`share/pkgconfig`; `PKG_CONFIG_PATH` is empty. Shared libraries have a runtime
search path to the same sysroot. The compiler, build tools and libc come from the
pinned container; all listed dependency libraries and XKB data come from sources.

wlroots builds headless + Wayland backends and the pixman renderer. DRM, libinput,
X11, sessions, Xwayland, GLES2, Vulkan, optional hardware allocators and color
management are disabled. Headless, Wayland and pixman are unconditional in
wlroots; empty optional backend/renderer arrays intentionally retain them.

Native scanner contract for cross builds:
`build/host/sysroot/bin/wayland-scanner`. Run it inside `env/run`; its runtime
search path already locates the pinned libraries. Do not globally export the
sysroot in `LD_LIBRARY_PATH`: it would replace the container tools' own library
dependencies (notably Python's Expat). `JOBS` overrides build parallelism.
Re-running the builder reconfigures Meson projects and performs
incremental compilation; source digest changes replace extracted sources.
