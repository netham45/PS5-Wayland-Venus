project('ps5wl', 'c', version: '0.1.0', license: 'MIT',
  meson_version: '>=1.3', default_options: ['c_std=c11', 'warning_level=2', 'wrap_mode=nodownload'])

add_project_arguments('-D_POSIX_C_SOURCE=200809L', '-DWLR_USE_UNSTABLE', language: 'c')
static_deps = meson.is_cross_build()
wlroots = dependency('wlroots-0.20', version: '=0.20.2', static: static_deps)
wayland_server = dependency('wayland-server', version: '>=1.24.0', static: static_deps)
xkbcommon = dependency('xkbcommon', static: static_deps)

sources = ['src/ps5wl.c']
deps = [wlroots, wayland_server, xkbcommon]
includes = []
if meson.is_cross_build()
  if get_option('videoout_only') and get_option('console_test_seconds') == 0
    error('videoout_only requires a nonzero console_test_seconds safety deadline')
  endif
  add_project_arguments('-DPS5WL_PS5', '-D__BSD_VISIBLE=1', language: 'c')
  add_project_arguments('-DPS5WL_TEST_SECONDS=' + get_option('console_test_seconds').to_string(), language: 'c')
  add_project_arguments('-DPS5WL_VIDEOOUT_ONLY=' + (get_option('videoout_only') ? '1' : '0'), language: 'c')
  sources += ['src/backend/ps5/backend.c', 'src/backend/ps5/input.c',
    'src/backend/ps5/autostart.c', 'src/backend/ps5/capture.c',
    'src/backend/ps5/safety.c', '../common/ps5log/ps5log.c']
  includes += include_directories('../common/ps5log')
  cc = meson.get_compiler('c')
  if not get_option('videoout_only')
    deps += dependency('ps5wl-gpud', static: true)
    foreach lib : ['SceAgc', 'SceAgcDriver']
      deps += cc.find_library(lib)
    endforeach
  endif
  mouse_import = shared_library('SceMouse', 'src/backend/ps5/mouse-stub.c',
    c_args: ['-ffreestanding', '-fno-builtin'],
    link_args: ['-nostdlib', '-nodefaultlibs', '-nostartfiles',
      '-Wl,-soname,libSceMouse.sprx'], install: false)
  deps += declare_dependency(link_with: mouse_import)
  foreach lib : ['SceVideoOut', 'SceSystemService', 'SceUserService', 'SceKeyboard', 'ScePad']
    deps += cc.find_library(lib)
  endforeach
  deps += dependency('threads')
endif
executable('ps5wl', sources, include_directories: includes,
  dependencies: deps, install: true)
if not meson.is_cross_build()
  executable('registry-dump', 'tests/registry-dump.c',
    dependencies: dependency('wayland-client', version: '>=1.24.0'))
  capture_test = executable('capture-test',
    ['tests/capture.c', 'src/backend/ps5/capture.c', '../common/ps5log/ps5log.c'],
    include_directories: include_directories('src/backend/ps5', '../common/ps5log'),
    dependencies: [wlroots, wayland_server, dependency('threads')])
  tiling_test = executable('tiling-test', 'tests/tiling.c',
    include_directories: include_directories('src/backend/ps5'))
  test('tiling-bounds', tiling_test)
  test('capture-png', find_program('python3'),
    args: [files('tests/capture.py'), capture_test], timeout: 15)
endif
