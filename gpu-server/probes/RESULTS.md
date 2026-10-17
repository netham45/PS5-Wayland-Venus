# GPU console probes C1 / C2 / C3 / C4

Status: **C1, all three C2a sizes, C2b, and corrected C3 completed.**
Both power-disabled A and PowerTick/getter-only B passed readback and clean
shutdown. **C4/G4 and L5 are accepted:** separate Venus and lavapipe clients each
rendered600 frames with600 named1920x1080 compositor commits and inspected
scanout PNGs. Normal Venus kill/relaunch passed against the final installed
app. The recovered-console bounded stop-trace run drained every gpud phase and
cleanly exited; the earlier timer teardown hang remains a historical,
unexplained failure, not a claimed source cure. The refreshed no-timeout,
PowerTick/getter-only app is installed and passed icon-handler launch,
automatic demo, normal Venus600, survival beyond60s and shipping title-close.
It is left **not running**; resident installer97 and keepalive86 remain.
**App flexible headroom at the measured baseline was only 428,867,584
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
Full-HD host acceptance also passed (`host-full-hd-suite.log`): 600 presents
at 1920x1080 for the single client and both concurrent clients, with killed-client
SHM counts `(0, 0) → (12, 22) → (0, 0)` (`host-full-hd/cleanup.log`) and zero
Vulkan fd exports. The constrained rerun after patch 0007 correctly returned
`vkCreateSwapchainKHR result=-1 extent=1920x1080 format=44 images=4`, after which
the upstream cube's `assert(!err)` aborted the client; the server logged no
missing object/decoder-fatal error and stopped cleanly (`host-ps5-checked/`).
That constrained cube run is an expected-OOM reproduction, **not a 600-frame
pass**. The dedicated allocation regression is the passing quota-recovery check.

## Corrected native thread identity and idle-power containment

Following the user-confirmed power cycle, Ps5Backend safely established the
private timer APIs' no-argument ABI through read-only native function-byte
inspection, then built shared `compositor/src/backend/ps5/power.{c,h}` support.
Its session keepalive PID 88 appears as `payload.elf`, **not** `ps5wl-*`.
Main initially missed that generic process name and requested another launch;
the second process 94 disappeared immediately while the existing port-9539
keepalive continued. It was not a second resident keepalive.

The no-GPU actual-Venus-thread-ID probe was rebuilt with the shared power helper,
then run as plain payload 95. Pre/post process checks and the upload/run receipt
are `thread-id-{before,started,after,run}.log`. Log observer exit **0**:

```text
[874970 ms] ps5wl-venus-thread-id: power thread-id-startup remaining_before=0 tick_rc=0 reset_rc=0 remaining_after=0
[874981 ms] ps5wl-venus-thread-id: vn_gettid main=101823 worker0=101826 worker1=101827 stable=1,1,1 RESULT PASS
```

These are three distinct, simultaneously live native thread IDs, stable across
10,000 queries per thread, from the corrected function body. This is not a run
of the unsafe old no-output-pointer syscall. The old ABI defect is established
by source/signature review; no deliberately unsafe reproduction was run.

The raw cached idle remaining value was **0**, before and after successful
PowerTick/Reset calls, in both the probe and keepalive samples. This records
API results, **not proof of a positive countdown reset or full idle/rest-mode
protection**. Bounded foreground reruns also log remaining seconds at startup.

An intervening actor launched old installer 96 and old non-gpud compositor 98
with demo 99 during that probe's log window. `intervening-compositor.log` identifies
the old build (no gpud or power-startup line). The title-checked shipping close
helper 100 closed that known project title; `old-app-close.log` records
SuspendApp, owned title kill and KillApp all returning zero and foreground
`0xffffffff`. A new unidentified plain payload 101 appeared in the post-check;
it was not signalled. Main was notified to coordinate the overlapping activity.

## Corrected C3 attempt — connectivity loss, mandatory safety stop

Main confirmed that the overlapping old-app launches above were the user
pressing the icon, then obtained exclusive console time. Shipping close helper
106 closed old title 103, with all close returns zero. Installer 96 and session
keepalive 88 were intentionally left resident. The next isolated diagnostic
launched driver 107, bounded compositor 108 and demo 109, then the corrected
Venus readback payload 110. Receipts and process snapshots are
`exclusive-close-*.log` and `c3-corrected-*.log`.

The compositor artifact was SHA256
`d61058fad6032c04bc010c54fa52e284e017f13df91f0b832f6149571fa70060`,
11,195,920 bytes. Exact terminal evidence:

```text
[1084431 ms] ps5wl-compositor: gpud start result=0; compositor renderer remains pixman
[1084431 ms] ps5wl-compositor: power startup remaining_before=0 tick_rc=0 reset_rc=0 remaining_after=0
[1084459 ms] ps5wl-compositor: bounded hardware validation shutdown in 60 seconds
[1084459 ms] ps5wl-compositor: [../../../compositor/src/ps5wl.c:1362] Running Wayland compositor on WAYLAND_DISPLAY=/system_tmp/wayland-0
[1084696 ms] ps5wl-compositor: [../../../compositor/src/ps5wl.c:792] client surface committed 1920x1080 count=1
[1085366 ms] ps5wl-compositor: flip-event seq=56 index=1 monotonic=1085.366834900
```

`c3-corrected-client.log`:

```text
ps5ctl: Cannot connect to 192.168.3.211:9591: timed out
```

The mandatory post-failure process check, `c3-corrected-failure-processes.log`:

```text
ps5ctl: [Errno 113] No route to host
```

All new console actions stopped immediately; no further ping or console retry
was issued by this agent. The already-running host diagnostic observers later
expired: body log status 124, driver log status 124, harness exit **1**. There
is no clean-shutdown/LoadExec, successful readback, or context-created line in
this attempt. Absence of retrieved client lines is not proof that client
`main` was never entered. Console power/screen state at loss was not observed
by this agent, so **crash, power-down and network loss remain unproven causes**.

### Ranked deltas versus the first passing C3

| Rank / candidate | Evidence and limits |
| --- | --- |
| 1. New in-app power calls, especially private Reset | The compositor now performs Get → PowerTick → Reset → Get at startup, adds a 10-second refresh timer, and ticks for dispatched input. Startup calls returned zero 935 ms before the last flip. The no-argument ABI was decoded, but the private Reset tail target's full semantics were not. A deferred or BigApp-context side effect is a hypothesis, not demonstrated. |
| 2. Client-triggered driver load / context creation | `vkr_renderer_init` only initializes flags, callbacks and the context list; it does **not** load RADV or create a Vulkan device at compositor startup. `vkr_context_create` does load the Vulkan library, and the `context created` log is emitted **after** that returns. Thus no completed-context line does not exclude an in-progress RADV `dlopen`. |
| 3. New Venus 0006 / 0007 | 0006 fixes the invalid FreeBSD thread-ID syscall and passed the actual native three-thread test. 0007 changes allocation replies after device/context initialization; its real host OOM/recovery regression passed. Neither is established as the connectivity-loss cause. The failed attempt never supplied enough client logging to locate its exact last call. |
| 4. GPU-server changes | Since the passing C3's context lifecycle logging, the only core-runtime addition was a quota-rejection log/prototype. Quotas, vtest protocol, imported-memory patch and startup logic were unchanged. That rejection branch is not used before a context allocates memory. |
| 5. External power or network event | The previous loss followed clean shutdown; this one stopped during startup/client launch. User-observed power state is needed to distinguish them. No captured GPU-fault, device-lost or wedged-fence message establishes a GPU hang. |

The 10-second compositor timer had **not** fired: first due time was about
1094431 ms, beyond the 1085366-ms last flip. The background keepalive's last
captured reset was 1079888 ms; its next was due at 1109888 ms, also beyond the
loss. Therefore the logs do not show literal simultaneous reset calls, although
both processes had used the same private API in different process contexts.
Keepalive had already returned zero over 17 samples spanning 450 seconds.
There were no logged key/button/axis events in the short failed window; motion
ticks do not have an independent log, so per-input calls are not ruled out.

The RADV discovery path did **not** change between C3 runs:
`/data/ps5wl/libvulkan.so.1`, not the C2 probe's separate
`/data/ps5wl/probe-radv.so.1`. No new installer was deployed by this agent before
the failed run; the user's old installer did not log writing a Vulkan asset.
Host `build/ps5/sysroot/lib/libvulkan.so.1` and `launcher/vulkan.so` both hash to
`acbee912f24388b455f918367715632cd3b2be2fa5a8938f94c8ac753cf17e08`.
The actual console file's hash was not measured and is not asserted.
The isolated launch route and 60-second containment matched the passing C3.

### Approved minimal power isolation

Main relayed user recovery after a fresh power cycle: only Payload Manager
and elfldr were present, with no installer or keepalive. To minimize costly
power-cycle failures, the coordinator replaced the longer reconstruction
proposal with current-source power isolation. The private
`sceShellCoreUtilResetAutoPowerDownTimer` call is removed permanently from
the compositor; only the separate plain keepalive payload may call it.

The exact initial 11,177,560-byte passing bounded compositor was overwritten
by subsequent power builds/repackaging; it was not hashed or copied at that
run. No previous binary was found in the retained compositor/launcher artifact
paths. Git `b8c8638` / `c052ffc` preserve the pre-power compositor sources;
checkpoint `0b53820` includes the power WIP. A source-equivalent baseline can be
reconstructed with the first C3's exact context-logging additions, but must
**not** be described as a byte-identical previously passing artifact.

Approved sequence, stopping at the first unexpected failure:

1. **A, no client:** current GPU-server and current Venus 0006/0007 sources,
   bounded 60-second compositor with **all compositor power calls compiled
   out**, no keepalive. Observe startup and clean exit.
2. **A, readback:** the exact same artifact plus corrected Venus readback.
3. **B, readback:** enable only compositor PowerTick plus remaining-time getter
   (never private Reset), and repeat the bounded readback run.
4. Run G4: cube, 600 presents, inspected PNG, kill/relaunch and resource cleanup.
5. Restart the separate plain keepalive only after the preceding GPU checks.
6. Run Lavapipe L5, then the final normal install/demo PNG/shipping-close check.

The build interface is `PS5WL_POWER=false` / Meson `-Dpower=false`; disabling
power omits its module and compiles out startup, timer and input hooks rather
than returning fake success from stubs. Step A therefore cannot sample the
remaining timer from inside the app. Startup getter logging returns in B.
Each action retains mandatory before/after process checks. Runs must be short
and sequential because idle auto-power-down remains a competing risk.
No budgets, app process creation, or deliberate GPU-fault probes are allowed.

Current failed compositor, current Venus payloads/ICD, native RADV and normal
installer were preserved in `bisect-preserved/` before further builds. The
approved power-isolation runs, final normal installation, automated PNG/close
check, corrected C4, and Lavapipe L5 are not yet claimed as passes here.

### A: all compositor power calls disabled — both runs passed

Artifact `bisect-preserved/no-power-compositor-60.elf`, SHA256
`91684592e81404f52e927b85eb40db8b032b2379bd13c32389ed200fcccbcf1e`,
11,177,672 bytes, retains the current GPU-server and the corrected Venus
0006/0007 client. Backend's offline symbol inspection found no PowerTick,
remaining-time getter, or private Reset import in this artifact. Native
startup has no power/timer sample in A by design.

`power-a-alone-*.log`: driver 85, compositor 86, demo 87; no Venus client,
installer or keepalive. After 60 seconds:

```text
[345217 ms] ps5wl-compositor: gpud: gpud stopped: Vulkan fd-export calls=0
[345217 ms] ps5wl-compositor: gpud stop result=0
[345311 ms] ps5wl-compositor: PS5 backend released VideoOut flips=3597
[345311 ms] ps5wl-compositor: compositor clean shutdown
[346457 ms] ps5wl-launch-diag: diagnostic result=0 child=86
```

`power-a-readback-*.log`: same artifact, driver 89, compositor 90, demo 91,
Venus readback 92:

```text
[358936 ms] ps5wl-compositor: gpud: context created id=1 name=payload.elf contexts=1
[358966 ms] ps5wl-vktest: GPU readback words=16384 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[358968 ms] ps5wl-vktest: RESULT PASS
[358969 ms] ps5wl-compositor: gpud: context destroyed id=1 resources_released=0 released_host_charge_bytes=0 released_device_charge_bytes=0 contexts=0
[418074 ms] ps5wl-compositor: gpud: gpud stopped: Vulkan fd-export calls=0
[418074 ms] ps5wl-compositor: gpud stop result=0
[418167 ms] ps5wl-compositor: PS5 backend released VideoOut flips=3598
[418167 ms] ps5wl-compositor: compositor clean shutdown
[419329 ms] ps5wl-launch-diag: diagnostic result=0 child=90
```

Both diagnostic harnesses exited **0**, and the readback log observer exited
**0**. Both post-run process snapshots contained no compositor, demo, client,
installer or keepalive; Payload Manager 83 and elfldr 81 remained responsive.
This passes current corrected C3 without compositor power calls. It does not
attribute the earlier loss specifically to private Reset: both the in-app
power bundle and background helper were absent in A. B is the next approved
single-variable test; no C4/L5/final-install acceptance is inferred.

### B: PowerTick and getter only — corrected C3 passed

`bisect-preserved/power-tick-only-compositor-60.elf`, SHA256
`06e3556a5444f07a723de363301cd52ca0cfba05dc517823b90c44dc87d1a915`,
11,179,640 bytes. Private Reset is absent; background keepalive stayed off.
`power-b-readback-*.log`: driver 94, compositor 95, demo 96, readback 97.

```text
[474073 ms] ps5wl-compositor: power startup remaining_before=0 tick_rc=0 remaining_after=0
[475109 ms] ps5wl-vktest: GPU readback words=16384 expected=1a2b3c4d first=1a2b3c4d mismatches=0
[475111 ms] ps5wl-vktest: RESULT PASS
[534121 ms] ps5wl-compositor: gpud: gpud stopped: Vulkan fd-export calls=0
[534121 ms] ps5wl-compositor: gpud stop result=0
[534217 ms] ps5wl-compositor: compositor clean shutdown
[535354 ms] ps5wl-launch-diag: diagnostic result=0 child=95
```

Startup plus six 10-second periodic samples all returned `tick_rc=0`,
`remaining_before=0`, `remaining_after=0`. Diagnostic and client observers
exited **0**; post-run `ps` remained responsive with no app/client/helper.
Neither this nor A proves a positive idle countdown, physical input activity,
or that the prior startup loss was specifically caused by private Reset.

### Corrected C4 — same first-present stall, then post-exit connectivity loss

The exact B artifact above was reused, changing only the client from readback
to the current corrected Venus cube. No keepalive/private Reset was running.
`c4-corrected-*.log`: driver 99, compositor 100, demo 101, cube 102.
The client extracted the 2,030,528-byte current Venus ICD, created its device
and swapchain, and stopped at:

```text
[560274 ms] ps5wl-vkcube: Vulkan prepare call completed
[560288 ms] ps5wl-vkcube: vkcube presented frame=1
```

After 42 seconds there was no frame 60/600 or cube wl_shm buffer commit.
The mandatory timeout process snapshot remained responsive: compositor
100 at 310.4 MiB, cube 102 at 19.1 MiB, demo 101 present. The server had
imported four 8,306,688-byte staging blobs, plus 262,144 and two 16,384-byte
blobs. No quota-rejection, device-lost, or wedged message was captured.
Demo scanout continued at 59.94 Hz; PowerTick/getter samples continued returning
zero. At the existing 60-second containment boundary:

```text
[619217 ms] ps5wl-compositor: bounded hardware validation timeout; releasing VideoOut
[619217 ms] ps5wl-compositor: gpud: vkr: destroying context 1 (vkcube) with a valid instance
[619217 ms] ps5wl-compositor: gpud: vkr: destroying device with valid objects
[619219 ms] ps5wl-compositor: gpud: context destroyed id=1 resources_released=10 released_host_charge_bytes=43106304 released_device_charge_bytes=0 contexts=0
[619219 ms] ps5wl-compositor: gpud: gpud stopped: Vulkan fd-export calls=0
[619219 ms] ps5wl-compositor: gpud stop result=0
[619318 ms] ps5wl-compositor: PS5 backend released VideoOut flips=3597
[619318 ms] ps5wl-compositor: compositor clean shutdown
[620504 ms] ps5wl-launch-diag: diagnostic result=0 child=100
```

Diagnostic harness exit **0**. Nevertheless the mandatory post-run process
snapshot in `c4-corrected-after.log` returned `Errno 113: No route to host`.
**All console actions stopped again.** The receipt-owned cube cleanup
conditional was not reached; no kill was issued. No PNG was taken because
the 600-present gate never arrived. No kill/relaunch or final installation
acceptance is claimed. The user subsequently power-cycled before the L5
run below.

Both observed C4 losses therefore followed forced cleanup of a stalled cube
context, while A/B readback contexts completed before bounded teardown and
their post-run process checks succeeded. This correlation motivates guarded
teardown and fence/WSI tracing; it does not by itself establish a GPU fault.
The upstream device teardown conditional is **enabled in this build**:
`gpu-server/meson.build` defines `ENABLE_RENDER_SERVER_WORKER_THREAD=1`, so
`ctx->on_worker_thread` is true and `vkr_device_destroy` should call
`vkDeviceWaitIdle`. An initial offline suspicion that the wait was skipped
was corrected after checking the actual build configuration. No two-second
idle-wait bound was found in the current device-destroy path.

### L5 first run — Vulkan instance failure, console stayed responsive

After Main relayed another fresh recovery, the bounded B compositor was
launched with no Venus client or keepalive. `l5-*.log`: driver 85, compositor
86, demo 87, separate CPU cube 88. The cube reported:

```text
[120523 ms] ps5wl-vkcube: client-init name=ps5wl-vkcube pid=88 display=/system_tmp/wayland-0 runtime=/system_tmp
[124539 ms] ps5wl-vkcube: lavapipe library extracted bytes=70117688 path=/data/ps5wl/libvulkan-lavapipe.so.1
[124638 ms] ps5wl-vkcube: wayland-connected fd=12
[124640 ms] ps5wl-vkcube: FAIL vkCreateInstance Failure: vkCreateInstance failed.
```

The client exited after its failure log window; no frame was presented and
no screenshot was taken. The upstream failure branch did not print its
`VkResult` or dump captured Mesa stderr; that limitation is being corrected
before another attempt. Both the failure process snapshot and the final
post-diagnostic `ps` were responsive. Diagnostic harness exit **0**, no app,
demo, cube or helper left; Payload Manager 83 and elfldr 81 remained.
Thus this run is **not an L5 pass** and did not reproduce the C4 console loss.

### Guarded Venus fence trace — CPU presentation remains stalled

The guarded teardown (`core/patches/0002-safe-teardown.patch`) and queue-fence
trace (`0003-sync-trace.patch`) were exercised with bounded compositor SHA256
`ae323b9f10695ca1cb72487c3b87e8353d2983524c445e8c58db9a6eb4cd550f`.
Evidence: `build/ps5/evidence/gpu/venus-trace-*.log`, compositor101, demo102,
cube103. The receipt-owned cube was killed at approximately eight seconds,
well before the compositor's 60-second exit. Nonzero queue-ring fences1–5
submitted, host waits returned0, and vtest retire/wait-pipe signaling completed.
The client stopped after:

```text
[1079983 ms] ps5wl-vkcube: vkcube presented frame=1
[1079987 ms] ps5wl-vkcube: Venus: sync-trace wsi-fence-wait-end result=0
[1079987 ms] ps5wl-vkcube: Venus: sync-trace wsi-flush-lock-end queue=880cafdc0
```

There was no `wsi-common-present-end`, frame60 or frame600. The observed GPU
fence retired; the unresolved interval is the CPU path after that wait, not
an observed unretired queue fence. Killed-client teardown's DeviceWaitIdle
returned0 in0ms and released10 resources/43,106,304 host-charge bytes.
The compositor logged `gpud stop result=0`, `Vulkan fd-export calls=0`,
`compositor clean shutdown`, and driver `diagnostic result=0 child=101`.
The final process snapshot was responsive with no remaining app/client/helper.
This is a contained diagnostic result, **not G4 presentation acceptance**.

Before this run, both `env/run gpu-server/tests/vtest-cube --sanitize
--sync-trace` and the `--full-hd` variant passed, including 600-frame
1920×1080 presentation, killed-client SHM cleanup and concurrent clients.
Logs: `trace-host-{120,600}-suite.log` in the same evidence directory.

### L5 corrected instance and first native fault capture

After the C11 symbol-rename fix, instance and Wayland-surface initialization
completed, but the standalone lavapipe cube faulted before device creation.
`build/ps5/evidence/gpu/l5-fault-client.log` records PID98:

```text
[847467 ms] ps5wl-vkcube: Vulkan instance and Wayland connection initialized
[847469 ms] ps5wl-vkcube: Vulkan Wayland surface created
Vulkan fatal signal=000000000000000b fault_address=0000000000000000 rip=000000082882448d rsp=00000007efabf980 trap=000000000000000c error=0000000000000004 native_mc_len=0000000000000480 driver_base=0000000000000000
```

The earlier `dladdr` lookup failed; driver_base0 in this record is not the
actual load bias. The logged `vkGetInstanceProcAddr` address0x204d20000 and
that symbol's zero ELF offset establish the load bias for this driver.
RIP0x82882448d is outside its executable text. No system-module attribution,
JIT, TLS or stack-size cause is established by this first capture. Zero
presents and no cube PNG: **L5 remains unaccepted**.

### Post-wait localization and full C11 audit (2026-10-16)

The relevant `compile_commands.json` entries all include
`-DC11_THREADS_RENAME`:101 C11/util/WSI source files each in the PS5 Venus and
RADV builds,104 in the PS5 lavapipe build. Offline objdump of the linked Venus
and RADV libraries found230 and693 C11 call/jump sites respectively, all
targeting `mesa_*`, not SDK `mtx_*`/`cnd_*`/`thrd_*`/`tss_*`/`call_once`.
The linked plain `mesa_mtx_init` returns0; `u_cnd_monotonic_timedwait` directly
calls pthread_cond_timedwait and maps native ETIMEDOUT60 to Mesa thrd_timedout1,
success to0, other errors to2. This rules out the previously demonstrated
lavapipe SDK-success4 collision in these observed Venus/RADV callsites.

Venus patches0009/0010 add postwait lock/reset, common CPU-fence, platform
present, Wayland-dispatch and shm-copy stages. Both driver targets and client
targets rebuilt successfully, then `env/run gpu-server/tests/vtest-cube
--sanitize --full-hd --sync-trace` passed600 frames plus killed-client baseline
and concurrent-client coverage (build output `artifact://1047`, suite logs
`build/gpud/logs/sanitize-full-hd-trace/`).

The single bounded postwait hypothesis run used the same guarded60-second
compositor, driver106/compositor107/demo108/cube109; the session keepalive105
was killed before it. Logs: `build/ps5/evidence/gpu/postwait-*.log`.

```text
[1952495 ms] ps5wl-vkcube: Venus: sync-trace wsi-relock-chains-end
[1952496 ms] ps5wl-vkcube: Venus: sync-trace wsi-relock-queue-end
[1952498 ms] ps5wl-vkcube: Venus: sync-trace wsi-fence-reset-end result=0
[1952499 ms] ps5wl-vkcube: Venus: sync-trace wsi-cpu-fence-wait-end
[1952500 ms] ps5wl-vkcube: Venus: sync-trace wsi-platform-present-begin
[1952501 ms] ps5wl-vkcube: Venus: sync-trace wsi-wl-present-dispatch-begin
```

There was no dispatch-end or copy-begin: **the first stall is inside the
presentation-ID queue dispatcher, before the wl_shm memcpy**. Both Venus
chain/queue reacquisitions, reset, and common CPU staging fence wait completed.
Cube109 was killed at8.000s and disappeared; compositor107 survived, drained,
and completed its bounded diagnostic with result0. The final `postwait-after`
process snapshot was responsive with only manager83/elfldr81 among project
services. Still only one asynchronous client present; no cube PNG or G4 pass.

### L5 extended capture — system module and LLVM caller identified

`clients/vkcube/runtime.c` now publishes saved registers using a shared
lock-free ready atomic (release store/acquire load), not fences without a
shared synchronization object. The diagnostic client and LLVM/Mesa retain
frame pointers. Embedded driver SHA256:
`0ca4345306c1ca442f77555cc43e605bdc9babfcc79a427632bab20bd63043b1`.
The extended bounded run used compositor112/demo113/cube114. Logs:
`build/ps5/evidence/gpu/l5-extended-{client,compositor,postclient,after}.log`.

```text
Vulkan module base=204630000 size=0x3b4c000 path=/data/ps5wl/libvulkan-lavapipe.so.1
Vulkan module base=823800000 size=0x1c0000 path=/system/common/lib/libSceLibcInternal.sprx
Vulkan fatal signal=000000000000000b fault_address=0000000000000000 rip=000000082380448d rsp=00000007efbd3980 trap=000000000000000c error=0000000000000004 native_mc_len=0000000000000480 driver_base=0000000204630000
Vulkan RIP bytes 000000082380448d c5f96f104883c010c5e974c1c4e27917
Vulkan extended fault capture complete
```

RIP is **libSceLibcInternal+0x448d**, a vector load from RAX0 in the strlen
implementation. The collector recorded all18 registers,64 code bytes,
256 stack bytes and15 frame records. Offline symbolization is retained in
`l5-extended-symbolized.log`: strlen(NULL) ←
`std::__1::string::__assign_external(const char *)` (ICD+0x1f88d5) ←
`llvm::sys::DynamicLibrary::getPermanentLibrary` (+0x1f4535d) ←
`llvm::EngineBuilder::create` (+0xf50cd3) ←
`lp_build_create_jit_compiler_for_module` ← `gallivm_compile_module` ←
`compile_function` ← `compile_jit_size_function` ←
`llvmpipe_init_sampler_matrix` ← `llvmpipe_create_context` ←
`lvp_queue_init` ← `lvp_CreateDevice`.

LLVM's process-symbol-library dlopen(NULL) calls the ICD's SDK-local dlopen
wrapper under `-Bsymbolic`; its weak private `__dlopen` hook is unresolved.
That wrapper returns NULL, and its similarly unbound dlerror wrapper returns
NULL, which LLVM assigns into std::string. This is a loader-binding defect,
not observed JIT execution, stack exhaustion or a demonstrated TLS failure.
Lavapipe's narrow public-loader binding fix is tracked in its driver patches;
no null-string suppression was applied. The faulting client exited itself;
the compositor survived and ended with diagnostic result0; final ps remained
responsive. Still zero presents/no L5 PNG at this diagnostic.

### Present-ID dispatcher and raw ppoll source defect

Patch0011 separates the initial present-ID mutex, condition wait and Wayland
loader call. Both Venus targets/clients rebuilt and the full-HD sanitized
600-frame/concurrent/killed-client suite passed again (`artifact://1059`).
Single bounded run119 stopped at:

```text
[2334083 ms] ps5wl-vkcube: Venus: sync-trace wsi-wl-id-lock-end
[2334083 ms] ps5wl-vkcube: Venus: sync-trace wsi-wl-loader-dispatch-begin
```

No condition wait occurred. The loader was called with an already-expired
absolute deadline, hence zero relative timeout. Cube119 was killed at8s,
compositor117 survived and exited with diagnostic result0; process checks
were responsive (`dispatch-*.log`).

Offline objdump shows ppoll in the PS5 Venus ICD is the SDK's raw syscall545
stub, with no timeout conversion or carry/error normalization. An independent
plain no-GPU payload126 proved its zero-timeout behavior is invalid:

```text
[2569979 ms] ps5wl-dispatch-probe: ppoll zero-empty BEGIN pid=126
[2569979 ms] ps5wl-dispatch-probe: ppoll zero-empty END result=3 errno=3 revents=0
```

Evidence: `ppoll-raw-{client,run,before,after}.log`, reproduction source
`ps5wl-dispatch-probe.c` under the same evidence directory. A single empty
pipe can never have three ready descriptors. **[INFERENCE]** Wayland interprets
this positive native error as readiness, receives no events, prepares again
and repeats forever instead of timing out. The earlier `dispatch-probe`
foreground attempt exited too quickly on this unexpected positive return for
its logs to be pulled; the corrected early-error log linger enabled the plain
reproduction above. It is not counted as a passing test.

The PS5 backend's BUFFER path submits a flip even with empty damage and sends
`presented=true` with the saved commit sequence and flip timestamp on completion
(`backend.c:73-117,141-175`). Idle re-flips intentionally have no new commit
presentation; no-BUFFER state changes return without a present event. The
observed first Venus stall precedes the shm copy, attach/commit and any
presentation-feedback request, so missing later feedback cannot explain this
specific zero-timeout dispatch failure.

The native system-module `pselect` export was probed separately without a
foreground app. Payload127 observed zero for an empty pipe with zero timeout,
one for a readable pipe, and zero after a measured 30,121,332 ns for a 30 ms
timeout. A temporarily unblocked pending SIGUSR1 interrupted the wait with
`-1/EINTR`; the original blocked mask was restored. Immediate handler count
was zero with a process-directed signal and a concurrent logger thread, so
that capture alone does not prove delivery to the waiting thread. The
central compatibility regression must use a thread-directed signal to make
that assertion. Native exports included pselect/poll/select; no
sceKernelPselect/Ppoll/Poll export was found. Evidence:
`pselect-probe-{build,run,client,before,after}.log`. The console remained
responsive after the probe; no C4/L5 render acceptance follows from it.

A second native probe129 removed that ambiguity by sending SIGUSR1 with
`pthread_kill(pthread_self(), SIGUSR1)`: send returned0, native pselect returned
`-1/EINTR`, the handler ran once, and the original blocked signal mask was
restored. Zero-empty, finite-empty and ready cases again returned0/0/1;
finite wait was30,130,714ns for requested30ms. Evidence:
`pselect-thread-{client,before,run,after}.log`. This proves the native primitive,
not the still-pending central ppoll wrapper. Before/after process checks were
responsive; only the explicitly started idle keepalive128 remained.

The native `sceKernelDlsym` API did **not** implement the SDK CRT lookup
contract: probe130 returned `0x80020003` and null for poll with handles1 and
0x2001. Reading [SDKv0.44's KERNEL_DLSYM macro](https://github.com/ps5-payload-dev/sdk/blob/v0.44/crt/kernel.h) identified its privileged
`kernel_dynlib_dlsym(-1, handle, name)` helper instead. Probe131 proved
`kernel_dynlib_dlsym(-1,1,"poll")` returned the identical native poll address
as the payload root's public dlsym; handle0x2001 returned null.
`kernel_dynlib_handle(-1,"libkernel_sys.sprx",&handle)` independently returned
handle1. Evidence: `native-poll-handle-*.log` and
`privileged-poll-handle-*.log`; all before/after checks remained responsive.
Those helpers work in a payload with its SDK CRT; a minimal `-shared
-Bsymbolic --no-undefined` link then failed on undefined kernel_dynlib_dlsym
because shared objects omit that CRT. They are therefore not the central
ICD resolver. No private CRT hooks are exported.

Probe132 then proved a public, handle-specific resolver with a real payload
poll interposer. `dlsym(RTLD_DEFAULT,"poll")` returned that interposer;
`dlopen("/system/common/lib/libkernel_sys.sprx",RTLD_NOW|RTLD_LOCAL)` followed
by `dlsym(handle,"poll")` returned the identical native function as the
independent privileged root-payload lookup. Calling the handle-resolved
function returned0 for an empty pipe and1/POLLIN for a readable pipe, errno0
in both cases. Evidence: `native-module-poll-{client,run,before,after}.log`
and `ps5wl-native-poll-probe.c`. Console checks remained responsive. The
central shared-link public-loader binding exception must use this
explicit-module lookup, not SDK-unsupported RTLD_NEXT or a global poll
lookup that re-enters epoll-shim.

### Central ppoll cutover — real shared-object console regression

The compatibility proxy now wraps ppoll and applies
`cross/public-loader.dyn` to PS5 shared links. It preserves `-Bsymbolic`
except for the five public dl* functions, which must bind to the payload
loader rather than SDK-local wrappers with unbound private hooks. Both Mesa
and compositor build fingerprints include that binding-list content and the
injected archive. The unmasked WSI path calls cached, explicitly resolved
native poll without allocation, copying or a helper thread.

Payload133 loaded a real `-Bsymbolic --no-undefined` DSO built against the
canonical archive. Its root poll interposer deliberately returned ELOOP on
the calling thread during the regression; logger transport used another
thread. The DSO returned test result0 after zero/finite empty timeouts,
readiness, duplicate descriptors, POLLNVAL, events0/POLLHUP, thread-directed
pending-signal interruption with handler1 and restored mask, a masked
blocking writer-readiness wait and a masked finite timeout. Thus accidental
root poll re-entry would not pass. The diagnostic root's final DSO dlclose
returned-1; the payload then exited normally. Evidence:
`ppoll-dso-{client,run,before,after,upload}.log`,
`ppoll-console-dso.c` and `ps5wl-native-poll-probe.c`. Before/after console
checks remained responsive. This is compatibility acceptance, not a Vulkan
render acceptance.

The required host preflight then available before the next Venus run passed:
`env/run gpu-server/tests/vtest-cube --sanitize --full-hd --sync-trace`,
600 frames, concurrent clients and killed-client SHM baseline in102.24s.
Evidence: `central-fix-host-preflight.log` and
`build/gpud/logs/sanitize-full-hd-trace/`. Compositor commit counters now also
include xdg app ID and title; host vkcube is explicitly `title=vkcube`.

### Fresh compositor startup regression — before a Vulkan client connects

Both PS5 ICDs and the corrected 60s compositor rebuilt successfully
(`artifact://1133`,124.67s); the new compiler fingerprints forced the intended
clean Mesa migration. Preserved compositor:
`central-fixed-compositor-60.elf`, SHA256
`fda71f5eabea9525c33e7d447b365275e190a1d5faf80fa546473839d9788af0`.
Venus ICD SHA256:
`189c9dbd7c5bfa6ab74db67d37155319f8f18227e9b41739c8dcfd1da9a89824`.

Foreground135 then faulted approximately465ms after Wayland readiness and
demo autostart, before cube137 was even launched. Cube137 never connected
or created a GPU context and was receipt-killed at8s. This is **not** a
post-fix C4 present attempt: zero frames and no PNG. The diagnostic launcher
reported child disappearance/result0, which therefore does **not** prove
normal compositor exit; the body log explicitly contains FATAL SIGSEGV.
Evidence: `g4-fixed-*.log`.

The safety handler was extended with bounded signal-context RIP/RSP/RBP and
RAX/RDI/RSI formatting. Subsequent 15s diagnostic143 recorded libc base
`0x8011d0000` and RIP `0x8011d6505`, hence
**libSceLibcInternal+0x6505**, fault address `0x840000000`.
RDI=`0x8806b5a60`, RSI=RAX=0. Evidence:
`startup-fault-{rip,map}-*.log`. Ordinary payload147's privileged, page-sized
code read proved the instruction is `49 8b 06` (`mov (%r14),%rax`) following
a global-pointer load in the native function prologue, not a demonstrated
memset buffer copy. Native malloc/calloc/free exports are+0x5f30/0x5f50/0x5f40;
memset+0x15170 and strlen+0x4480. Evidence:
`startup-libc-copy-*.log`. The earlier direct userspace code-read probe closed
its log connection before capture; the protected-copy probe supersedes it.

All before/after process checks remained responsive. No foreground app,
demo or Vulkan client remained after cleanup; idle keepalive138 was the only
owned payload. The actual caller of the faulting libc prologue remains
unproven. SDK source and the actual compositor ELF ruled out the proposed
resident-kernel fini execution path: sprx_fini/ref_fini are no-ops, and the
public loader wrappers bind to their initialized payload CRT implementations.

### Coordinator-authorized module lifetime fix — solo startup gate passed

The coordinator directed the native poll cache to retain its explicit
kernel_sys loader handle for the lifetime of the cached function pointer,
without waiting for proof of the exact libc caller. The resolver no longer
calls dlclose. Compatibility archive SHA256 is now
`8f883c9628b356c3251e8502f797b49d29d6d6bf8d9528ecebdc9aa360168d57`.
Ps5Deps reran its targeted host semantics, archive bootstrap and minimal
Bsymbolic shared link successfully.

Rebuilt foreground149 then passed the required complete60s solo run
(no Vulkan payload; existing native demo autostart retained): approximately
3480 named demo commits, no FATAL, bounded clean teardown and responsive
after-run process check. Completed-scanout `retain-module-solo.png` was
pulled on9531 and inspected: readable interactive demo text, checkerboard,
1920x1080 frame and pointer, no observed corruption. Evidence:
`retain-module-solo-*.log`; accepted bounded artifact
`retained-module-compositor-60.elf` SHA256
`4f95dcfc81ddcb493f99e7b09ef1b53b68a9e670cafb24b18922fba378a8bcef`.
This proves that lifetime change removed the reproduced startup regression.
It does not by itself prove which native libc call faulted, G4 rendering or
the final refreshed installation. Venus/RADV/lavapipe are being relinked
against this final archive before further Vulkan console use.

### Refused stale GPU endpoints — reproduced and fixed centrally

The retained-module Venus attempt (`g4-retained-*.log`, foreground153,
client155) did not reach WSI: gpud startup returned `-48` (EADDRINUSE),
then the client enumerated zero devices. Payload157 established that both
`/system_tmp/ps5wl-gpu-0` and `/system_tmp/ps5wl-gpu-lkl` were socket files
whose correctly typed connect returned ECONNREFUSED61; it removed both.
Evidence: `stale-gpu-cleanup-{before,run,client,after}.log`.

The same source failure was reproduced host-first using the existing sanitized
`gpud-host`: pre-created bound-and-closed vtest/render sockets caused
`gpud start: -98` and exit1. `core/unix_listener.c` now handles only
EADDRINUSE followed by a matching-transport ECONNREFUSED: it verifies a socket
entry and unchanged inode/device, unlinks, logs, and retries bind once.
Live listeners, full backlogs, other connect errors and regular files are not
removed. Both vtest and render startup use this helper. The host suite now
preseeds both stale production endpoints and requires their recovery log lines;
the native regression covers stale/live stream and seqpacket sockets plus a
regular-file collision.

The rebuilt sanitized host suite passed after stale recovery:
`env/run gpu-server/tests/vtest-cube --sanitize --full-hd --sync-trace`
reported600 presents, concurrent clients and killed-client SHM baseline,
including the new stream/seqpacket/native listener regression and both real
startup recovery logs. Combined server/compositor build command bg350 exited0
in105.18s. Rebuilt PS5 gpud, bounded60s and normal0s power-enabled compositor
links also succeeded.

### Post-stale-recovery console gates — two new source failures

The first stale-safe run (foreground159/client161; historical agent transcript,
before the later passing run reused the `g4-stale-safe-*` artifact names)
selected the Venus RADV device, then asserted in the initial surface-format
query before any presents. It exited before the8s cleanup; no kill was sent to
another PID. The compositor survived all60s, drained gpud with result0 and
cleanly exited through the diagnostic driver. Final process snapshot contains
only keepalive138 among owned payloads.

After adding exact query-result diagnostics and optional Wayland client tracing,
the host full-HD sanitized suite passed again (bg351,98.71s, artifact://1186).
`g4-formats-trace-*.log`, foreground169/client171, reports
`vkGetPhysicalDeviceSurfaceFormatsKHR result=-1000000000` (SURFACE_LOST),
`Wayland display error=0`. The root client registry received wl_shm and xdg
globals. The compositor again survived its full timer and exited cleanly.
No cube frames/PNG are claimed. Venus initialization tracing is used to locate
the failing WSI display-initialization path before further cure attempts.

`l5-final-loader-*.log`: foreground164/client166 used the requested driver
SHA256 `6f8c410d989f7c54df3c3feebfe33dc6bdf01cdea2e9aef380035fe30332707a`.
It passed the original loader failure, then faulted with RIP0 during MCJIT
RX finalization. Saved stack top returns to driver+0xf4b2; disassembly proves
this is `mprotect` calling its unbound weak `kernel_mprotect` GOT slot.
`l5-final-loader-symbolized.log` records the chain through LLVM mapped-memory
protection, SectionMemoryManager, MCJIT, llvmpipe sampler JIT and lvp_CreateDevice.
The source fix adds SDK public `mmap`/`mprotect` to the canonical DSO binding
list, using the payload's initialized wrappers rather than exporting private
hooks or bypassing executable-page protection. Both public functions were
observed exported in the actual client ELF. The rebuilt driver necessarily
supersedes the requested old hash; its acceptance remains a separate gate.
Client166 exited after its full extended fault capture; the compositor again
completed60s and clean teardown. All before/after console checks remained
responsive; keepalive138 was left running.

### Actual ICD symbol visibility — WSI root cause and rendering acceptance

`g4-initialization-trace-*.log` (foreground174/client176) showed the Venus WSI
display initialization finding neither wl_shm nor dmabuf, with no Wayland
display error and errno78/ENOSYS. Actual ICD disassembly proved `resolve_poll`
called SDK-local `dlopen`/`dlsym` directly despite the canonical dynamic-list.
Mesa's PS5 version script had `local: *`; the dynamic-list cannot override
that localization. `deps/mesa/patches/0005-ps5-public-runtime-bindings.patch`
therefore makes the seven canonical public SDK runtime functions global in
the actual ICD version script; no private hooks are exported. The optional
WSI trace is `deps/mesa/venus-patches/0012-wsi-initialization-trace.patch`.

`deps/mesa/tests/public-runtime-bindings.py` audits actual final relocations
and disassembly rather than a different link configuration. The rebuilt Venus
and RADV ICD audits both passed in bg358 (exit0,112.60s, artifact://1212).
The sanitized host full-HD traced cube suite passed after WSI tracing too
(bg353,139.23s, artifact://1196).

The later `g4-stale-safe-*` artifacts are the **passing rendering run**:

- Foreground179, automatic native demo180; separate Venus clients181 then182.
- First client log: `[6824035 ms] ... vkcube presented frame=600`.
- Compositor log: `[6824059 ms] ... client surface committed 1920x1080
  count=600 app_id=(unset) title=vkcube`.
- `g4-stale-safe-cube.png` and `g4-stale-safe-relaunch.png` were both opened
  and inspected: readable textured LunarG cube on gray background,1920x1080,
  at different orientations. These are compositor9531 completed-scanout PNGs,
  not GPU staging-buffer captures.
- Client181 was receipt-killed after600; compositor179 survived. Client182
  relaunched and reached120 presents before its receipt-owned kill.
- Context1 teardown logged DeviceWaitIdle0 and released11 resources,
  51,494,912 host-charge bytes; context2 logged DeviceWaitIdle0 and released10
  resources,43,106,304 host-charge bytes. Both reached contexts0.
- Keepalive138 was not signaled.

SHA256 at that run:

```text
849f436d47312e016e29fd6b803889e544c4cd6b8888eb228019018c2747832d  build/ps5/evidence/gpu/stale-safe-compositor-60.elf
98ecd9eb4f565565b423667bf5b2d2e5d6323403337a76426a9359ba8ca9680d  build/ps5/venus/lib/libvulkan-venus.so.1
abc955b8cb5044961e2a41469f28f830785a4d31d6bae1807be8fb73e2108111  build/ps5/vkcube-venus/ps5wl-vkcube-venus.elf
```

### Rendering passed; timer teardown blocked — no final deployment claim

After both cube contexts were destroyed, native-demo rendering continued
until `[6867380 ms] bounded hardware validation timeout; releasing VideoOut`.
The watchdog then reported a stalled heartbeat; no `gpud stop result` or clean
shutdown marker appeared. Diagnostic driver178 reported child179 still present
after75s; the bounded harness exited1. `g4-stale-safe-after.log` still listed
foreground179 and demo180. This is not a clean bounded shutdown acceptance.

The shipping `launcher/ps5wl-close.elf` was uploaded once as receipt-owned183.
Its9585 log listener never opened; `g4-versioned-close-after.log` still lists
183,179,180. Payload Manager and process inspection remained responsive.
`g4-versioned-manager.log` preserves its upload/loader receipt. No standalone
BigApp SIGKILL, budget change, or app-process fork was used. Console cleanup
and further app runs are held pending a safe title-close path; the refreshed
installer has not been deployed. Stop-phase localization logs were added at
the daemon source boundaries for the next permitted diagnostic run.

### Offline completion while console title-close is blocked

The complete LLVM/X86 release prerequisite, PS5 lavapipe ICD and fault-traced
separate lavapipe cube rebuilt successfully (bg355,647.18s,
artifact://1206). Its actual final-ICD runtime-binding audit passed:
`env/run python3 deps/mesa/tests/public-runtime-bindings.py lavapipe`.
All three actual drivers now pass this audit. Lavapipe600 presents and its
scanout PNG remain unrun because the known foreground title is still blocked.

The stop-phase diagnostic changes passed the complete sanitized full-HD traced
host600/concurrency/killed-SHM/stale-listener suite; PS5 gpud, preserved bounded
compositor, normal no-timeout power-enabled compositor, and installer all
rebuilt successfully (bg359,119.05s, artifact://1223). Existing `_GNU_SOURCE`
redefinition, compositor unused-parameter, and launcher sign-compare warnings
remain in the build output. No deployed bytes are inferred from these offline
builds.

Current staged artifacts (SHA256):

```text
f613858b6ff6d82f09d57249f13e6f01c009925e0052596438ca6438c10e5097  build/mesa/ps5-lavapipe/sysroot/lib/libvulkan_lvp.so
ff9d328f1acc334729f6229268ad110f25fcbd3a4be8da38c4702bc81479dea5  build/ps5/vkcube-lavapipe/ps5wl-vkcube-lavapipe.elf
57831d85e8c6c41af9f1d80334cb5bfdf4316987923b572670138d721e130782  build/ps5/evidence/gpu/stop-trace-compositor-60.elf
3f68176f44cb854bae4fb83a18e600bf243c418168d96cafe93d885e3e408eaf  build/ps5/compositor/ps5wl
34a58ac59784951e55d4cbbd042e0b47a01482e3930d9420b8afde195489c57d  build/ps5/clients/ps5wl-demo.elf
421c931feb68733def2ccf38930c88cbeafa8367143a4300fe25fbf2943bbca8  build/ps5/sysroot/lib/libvulkan.so.1
51496562456b6bfa734e3ac9ec95b77601d1a1e6584b7748727e8098e943cef6  launcher/ps5wl-install.elf
5ec69c3e3fa95df96ccfc886384eaf69b104eb811b57033846147ffd384ad4f9  launcher/ps5wl-close.elf
d318ff16abbe933f7bbb60aac6066d439ece8aa2dc697ac8e3c4535f86f06f89  build/ps5/evidence/gpu/g4-stale-safe-cube.png
f841023426fc01fa708f4dcd8ea49cb2f799caf796ef5592b0852616bc35a482  build/ps5/evidence/gpu/g4-stale-safe-relaunch.png
```

`local://need-user.md` now explicitly holds all icon/payload actions, names the
safe physical known-title close prerequisite, and separates the later visual
check instructions from the final installation that is not yet deployed.
Checkpoint `hub jobs`/`hub ps` are unavailable (exit127); both owned builds have
settled. No `.img` files were found under the scoped PS5 evidence/probe paths,
so no probe disk image was deleted.

### Recovered console — bounded stop, L5, and final installed app accepted

Main relayed the user's power-cycle recovery and explicitly resumed the run
ladder. `recovered-stop-trace-before.log` confirmed old179/180/183 and the old
keepalive were gone. The plain keepalive was relaunched first as receipt-owned86
(`recovered-keepalive-*.log`, port9539). It was not killed afterward.

The exact stop-trace compositor SHA57831d85… ran once with Venus:
`recovered-stop-trace-*`, driver87/compositor88/demo89/client90. Client90
reached120 presents, was screenshot-captured and receipt-killed early. Context1
released10 resources and43,106,304 host-charge bytes. At222204ms the60s timer
logged all five stop-phase boundaries, `gpud stop result=0`, then
`compositor clean shutdown` at222306ms. Driver87 confirmed child88 disappeared;
harness exit0, no owned app/client/demo afterward. The earlier hang did **not**
reproduce; no speculative workaround or claimed root-cause cure was added.

L5 used the final driver f613858b… and cube ff9d328f…:
`l5-final-public-runtime-*`, driver92/compositor93/demo94/client95.
At267037ms the client logged `vkcube presented frame=600` and compositor logged
`client surface committed 1920x1080 count=600 app_id=(unset) title=vkcube`.
`l5-final-public-runtime-cube.png` was opened and inspected: correctly textured
LunarG cube,1920x1080. Receipt-owned client cleanup preceded timer expiry.
At305376ms all gpud stop phases completed, result0, clean shutdown305472ms;
driver confirmed child93 disappeared and harness exited0. This is actual PS5
LLVM JIT/lavapipe rendering acceptance, not just a link or host-only test.

The normal Venus client was rebuilt **without** `--sync-trace` (bg362,
exit0,3.04s, artifact://1232). Installer97 then deployed the existing normal0s
power-enabled compositor, native RADV and automatic demo:

```text
[338769 ms] installed demo bytes=1179336
[338769 ms] installed Vulkan driver bytes=21202376 path=/data/ps5wl/libvulkan.so.1
[338845 ms] register title=PSWL00001 method=AppInstallTitleDir rc=0x0 bytes=11220800
[338845 ms] install result=0
[338845 ms] ready http=127.0.0.1:9581 logs=9580
```

`final-gpu-*` records the actual final installation preflight:

- Normal loopback icon handler returned HTTP200 through trigger98.
- Installed foreground99 started gpud with result0; installed demo100
  automatically connected. `final-gpu-demo.png` was opened and inspected:
  readable interactive-demo heading, checkerboard, pointer/text instructions.
- Normal separate Venus101 logged frame600 at376831ms; the compositor logged
  named vkcube commit600 at376847ms. `final-gpu-venus.png` was inspected and
  shows the correct textured cube.
- Client101 was receipt-killed; compositor survived. Separately launched102
  reached120 presents, `final-gpu-venus-relaunch.png` was inspected, and102
  was receipt-killed. Both GPU contexts logged idle0 and full destruction.
- The installed compositor/demo remained running beyond66s from trigger,
  with `final-gpu-demo-past-sixty.png` showing demo frame3462 at436490ms.
  This PNG was inspected; there was no bounded60s shutdown in the normal app.
- Shipping close103 verified exact titlePSWL00002/app0x4019, SuspendApp0,
  owned99 SIGKILL0, KillApp0, then running app0xffffffff and completion0.
  The title-scoped shipping sequence, not standalone BigApp SIGKILL, was used.
- `final-gpu-processes.log` after helper expiry lists resident installer97
  and keepalive86, but no compositor, demo, Vulkan client or test/close helper.
  The refreshed installation is left **not running**.

All screenshots above are actual completed-scanout9531 captures at1920x1080.
Automated loopback launching does not prove human home-screen icon selection
or TV visibility; exact final visual-check instructions are in
`local://need-user.md`. Physical input was previously confirmed by the user.
The earlier ownership HOLD is released by this final installed-not-running
handoff; no further automated console run is pending.

Final additional SHA256 (the compositor/RADV/demo/close hashes in the staged
table above are unchanged):

```text
b6853ad0cb1b342bf30cd1912237f35537107bbc2396ce8e164848e8c5cc6366  build/ps5/vkcube-venus/ps5wl-vkcube-venus.elf
31f8e77d8cd3e5352ffd4c12a0d59f3051f9bfb466cedae669bfeb67828c5076  launcher/ps5wl-install.elf
7abf6f2a395b3c53cc9bab26dc885ec31a3ee35c51f0236429041c66c6bd61cf  build/ps5/evidence/gpu/l5-final-public-runtime-cube.png
746ca30c90a91e8e1b6872cf917d11957e3210e1a1b0f45847ad02eb13f1d5e6  build/ps5/evidence/gpu/final-gpu-demo.png
195eed039bbe04b5db55e8fabc059846bc0cf4f69259d66584ef4706e56a5e51  build/ps5/evidence/gpu/final-gpu-venus.png
1a6acfebda9537afc117e4779ebda6444e60507136eee2e4b2d8e884d8dbb5ab  build/ps5/evidence/gpu/final-gpu-venus-relaunch.png
ef6a4f44fe36dc04cd2873830736a13d3cb7086c44cd57a81a87c8c7a76b5f1c  build/ps5/evidence/gpu/final-gpu-demo-past-sixty.png
```
