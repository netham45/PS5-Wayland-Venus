# LKL virtio-gpu → ps5wl-gpud interface

This is the implementation contract for the device model in `~/lkl-freebsd`.
That tree is read-only for this change; the device model and kernel changes
below are required integration work, not code already installed in LKL.
The executable reference client is `client.c`; transport code is `wire.c`.
The renderer is virglrenderer **1.2.0**, directly inside the compositor process.
Neither the device model nor the compositor launches a render-server process.

## 1. Endpoints and transport ABI

Connect AF_UNIX to `/system_tmp/ps5wl-gpu-lkl` (or compositor configuration's
`render_path`). Both ends must select the same socket type:

* `PS5WL_RENDER_TRANSPORT=stream` (the default): SOCK_STREAM, with framing below.
* `PS5WL_RENDER_TRANSPORT=seqpacket`: SOCK_SEQPACKET, original upstream records.

A successful client CREATE_CONTEXT passes a **new socket fd**, of the same
socket type, over SCM_RIGHTS. Context traffic goes only on that fd. The original
connection is the control connection for this supervisor's context set.
There is one context thread per context and a control thread per supervisor,
not subprocesses. Client context IDs are scoped to the control connection;
the core assigns independent global IDs, so two supervisors may both use ID 1.

Messages are the unmodified C structures in pinned
`server/render_protocol.h`, included through `wire.h`. This internal upstream
ABI is deliberately pinned, not a public stable protocol. Both peers are
little-endian x86-64 with 32-bit enums and 64-bit `size_t`, using the compiler's
normal structure padding. Zero-initialize structures before transmission;
do not pack them or serialize Vulkan C structs into command bytes. Guest
virtio messages are little-endian virtio UAPI and must be decoded separately.

### Stream records

Each record is `uint32_t payload_bytes` in **little endian**, followed by exactly
that many payload bytes. Length is nonzero and at most 1,048,576. Preserve the
same record boundaries as the seqpacket transport, including SUBMIT_CMD's
separate overflow record. SCM_RIGHTS is attached to the **first byte of the
4-byte length prefix**, never the payload or a subsequent header fragment.
`wire.c` sends the prefix with sendmsg and handles short writes before sending
the payload. Receive each fragment with recvmsg; reads must not consume bytes
from the next record. Never use plain read for bytes which could carry rights.
Unexpected/truncated rights and unexpected payload sizes disconnect the peer.
At most two fds are accepted at the wire layer; each operation below has a
stricter count. The server never sends eventfds or accepts an optional eventfd.

For seqpacket, one payload is one record with its rights on that record and no
length prefix. MSG_TRUNC/MSG_CTRUNC is fatal. Do not assume a large seqpacket
send will work: keep Venus inline submission small, and use Venus shared rings
for normal traffic. The stream implementation can transport larger records,
but it has the same one-MiB bound.

Every receiver explicitly applies `fcntl(F_SETFD, FD_CLOEXEC)` to passed fds.
Do not rely on F_DUPFD_CLOEXEC, SOCK_CLOEXEC or MSG_CMSG_CLOEXEC on PS5. Sending
SCM_RIGHTS duplicates a reference; it does not transfer the sender's numerical
fd or close it. Serialize sends on each socket, including overflow records.

## 2. Control connection

Use `render_client_op_*_request`; no fds on requests. Requests without a listed
reply are one-way. An invalid operation or record closes the connection and
tears down its contexts.

| Operation | Request and result |
| --- | --- |
| INIT = 1 | First request, exactly once. `flags` does not change the already initialized in-process renderer. No reply. |
| NOP = 0 | Header-only, after INIT; no reply. |
| RESET = 2 | Header-only; destroys/joins all contexts owned by this connection. No reply. |
| CREATE_CONTEXT = 3 | `ctx_id != 0`, `ctx_name[32]` (server bounds it). Reply is `render_client_op_create_context_reply` (`bool ok`), plus exactly one context socket fd iff `ok`. Duplicate ID or exhausted limit returns `ok=false` without a descriptor. At most 64 live context records per control connection, additionally bounded by core `max_ctx`. |
| DESTROY_CONTEXT = 4 | `ctx_id`; closes and joins the corresponding context thread. Idempotent for unknown ID. No reply. |
| GET_CAPSET = 5 | ps5wl extension described below. |

### Capset discovery extension

Upstream's render protocol has **no GET_CAPSET**: its virgl proxy obtains the
capset locally. An independent LKL supervisor cannot do that. `wire.h` defines
`GPUD_RENDER_GET_CAPSET = RENDER_CLIENT_OP_COUNT` (5):

```
request: uint32_t op=5, capset_id=4, version=0;
reply:   uint32_t capset_id=4, version=0, size;
         uint8_t venus_capset[size];
```

Header and capset bytes are one record with no descriptors. Any different
capset ID/version is invalid. The data comes from `vkr_get_capset`, not invented
capability bits. Cache it once per renderer connection. Expose virtio-gpu
`num_capsets=1`; GET_CAPSET_INFO index 0 reports ID 4 (VENUS), max_version 0,
and this exact size. GET_CAPSET with ID 4/version 0 returns the exact bytes.
Unknown indices/capsets get the virtio invalid-parameter response. Do not
advertise VIRGL, dma-buf sharing, external Vulkan fd support or native contexts.

## 3. Context connection

Use `render_context_op_*_request`. INIT is first; NOP and other operations are
accepted only after successful INIT. There is no explicit INIT reply. Follow
it with a blob-create request (which has a reply) to detect successful startup.
An initialization failure closes the context socket.

| Operation | Exact contract |
| --- | --- |
| INIT = 1 | `flags=4` (Venus capset only), `shmem_size=16384`; exactly **one** supervisor-created SHM fd, ftruncate'd to at least 16384. Server maps shared read/write and initializes the timeline slots to zero. No optional eventfd. |
| NOP = 0 | Header only, no fds/reply. |
| CREATE_RESOURCE = 2 | `res_id != 0`, `blob_id`, `blob_size > 0`, `blob_flags` either 0 or USE_MAPPABLE (1), no incoming fd. Size is rounded up to 16384. Reply is upstream `render_context_op_create_resource_reply`; success has `fd_type=VIRGL_RESOURCE_FD_SHM` (2), `map_info` and exactly one owned fd. Failure has INVALID (-1), no fd. Vulkan opaque-handle info is zero/not applicable. |
| IMPORT_RESOURCE = 3 | **Unsupported: closes context.** Pinned vkr only imports DMABUF/OPAQUE, not SHM. The design has server-created, non-shareable SHM blobs; never send a SHM fd as a dma-buf or opaque Vulkan handle. |
| DESTROY_RESOURCE = 4 | `res_id`; no fds/reply. Unrefs the core resource. |
| SUBMIT_CMD = 5 | Exact fixed request containing `size` and `cmd[256]`; no fds. `size` is positive, a multiple of 4, at most 1 MiB. Inline min(size,256) bytes. If size > 256, immediately send a **second record of exactly size-256 bytes**, no fds, before any other message. Never put extra bytes after the fixed struct in its record. |
| SUBMIT_FENCE = 6 | `flags` 0 or MERGEABLE (1), `ring_index` 0..63, `seqno` uint32; no fds/reply. Completion is a release store into timeline[ring_index], not a socket reply. |

A blob ID of zero allocates ordinary shared command/reply/ring storage. A
nonzero blob ID refers to an already-created Venus VkDeviceMemory object. The
Venus driver issues vkAllocateMemory first; the device model forwards its
command stream without inspecting Vulkan calls, then exports the memory with
CREATE_RESOURCE. The server creates SHM and imports its host pointer through
VK_EXT_external_memory_host. The returned fd is **ordinary SHM**, not a dma-buf.
Only importable memory types are advertised HOST_VISIBLE by the core.

The test's typed, pinned Venus encoder covers vkCreateInstance, physical-device
enumeration, memory properties, vkCreateDevice, vkAllocateMemory and reply
stream setup/seek. The real guest uses stock Mesa's encoder and shared rings.
Render SUBMIT_CMD does not contain a virtio ring index; do not prepend one to
the Venus stream. virtio fence ring indices go in SUBMIT_FENCE instead.

## 4. Virtio device model responsibilities

Implement the model alongside `src/lkl/netdev.c` / `src/lkl/display.c`, with
host fd/window integration alongside `host/freebsd/worker.c` and
`host/freebsd/lkl_physical_backing.h`. Reuse the LKL virtio library
`vendor/lkl/tools/lkl/lib/virtio.{c,h}` and the descriptor validation patterns
in `virtio_blk.c` / `virtio_net.c`. Do not modify those unrelated models.

Expose virtio device ID 16 (GPU), two queues (controlq=0, cursorq=1), features
VIRTIO_F_VERSION_1, RESOURCE_BLOB, CONTEXT_INIT, VIRGL (3D command capability
required by virtio-gpu/Venus), plus only the generic ring features the LKL
library actually implements. Advertising virtio VIRGL feature does **not**
mean exposing a VIRGL capset: expose only Venus capset ID 4. Do not advertise
RESOURCE_UUID, CROSS_DEVICE, EDID, or a dma-buf/native-context capset as a proxy
for missing functionality. Guest DISPLAY_INFO can report no scanouts for the
headless Venus/Wayland path; cursor/display legacy 2D operations require the
separate display model if later enabled.

Translate the controlq as follows, bounds-checking every descriptor chain,
request length, response capacity, context/resource ID, size and arithmetic:

| Guest command | Supervisor action |
| --- | --- |
| GET_CAPSET_INFO, GET_CAPSET | Serve the cached capset described above. |
| CTX_CREATE with CONTEXT_INIT capset 4 | Control CREATE_CONTEXT, receive its fd, create/map its 16-KiB timeline SHM, context INIT with only that fd. Reject unsupported context_init flags. |
| CTX_DESTROY | Revoke context-related mappings, close context fd, control DESTROY_CONTEXT. |
| SUBMIT_3D | Send its Venus command bytes using SUBMIT_CMD (and overflow record if needed). |
| RESOURCE_CREATE_BLOB HOST3D | Context CREATE_RESOURCE using resource_id/blob_id/size/flags. Retain returned SHM fd, rounded allocation size, map_info and owning context. Reject GUEST/HOST3D_GUEST and unsupported flags rather than silently treating them as HOST3D. |
| RESOURCE_MAP_BLOB | Validate guest-selected window offset/size/alignment/nonoverlap; install fd-backed region below; respond OK_MAP_INFO using server map_info. No renderer wire operation is needed: fd was obtained at create. |
| RESOURCE_UNMAP_BLOB | Revoke worker aliases before removing the window interval. Complete only after inaccessible in every affected worker. No renderer wire operation. |
| RESOURCE_UNREF | Revoke aliases/window first, context DESTROY_RESOURCE, close supervisor fd after all mapping references drain. |
| CTX_ATTACH_RESOURCE / CTX_DETACH_RESOURCE | Maintain associations for the creating context; reject unsupported cross-context imports (no SHM import protocol). Do not relabel SHM as Vulkan external memory. |

Venus depends on context_init, blob-zero storage, multiple timelines, ring-index
fences and the host-visible region, not legacy 2D scanout emulation.
Surface unsupported guest operations as defined virtio errors, never as silent
success. Renderer EOF/error marks all dependent contexts lost; do not fabricate
successful fences for unexecuted work.

### Host-visible shared-memory window

`vendor/lkl/drivers/virtio/virtio_mmio.c:536-563` already reads SHM_SEL,
SHM_LEN_LOW/HIGH and SHM_BASE_LOW/HIGH. The host library's register switches
`tools/lkl/lib/virtio.c:347-505` implement none of them. Add region selector and
64-bit base/length state to `struct virtio_dev` in `tools/lkl/lib/virtio.h`:

* SHM_SEL write selects region ID; virtio-gpu host-visible region ID is 1.
* SHM_LEN_LOW/HIGH reads yield its reserved window length, or UINT64_MAX for
  an unsupported selector. SHM_BASE_LOW/HIGH yield the reserved guest-physical
  base for region 1. Registers are individually little-endian uint32.
* Reserve one **16384-aligned** guest-physical range outside ordinary LKL RAM
  and other MMIO. Base/length must remain stable until device reset/shutdown.
  Advertise capacity consistent with the actual virtual address reservation,
  not app GPU memory size. There is no required fixed base or window size.
* MAP_BLOB's `offset` is relative to that region's base, not a file offset.
  Both offset and rounded mapped length must be multiples of 16384; overflow,
  overlap, and out-of-window ranges are errors. Map the blob's SHM fd at
  file offset **zero**, unless a later explicit suballocation API says otherwise.
  Retain `(window offset,length,resource,generation,fd,file offset=0)` records.
* `drivers/gpu/drm/virtio/virtgpu_vram.c:153-154` currently uses
  `drm_mm_insert_node` with no explicit 16-KiB alignment. Replace it with an
  aligned allocator (`drm_mm_insert_node_generic`, alignment=16384), and round
  host-visible object/window reservation sizes to 16384. Aligning only the
  host mmap address does not fix 4-KiB guest offsets.

The guest sees device PFNs in this window, not `/lkl_phys_mem` RAM offsets.
The canonical mapping path was extended for ordinary DRM PFNMAP by
`patches/lkl/0015-external-drm-mapping.patch`, but
`arch/lkl/kernel/external_tasks.c:1853-1860` still requires `pfn_valid` and
rejects zone-device pages. Keep that protection for arbitrary devices. Add a
separate, admitted **virtio-gpu window PFN** mapping class that resolves only
registered live resource intervals to a retained SHM backing and generation.
Never pass those PFNs through `pfn_to_page` or alias unrelated device memory.

The mapping chain is server → supervisor → worker via SCM_RIGHTS. Extend the
canonical source metadata / runner mapping messages with the fd-backed GPU
class; ordinary RAM keeps its current projection rules. At a worker mapping,
resolve the 16-KiB-aligned native extent to the retained server fd and map
MAP_SHARED at the proper native virtual address, enforcing guest permissions
and never executable access. A 4-KiB guest subrange must not expose neighboring
pages that were not admitted; use existing protection/projection machinery or
reject a native alias that cannot preserve those permissions on PS5's 16-KiB
pages. Do not issue 4-KiB MAP_FIXED offsets on PS5. On unmap/protection change,
resource destruction and reset, stop/revoke affected worker aliases before
recycling a window offset or backing generation. Retain fds through alias
retirement so server unref cannot invalidate still-owned mappings.

### Fence completion without eventfd

The timeline object is a 16-KiB shared page, `atomic_uint slots[4096]` (32-bit,
lock-free on the supported x86-64 hosts); only slots 0..63 are submitted to vkr.
Server INIT zeros them. Ring 0 is the CPU/control timeline; other rings are
Venus queue timelines established by the Venus protocol itself. Do not submit
a fence on an uninitialized queue timeline.

For each fenced virtio command, allocate a monotonically increasing uint32
sequence **per context/ring** and queue `(seq, original uint64 virtio fence_id,
ring_idx, response descriptor)` locally. First forward the command; then send
SUBMIT_FENCE on that context socket. No 64-bit guest fence ID is truncated:
only the private transport seq is 32-bit. Poll with
`atomic_load_explicit(&slots[ring], memory_order_acquire)` and retire pending
requests in order through the observed seq, using wrap-aware comparison
`(int32_t)(observed - pending) >= 0`. Keep fewer than 2^31 outstanding sequence
values; do not mistake the initial zero for a submitted fence. MERGEABLE permits
skipped intermediate stores: equality-only waiting is not correct generally.
Complete the original virtio response with its **original** fence_id, flags and
ring index, place it on the used ring and raise the virtio interrupt according
to negotiated suppression rules. Poll on the device worker, not inside an
unbounded spin on the compositor thread. No eventfd dependency exists.

The LKL virtio library currently documents synchronous enqueue completion in
`tools/lkl/lib/virtio.h:26-35`; extending it for deferred GPU requests is a real
prerequisite. Either keep the descriptor chain owned and valid until the GPU
worker calls `virtio_req_complete`, adding correct queue locking/lifetime, or
serialize processing on a dedicated worker with retained descriptors. Do not
return success and later write a response into a freed/reused descriptor. In
particular do not block queue submission so that a later queue command needed
to signal the earlier fence can never be forwarded. Reset must cancel/drain
pending requests before queue memory and timeline mappings are released.

## 5. Lifecycle and reference checks

The supervisor owns all received fds, closes them on every parse/dispatch error,
and closes context sockets before destroying their control records. Control
EOF destroys every context on that connection. Server context teardown joins
vkr callbacks before unmapping timeline SHM. A supervisor crash therefore cannot
leave a callback pointing into freed timeline memory. Existing native mappings
of an exported SHM fd remain valid according to their own references after
server teardown; stale mappings must still be revoked before guest reuse.

Compositor shutdown calls `gpud_render_shutdown()` before
`gpud_render_drain(drain_ms)`. The latter waits for accept/client/context work
to finish and returns -EBUSY at its deadline rather than joining a wedged
Vulkan thread indefinitely. On -EBUSY the core must remain initialized and
all in-flight callback storage stays owned; a later drain may finish it.
The accept thread polls a nonblocking listener and a dedicated wake pipe;
shutdown writes that pipe instead of relying on shutdown of a listening socket.

Host builds use the pinned container, not workstation libraries:

```
env/run python3 gpu-server/build.py host --sanitize
# Select the constrained lavapipe library built by the Mesa host variant:
env/run env LD_LIBRARY_PATH="$PWD/build/host/venus/lib:$PWD/build/host/sysroot/lib" \
  PS5WL_VULKAN_LIBRARY="$PWD/build/host/venus/lib/libvulkan_lvp.so" \
  PS5WL_NO_EXTERNAL_FD=true build/gpud/host-sanitize/render-client stream
env/run env LD_LIBRARY_PATH="$PWD/build/host/venus/lib:$PWD/build/host/sysroot/lib" \
  PS5WL_VULKAN_LIBRARY="$PWD/build/host/venus/lib/libvulkan_lvp.so" \
  PS5WL_NO_EXTERNAL_FD=true build/gpud/host-sanitize/render-client seqpacket
```

Both clients run the renderer as threads in their **own test host process**;
there is no render-server proxy, fork or exec in that process. A PASS reports
successful Vulkan replies, SHM device-memory blob export/map and timeline
seqno 9 (including explicit Vulkan teardown). The CPU byte-pattern check is a SHM mapping check, **not** a GPU
render/readback claim. The client also exercises SUBMIT_CMD overflow records,
16-KiB rounding, independent supervisors reusing a context ID, and shutdown of
a second context stalled before INIT. It also frees VkDeviceMemory while the
exported blob resource is live, remaps the retained fd, then makes vkr write a
Vulkan enumeration reply through that resource. This specifically detects a
dangling server-side SHM alias; a client mapping read alone cannot.
With a 1-MiB host quota, the retained 640-KiB allocation must continue consuming
quota after vkFreeMemory: another 640-KiB blob-zero allocation must fail until
the exported resource is unreferenced, then the same request must succeed.
`wire-test.c` independently covers both
socket types, SCM_RIGHTS+CLOEXEC, fragmented stream headers, coalesced records,
oversized records, EOF mid-header, truncated seqpacket payloads, and rejection
of rights attached after the first frame byte (with a pipe-EOF fd-leak check).
PS5 shared-SHM GPU coherence and LKL device-model execution remain separate
console and LKL integration acceptance gates; host passes cannot establish them.

### Recorded host acceptance

The full-core `host-sanitize/render-client stream` and `seqpacket` runs both
exited 0 with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Both reported:

```
host-pointer blob: size=655360 (ordinary SHM fd; no Vulkan fd export)
gpud stopped: Vulkan fd-export calls=0
render <transport>: host quota=1048576 retained=655360, INVALID before UNREF, success after UNREF PASS
render <transport>: Venus instance/device, retained host-pointer SHM blob, timeline seqno=9 PASS
```

The integrated sanitized `host-sanitize/render-wire-test` also exited 0.
No sanitizer diagnostics, preload, or leak suppressions were used in those
passing runs. An earlier run exposed a real 128-byte Mesa CPU-topology
allocation leak at ICD unload; the constrained host driver was rebuilt with
`deps/mesa/venus-patches/0003-cpu-topology-unload.patch` before these passes.
The misplaced-rights regression failed before the first-byte recvmsg fix and
passed afterward, including the pipe-EOF descriptor-lifetime assertion.
