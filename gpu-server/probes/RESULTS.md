# GPU console probes C1 / C2

Status: **C1 complete with a 409 MiB app-flexible-memory limit; C2a 16 KiB
GPU fill and cross-process verification PASS**. App SHM counts against that
pool. A 1 GiB map failed ENOMEM; Main classified that as the expected §6 quota
gate and authorized the remaining C1 items, then C2, within measured headroom.
Each run still requires Backend's grant. Firmware was not queried by these
probes; process contexts below are from actual runs.

## Build evidence

Command (2026-10-16):

```sh
env/run python3 gpu-server/probes/build.py
```

Observed exit code: **0**. All eight links completed using
`cross/bin/prospero-clang`, which links the PS5 fd/flag compatibility shim.
There were no compiler warnings in the probe build output. The independent
launcher driver rebuild separately emitted existing sign-compare warnings in
`launcher/hbldr.c:152` and `launcher/vendor/elfldr.c:191`.

Artifacts under `build/ps5/gpu-probes/`:

- `ps5wl-gpu-c1-plain.elf`: log port 9592; native 30 s alarm, `_exit(124)` on expiry.
- `ps5wl-gpu-c1-app.elf`: log port 9530; 30 s watchdog; native shell exit.
- `ps5wl-gpu-c1-remaining.elf`: log port 9530; app continuation skips the already measured memory stages; native 30 s watchdog and shell exit.
- `ps5wl-gpu-c2a-16384.elf`: log port 9593.
- `ps5wl-gpu-c2a-8388608.elf`: log port 9593.
- `ps5wl-gpu-c2a-67108864.elf`: log port 9593.
- `ps5wl-gpu-c2-verify.elf`: log port 9594.
- `ps5wl-gpu-c2b.elf`: log port 9530; 30 s watchdog; native shell exit.

C1 artifact SHA256 (watchdog build):

```text
532379d3b145c3e45f4861743c3d43bbcf55d87f49068496966dff169273b72b  ps5wl-gpu-c1-plain.elf
303b7d9d509ba740ba60cf68a9188e207a57f06ad7eb0f93924fe7fdd9eeabcb  ps5wl-gpu-c1-app.elf
89ec98e583af43889f9f4aff23d4f0fa4207fdb7bdcca823267b4720da5c2a2d  ps5wl-gpu-c1-remaining.elf
```

The app probes do not use VideoOut, process creation, or budget-changing APIs.
Their normal and watchdog exits call `sceSystemServiceLoadExec("exit", NULL)`;
there is no app `_exit`/SIGKILL cleanup path. A separate close helper must remain
ready. Plain C2 retains potentially GPU-owned memory on a failed submit/fence,
rather than freeing live pages or calling an unbounded `vkDeviceWaitIdle`.

## Run authorization and containment

Ps5Backend owns the console and grants **one exact run at a time** after a fresh
process check. Do not run any artifact merely because it built. Stop the ladder
at the first unexpected result. Plain C1 and the first app C1 run have completed.

The first request is C1 plain, using `tools/ps5ctl run` and a bounded port-9592
log pull. Backend initially withheld the grant because normal compositor PID 91
and demo PID 92 were foreground alongside resident installer PID 85. Main then
authorized Backend to close the normal app using helper PID 93. Backend reported
all close returns zero, no running BigApp (`ffffffff`), and a fresh process list
with only installer 85 among project payloads. Backend additionally required a
native plain-process watchdog, implemented and rebuilt before the granted run.

Main approved an independent background hbldr diagnostic route instead of the
original `launcher/test.sh --elf`, because that original mode replaces the
installed target and conflicts with installer 85. Launcher owns the independent
route. Neither diagnostic may replace the stable installed target, its manifest,
or installer 85. This authorization changes the harness only, not the required
foreground app context, deadline, or LoadExec exit.

## Per-item verdicts

| Experiment | Item | Verdict | Return code / excerpt |
| --- | --- | --- | --- |
| C1 plain | 16 KiB SHM_ANON / MAP_SHARED; GPU-RW mprotect | PASS | shm/ftruncate/mmap/unmap/close successful; mprotect `rc=0x0` |
| C1 app | 16 KiB SHM_ANON / MAP_SHARED; GPU-RW mprotect | PASS | shm/ftruncate/mmap/unmap/close successful; mprotect `rc=0x0` |
| C1 plain | Flexible availability before / mapped-and-touched / released, 64 MiB | MEASURED; mapping PASS | All query returns `0x0`; availability always 8555331584 |
| C1 plain | Flexible availability, 256 MiB | MEASURED; mapping PASS | All query returns `0x0`; availability always 8555331584 |
| C1 plain | Flexible availability, 1024 MiB | MEASURED; mapping PASS | All query returns `0x0`; availability always 8555331584 |
| C1 app | Flexible availability, 64 MiB | MEASURED; mapping PASS | 428867584 → 361758720 → 428867584; all query returns `0x0` |
| C1 app | Flexible availability, 256 MiB | MEASURED; mapping PASS | 428867584 → 160432128 → 428867584; all query returns `0x0` |
| C1 app | Flexible availability, 1024 MiB | EXPECTED CAPACITY LIMIT; first app run STOP | Before 428867584; shm/ftruncate succeeded, mmap failed `errno=12` (ENOMEM); no mapped query possible |
| C1 app | Independent type-12 full-pool direct allocations, 2 MiB steps through 256 MiB; each released | PASS | All 128 allocations and 128 releases returned `0x0`; pool 12884901888 bytes |
| C1 app | Independent SDL-style MainDirectMemory type-3 allocations, 2 MiB steps through 256 MiB; each released | PASS | All 128 allocations and 128 releases returned `0x0` |
| C1 app | RADV dlopen / vkCreateInstance only | PASS | dlopen non-null / no error; `vkCreateInstance result=0` |
| C1 plain | AF_UNIX SOCK_SEQPACKET socketpair and one-byte transfer | PASS | `rc=0 errno=0`; `send=1 recv=1 byte=0x51` |
| C1 app | AF_UNIX SOCK_SEQPACKET socketpair and one-byte transfer | PASS | `rc=0 errno=0`; `send=1 recv=1 byte=0x51` |
| C2a | 16 KiB imported SHM GPU fill / 2 s fence / CPU readback / second-payload verification | PASS | fence `0`; both 4096-word readbacks `mismatches=0`; native and host pair PASS |
| C2a | 8 MiB imported SHM GPU fill / 2 s fence / both readbacks | NOT RUN — gated by smaller size | No hardware log |
| C2a | 64 MiB imported SHM GPU fill / 2 s fence / both readbacks | NOT RUN — gated by smaller size | No hardware log |
| C2b app | RADV device / R32_UINT image clear / copy to imported 16 KiB SHM / 2 s fence / readback, no VideoOut | NOT RUN — gated by C2a | No hardware log |

C2a fills the complete allocation with `0x1a2b3c4d`. The second payload receives
the **same SHM_ANON fd** through SCM_RIGHTS at
`/system_tmp/ps5wl-gpu-probe`, maps it MAP_SHARED, checks every word, and sends an
acknowledgement. The producer does not spawn the verifier. Backend authorizes
both dependent launches as one exact size-specific C2a experiment. Import types
are selected from the intersection of host-pointer and buffer memory-type masks
and must be coherent.
C2b clears a 64×64 R32_UINT image to the same value, copies all 4096 pixels into
its imported SHM page, and checks all words after the fence.

## C1 plain exact hardware evidence

Granted command:

```sh
tools/ps5ctl run build/ps5/gpu-probes/ps5wl-gpu-c1-plain.elf &&
timeout 35 tools/ps5ctl logs --port 9592 --retry 10 |
tee build/ps5/gpu-probes/c1-plain.log
```

Composite command exit **0**, elapsed **8.19 s**; the pipeline did not record the
log pull's status separately, so that isolated status is **not claimed**. The
payload logged `code=0`, not its native process wait status (not provided by
Payload Manager). One post-run `tools/ps5ctl ps` exited 0 and showed installer
85 intact with PID 94 absent, no diagnostic, and no compositor/demo.

Exact excerpts from the full log `build/ps5/gpu-probes/c1-plain.log`:

```text
[2368799 ms] ps5wl-gpu-c1-plain: context pid=94 uid=1 app_rc=0x0 own_app=0x0 foreground=0xffffffff page=16384
[2368799 ms] ps5wl-gpu-c1-plain: shm_open bytes=16384 fd=8 errno=0
[2368799 ms] ps5wl-gpu-c1-plain: ftruncate bytes=16384 rc=0 errno=0
[2368799 ms] ps5wl-gpu-c1-plain: mmap MAP_SHARED bytes=16384 ptr=2001ec000 errno=0
[2368799 ms] ps5wl-gpu-c1-plain: sceKernelMprotect SHM_ANON MAP_SHARED bytes=16384 protection=0x33 rc=0x0
[2368799 ms] ps5wl-gpu-c1-plain: munmap bytes=16384 rc=0
[2368799 ms] ps5wl-gpu-c1-plain: close SHM rc=0
[2368799 ms] ps5wl-gpu-c1-plain: flexible stage=before mapping=67108864 rc=0x0 available=8555331584
[2368830 ms] ps5wl-gpu-c1-plain: flexible stage=mapped-touched mapping=67108864 rc=0x0 available=8555331584
[2368832 ms] ps5wl-gpu-c1-plain: flexible stage=released mapping=67108864 rc=0x0 available=8555331584
[2368832 ms] ps5wl-gpu-c1-plain: flexible stage=before mapping=268435456 rc=0x0 available=8555331584
[2368987 ms] ps5wl-gpu-c1-plain: flexible stage=mapped-touched mapping=268435456 rc=0x0 available=8555331584
[2368995 ms] ps5wl-gpu-c1-plain: flexible stage=released mapping=268435456 rc=0x0 available=8555331584
[2368995 ms] ps5wl-gpu-c1-plain: flexible stage=before mapping=1073741824 rc=0x0 available=8555331584
[2372263 ms] ps5wl-gpu-c1-plain: flexible stage=mapped-touched mapping=1073741824 rc=0x0 available=8555331584
[2372313 ms] ps5wl-gpu-c1-plain: flexible stage=released mapping=1073741824 rc=0x0 available=8555331584
[2372313 ms] ps5wl-gpu-c1-plain: AF_UNIX SOCK_SEQPACKET socketpair rc=0 errno=0
[2372313 ms] ps5wl-gpu-c1-plain: SEQPACKET send=1 recv=1 byte=0x51
[2372313 ms] ps5wl-gpu-c1-plain: C1 RESULT PASS code=0
```

Each larger mapping was touched and read at every 16 KiB page before the second
availability query and then unmapped/closed. This demonstrates that those plain
SHM mappings succeed, **not** that `AvailableFlexibleMemorySize` accounts SHM:
its value did not change, even with 1 GiB touched. Do not derive an app host-byte
quota or a kernel-wide free-memory estimate from that unchanged number.

## C1 app exact hardware evidence and quota gate

Backend granted exactly:

```sh
launcher/test.sh --diagnostic-elf build/ps5/gpu-probes/ps5wl-gpu-c1-app.elf
```

Combined stdout/stderr was captured with `bash -o pipefail` and `tee` to
`build/ps5/gpu-probes/c1-app.log`. Harness exit **0**, elapsed **12.24 s**,
proves lifecycle completion, **not C1 success**: the body logged `STOP code=1`.
The source requests `LoadExec("exit")` after its 5 s log flush; the app vanished
before its LoadExec return-code line could be captured. The driver observed
child disappearance. One post-run `tools/ps5ctl ps` exited 0: both body PID 96
and driver PID 95 were absent, installer 85 remained, no compositor/demo ran.

```text
[2555945 ms] ps5wl-gpu-c1-app: context pid=96 uid=0 app_rc=0x0 own_app=0x4019 foreground=0x4019 page=16384
[2555945 ms] ps5wl-gpu-c1-app: shm_open bytes=16384 fd=15 errno=0
[2555945 ms] ps5wl-gpu-c1-app: ftruncate bytes=16384 rc=0 errno=0
[2555945 ms] ps5wl-gpu-c1-app: mmap MAP_SHARED bytes=16384 ptr=201e50000 errno=0
[2555945 ms] ps5wl-gpu-c1-app: sceKernelMprotect SHM_ANON MAP_SHARED bytes=16384 protection=0x33 rc=0x0
[2555945 ms] ps5wl-gpu-c1-app: flexible stage=before mapping=67108864 rc=0x0 available=428867584
[2555951 ms] ps5wl-gpu-c1-app: flexible stage=mapped-touched mapping=67108864 rc=0x0 available=361758720
[2555952 ms] ps5wl-gpu-c1-app: flexible stage=released mapping=67108864 rc=0x0 available=428867584
[2555952 ms] ps5wl-gpu-c1-app: flexible stage=before mapping=268435456 rc=0x0 available=428867584
[2555979 ms] ps5wl-gpu-c1-app: flexible stage=mapped-touched mapping=268435456 rc=0x0 available=160432128
[2555982 ms] ps5wl-gpu-c1-app: flexible stage=released mapping=268435456 rc=0x0 available=428867584
[2555982 ms] ps5wl-gpu-c1-app: flexible stage=before mapping=1073741824 rc=0x0 available=428867584
[2555982 ms] ps5wl-gpu-c1-app: shm_open bytes=1073741824 fd=15 errno=0
[2555982 ms] ps5wl-gpu-c1-app: ftruncate bytes=1073741824 rc=0 errno=0
[2555982 ms] ps5wl-gpu-c1-app: mmap MAP_SHARED bytes=1073741824 ptr=ffffffffffffffff errno=12
[2555982 ms] ps5wl-gpu-c1-app: C1 RESULT STOP code=1
[2555024 ms] ps5wl-launch-diag: isolated driver pid=95 auth_rc=0 bytes=21483256
[2555670 ms] ps5wl-launch-diag: sceSystemServiceLaunchApp title=PSWL00002 user=437284342 rc=0x4019
[2555757 ms] ps5wl-launch-diag: diagnostic launch child=96 errno=0 staging_unlink=0 errno=0
[2561163 ms] ps5wl-launch-diag: diagnostic child=96 disappeared
[2561163 ms] ps5wl-launch-diag: diagnostic result=0 child=96
```

Both successful sizes reduced availability by **exactly their allocation size**
and restored it fully after release. The measured 409 MiB is a diagnostic
baseline, not a promise of remaining space after compositor/runtime startup.
GpudCore and VenusIcd were notified. Main authorized a continuation that skips
already measured SHM stages and any mapping above 256 MiB. No retry of the
failed 1 GiB allocation is authorized or needed.

## C1 authorized continuation exact evidence

Granted command:

```sh
launcher/test.sh --diagnostic-elf build/ps5/gpu-probes/ps5wl-gpu-c1-remaining.elf
```

Full combined capture: `build/ps5/gpu-probes/c1-remaining.log`; pipefail-enabled
harness exit **0**, elapsed **15.57 s**, native **`C1 RESULT PASS code=0`**.
Both direct allocation modes completed all 128 independent sizes, releasing
each allocation before the next size: **256 allocations and 256 releases,
all rc=0**. No direct memory was mapped or submitted to the GPU. One final
process check exited 0: app 99 and driver 98 gone, installer 85 intact.

```text
[2671921 ms] ps5wl-gpu-c1-app: context pid=99 uid=0 app_rc=0x0 own_app=0x6019 foreground=0x6019 page=16384
[2671921 ms] ps5wl-gpu-c1-app: C1 authorized continuation: skip already measured flexible-memory stage
[2671921 ms] ps5wl-gpu-c1-app: AF_UNIX SOCK_SEQPACKET socketpair rc=0 errno=0
[2671921 ms] ps5wl-gpu-c1-app: SEQPACKET send=1 recv=1 byte=0x51
[2671921 ms] ps5wl-gpu-c1-app: direct pool=12884901888 independent released allocations step=2MiB ceiling=256MiB
[2671921 ms] ps5wl-gpu-c1-app: direct api=full type=12 bytes=2097152 rc=0x0 physical=0x200000
[2671921 ms] ps5wl-gpu-c1-app: direct release bytes=2097152 rc=0x0
[2673207 ms] ps5wl-gpu-c1-app: direct api=full type=12 bytes=268435456 rc=0x0 physical=0x200000
[2673207 ms] ps5wl-gpu-c1-app: direct release bytes=268435456 rc=0x0
[2673217 ms] ps5wl-gpu-c1-app: direct api=main type=3 bytes=2097152 rc=0x0 physical=0x200000
[2673217 ms] ps5wl-gpu-c1-app: direct release bytes=2097152 rc=0x0
[2674502 ms] ps5wl-gpu-c1-app: direct api=main type=3 bytes=268435456 rc=0x0 physical=0x200000
[2674502 ms] ps5wl-gpu-c1-app: direct release bytes=268435456 rc=0x0
[2675555 ms] ps5wl-gpu-c1-app: RADV extracted path=/data/ps5wl/probe-radv.so.1 bytes=21201344 written=21201344 fclose=0
[2675575 ms] ps5wl-gpu-c1-app: RADV dlopen handle=880bb9160 error=none
[2675575 ms] ps5wl-gpu-c1-app: RADV vkCreateInstance result=0 instance=880bab8c0 (no physical-device enumeration or GPU work)
[2675597 ms] ps5wl-gpu-c1-app: C1 RESULT PASS code=0
[2680739 ms] ps5wl-launch-diag: diagnostic child=99 disappeared
[2680739 ms] ps5wl-launch-diag: diagnostic result=0 child=99
```

This clears dynamic loading and instance creation only, not RADV device/AGC
initialization or GPU host-pointer import. Those remain C2 measurements.

## C2a 16 KiB exact hardware evidence

Granted command: `python3 gpu-server/probes/run-c2a.py 16384`.
Full capture: `build/ps5/gpu-probes/c2a-16384.log`. Runner exit **0**, elapsed
**9.90 s**. Both uploads, both launches, verifier log pull and producer log pull
each returned **0**. Backend had started the installed **normal non-gpud**
compositor PID 102 (foreground app `0x8019`) and demo 103; installer 85 remained.
One final process check returned 0: producer 104 and verifier 105 absent,
102/103/85 retained.

```text
[2934393 ms] ps5wl-gpu-c2a: plain context pid=104 app_rc=0x0 own_app=0x0 foreground=0x8019
[2935612 ms] ps5wl-gpu-c2a: RADV dlopen handle=8808c1580 error=none
[2935619 ms] ps5wl-gpu-c2a: device=PlayStation 5 GPU (RADV NAVI21) host-pointer alignment=16384
[2935619 ms] ps5wl-gpu-c2a: pCreateDevice(physical, &dci, NULL, &device) result=0
[2935619 ms] ps5wl-gpu-c2a: SHM_ANON MAP_SHARED fd=13 bytes=16384 ptr=202d98000 errno=32
[2935619 ms] ps5wl-gpu-c2a: pGetMemoryHostPointerPropertiesEXT(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, mapping, &pointer) result=0
[2935619 ms] ps5wl-gpu-c2a: import pointer-types=0x20 buffer-types=0x7ad type=5 allocation-size=16384 SHM-size=16384
[2935619 ms] ps5wl-gpu-c2a: pAllocateMemory(device, &mai, NULL, &memory) result=0
[2935619 ms] ps5wl-gpu-c2a: pBindBufferMemory(device, buffer, memory, 0) result=0
[2935620 ms] ps5wl-gpu-c2a: pQueueSubmit(queue, 1, &submit, fence) result=0
[2935623 ms] ps5wl-gpu-c2a: vkWaitForFences timeout_ns=2000000000 result=0
[2935623 ms] ps5wl-gpu-c2a: readback bytes=16384 words=4096 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[2937109 ms] ps5wl-gpu-c2-verifier: second process pid=105 SCM_RIGHTS fd=9 MAP_SHARED bytes=16384 ptr=200e98000 errno=3
[2937109 ms] ps5wl-gpu-c2-verifier: readback bytes=16384 words=4096 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[2937109 ms] ps5wl-gpu-c2-verifier: C2 cross-process RESULT PASS code=0
[2937109 ms] ps5wl-gpu-c2a: second-process acknowledgement=0x50 result=0
[2937134 ms] ps5wl-gpu-c2a: C2 RESULT PASS code=0 wedged=0
HOST command returncode=0
HOST producer log returncode=0
HOST C2a PAIR PASS
```

The `errno` fields beside successful mmap pointers are stale libc errno values:
the returned pointers are valid, neither mmap returned MAP_FAILED, and both
readbacks passed. Only a failed operation's errno is an error result.

## PLAN.md §6 decision gates

| Gate | Outcome |
| --- | --- |
| BigApp SHM mprotect fails → private imports plus server-side memcpy | NOT TRIGGERED: 16 KiB shared mapping mprotect passed; actual Vulkan import/GPU access remains C2 |
| Flexible headroom below 1 GiB → lower host quotas and report | TRIGGERED: app baseline 428867584 bytes (409 MiB), SHM counted exactly; lower aggregate/per-context host quotas and retain runtime/compositor reserve; Main and GpudCore notified |
| BigApp RADV dlopen fails → static link | NOT TRIGGERED: shared RADV dlopen and vkCreateInstance passed in app 99; device/AGC execution remains C2 |
| C2a optional all-flexible RADV BO option-f passes fully → offer native fast path | NOT TESTED; optional all-BO-flexible allocator is not implemented by these probes; ordinary imported-SHM fill is not evidence for option f |
| SOCK_SEQPACKET unsupported → framed SOCK_STREAM render front end | Socketpair PASSED in both plain and app C1; no availability-driven fallback needed from this result; bound sockets / protocol fd passing still require their own tests |

Neither GpudCore nor VenusIcd should infer a positive memory, loading, coherence,
or payload-GPU gate from a successful cross-build.
