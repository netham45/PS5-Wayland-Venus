project('ps5wl-clients', 'c', version: '0.1.0', license: 'MIT',
  meson_version: '>=1.3', default_options: ['c_std=c11', 'warning_level=2', 'wrap_mode=nodownload'])
add_project_arguments('-D_POSIX_C_SOURCE=200809L', language: 'c')
ps5 = meson.is_cross_build()
if ps5
  add_project_arguments('-DPS5WL_PS5=1', language: 'c')
endif
wayland = dependency('wayland-client', version: '>=1.24.0', static: ps5)
xkb = dependency('xkbcommon', static: ps5)
threads = dependency('threads')
protocols = dependency('wayland-protocols')
scanner = find_program('wayland-scanner', native: true)
xdg_xml = protocols.get_variable(pkgconfig: 'pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml'
xdg_header = custom_target('xdg-shell-header', input: xdg_xml,
  output: 'xdg-shell-client-protocol.h', command: [scanner, 'client-header', '@INPUT@', '@OUTPUT@'])
xdg_code = custom_target('xdg-shell-code', input: xdg_xml,
  output: 'xdg-shell-protocol.c', command: [scanner, 'private-code', '@INPUT@', '@OUTPUT@'])
common = static_library('client-window', ['src/window.c', '../common/ps5wl-client/ps5wl-client.c',
  '../common/ps5log/ps5log.c', xdg_header, xdg_code], dependencies: [wayland, xkb, threads])
output_options = ps5 ? {'name_suffix': 'elf'} : {}
foreach name : ['simple-shm', 'demo']
  executable('ps5wl-' + name, ['src/' + name + '.c', xdg_header],
    dependencies: [wayland, xkb, threads], link_with: common, kwargs: output_options)
endforeach
if not ps5
  subdir('tests')
endif
