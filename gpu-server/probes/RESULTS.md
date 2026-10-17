# GPU console probes C1 / C2

Status: built; console measurements not yet authorized. No hardware pass is
claimed by this document. Firmware and process context must come from each run's
captured log, not from earlier experiments.

## Build evidence

Command (2026-10-16):

```sh
env/run python3 gpu-server/probes/build.py
```

Observed exit code: **0**. All seven links completed using
`cross/bin/prospero-clang`, which links the PS5 fd/flag compatibility shim.
There were no compiler warnings in the captured build output.

Artifacts under `build/ps5/gpu-probes/`:

- `ps5wl-gpu-c1-plain.elf`: log port 9592; native 30 s alarm, `_exit(124)` on expiry.
- `ps5wl-gpu-c1-app.elf`: log port 9530; 30 s watchdog; native shell exit.
- `ps5wl-gpu-c2a-16384.elf`: log port 9593.
- `ps5wl-gpu-c2a-8388608.elf`: log port 9593.
- `ps5wl-gpu-c2a-67108864.elf`: log port 9593.
- `ps5wl-gpu-c2-verify.elf`: log port 9594.
- `ps5wl-gpu-c2b.elf`: log port 9530; 30 s watchdog; native shell exit.

C1 artifact SHA256 (watchdog build):

```text
532379d3b145c3e45f4861743c3d43bbcf55d87f49068496966dff169273b72b  ps5wl-gpu-c1-plain.elf
303b7d9d509ba740ba60cf68a9188e207a57f06ad7eb0f93924fe7fdd9eeabcb  ps5wl-gpu-c1-app.elf
```

The app probes do not use VideoOut, process creation, or budget-changing APIs.
Their normal and watchdog exits call `sceSystemServiceLoadExec("exit", NULL)`;
there is no app `_exit`/SIGKILL cleanup path. A separate close helper must remain
ready. Plain C2 retains potentially GPU-owned memory on a failed submit/fence,
rather than freeing live pages or calling an unbounded `vkDeviceWaitIdle`.

## Run authorization and containment

Ps5Backend owns the console and grants **one exact run at a time** after a fresh
process check. Do not run any artifact merely because it built. Stop the ladder
at the first unexpected result. No console run has yet been performed by this
assignment.

The first request is C1 plain, using `tools/ps5ctl run` and a bounded port-9592
log pull. Backend initially withheld the grant because normal compositor PID 91
and demo PID 92 were foreground alongside resident installer PID 85. Main then
authorized Backend to close the normal app using helper PID 93. Backend reported
all close returns zero, no running BigApp (`ffffffff`), and a fresh process list
with only installer 85 among project payloads. Backend additionally required a
native plain-process watchdog, now implemented and rebuilt; the exact run grant
is pending.

Main approved an independent background hbldr diagnostic route instead of the
original `launcher/test.sh --elf`, because that original mode replaces the
installed target and conflicts with installer 85. Launcher owns the independent
route. Neither diagnostic may replace the stable installed target, its manifest,
or installer 85. This authorization changes the harness only, not the required
foreground app context, deadline, or LoadExec exit.

## Per-item verdicts

| Experiment | Item | Verdict | Return code / excerpt |
| --- | --- | --- | --- |
| C1 plain | 16 KiB SHM_ANON / MAP_SHARED; GPU-RW mprotect | NOT RUN — awaiting Backend grant | No hardware log |
| C1 app | 16 KiB SHM_ANON / MAP_SHARED; GPU-RW mprotect | NOT RUN — after plain C1 | No hardware log |
| C1 | Flexible availability before / mapped-and-touched / released, 64 MiB | NOT RUN | No hardware log |
| C1 | Flexible availability, 256 MiB | NOT RUN | No hardware log |
| C1 | Flexible availability, 1024 MiB | NOT RUN | No hardware log |
| C1 app | Independent type-12 full-pool direct allocations, 2 MiB steps through 256 MiB; each released | NOT RUN | No hardware log |
| C1 app | Independent SDL-style MainDirectMemory type-3 allocations, 2 MiB steps through 256 MiB; each released | NOT RUN | No hardware log |
| C1 app | RADV dlopen / vkCreateInstance only | NOT RUN | No hardware log |
| C1 | AF_UNIX SOCK_SEQPACKET socketpair and one-byte transfer | NOT RUN | No hardware log |
| C2a | 16 KiB imported SHM GPU fill / 2 s fence / CPU readback / second-payload verification | NOT RUN — gated by C1 | No hardware log |
| C2a | 8 MiB imported SHM GPU fill / 2 s fence / both readbacks | NOT RUN — gated by smaller size | No hardware log |
| C2a | 64 MiB imported SHM GPU fill / 2 s fence / both readbacks | NOT RUN — gated by smaller size | No hardware log |
| C2b app | RADV device / R32_UINT image clear / copy to imported 16 KiB SHM / 2 s fence / readback, no VideoOut | NOT RUN — gated by C2a | No hardware log |

C2a fills the complete allocation with `0x1a2b3c4d`. The second payload receives
the **same SHM_ANON fd** through SCM_RIGHTS at
`/system_tmp/ps5wl-gpu-probe`, maps it MAP_SHARED, checks every word, and sends an
acknowledgement. The producer does not spawn the verifier. Each verifier launch
requires its own Backend authorization. Import types are selected from the
intersection of host-pointer and buffer memory-type masks and must be coherent.
C2b clears a 64×64 R32_UINT image to the same value, copies all 4096 pixels into
its imported SHM page, and checks all words after the fence.

## PLAN.md §6 decision gates

| Gate | Outcome |
| --- | --- |
| BigApp SHM mprotect fails → private imports plus server-side memcpy | UNRESOLVED; no app measurement |
| Flexible headroom below 1 GiB → lower host quotas and report | UNRESOLVED; no accounting measurement |
| BigApp RADV dlopen fails → static link | UNRESOLVED; no app dlopen result |
| C2a optional all-flexible RADV BO option-f passes fully → offer native fast path | NOT TESTED; optional all-BO-flexible allocator is not implemented by these probes; ordinary imported-SHM fill is not evidence for option f |
| SOCK_SEQPACKET unsupported → framed SOCK_STREAM render front end | UNRESOLVED; no socketpair result |

Neither GpudCore nor VenusIcd should infer a positive memory, loading, coherence,
or payload-GPU gate from a successful cross-build.
