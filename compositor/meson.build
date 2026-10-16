project('ps5wl', 'c', version: '0.1.0', license: 'MIT',
  meson_version: '>=1.3', default_options: ['c_std=c11', 'warning_level=2', 'wrap_mode=nodownload'])

add_project_arguments('-D_POSIX_C_SOURCE=200809L', '-DWLR_USE_UNSTABLE', language: 'c')
static_deps = meson.is_cross_build()
wlroots = dependency('wlroots-0.20', version: '=0.20.2', static: static_deps)
wayland_server = dependency('wayland-server', version: '>=1.24.0', static: static_deps)
xkbcommon = dependency('xkbcommon', static: static_deps)

executable('ps5wl', 'src/ps5wl.c',
  dependencies: [wlroots, wayland_server, xkbcommon], install: true)
if not meson.is_cross_build()
  executable('registry-dump', 'tests/registry-dump.c',
    dependencies: dependency('wayland-client', version: '>=1.24.0'))
endif
