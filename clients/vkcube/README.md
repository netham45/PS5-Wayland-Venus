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

The host smoke starts a private pixman/headless ps5wl, loads this build's
lavapipe ICD directly (not a host distro Vulkan loader), renders 120 cube
frames through Wayland WSI, and requires the completed frame counter. It
terminates its own compositor in `finally` and propagates either exit status.

## Console sequence

Run the GPU smoke **without** the compositor first:

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

Only after that passes, start the compositor by its normal deployment path,
then:

```
tools/ps5ctl run build/ps5/vkcube/ps5wl-vkcube.elf
tools/ps5ctl logs --port 9590
```

The payload defaults to 300 frames, logs the first and every 60th successful
present, releases its Vulkan/Wayland objects, and lingers for 30 seconds for
log pulls. It shares `/data/ps5wl/libvulkan.so.1` with the smoke; never
run two differently built driver payloads concurrently. The compositor uses
pixman and owns VideoOut; the client builds only the Wayland WSI platform.
Compare client presents with the compositor's surface commit/output frame
counters and screenshot before the final human end-to-end interactive check.

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

Console GPU/cube acceptance is not yet established by these host results.

