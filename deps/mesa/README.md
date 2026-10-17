# PS5 RADV and Wayland shm

Sources and archive SHA-256 values are in `deps/versions.lock.json`: Mesa
26.2.0 and pacbrew-repo commit `722187f0ee246c0d7d57401c8a97de1c7f4e2ff0`.
The build verifies both archives, applies all 28 patches from that recipe in
order, then applies this directory's patches. Pacbrew explicitly labels its
PS5 patches experimental; successful compilation is not evidence of GPU
correctness.

Build in the pinned environment after the core host and PS5 sysroots exist:

```
env/run deps/mesa/build.sh tools
env/run deps/mesa/build.sh host
env/run deps/mesa/build.sh ps5
env/run clients/vkcube/build.sh host
env/run clients/vkcube/build.sh ps5
```

`tools` builds matching `mesa_clc` and `vtn_bindgen2` using the pinned
container's LLVM/Clang and SPIR-V build tools. `host` builds lavapipe from the
same patched Mesa source and uses only the project's host Wayland sysroot.
`ps5` uses the SDK cross toolchain and project's PS5 Wayland static libraries.
Shader caches and compression dependencies are disabled for these payloads.
The build does not enable EGL, OpenGL, X11, GBM, or the VideoOut platform.

## Venus and lavapipe variants

```
env/run deps/mesa/build.sh host --driver venus
env/run deps/mesa/build.sh ps5 --driver venus
env/run clients/vkcube/build.sh host --driver venus
env/run clients/vkcube/build.sh ps5 --driver venus
```

Venus uses an isolated patched source tree (`build/mesa/sources/mesa-26.2.0-venus`)
and build directories `build/mesa/{host,ps5}-venus`; it never replaces RADV.
The host prefix `build/host/venus` contains `lib/libvulkan_virtio.so` and
`lib/libvulkan_lvp.so` plus their ICD manifests under `share/vulkan/icd.d`.
Use `LD_LIBRARY_PATH=$PWD/build/host/sysroot/lib` when loading these host
drivers, including from Python/ctypes; their pinned libdrm dependency lives
there, not in the container's system library directory.
Set `PS5WL_NO_EXTERNAL_FD=true` in the **server's** environment to mask
lavapipe's external memory/fence/semaphore fd and DRM-modifier extensions.
The switch is applied after capability detection and before WSI initialisation;
it leaves `VK_EXT_external_memory_host` and ordinary POSIX SHM fds available.
This is the GPU server's host stand-in, not a claim that SHM GPU imports work
on the console.

The PS5 prefix `build/ps5/venus` contains `lib/libvulkan-venus.so.1`.
It reuses pacbrew patch 0021's `vk_exports_gen.py` dispatch wrappers and symbol
map, exporting the core `vk*` API without a Vulkan loader. Its library name is
distinct from RADV; native Venus clients extract it atomically to
`/data/ps5wl/libvulkan-venus.so.1`. The driver itself defaults to the vtest
socket `/system_tmp/ps5wl-gpu-0`, overridable with `VTEST_SOCKET_NAME`; no
`VN_DEBUG=vtest` or client environment is required on PS5. DRM and VideoOut are
not compiled into this variant. All PS5 compiler/linker paths still use
`cross/bin` and its descriptor wrappers.

Source review of `vn_wsi.c:134-185` confirms that absence of the renderer's
dma-buf extension sets common WSI's software-transport flag; shaders still
execute on the server. Venus does not advertise `EXT_external_memory_host`,
so `wsi_common_wayland.c` chooses `WSI_IMAGE_TYPE_CPU` with
`WSI_WL_BUFFER_SHM_MEMCPY`, not direct client host-pointer import. Common WSI
uses an image-to-buffer blit by default, maps the host-visible buffer, waits
for completion, then copies to a real `wl_shm` pool. Existing patch 0001
already supports this path; no extra Venus WSI patch is needed. Rendering
correctness still requires the GPU server's end-to-end tests.

The independent CPU fallback is built with:

```
env/run deps/mesa/build.sh host --driver lavapipe
env/run deps/mesa/build.sh ps5 --driver lavapipe
env/run clients/vkcube/build.sh host --driver lavapipe
env/run clients/vkcube/build.sh ps5 --driver lavapipe
```

`deps/mesa/lavapipe.py` owns its isolated LLVM/Mesa configuration and installs
under `build/mesa/{host,ps5}-lavapipe/sysroot`. These clients use
`/data/ps5wl/libvulkan-lavapipe.so.1` on PS5 and need no GPU server or RADV.


## Presentation path

`0001-wayland-hardware-shm.patch` binds `wl_shm` for hardware devices too,
selecting it when the compositor does not advertise linux-dmabuf. It does
**not** mark the Vulkan device as software. Instead it uses Mesa's existing
`WSI_IMAGE_TYPE_CPU` image-to-buffer transfer: GPU renders an optimal image,
GPU copies it to a host-coherent linear staging buffer, a transfer-to-host
barrier and a checked fence wait complete, then Wayland WSI copies the mapped
pixels to a real shared-memory pool and attaches its `wl_buffer`. The CPU
present fence wait is required for hardware devices too, not just lavapipe.
Hardware uses the memcpy variant deliberately: importing an arbitrary shm
mapping into the AGC winsys adds GPU-map alignment and ownership constraints
that are unnecessary for correctness. Software keeps upstream host-import
behavior. Shm allocation failure is returned, not asserted or presented.

The PS5 driver follows pacbrew's shared `libvulkan.so.1` model and exports
Vulkan entrypoints itself; no Vulkan loader is needed. Each payload embeds
the driver and extracts it to `/data/ps5wl/libvulkan.so.1` before the
SDK rtld opens it. A per-process temporary file is atomically renamed over
the installed path, so another running client keeps its mapped inode intact.
This keeps deployment a single ELF while retaining the recipe's shared-library
ABI. Concurrent Vulkan clients must be built from the same tree.

The generated PS5 cross file selects `cross/bin/prospero-clang{,++}`.
These proxies inject the common descriptor-compatibility archive into the
shared-driver link as well as payload links. Mesa's FreeBSD anonymous-shm
and descriptor-duplication paths rely on those wrappers; do not substitute
the raw SDK compiler or add separate per-call-site flag workarounds.

`0002-wayland-without-videoout.patch` disables pacbrew's otherwise
unconditional VideoOut WSI when Wayland is selected and removes its system
module dependency. The driver and payloads do not link SceVideoOut.

`0003-ps5-allocation-diagnostics.patch` reports the failed BO size, alignment,
flags and the existing native direct-memory error string.
`0004-ps5-allocation-heap-diagnostics.patch` reports failed allocation type,
heap size and tracked overallocation state, including failures before any BO
call. The client captures stderr in a per-process file and includes its tail
in the pull log on a fatal assertion. This also captures Mesa errors when
its `MESA_LOG_FILE` credential check would reject a separate destination;
neither error nor assertion is suppressed.

Hardware and host acceptance results belong in `clients/vkcube/README.md`;
none are implied by this design description.

## ICD unload regression

The Venus-host lavapipe stand-in also applies
`venus-patches/0003-cpu-topology-unload.patch`. Mesa's CPU topology discovery
allocates L3 affinity masks once and publishes a shallow copy of the owning
pointer. The ICD destructor releases that allocation once and clears both
aliases; otherwise unloading the driver loses it. No preload or leak
suppression is used.

Exercise real physical-device enumeration and four unload/reload cycles:

```
env/run clang -g -fsanitize=address -Ibuild/mesa/sources/mesa-26.2.0-venus/include \
  deps/mesa/tests/driver-unload.c -ldl -o build/host/mesa-driver-unload
env/run env LD_LIBRARY_PATH="$PWD/build/host/sysroot/lib" \
  ASAN_OPTIONS=detect_leaks=1 PS5WL_NO_EXTERNAL_FD=true \
  build/host/mesa-driver-unload "$PWD/build/host/venus/lib/libvulkan_lvp.so"
```

The test deliberately calls `dlclose`, so retaining the library until process
exit cannot conceal an ICD-owned leak.

Observed after this patch: all four cycles enumerated one physical device
and the ASan/LeakSanitizer process exited 0 with `detect_leaks=1`, without
preloading the ICD or suppressing leaks. Host and PS5 Venus drivers and the
PS5 client payloads were rebuilt successfully with the cleanup applied.

## Vtest disconnect regression

`venus-patches/0004-vtest-terminal-io.patch` makes zero-byte socket reads and
writes fatal transport failures instead of allowing a no-progress loop.
Interrupted syscalls resume; EOF and other errors do not trigger reconnects.
`env/run python3 deps/mesa/tests/vtest-eof.py` runs the real host Venus
readback client against a local socket peer, drains its initialization
requests, then closes the peer's write half. The test requires an EOF
diagnostic and client termination within five seconds, not a harness kill.
Set `LD_LIBRARY_PATH=$PWD/build/host/sysroot/lib` for pinned host libraries.
Observed with the EOF patch: the real client emitted
`lost connection to rendering server on 8 read 0 0` and exited with SIGABRT
within the five-second deadline; the regression exited 0. It enables
`MESA_LOG_LEVEL=debug` because Venus transport diagnostics use Mesa's debug
log level.

`venus-patches/0005-rtasm-unload.patch` similarly releases the runtime
assembler's global `u_mm` heap and its 10-MiB executable mapping at ICD unload.
The simple enumeration regression does not exercise that graphics shader
path; the GPU server's cube-under-LeakSanitizer test covers its teardown.
