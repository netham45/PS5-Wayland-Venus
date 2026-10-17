project('ps5wl-gpud', 'c', version: '0.1', default_options: ['c_std=gnu11', 'warning_level=2'])
cc = meson.get_compiler('c')
threads = dependency('threads')
src = get_option('vkr_source')
conf = configuration_data()
foreach key : ['_GNU_SOURCE', 'VIRGL_RENDERER_UNSTABLE_APIS', 'ENABLE_VENUS', 'ENABLE_VULKAN_DLOAD', 'ENABLE_RENDER_SERVER_WORKER_THREAD', 'HAVE_PTHREAD', 'HAVE_SYS_UIO_H', 'HAVE_SYS_SELECT_H', 'HAVE_DLFCN_H', 'HAVE_STRTOk_R', 'HAVE_TIMESPEC_GET']
  conf.set(key, 1)
endforeach
conf.set('HAVE_STRTOK_R', 1)
foreach h : ['pthread_np.h', 'sys/eventfd.h']
  if cc.has_header(h)
    conf.set('HAVE_' + h.to_upper().replace('/', '_').replace('.', '_'), 1)
  endif
endforeach
foreach f : ['memfd_create', 'posix_fallocate']
  if cc.has_function(f, prefix: '#define _GNU_SOURCE\n#include <sys/mman.h>\n#include <fcntl.h>')
    conf.set('HAVE_' + f.to_upper(), 1)
  endif
endforeach
foreach b : ['bswap32', 'bswap64', 'clz', 'clzll', 'expect', 'ffs', 'ffsll', 'popcount', 'popcountll', 'types_compatible_p', 'unreachable']
  conf.set('HAVE___BUILTIN_' + b.to_upper(), 1)
endforeach
foreach a : cc.get_supported_function_attributes(['const', 'format', 'malloc', 'noreturn', 'packed', 'pure', 'unused', 'warn_unused_result', 'aligned'])
  conf.set('HAVE_FUNC_ATTRIBUTE_' + a.to_upper(), 1)
endforeach
conf.set('HAVE_STRUCT_TIMESPEC', 1)
conf.set('PACKAGE_VERSION', '"0.1"')
conf.set('UTIL_ARCH_LITTLE_ENDIAN', host_machine.endian() == 'little' ? 1 : 0)
conf.set('UTIL_ARCH_BIG_ENDIAN', host_machine.endian() == 'big' ? 1 : 0)
configure_file(output: 'config.h', configuration: conf)
add_project_arguments('-imacros', meson.current_build_dir() / 'config.h', '-DHAVE_CONFIG_H=1', language: 'c')
ver = configuration_data()
ver.set('VIRGL_MAJOR_VERSION', 1)
ver.set('VIRGL_MINOR_VERSION', 2)
ver.set('VIRGL_MICRO_VERSION', 0)
configure_file(input: src/'src/virgl-version.h.meson', output: 'virgl-version.h', configuration: ver)
inc = include_directories('.', 'include', 'core', src/'src', src/'src/venus', src/'src/mesa', src/'src/mesa/compat', src/'src/mesa/pipe', src/'src/mesa/util', src/'src/gallium/include', src/'src/gallium/auxiliary', src/'src/drm/drm-uapi', src/'vtest', src/'server')
venus = ['acceleration_structure', 'buffer', 'command_buffer', 'common', 'context', 'cs', 'descriptor_set', 'device', 'device_memory', 'host_copy', 'image', 'instance', 'library', 'physical_device', 'pipeline', 'query_pool', 'queue', 'render_pass', 'renderer', 'ring', 'transport']
sources = []
foreach name : venus
  sources += src/'src/venus/vkr_' + name + '.c'
endforeach
foreach name : ['bitscan', 'hash_table', 'os_file', 'os_misc', 'ralloc', 'u_cpu_detect', 'u_debug', 'u_math', 'anon_file']
  sources += src/'src/mesa/util/' + name + '.c'
endforeach
sources += [src/'src/virgl_resource.c', src/'src/virgl_util.c']
gen = custom_target('venus_objects', input: [src/'src/venus/vkr_device_object.py', src/'src/venus/vkr_device_object.json'], output: ['vkr_buffer_gen.h', 'vkr_command_buffer_gen.h', 'vkr_descriptor_set_gen.h', 'vkr_device_memory_gen.h', 'vkr_image_gen.h', 'vkr_pipeline_gen.h', 'vkr_query_pool_gen.h', 'vkr_queue_gen.h', 'vkr_render_pass_gen.h'], command: [find_program('python3'), '@INPUT0@', '-o', '@OUTDIR@', '@INPUT1@'])
deps = [threads]
if host_machine.system() == 'linux'
  deps += [cc.find_library('m'), cc.find_library('dl')]
endif
if meson.is_cross_build()
  add_project_arguments('-DPS5WL_PS5=1', '-D__BSD_VISIBLE=1', language: 'c')
  inc = [inc, include_directories('../common/ps5log')]
  sources += '../common/ps5log/ps5log.c'
endif
lib = static_library('ps5wl-gpud', sources, gen, 'core/gpud.c', 'vtest/vtest.c', 'render/render.c', 'render/wire.c', include_directories: inc, dependencies: deps, install: true)
gpud_dep = declare_dependency(link_with: lib, include_directories: include_directories('include'), dependencies: deps)
install_headers('include/ps5wl_gpud.h')
pkg = import('pkgconfig')
private_libs = ['-pthread']
if host_machine.system() == 'linux'
  private_libs += ['-lm', '-ldl']
endif
pkg.generate(lib, name: 'ps5wl-gpud', description: 'In-process Venus GPU server', extra_cflags: ['-pthread'], libraries_private: private_libs, install_dir: get_option('libdir') / 'pkgconfig')
executable('gpud-host', 'tests/host.c', dependencies: gpud_dep, install: true)
if not meson.is_cross_build()
  mask = executable('memory-mask', 'tests/memory-mask.c', include_directories: inc)
  test('memory-mask', mask)
  executable('render-client', 'render/client.c', 'render/wire.c', include_directories: inc, dependencies: gpud_dep)
  wire_test = executable('render-wire-test', 'render/wire-test.c', 'render/wire.c', include_directories: inc, dependencies: threads)
  test('render-wire', wire_test)
endif
