# Vulkan cube payload

The standalone build uses pinned Vulkan-Tools 1.4.341 `cube/cube.c`, its
checked-in SPIR-V shaders and texture, and headers from the exact pinned Mesa
source. It links the repository's `common/ps5wl-client` helper and `ps5log`;
there is no second Wayland environment convention. The upstream cube source
retains its Apache-2.0 license and attribution in the fetched source.

Build prerequisites and Mesa commands: [../../deps/mesa/README.md](../../deps/mesa/README.md).

```
env/run clients/vkcube/build.sh host
env/run python3 clients/vkcube/smoke.py
env/run clients/vkcube/build.sh ps5
```

Both PS5 payload links read the compiler, C flags, and linker flags from
`build/ps5/cross.ini`, exactly as Mesa does. This includes the common PS5
descriptor-compatibility wrappers even for the standalone GPU smoke, which
does not link Wayland.

The host smoke starts a private pixman/headless ps5wl, loads this build's
lavapipe ICD directly (not a host distro Vulkan loader), renders 120 cube
frames through Wayland WSI, and requires the completed frame counter. It
terminates its own compositor in `finally` and propagates either exit status.

## Console sequence

Start the compositor through its foreground BigApp launcher and wait for its
Wayland socket. Keep that app active while running either Vulkan payload.
With no foreground app, the measured `sceKernelGetDirectMemorySize()` is
zero and RADV exposes no memory heaps; with the compositor foreground it
reports 12 GiB. This is an application-budget prerequisite, not a reason to
invent a fixed-size fallback. Then run the GPU smoke:

```
tools/ps5ctl run build/ps5/vkcube/ps5wl-vktest.elf
tools/ps5ctl logs --port 9591
```

The smoke enumerates a physical device and its host-visible memory types,
creates a graphics queue, clears an optimal-tiled 128×128 `R32_UINT` image
on that queue, copies it to a 64-KiB host-visible Vulkan buffer, waits
for a fence, invalidates host caches, and checks all 16,384 readback words
against `0x1a2b3c4d`. Acceptance requires `mismatches=0` and `RESULT PASS` from
the console, not merely successful device creation. It creates no surface
and opens no VideoOut handle. It destroys resources, then keeps the log
service alive for 30 seconds before exiting. Kill only receipt-owned
processes if a failure leaves the process alive.

After that passes, leave the compositor foreground and run the cube:

```
tools/ps5ctl run build/ps5/vkcube/ps5wl-vkcube.elf
tools/ps5ctl logs --port 9590
```

The payload runs until its window closes or Escape is pressed, and logs the
first and every 60th successful present. On normal exit it releases its
Vulkan/Wayland objects and lingers for 30 seconds for log pulls. It shares
`/data/ps5wl/libvulkan.so.1` with the smoke; never
run two differently built driver payloads concurrently. The compositor uses
pixman and owns VideoOut; the client builds only the Wayland WSI platform.
Compare client presents with the compositor's surface commit/output frame
counters and screenshot before the final human end-to-end interactive check.

PS5 startup records process credentials and the direct-memory pool's largest
available range using 2-MiB alignment before loading RADV. A fatal assertion
repeats that query and includes a bounded snapshot of captured stderr in the
pull log, so native allocator and Vulkan heap failures remain observable.
The per-process capture file is removed on normal exit or after an assertion
snapshot; the assertion still terminates the payload with failure.

## Evidence

Observed host acceptance (`env/run python3 clients/vkcube/smoke.py`, exit 0):
`Selected GPU 0: llvmpipe (LLVM 23.1.1, 256 bits), type: Cpu`;
client logs show `vkcube presented frame=120` and
`vkcube exit frames=120 validation_error=0`. The private compositor logged
`Created shm allocator`, pixman renderer, and
`client surface committed 500x500 count=120`. Both processes exited cleanly.
The test completed in 33.05 seconds including its log-pull linger.

Observed host image-clear acceptance (exit 0): `ps5wl-vktest` reports
`GPU readback words=16384 expected=1a2b3c4d first=1a2b3c4d mismatches=0`
and `RESULT PASS` using the same llvmpipe driver. This checks optimal-image
clear, image-to-buffer copy, fence completion, and CPU-visible data; it does
not establish PS5 hardware execution.

Observed console GPU acceptance (log port 9591, payload pids 137 and 143):
`device=PlayStation 5 GPU (RADV NAVI21) vendor=1002 device=0000 api=1.4.354`;
all 16,384 image-clear readback words matched `0x1a2b3c4d`, with
`mismatches=0` and `RESULT PASS` in both runs. These payloads created no
surface and did not link or open VideoOut. The second run also exercised
atomic driver-file replacement. Console Wayland cube acceptance remains unproven.

The descriptor-wrapper cutover was clean-built (825 targets) with both C and
C++ compiler paths explicitly selecting `cross/bin/prospero-clang{,++}`.
Wrapped payload pid 143 at 2771460 ms reported a 12-GiB direct-memory pool,
11 memory types, two heaps and the same zero-mismatch `RESULT PASS` while
foreground compositor pid 142 was active. Without that foreground context,
the wrapped payload observed a zero-byte pool and correctly failed acceptance.

The embedded 21,200,784-byte driver was extracted successfully under
`/data/ps5wl`. A prior `/system_tmp` extraction failed with `ENOSPC` after
262,144 bytes; its truncated file was removed. Use `/data`, not the small
temporary filesystem, for the shared driver.

The full-HD Wayland run remains blocked by a real native allocation failure.
At 4145472 ms, cube pid 191 reported `uid=1 euid=0 gid=1 egid=1` and a
12,884,901,888-byte pool, but
`sceKernelAvailableDirectMemorySize(0, pool, 0x200000, ...)` returned
`0x8002000c` before loading RADV; its initialized output sentinels remained
`start=-1 bytes=0`. The same query returned the same values at the assertion.
Captured Mesa stderr then identified the actual failed BO: requested
8,294,400 bytes, input alignment 65,536, flags `0x253`; the native
`sceKernelAllocateDirectMemory` request was 8,306,688 bytes and returned
`0x80020023`. Vulkan selected memory type 2 in heap 0, whose advertised size
was 4,294,967,296 bytes; overallocation tracking was disabled. The first
`vkCreateSwapchainKHR` returned `VK_ERROR_OUT_OF_DEVICE_MEMORY` for
1920x1080, format 44, four images, before any successful present. This is
not evidence of a working interactive Vulkan window or of an exhausted
global pool: the available-range query already failed in this client before
Vulkan initialization. No budget syscall, heap-size override or image-count
reduction was attempted.
