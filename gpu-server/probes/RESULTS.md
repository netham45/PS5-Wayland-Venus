# GPU console probes C1 / C2 / C3 / C4

Status: **C1, all three C2a sizes, C2b, and the initial C3 completed.** C4's
first run stalled after the first asynchronous present; investigation and the
corrected rerun are recorded below. Shared-SHM GPU import,
GPU writes/readback, cross-process coherence, and in-app shared RADV execution
passed. **App flexible headroom at the measured baseline was only 428,867,584
bytes (409 MiB); SHM is counted against it.** The 1 GiB map failed ENOMEM.
Main treated this as the expected §6 quota gate and authorized the remaining
measurements; it was not retried. All runs had separate Backend grants.
Firmware was not queried by these probes; contexts below are from actual runs.

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

Artifact SHA256 at the corresponding granted runs (historical C1 artifacts may
differ from later rebuilt debug binaries):

```text
532379d3b145c3e45f4861743c3d43bbcf55d87f49068496966dff169273b72b  ps5wl-gpu-c1-plain.elf
303b7d9d509ba740ba60cf68a9188e207a57f06ad7eb0f93924fe7fdd9eeabcb  ps5wl-gpu-c1-app.elf
89ec98e583af43889f9f4aff23d4f0fa4207fdb7bdcca823267b4720da5c2a2d  ps5wl-gpu-c1-remaining.elf
b2dceb75a4a7d0f9fbfcc0fe3b6ff23f5862877deb3ad4f5e2c093bf57bc9b6f  ps5wl-gpu-c2a-16384.elf
fea6c61c52f44763842613d71bed734ab050163f1e07237e310a8530b978f279  ps5wl-gpu-c2a-8388608.elf
db2b17258260d10818ea1cc929b507a19e918388489dc9553cd0b9164d56627d  ps5wl-gpu-c2a-67108864.elf
c326221d43270827061a0c42b216e85f43d121512c8cfdd4d9fdcb8f520d3b11  ps5wl-gpu-c2-verify.elf
69976f37d4a8713362133277ab99c9a6f9e48153e6980ac5ba05d63049b757a6  ps5wl-gpu-c2b.elf
```

The app probes do not use VideoOut, process creation, or budget-changing APIs.
Their normal and watchdog exits call `sceSystemServiceLoadExec("exit", NULL)`;
there is no app `_exit`/SIGKILL cleanup path. A separate close helper must remain
ready. Plain C2 retains potentially GPU-owned memory on a failed submit/fence,
rather than freeing live pages or calling an unbounded `vkDeviceWaitIdle`.

## Run authorization and containment

Ps5Backend owns the console and grants **one exact run at a time** after a fresh
process check. Do not run any artifact merely because it built. Stop the ladder
at the first unexpected result. Every required C1/C2 measurement is now recorded.

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
| C2a | 8 MiB imported SHM GPU fill / 2 s fence / both readbacks | PASS | fence `0`; both 2097152-word readbacks `mismatches=0`; native and host pair PASS |
| C2a | 64 MiB imported SHM GPU fill / 2 s fence / both readbacks | PASS | fence `0`; both 16777216-word readbacks `mismatches=0`; native and host pair PASS |
| C2b app | RADV device / R32_UINT image clear / copy to imported 16 KiB SHM / 2 s fence / readback, no VideoOut | PASS | device/import/submit/fence `0`; all 4096 words `mismatches=0`; native and lifecycle PASS |

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

## C2a 8 MiB exact hardware evidence

Granted command: `python3 gpu-server/probes/run-c2a.py 8388608`.
Full capture: `build/ps5/gpu-probes/c2a-8388608.log`. Runner exit **0**, elapsed
**9.32 s**; both uploads, both launches, and both log pulls returned **0**.
One final process check exited 0: producer 107 and verifier 108 absent;
normal compositor 102, demo 103 and installer 85 intact.

```text
[3059161 ms] ps5wl-gpu-c2a: plain context pid=107 app_rc=0x0 own_app=0x0 foreground=0x8019
[3060480 ms] ps5wl-gpu-c2a: device=PlayStation 5 GPU (RADV NAVI21) host-pointer alignment=16384
[3060481 ms] ps5wl-gpu-c2a: pCreateDevice(physical, &dci, NULL, &device) result=0
[3060481 ms] ps5wl-gpu-c2a: SHM_ANON MAP_SHARED fd=13 bytes=8388608 ptr=204200000 errno=32
[3060483 ms] ps5wl-gpu-c2a: import pointer-types=0x20 buffer-types=0x7ad type=5 allocation-size=8388608 SHM-size=8388608
[3060483 ms] ps5wl-gpu-c2a: pAllocateMemory(device, &mai, NULL, &memory) result=0
[3060485 ms] ps5wl-gpu-c2a: pQueueSubmit(queue, 1, &submit, fence) result=0
[3060485 ms] ps5wl-gpu-c2a: vkWaitForFences timeout_ns=2000000000 result=0
[3060486 ms] ps5wl-gpu-c2a: readback bytes=8388608 words=2097152 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[3060869 ms] ps5wl-gpu-c2-verifier: second process pid=108 SCM_RIGHTS fd=9 MAP_SHARED bytes=8388608 ptr=20087c000 errno=3
[3060871 ms] ps5wl-gpu-c2-verifier: readback bytes=8388608 words=2097152 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[3060871 ms] ps5wl-gpu-c2-verifier: C2 cross-process RESULT PASS code=0
[3060871 ms] ps5wl-gpu-c2a: second-process acknowledgement=0x50 result=0
[3060890 ms] ps5wl-gpu-c2a: C2 RESULT PASS code=0 wedged=0
HOST command returncode=0
HOST producer log returncode=0
HOST C2a PAIR PASS
```

## C2a 64 MiB exact hardware evidence

Granted command: `python3 gpu-server/probes/run-c2a.py 67108864`.
Full capture: `build/ps5/gpu-probes/c2a-67108864.log`. Runner exit **0**, elapsed
**9.12 s**; both uploads, both launches and both log pulls returned **0**.
One final process check exited 0: producer 110 and verifier 111 absent;
normal compositor 102, demo 103 and installer 85 intact.

```text
[3182996 ms] ps5wl-gpu-c2a: plain context pid=110 app_rc=0x0 own_app=0x0 foreground=0x8019
[3184217 ms] ps5wl-gpu-c2a: device=PlayStation 5 GPU (RADV NAVI21) host-pointer alignment=16384
[3184217 ms] ps5wl-gpu-c2a: pCreateDevice(physical, &dci, NULL, &device) result=0
[3184217 ms] ps5wl-gpu-c2a: SHM_ANON MAP_SHARED fd=13 bytes=67108864 ptr=204800000 errno=32
[3184236 ms] ps5wl-gpu-c2a: import pointer-types=0x20 buffer-types=0x7ad type=5 allocation-size=67108864 SHM-size=67108864
[3184240 ms] ps5wl-gpu-c2a: pAllocateMemory(device, &mai, NULL, &memory) result=0
[3184242 ms] ps5wl-gpu-c2a: pQueueSubmit(queue, 1, &submit, fence) result=0
[3184244 ms] ps5wl-gpu-c2a: vkWaitForFences timeout_ns=2000000000 result=0
[3184252 ms] ps5wl-gpu-c2a: readback bytes=67108864 words=16777216 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[3184635 ms] ps5wl-gpu-c2-verifier: second process pid=111 SCM_RIGHTS fd=9 MAP_SHARED bytes=67108864 ptr=201024000 errno=3
[3184645 ms] ps5wl-gpu-c2-verifier: readback bytes=67108864 words=16777216 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[3184645 ms] ps5wl-gpu-c2-verifier: C2 cross-process RESULT PASS code=0
[3184645 ms] ps5wl-gpu-c2a: second-process acknowledgement=0x50 result=0
[3184673 ms] ps5wl-gpu-c2a: C2 RESULT PASS code=0 wedged=0
HOST command returncode=0
HOST producer log returncode=0
HOST C2a PAIR PASS
```

All three required C2a sizes therefore passed GPU-write/CPU-read coherence both
in the producer and through a separately mapped SCM_RIGHTS SHM fd in another
process, while the ordinary compositor remained foreground. This is not an
all-flexible-BO allocator experiment and does not settle optional option f.

## C2b BigApp exact hardware evidence

Backend closed only its owned normal compositor using the shipping helper
before granting this diagnostic. Granted command:

```sh
launcher/test.sh --diagnostic-elf build/ps5/gpu-probes/ps5wl-gpu-c2b.elf
```

Full pipefail-enabled combined capture: `build/ps5/gpu-probes/c2b.log`.
Harness exit **0**, elapsed **13.09 s**; native **`PASS code=0 wedged=0`**;
driver **`result=0`**, both log observers reached EOF. One final process check
exited 0: app 115 and driver 114 absent; only installer 85 among project
processes, no compositor/demo. The stable installed target and resident
installer were not replaced. As in C1, shell termination preceded any captured
LoadExec return-code line; child disappearance is the observed exit evidence.

```text
[3296964 ms] ps5wl-gpu-c2b: context pid=115 app_rc=0x0 own_app=0xa019 foreground=0xa019
[3298052 ms] ps5wl-gpu-c2b: RADV dlopen handle=880771160 error=none
[3298053 ms] ps5wl-gpu-c2b: pCreateInstance(&ici, NULL, &instance) result=0
[3298053 ms] ps5wl-gpu-c2b: pEnumeratePhysicalDevices(instance, &count, &physical) result=0
[3298053 ms] ps5wl-gpu-c2b: device=PlayStation 5 GPU (RADV NAVI21) host-pointer alignment=16384
[3298053 ms] ps5wl-gpu-c2b: pCreateDevice(physical, &dci, NULL, &device) result=0
[3298053 ms] ps5wl-gpu-c2b: SHM_ANON MAP_SHARED fd=15 bytes=16384 ptr=202ae8000 errno=32
[3298053 ms] ps5wl-gpu-c2b: import pointer-types=0x20 buffer-types=0x7ad type=5 allocation-size=16384 SHM-size=16384
[3298053 ms] ps5wl-gpu-c2b: pAllocateMemory(device, &mai, NULL, &memory) result=0
[3298053 ms] ps5wl-gpu-c2b: pBindBufferMemory(device, buffer, memory, 0) result=0
[3298053 ms] ps5wl-gpu-c2b: pCreateImage(device, &image_info, NULL, &image) result=0
[3298053 ms] ps5wl-gpu-c2b: pAllocateMemory(device, &mai, NULL, &image_memory) result=0
[3298053 ms] ps5wl-gpu-c2b: pBindImageMemory(device, image, image_memory, 0) result=0
[3298056 ms] ps5wl-gpu-c2b: pQueueSubmit(queue, 1, &submit, fence) result=0
[3298056 ms] ps5wl-gpu-c2b: vkWaitForFences timeout_ns=2000000000 result=0
[3298056 ms] ps5wl-gpu-c2b: readback bytes=16384 words=4096 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[3298090 ms] ps5wl-gpu-c2b: C2 RESULT PASS code=0 wedged=0
[3296099 ms] ps5wl-launch-diag: isolated driver pid=114 auth_rc=0 bytes=21533496
[3296724 ms] ps5wl-launch-diag: sceSystemServiceLaunchApp title=PSWL00002 user=437284342 rc=0xa019
[3296786 ms] ps5wl-launch-diag: diagnostic launch child=115 errno=0 staging_unlink=0 errno=0
[3303293 ms] ps5wl-launch-diag: diagnostic child=115 disappeared
[3303293 ms] ps5wl-launch-diag: diagnostic result=0 child=115
```

The app path recorded a 64×64 R32_UINT image clear, image-to-buffer copy, and
transfer-to-host barrier; the imported SHM buffer contained all 4096 expected
words. There is no VideoOut use in the diagnostic. This demonstrates RADV
device initialization and successful GPU execution in the hbldr BigApp; a
separate explicit `sceAgcInit` return was not instrumented or claimed.

## PLAN.md §6 decision gates

| Gate | Outcome |
| --- | --- |
| BigApp SHM mprotect fails → private imports plus server-side memcpy | NOT TRIGGERED: C1 shared mprotect and C2b imported-SHM GPU readback passed; keep the shared host-import design |
| Flexible headroom below 1 GiB → lower host quotas and report | TRIGGERED: app baseline 428867584 bytes (409 MiB), SHM counted exactly; lower aggregate/per-context host quotas and retain runtime/compositor reserve; Main and GpudCore notified |
| BigApp RADV dlopen fails → static link | NOT TRIGGERED: shared RADV dlopen/instance passed C1; device creation and GPU execution passed C2b; no loading-driven static fallback required |
| C2a optional all-flexible RADV BO option-f passes fully → offer native fast path | NOT TESTED; optional all-BO-flexible allocator is not implemented by these probes; ordinary imported-SHM fill is not evidence for option f |
| SOCK_SEQPACKET unsupported → framed SOCK_STREAM render front end | Socketpair PASSED in both plain and app C1; no availability-driven fallback needed from this result; bound sockets / protocol fd passing still require their own tests |

GpudCore and VenusIcd received the loading, memory-type, quota and coherence
results. The C1/C2 ladder alone was not an end-to-end Venus/vkr/vtest or
presentation pass. No optional all-flexible-BO option-f run, deliberate GPU-hang
recovery test, or LKL-tree changes were performed here.

## C3 / G3 — Venus payload readback through the compositor

All captures in this and the C4 sections are under
`build/ps5/evidence/gpu/`. The initial bounded run used:

```sh
env/run python3 gpu-server/build.py ps5
env/run env PS5WL_TEST_SECONDS=60 compositor/build.sh ps5
launcher/test.sh --diagnostic-elf "$PWD/build/ps5/compositor/ps5wl"
# From a second host observer, after the compositor's Wayland-ready line:
tools/ps5ctl run build/ps5/vkcube-venus/ps5wl-vktest-venus.elf
tools/ps5ctl logs --port 9591 --retry 10
```

The isolated diagnostic mode was chosen deliberately: it preserves resident
installer 85, the home-screen icon, installed compositor/demo, RADV, and demo-only
autostart manifest. The existing installed RADV was used, not a diagnostic
replacement. The diagnostic still starts the real foreground compositor and
the existing manifest autostarts the separate demo. No process is created from
the app process: autostart requests go to the existing elfldr.

`c3-{before,app-started,client-started,after}.log` contain successful process
checks around the foreground/client launches and after completion. Foreground
compositor PID 118, driver 117, demo 119 and Venus readback payload 120 all
disappeared; only the expected installer 85 remained among project processes.
Both diagnostic harness and client log observer exited **0**.

Exact excerpts (`c3-client.log`, `c3-compositor.log`):

```text
[3714153 ms] ps5wl-vktest: device=Virtio-GPU Venus (PlayStation 5 GPU (RADV NAVI21)) vendor=1002 device=0000 api=1.4.307
[3714173 ms] ps5wl-vktest: GPU readback words=16384 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[3714174 ms] ps5wl-vktest: RESULT PASS
[3714148 ms] ps5wl-compositor: gpud: context created id=1 name=payload.elf contexts=1
[3714151 ms] ps5wl-compositor: gpud: vkr: gpud host-pointer importable memory types: 0x20
[3714173 ms] ps5wl-compositor: gpud: vkr: host-pointer blob: size=65536 (ordinary SHM fd; no Vulkan fd export)
[3714175 ms] ps5wl-compositor: gpud: context destroyed id=1 resources_released=1 released_host_charge_bytes=8388608 released_device_charge_bytes=0 contexts=0
[3773124 ms] ps5wl-compositor: gpud: gpud stopped: Vulkan fd-export calls=0
[3773124 ms] ps5wl-compositor: gpud stop result=0
[3774366 ms] ps5wl-launch-diag: diagnostic result=0 child=118
```

The lifecycle log is emitted after vkr destroys the context and its resources.
The 8-MiB charge is a discarded transport-resource ledger charge, not a leaked
allocation. Native clean-shutdown/backend-release lines and the driver's
observed child disappearance establish the bounded foreground exit.

## C4 / G4 — initial failure and host-first investigation

The same bounded diagnostic artifact was used with the separate plain payload
`build/ps5/vkcube-venus/ps5wl-vkcube-venus.elf`. Full logs:
`c4-compositor.log`, `c4-client.log`, launch receipts and process snapshots
`c4-*.log`. Driver 122 launched foreground compositor 123; demo 124 autostarted;
cube 125 connected. The cube logged:

```text
[3794994 ms] ps5wl-vkcube: Vulkan prepare call completed
[3795003 ms] ps5wl-vkcube: vkcube presented frame=1
```

No 60th or 600th cube frame was observed. The cube uses Venus's asynchronous
present worker, so that first client counter is not proof of a completed image.
There was no cube surface-buffer commit in the compositor log. The compositor's
continuing 1920x1080 commit counters belonged to the demo and must not be counted
as cube acceptance. Port-9531 readback `c4-first-frame.png` (6,240,291 bytes)
was inspected: it shows the interactive checkerboard demo, **not a cube**.

The compositor continued flipping until its 60-second timer and recorded:

```text
[3853987 ms] ps5wl-compositor: bounded hardware validation timeout; releasing VideoOut
[3853988 ms] ps5wl-compositor: gpud: vkr: destroying context 1 (vkcube) with a valid instance
[3853988 ms] ps5wl-compositor: gpud: vkr: destroying device with valid objects
[3853989 ms] ps5wl-compositor: gpud: context destroyed id=1 resources_released=10 released_host_charge_bytes=43106304 released_device_charge_bytes=0 contexts=0
[3853989 ms] ps5wl-compositor: gpud: gpud stopped: Vulkan fd-export calls=0
[3853989 ms] ps5wl-compositor: gpud stop result=0
[3854082 ms] ps5wl-compositor: direct release rc=0
[3854082 ms] ps5wl-compositor: compositor clean shutdown
[3855238 ms] ps5wl-launch-diag: diagnostic child=123 disappeared
[3855238 ms] ps5wl-launch-diag: diagnostic result=0 child=123
```

The mandatory post-run `ps5ctl ps` then failed with connection refused
(`c4-after.log`); all console actions stopped. Main subsequently reported no
ping response. The user power-cycled and suspected idle auto power-down; that
is a hypothesis, **not proof of either a GPU crash or an idle shutdown**.
There is no captured GPU-fault message, wedged-drain log, or blocked teardown:
vkr context destruction and gpud stop each completed within one millisecond.

### Offline defects and constraints distinguished

1. **FreeBSD thread-ID ABI defect.** The pinned Venus `vn_gettid` used
   `syscall(SYS_thr_self)` without the required `long *` output argument.
   SDK `sys/thr.h` declares `thr_self(long *)`, and `sys/sysproto.h` defines
   `struct thr_self_args` with that pointer. Syscall status is not a thread ID.
   WSI compares this value to distinguish the async present worker before
   skipping queue flushes or releasing locks. Patch
   `deps/mesa/venus-patches/0006-freebsd-thread-id.patch` supplies the pointer
   and checks failure. This is a definite ABI defect and a C4-stall hypothesis;
   the initial logs alone cannot identify the worker's exact blocking call.

2. **Quota errors were lost by asynchronous Venus memory allocation.** The
   added `gpu-server/tests/vtest-cube --sanitize --ps5-constraints` reproduces
   1920x1080/128-MiB/two-context/no-external-fd conditions on host lavapipe.
   The old client aborted after frame 1. Unlike the console, the host compositor
   committed one cube image. Server evidence in `host-ps5-quota/server.log`:

   ```text
   quota rejected context=1 host=1 used=126173184 request=8306688 limit=134217728 allocations=20
   vkr: failed to look up object 91 of type 8
   vkr: vkBindBufferMemory2 resulted in CS error
   ```

   Venus's async `vkAllocateMemory` returned success before the renderer's
   rejection, creating a phantom client handle. Patch
   `0007-vtest-checked-memory-allocation.patch` requires actual allocation
   replies for vtest. A real two-80-MiB-allocation regression
   (`deps/mesa/tests/vtest-quota.py`) failed before the patch with SIGABRT/ring
   fatal error (`quota-before.log`) and passed after it (`quota-after.log`):

   ```text
   quota allocations first=0 second=-1
   quota recovery=0 RESULT PASS
   ```

   This does **not** establish console quota exhaustion. Lavapipe has one
   host-visible memory type, charging optimal images as well as staging buffers
   to the host quota. RADV's import mask is 0x20; other types are device-only.
   The console exported four 8,306,688-byte staging blobs and had 43,106,304
   bytes of retained host charges at teardown. Its 128-MiB quota was not raised.

3. **Host-pointer mapping and lifetime review.** `vkr_shmem_import` aligns and
   sizes imports to at least 16 KiB, then passes that same live SHM mapping in
   `VkImportMemoryHostPointerInfoEXT`. RADV's `radv_ps5_buffer_from_ptr` checks
   address/size bounds and requires `radv_ps5_memory_grant_gpu` success, which
   calls `sceKernelMprotect` with CPU/GPU read/write bits before returning a BO.
   An exported blob duplicates the SHM fd; it does not replace the GPU mapping.
   These source paths do not support a hypothesis of intentionally unmapped
   host memory. They do not rule out a RADV GPU fault on more complex draws.

The corrected standard ASan/LSan/UBSan host suite passed after the core logging
and Venus changes (`host-final-suite.log`): 120 presents, malformed/stalled-peer
isolation, pipe waits, killed-client SHM baseline and two simultaneous clients.
Full-HD and corrected console evidence follow below when exercised.
