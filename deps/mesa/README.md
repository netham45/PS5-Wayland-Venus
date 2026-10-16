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
`0002-wayland-without-videoout.patch` disables pacbrew's otherwise
unconditional VideoOut WSI when Wayland is selected and removes its system
module dependency. The driver and payloads do not link SceVideoOut.

Hardware and host acceptance results belong in `clients/vkcube/README.md`;
none are implied by this design description.
