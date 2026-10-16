# Venus/vtest research — draft, not a plan

**Status: implementation stopped by coordinator instruction. This is research
and an inventory of unreviewed experiments, not an approved architecture,
implementation plan, or completed GPU server.** No Venus client has presented a
frame in this investigation. No PS5 build or console run was performed here.

## Finding and feasibility boundary

**Unmodified virglrenderer 1.2.0 vtest is not a usable no-Vulkan-fd transport for
PS5 RADV.** Its host-visible device-memory blob path needs Vulkan external-memory
fd export/import. The separate command/reply/ring shared-memory path does not.
Ordinary POSIX shared-memory fds passed with SCM_RIGHTS are not Vulkan external
memory fds; preserving that distinction is central to this investigation.

**[INFERENCE] A server-owned SHM_ANON allocation imported with
VK_EXT_external_memory_host could replace the device-memory export path without
changing the existing vtest client blob-create message.** This is a plausible
adaptation, not a demonstrated feasibility result. Host execution, memory-type
compatibility, lifetime correctness, PS5 GPU access to SHM_ANON mappings, and
actual presentation remain unproven. Client-created SHM pages require additional
protocol work and are not interchangeable with the server-owned proposal.

The full originally requested acceptance (Venus vkcube frames to ps5wl with fd
extensions disabled, plus PS5 compilation) was **not reached** before the stop.
No implementation or additional builds should be inferred from these notes.

## Exact source pins and citation convention

`deps/versions.lock.json:14-18` contains the pins below. Ps5Deps added the two new
entries after receiving locally measured SHA256 values; existing pins were not
changed by this assignment.

| Component | Pin | Archive SHA256 |
| --- | --- | --- |
| Mesa | 26.2.0, tag `mesa-26.2.0` | `efd4bb08cdb7c365a812cd4e6c9202ab55b2f22cdcd13c7d6c4f9647b799a4ef` |
| pacbrew PS5 Mesa patch recipe | commit `722187f0ee246c0d7d57401c8a97de1c7f4e2ff0` | `26c85165813c98d372f8ce5f4aa9a5d3e0bbfd13a6c858c4e44494e7fdcdea8e` |
| virglrenderer | 1.2.0, tag `1.2.0` | `f4f52db11297b52b35c8c2d5bf5e21b7997b52f8bfad99ea2b1c155997cff4ad` |
| libepoxy | 1.5.10, tag `1.5.10` | `a7ced37f4102b745ac86d6a70a9da399cc139ff168ba6b8002b4d8d43c900c15` |
| Vulkan-Tools / cube | 1.4.341, commit `48a4bcbdf619e57204783f8c1a04c76c160ddd5b` | `a7d94a95e8c4b4331ab291638b16d1ce622cee9a1e65389790af3d3fbe2f98b7` |

Primary sources:

- [Mesa 26.2.0 archive](https://mesa.freedesktop.org/archive/mesa-26.2.0.tar.xz)
  and [tagged source](https://gitlab.freedesktop.org/mesa/mesa/-/tree/mesa-26.2.0).
- [pacbrew pinned source](https://github.com/ps5-payload-dev/pacbrew-repo/tree/722187f0ee246c0d7d57401c8a97de1c7f4e2ff0/mesa).
- [virglrenderer 1.2.0 archive](https://gitlab.freedesktop.org/virgl/virglrenderer/-/archive/1.2.0/virglrenderer-1.2.0.tar.bz2)
  and [tagged source](https://gitlab.freedesktop.org/virgl/virglrenderer/-/tree/1.2.0).
- [libepoxy 1.5.10 source](https://github.com/anholt/libepoxy/tree/1.5.10).

Below, `V:` means a file in the **unmodified virglrenderer 1.2.0 archive**;
line numbers do not include our experimental patches. Sources were extracted to
`build/host/sources/virglrenderer-1.2.0` and
`build/gpu-server/sources/virglrenderer-1.2.0`. The former has experimental source
edits and therefore shifted line numbers. Use the pinned archive for original
`V:` citations. `M:` means the prepared Mesa 26.2.0 tree at
`build/mesa/sources/mesa-26.2.0`, including the existing pacbrew/repository patches
at research time. The research did not edit that shared Mesa tree.

## What vtest actually shares

### Client transport and blob flags

- `M:src/virtio/vulkan/vn_renderer_vtest.c:318-353` sends
  `VCMD_RESOURCE_CREATE_BLOB` with type, flags, 64-bit size and 64-bit blob ID;
  it reads a resource ID and **receives an fd from the server**. There is no
  client-provided fd in this request.
- `V:vtest/vtest_protocol.h:179-191` defines `GUEST=1`, `HOST3D=2`,
  `HOST3D_GUEST=3`; `MAPPABLE=1<<0`, `SHAREABLE=1<<1`, `CROSS_DEVICE=1<<2`.
- `M:src/virtio/vulkan/vn_renderer_vtest.c:691-725` maps Vulkan host-visible
  memory to `MAPPABLE`, external-handle requests to `SHAREABLE`, and dma-buf
  external-handle requests to `CROSS_DEVICE`. A nonzero device-memory object ID
  is used as the `HOST3D` blob ID.
- `M:src/virtio/vulkan/vn_renderer_vtest.c:628-670` maps the returned fd with
  `MAP_SHARED`. Its comment explicitly calls treating dma-buf mmap and
  `vkMapMemory` as equivalent a wrong assumption. Export-to-dma-buf simply
  duplicates the saved fd for a shareable blob. A plain shm fd must never be
  advertised as a dma-buf through that operation.
- `V:vtest/vtest_renderer.c:439-471` implements fd transfer with `sendmsg` /
  `SCM_RIGHTS`. `V:vtest/vtest_server.c:405-424` creates and binds an AF_UNIX
  stream socket. The client chooses `VTEST_SOCKET_NAME` at
  `M:src/virtio/vulkan/vn_renderer_vtest.c:980-1003`.

### Command/reply/ring memory is already ordinary shared memory

- `M:src/virtio/vulkan/vn_renderer_vtest.c:771-812` requests a mappable blob
  with blob ID zero, receives its fd, mmaps it shared, then closes the fd.
  `:1013-1015` chooses `HOST3D` for this shared-memory allocation path.
- `V:src/venus/vkr_context.c:275-306` creates blob-zero backing with
  `os_create_anonymous_file`, mmaps it, and returns a `VIRGL_RESOURCE_FD_SHM`
  blob. This allocation is not a `VkDeviceMemory` export.
- `V:src/mesa/util/anon_file.c:95-126` documents SCM_RIGHTS sharing and uses
  `memfd_create` on Linux or `shm_open(SHM_ANON, ...)` on FreeBSD. Thus upstream
  already has a relevant POSIX shared-memory primitive; it is not proof that
  GPU-accessible Vulkan device memory is backed by it.

### Device memory is not the same shared-memory path

- `V:src/venus/vkr_device_memory.c:246-259` translates private
  `VkImportMemoryResourceInfoMESA` into a Vulkan fd import.
- `V:src/venus/vkr_device_memory.c:267-360` forces host-visible allocations
  toward dma-buf/opaque-fd export, or externally allocated dma-buf import via
  GBM/udmabuf. `:363-386` records valid fd types. The comment at `:267-276`
  acknowledges the specification problem of binding external memory to
  non-external buffers/images. There is **no external-memory-host import
  alternative** in this release's allocation path.
- `V:src/venus/vkr_physical_device.c:193-241` queries dma-buf/opaque-fd
  exportability and selects GBM/udmabuf fallbacks. Those fallbacks still need
  dma-buf support; they do not solve PS5's no-fd case.
- `V:src/venus/vkr_device_memory.c:499-615` exports a device-memory blob.
  `:530-564` chooses dma-buf or opaque-fd and otherwise reports
  `mem is not exportable`; `:582-593` calls `vkGetMemoryFdKHR` unless GBM or
  udmabuf already supplied a dma-buf. `CROSS_DEVICE` requires dma-buf.
- `V:vtest/vtest_renderer.c:1349-1447` decodes the blob request and exports
  `HOST3D` resources. At `:1415-1420` vtest accepts only DMABUF or SHM fd types,
  **rejecting OPAQUE**. It sends the accepted fd at `:1435-1443`.
- `V:src/virglrenderer.c:1328-1343` preserves the resource fd type when
  exporting a blob; it does not convert opaque Vulkan fds into shm fds.
- `V:src/venus/vkr_allocator.c:219-223` separately hard-requires
  `VK_KHR_external_memory_fd` for its opaque-fd mapping allocator. That is not
  an fd-free fallback for vtest; the vtest export restriction above remains.

Therefore it is imprecise to say “vtest uses shmem, so it will work unchanged.”
Blob-zero transport memory is shmem; host-visible Vulkan device-memory blobs
are exported external GPU memory in the unmodified renderer.

## Host Vulkan requirements: hard gates versus functional needs

There is **not** a universal vkr instance-creation requirement for all Linux
external-fd extensions:

- `V:src/venus/vkr_instance.c:109-123` rejects guest instance extensions and
  requires host Vulkan **1.1**; optional validation adds its own layer/debug
  extensions at `:145-152`.
- `M:src/virtio/vulkan/vn_instance.h:21` defines
  `VN_MIN_RENDERER_VERSION=VK_API_VERSION_1_1`.
  `vn_instance.c:73-107` and `vn_physical_device.c:1484-1512` enforce it for
  renderer instance/device versions. `vn_instance.c:174-185` also checks the
  negotiated protocol XML version.
- `V:src/venus/venus-protocol/vulkan_core.h:72-75` pins renderer protocol
  headers to Vulkan **1.4.307**. Mesa's newer API headers do not imply the wire
  peer supports every newer command/extension.
- `V:src/venus/vkr_device.c:130-155` appends
  `VK_KHR_external_memory_fd`, `VK_EXT_external_memory_dma_buf`, and
  `VK_KHR_external_fence_fd` **only when detected**. The physical-device
  extension filtering is at `V:src/venus/vkr_physical_device.c:245-303`.
- `V:src/venus/vkr_queue.c:31-40` attaches an export-fence create structure
  only when external-fence-fd support exists. Ordinary Vulkan fence completion
  is available otherwise; external fence fds are not a global hard gate.
- `M:src/virtio/vulkan/vn_renderer_vtest.c:879-910` advertises no external
  sync and no dma-buf import for vtest. It requires the renderer's blob-zero,
  wait-sync and multiple-timeline protocol capabilities.
- `V:src/venus/vkr_renderer.c:66-69` requires internal thread-sync and
  asynchronous-fence-callback renderer flags. These are virglrenderer flags,
  not Vulkan device extensions.

The **functional memory requirement** is stricter than instance creation:
without an exportable/importable fd path, upstream cannot expose ordinary
host-visible application Vulkan memory to Venus. The proposed replacement
would instead require host `VK_EXT_external_memory_host`, suitable host-visible
memory-type bits, alignment and coherent CPU/GPU access. No implementation here
has established that this replacement meets those constraints on PS5.

## SHM_ANON + host-pointer import hypothesis

The minimal protocol-preserving variant considered was **server-owned memory**:

1. For host-visible allocations that would otherwise need fd export, create an
   ordinary shared object (`memfd` on host; `SHM_ANON` on PS5), size it and map
   it in the GPU-owning process.
2. Meet `minImportedHostPointerAlignment`; query
   `vkGetMemoryHostPointerPropertiesEXT` and check the requested memory type.
   Import the server's mapped pointer with
   `VkImportMemoryHostPointerInfoEXT` / `HOST_ALLOCATION_BIT_EXT` in the host
   `vkAllocateMemory` call.
3. Retain mapping and fd until the corresponding Vulkan memory is actually
   freed, including context/device destruction and error paths.
4. When vtest creates the associated non-shareable device-memory blob, return
   a duplicated **SHM** fd. The existing client mmap then refers to the same
   pages without `vkGetMemoryFdKHR`, dma-buf or client direct-memory use.
5. Do not offer dma-buf, cross-device or general external-memory export for
   that resource. These semantics cannot be fabricated from a shm fd.

**[INFERENCE]** This could avoid a client-side wire change because the current
blob response already transports an fd and vtest accepts SHM. It does not remove
fds from the IPC protocol, only Vulkan external-memory fd extensions.

For **client-provided** SHM_ANON pages, the existing `CREATE_BLOB` request has no
incoming-fd slot. A real client-to-server import operation and resource identity
would be needed, and Vulkan allocation would have to use those pages before
binding resources. Sending a raw client virtual address would be incorrect:
only the server's own mapping address is importable in the server Vulkan device.
No such client-provided-memory operation was implemented here.

Important unresolved constraints:

- Host-import memory-type compatibility cannot be assumed merely from support
  for `VK_EXT_external_memory_host`.
- Import size/alignment, memory lifetime after `vkFreeMemory`, allocation
  failure cleanup, client disconnect and whole-device destruction need review.
- GPU/CPU coherence and visibility are not established by a CPU-only IPC test.
  vtest's no-op flush/invalidate behavior is not a general non-coherent-memory
  solution (`M:vn_renderer_vtest.c:619-625` for flush).
- The upstream external/non-external resource binding specification caveat
  remains relevant to a host-import implementation; it cannot be omitted from
  correctness review just because upstream already has an analogous issue.
- PS5 anonymous-private host-pointer GPU access does not prove host-pointer
  GPU access to cross-process SHM_ANON pages. No such console GPU probe was run.

Existing console IPC evidence is narrower and useful:
`probes/RESULTS.md:14-22,34-67` records separate-payload SCM_RIGHTS and full
bidirectional 32-MiB SHM_ANON CPU coherence. `:97-107` records the broken
`F_DUPFD_CLOEXEC` behavior. The production descriptor shim must cover every new
server/client binary; trusting successful CLOEXEC flags is unsafe on this PS5.

## Wayland presentation: client-side WSI, not server VideoOut

- `M:src/virtio/vulkan/vn_wsi.c:131-185` initializes common Mesa WSI and
  selects its software-device transport option when the renderer lacks
  dma-buf support. This is not a request to execute Vulkan shaders in the
  client; Vulkan operations still go through the Venus renderer.
- `M:src/virtio/vulkan/vn_physical_device.c:1016-1054` bases Venus external
  memory support on the renderer's dma-buf extension.
- Existing repository patch
  `deps/mesa/patches/0001-wayland-hardware-shm.patch:35-65` binds `wl_shm`
  even for hardware devices and selects shm when the compositor lacks dma-buf.
  `:91-104` selects CPU-image transport and reserves direct shm host-pointer
  import for the software-device case. `:105-124` waits for completion before
  the CPU consumes mapped image data.
- `M:src/vulkan/wsi/wsi_common.c:3588-3617` maps image-to-buffer staging
  memory and selects the buffer-blit path unless linear-image handling is
  requested. `:2893-2900` contains the patched CPU-consumption fence wait.

**[INFERENCE]** For a no-fd Venus renderer, presentation could therefore use a
GPU-produced image, host-visible staging backed by the proposed shared memory,
CPU consumption/copy in client WSI, and an ordinary Wayland wl_shm buffer sent
to ps5wl. The client and compositor may use different shared objects; zero-copy
presentation is not established or required by the research. No Venus
presentation run confirmed the exact path, image correctness or synchronization.
Existing direct-lavapipe cube evidence in `clients/vkcube/README.md:74-87` is not
Venus evidence and must not be relabelled.

## Embedding issue: a worker thread alone does not stay in the BigApp

The intended process-budget observation supplied by the coordinator is that
plain elfldr clients have no direct-memory budget, while foreground compositor
BigApp owns the usable pool. A GPU service therefore must execute Vulkan in
that process, not simply launch an ordinary server payload.

There is a significant upstream process-boundary trap:

- `V:vtest/vtest_server.c:301-304` sets both VENUS and RENDER_SERVER flags.
- `V:src/proxy/proxy_server.c:88-115` first asks for a supplied server fd;
  without one, `:39-85` **forks and execs** `virgl_render_server`.
- `V:server/main.c:9-25` explains that worker threads are threads of the
  render-server process. `-Drender-server-worker=thread` does **not** embed
  that render-server process in the compositor.
- `V:server/render_server.c:100-171,209-220` is an argc/argv + getopt entry
  point; embedding it naively in a second thread would also introduce global
  getopt state and lifecycle concerns.

A possible interface discussed with Ps5Backend was a compositor-linked
`gpu-server` library with a start/stop boundary and socket
`/system_tmp/ps5wl-vtest`. No library/header or compositor callsite was written.
An in-process render-server endpoint supplied through the existing server-fd
callback is a candidate, not an approved solution. It would need ownership of
thread startup, endpoints, context workers, shutdown and failure propagation;
using the existing fork fallback would miss the actual budget requirement.

No compositor files were edited. Ps5Backend acknowledged ownership boundaries
and required host/protocol evidence and coordinator approval before integration.

## Build research and GL dependency issue

- Mesa Venus build input is `-Dvulkan-drivers=virtio`, with Wayland enabled for
  this use. `M:src/virtio/vulkan/meson.build:125-146` includes vtest on
  non-Windows, adds virtgpu only for KMS/DRM systems, and includes Wayland WSI.
  `:157-168` builds `libvulkan_virtio` as a shared library.
- The client needs the Venus ICD plus Wayland/runtime support, not RADV's PS5
  winsys or direct-memory allocator. **[INFERENCE]** Its normal vtest path can
  run without allocating client-side GPU memory. A PS5 build was not attempted.
- The pinned Mesa preparation and patch convention is
  `deps/mesa/build.py:51-64`; shared sources should not be repurposed or
  reconfigured under another agent's active build. The experimental recipe
  uses its own `build/gpu-server/mesa` source area.
- `V:meson.build:85` requires libepoxy unconditionally. `V:src/meson.build:34-45,
  141-162` compiles the GL vrend sources and links epoxy even when runtime
  `--no-virgl` will be used. That runtime switch is not a build-time Venus-only
  configuration. `V:src/virglrenderer.c:28,46-47` includes epoxy/vrend headers.
- libepoxy 1.5.10 was built with `-Dglx=no -Degl=no -Dx11=false -Dtests=false`.
  This produced an epoxy dispatch library without adding host distro GL/X11
  dependencies. It does not supply a PS5 GL implementation; vtest would still
  run in `--no-virgl` mode.
- `V:meson.build:337-359` enables Venus/render server, supports dynamic Vulkan
  loading, and uses epoll-shim on FreeBSD. `:383-389` selects worker type.
- `V:src/venus/vkr_library.c:39-69` originally dlopens `libvulkan.so.1` or
  `libvulkan.so` and resolves `vkGetInstanceProcAddr`. Host lavapipe exports an
  ICD resolver (`vk_icdGetInstanceProcAddr`); the experimental patch adds the
  existing project `PS5WL_VULKAN_LIBRARY` convention and ICD-resolver fallback.
- `build/ps5/cross.ini:2-3` selects the repository compiler wrappers.
  `cross/pkg-config:6-10` forcibly confines lookup to the normal PS5 sysroot.
  **[INFERENCE]** An isolated new prefix in the experimental recipe will need
  a reviewed cross/pkg-config arrangement; environment overrides alone do not
  change that wrapper. This was noticed but not implemented before the stop.

## Observed builds and exact non-results

All build attempts were inside `env/run`; no console tool was invoked here.

1. Host libepoxy 1.5.10 configured, compiled and installed successfully under
   `build/gpu-server/host/sysroot`. The completed install output is in session
   artifact `artifact://564` and subsequent build outputs.
2. Initial virglrenderer builds encountered real portability failures with the
   pinned host glibc/Clang 23.1.1 combination:
   - `artifact://564`: bundled C11 `once_flag` / `call_once` conflicted with
     glibc declarations.
   - `artifact://567`: native-C11 substitution exposed missing pthread
     declarations; TGSI `memchr` assignment discarded const.
   - `artifact://570`: `vtest/threadpool.c` lacked an explicit stdlib include.
   - `artifact://577`: native thread substitution lacked upstream's private
     `_MTX_INITIALIZER_NP` macro.
3. The last build (`env/run python3 gpu-server/build.py host --upstream`,
   job `bg_222`) **did complete the host virglrenderer/vtest links and install**
   after the experimental portability/loader patch. Its output includes:
   `Linking target vtest/virgl_test_server`,
   `Linking target server/virgl_render_server`, and installation of both.
   This invocation omitted the host-pointer-memory patch; it is not evidence
   that the replacement memory implementation compiles.
4. That same job was canceled immediately on the coordinator's stop message.
   At cancellation it had applied existing Mesa patches and the experimental
   lavapipe constraint patch, then entered Meson setup for the private
   `virtio,swrast` Mesa build. No completed Venus ICD or constrained lavapipe
   build was observed. Cancellation and captured output are available at
   `proc://bg_222` in this session.
5. `gpu-server/smoke.py` was **never executed**. There is no reproduced runtime
   baseline failure, no no-fd presentation log, no pixel/readback result and
   no established protocol-version compatibility run for this Mesa/vkr pair.
6. No PS5 compilation, upload, deployment, console GPU action, fork experiment,
   process inspection or log pull was performed by this assignment.

At the subsequent coordinator console-down notice, there were **no owned
console jobs or log pullers to cancel and no last console log lines held here**.
The only owned active job was the host build already canceled above. The
coordinator reported the console outage after another experiment; these notes
have no independent console observation of its cause.

## Unreviewed partial code left in place by instruction

These files are **not reviewed, not accepted, and not safe deployment inputs**:

| File | Partial intent / limitations |
| --- | --- |
| `gpu-server/build.py` | Isolated pinned epoxy/virgl/Mesa build experiment; host baseline got through virgl install only. PS5 isolation, cross dependencies and end-to-end outputs remain unproven. `--upstream` means no host-pointer patch, not pristine upstream: portability and direct-ICD loading patch still applies. |
| `gpu-server/smoke.py` | Unexecuted host harness for private headless ps5wl, vtest, and existing cube; expects 120 frames/commits and no-fd markers. It does not yet establish rendered image correctness, and its baseline expectation has not been tested. |
| `gpu-server/patches/0001-native-linux-threads.patch` | Experimental host portability plus direct-ICD loader changes. Compiled in the final host virgl invocation. Native mutex representation assumption and portability need review. |
| `gpu-server/patches/0002-host-pointer-memory.patch` | Experimental server-owned shared-memory allocation/import/export path across vkr memory/physical-device/device code. **Never compiled or run.** Alignment arithmetic, lifetime on every teardown path, import restrictions and resource binding semantics remain unreviewed. No client-provided SHM operation is present. |
| `gpu-server/mesa-patches/0001-lavapipe-no-external-fd.patch` | Experimental `PS5WL_NO_EXTERNAL_FD` host-driver extension masking. Patch application was observed; no completed constrained driver build or runtime extension enumeration was observed. |

Generated source edits also remain in
`build/host/sources/virglrenderer-1.2.0` and private generated sources/artifacts
remain under `build/gpu-server/`. They are disposable research state, not
additional checked-in source pins. No compositor integration, public server
API or PS5 service binary was implemented. The coordinator requested a separate
proper plan/review pass; this document deliberately does not prescribe one.
