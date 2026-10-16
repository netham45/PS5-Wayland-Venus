# LKL host capabilities on PS5

Target: the single live firmware 10.60 console (`0x10600006`), FreeBSD-11-derived
PS5 kernel, SDK v0.44, pinned LLVM 23 build container. This is a host-capability
investigation, not a claim that LKL or Ubuntu has booted on PS5. Observed payload
credentials are uid 1 / euid 0; results apply to this jailbroken elfldr context,
not an unmodified retail process.

## Verdict and qualification boundary

**The stock FreeBSD-14.4 host design is not a drop-in PS5 runtime.** Executable
shared backing, native worker creation, thread TLS, shared UMTX, signal-frame
resumption (with a PS5 ABI adapter), CPU extended state and privileged own-child
ptrace have actual hardware evidence. They do not establish an LKL boot.

The material design-level incompatibilities are the **native 16-KiB mapping
contract versus Linux's independent 4-KiB PTE projection**, and failed upper
47-bit VA placement. These cannot be fixed by renaming imports alone. A lower
guest VA contract and a deliberate subpage enforcement/coalescing strategy
would be design changes. The latest raw-syscall bypass probes are compiled but
UNTESTED, so a raw-kernel bypass of the libc mapping restrictions has **not**
been ruled out; do not claim a hardware impossibility from the libc failures.

Adapter changes supported by evidence include raw fork carry/second-return
handling, elfldr startup/parentage plus kqueue instead of execve/wait4 on
nonchildren, the PS5 signal-context prefix/extended-state layout, host-fault
classification, and the existing descriptor shim. Raw JIT shared aliases are a
proven executable-memory alternative; sceKernelJitMapSharedMemory returning
zero did not establish executable alias coherence in the attempted API path.
8-GiB backing exists through small windows despite an 8-GiB contiguous-map
failure. A 4-GiB contiguous pool works.

**Hardware coverage stopped at the user's wrap-up request while the console
was exclusively held for another agent's native-app fork diagnostic.** The
coordinator explicitly prohibited new LKL launches until that application's
parent/child retirement was resolved. Network, storage, introspection and the
full ABI census therefore remain UNTESTED, not failed. All fourteen payloads
build, including those groups; no further console actions were attempted after
that hold. This is a qualified capability report, not full PS5-host acceptance.

### Console outage boundary

After the hold, Main reported the console DOWN (no ping / Payload Manager),
and Ps5Backend reported `[Errno 113] No route to host` following its separate
BigApp fork parent 195 / child 196 diagnostic. No LKL payload was active;
this agent made no console calls after the hold and has no log puller/service
to cancel. This report does not assign an unobserved cause to the outage.
Further hardware coverage is blocked pending coordinator recovery.

Last **owned** hardware log lines, verbatim from [backing][backing]:

```text
[4211022 ms] lklcap-backing: FAIL VA_reservation address=7ffffff00000 collision_or_denial=1 errno=12
[4211022 ms] lklcap-backing: FAIL 4K_address address=2007b9000 map=ffffffffffffffff errno=22
[4211022 ms] lklcap-backing: FAIL 4K_offset offset=4096 map=ffffffffffffffff errno=22 native_page=16384
[4211022 ms] lklcap-backing: EXIT 1 failures=10
```

## Running and interpreting the probes

Build all fourteen payloads with `env/run make -C probes/lkl`. The Makefile uses
`cross/bin/prospero-clang`, including the repository's descriptor compatibility
shim. One payload covers each of thirteen capability groups; `helper.c` is the
separate elfldr process used for descriptor passing, RX execution and UMTX.

Run, for example:

```sh
# First obtain an idle console acknowledgement from Ps5Backend and VulkanClient.
python3 probes/lkl/run.py --ack-window execmem execmem
# Low-risk groups can be selected together, only outside an exclusive GPU window.
python3 probes/lkl/run.py network storage introspection
```

The runner serializes launches with a checkout-local lock, uploads the helper
without launching it, uses only `tools/ps5ctl` for deployment and log pulling,
and saves deployment, before/after process listings, raw TCP logs and an exit
ledger under `build/ps5/evidence/lkl/`. It refuses a batch containing risky groups;
a fresh `--ack-window GROUP` is required for each. The flag records an externally
coordinated idle window; it does not itself contact the other agents. Every
payload has a 55-second alarm, emits per-check results and explicit `EXIT N`,
and normally lingers 2.5 seconds for log pulls. The watchdog uses the logger's
nonblocking emergency writer. Own forked children have shorter deadlines and
are killed/reaped on parent failure. elfldr helpers are not the requester's
children and self-bound independently. No system/Wayland processes are killed.
If the console's `ps5ctl ps` fails, stop all console actions immediately.

A payload's `EXIT 1` means at least one capability check failed, not that every
row in that group failed. Expected negative controls (private UMTX across
processes and `wait4` on an elfldr nonchild) can pass their *test* while proving
that the corresponding native operation is unavailable for that usage.
`UNTESTED` is never converted into `FAIL` or inferred from an absent symbol.

Safety bounds: shared objects are tested at 1/2/4/8 GiB with at most 64 touched
16-KiB pages per mapping. MAP_FIXED only replaces a slot first reserved by this
payload; a relocated hint is reported rather than overwriting an existing
mapping. Anonymous resident commitment is bounded at 128 MiB and thread creation
at 2048 or eight seconds. These establish measured lower bounds or failure
points, not absolute console/global resource maxima. Storage creates a 4-GiB
sparse file only when that apparent size fits within 10% of available `/data`
space, limits the data working set to 32 MiB, bounds the eight cached-I/O
measurements to 20 seconds total, and unlinks the open file before doing I/O.
Throughput includes filesystem caching and is not a durable-media benchmark.

## Measured capability results: translator and workers

All logs below are raw pull captures in `build/ps5/evidence/lkl/`, dated
2026-10-16. Exit codes are the explicitly logged payload result, not the host
deployment command's exit status. An errno after a successful libc call can be
stale; only failure errno or the raw syscall's translated negative return is
an error result.

| Item / capability | Status | Payload EXIT | Key log excerpt | PS5 alternative / implication for failures in items 1–7 |
|---|---|---:|---|---|
| 1: anonymous RW → RX, actual execution | PASS | 1 | [execmem][execmem]: `mprotect=0 errno=0 value=42 signal=0` | Native translator code islands work in this payload context. |
| 1: anonymous RWX, actual execution | PASS | 1 | `PASS anon_RWX ... value=42 signal=0` | No privileged fallback needed for this context. |
| 1: process-1 SHM_ANON RW, SCM_RIGHTS, process-2 shared RX execution | PASS | 1 | `helper=10 pid=188 value=42 mmap_errno=0 signal=0 ppid=82` | Separate helper is an elfldr child, not a fork of requester 187. |
| 1: raw PS5 JIT create / alias / mmap execution | PASS | 1 | `jit_create_syscall rc=170 ... maxprot=7`; `jit_alias_syscall rc=219`; `jit_alias_execute ... value=42` | Verified native fallback: syscalls 533/534, RX object plus RW alias, mapped with mmap. |
| 1: sceKernelJitCreateSharedMemory / CreateAlias / Map API return codes | PASS | 1 | Create `rc=0 fd=760`; alias `rc=0 ... alias_fd=456`; map `rc=0 ... address=900000000` | Exact API return codes are zero; not by themselves proof of execution. |
| 1: execute code written through alias using sceKernelJitMapSharedMemory's returned address | FAIL | 1 | `FAIL sceJIT_alias_execute rx=900000000 rw=200e18000 value=-11 signal=11` | **Yes:** ordinary cross-process SHM_ANON RX and raw JIT alias mmap both executed 42 in the same payload. The API-map execution fault is not suppressed or reclassified as PASS; its cause is not established. |
| 1/13: memfd_create | See ABI census below | — | Symbol/raw-ABI census is separate from the proven SHM_ANON path. | SHM_ANON is a proven alternative; no reliance on memfd_create. |
| 2: SHM_ANON object size 1/2/4/8 GiB, sparse windows including last page | PASS | 1 | [backing][backing]: `large_shm_object_windowed GiB=8 windows=64 last_offset=8589918208 errno=0` | Largest object tested is 8 GiB, not an absolute maximum. |
| 2: contiguous shared 1/2/4-GiB mappings | PASS | 1 | `large_contiguous_shm_map GiB=4 truncate=0 ... errno=0` | Largest contiguous mapping tested successfully is 4 GiB. |
| 2: contiguous 8-GiB shared mapping | FAIL | 1 | `GiB=8 truncate=0 map=ffffffffffffffff errno=12` | **Yes, with changed allocation contract:** the same 8-GiB object works in 16-KiB windows. A smaller (≤4-GiB tested) contiguous LKL physical pool also works. Neither proves that the stock runtime can reserve its desired larger contiguous pool. |
| 2: fixed mapping, touch, read-only/RW protect, munmap cycles | PASS at tested addresses | 1 | `MAP_FIXED_cycles address=400000 cycles=8`; also `10000000`, `100000000`, `1000000000`, `8000000000` | Proven sampled addresses span 4 MiB through 512 GiB; this is not a claim of continuous hole-free VA availability or an exact maximum. |
| 2: upper 47-bit user VA samples | FAIL | 1 | `VA_reservation address=ff00000000 ... errno=12`; also `ffffffc000`, `10000000000`, `100000000000`, `400000000000`, `7f0000000000`, `7ffffff00000` | **Only a narrower-VA alternative is proven:** mappings at the lower addresses above work. Unchanged FreeBSD/Linux 47-bit address placement is not available. Lower stack/PIE/mmap placement requires an explicit guest/worker VA contract; arbitrary high guest addresses cannot simply be passed through. |
| 2: 4-KiB-aligned MAP_FIXED address / backing offset | FAIL | 1 | `4K_address ... errno=22`; `4K_offset offset=4096 ... errno=22 native_page=16384` | **No tested 4-KiB alternative.** PS5's 16-KiB mapping/protection unit cannot directly project arbitrary independent Linux 4-KiB PTEs. JIT aliases do not establish smaller pages. Coalescing or software enforcement would change the projection design, not just a syscall spelling. |
| 3: raw fork / vfork / rfork | PASS | 1 | [process][process]: children 163/164/165, `wait=0 status=1700`, distinct raw getpid and requester ppid 162 | Each child exits explicitly with 23; all reaped. vfork child executes only inline raw syscalls. |
| 3: forked child retains working libkernel | PASS | 1 | `PASS fork_libkernel child=166 libkernel=1 status=5a00` | Child getpid, getpagesize and pipe worked before execve was attempted. |
| 3: execve an unsigned SDK runner from /data | FAIL | 1 | `FAIL execve_runner status=5a00 errno=13` | **Yes:** fork itself and elfldr file-URI spawn both work. Using elfldr changes parentage and startup protocol; a signed/native exec path was not tested. |
| 3: long-lived own child, kqueue NOTE_EXIT and wait4 | PASS | 1 | `own_NOTE_EXIT pid=167 events=1 fflags=80000000`; `own_wait4 ... status=f expected=reaped` | Standard own-child lifecycle is observable. |
| 3: elfldr spawn, long-lived helper and kqueue NOTE_EXIT | PASS | 1 | `elfldr_spawn pid=168 self=162`; `ppid=82 requester=162`; `elfldr_NOTE_EXIT ... events=1` | Separate workers can be created without execve, but requester is not their parent. |
| 3: wait4 on elfldr-spawned helper as if it were our child | FAIL by parentage | 1 | `rc=-1 errno=10 ... expected=ECHILD_nonchild` | **Yes:** kqueue NOTE_EXIT works on that same nonchild. Do not translate wait4 failure into a worker-exit failure; kernel parenting semantics differ. |
| 3: PROC_PDEATHSIG_CTL via SDK syscall 544, command 11 | FAIL | 1 | `rc=-3 errno=3` for self | **Adapter alternative:** bounded control-channel EOF/deadline and explicit retirement are exercised by these helpers; own-child kill/reap and kqueue exit observation work. No native PDEATHSIG semantic capability is proven; PS5 syscall-number divergence remains relevant. |
| 3/13: posix_spawn / file-actions presence and unsigned-runner launch | See ABI census below | — | `posix_spawn_symbol address=813afba70` is presence, not proof of execution. | Do not infer absence from earlier SDK export searches. |
| 4: thr_new custom stack and TLS | PASS | 0 | [threads][threads]: `rc=0 ... tid=101840 stack=200a5c000 tls=200a109c0` | Raw-created LWP never calls libc after FS replacement. |
| 4: per-thread sysarch FS/GS replacement, segment reads, parent FS unchanged | PASS | 0 | `setfs=0 setgs=0 fs_word=12345678 gs_word=87654321`; parent `before=8ff800080 after=8ff800080` | Native architectural TLS primitives exist. |
| 4: thr_kill2 exact LWP | PASS | 0 | `tid=101840 signal=0 rc=0 errno=0`; asynchronous nonzero signal is covered in item 6. | No fallback required. |
| 5: shared-process UMTX WAIT_UINT / WAKE | PASS | 0 | [umtx][umtx]: `helper=171 result=0 errno=0 word=0 expected=wake`, one wake attempt | Word never changed: success is not the mismatch fast path. |
| 5: PRIVATE variants across processes | FAIL for cross-process wake, expected negative control PASS | 0 | `helper=172 result=-60 errno=60 word=0 expected=timeout` after 198 private wake attempts | **Yes:** use non-private WAIT_UINT/WAKE, demonstrated immediately above. PRIVATE intentionally does not rendezvous across processes. |
| 6: stock SDK ucontext layout used unchanged for RIP resume | FAIL | 124 initially; 2 bounded diagnostic | [initial timeout][fault-timeout] and [raw ABI capture][fault-abi]: RIP remained at UC qword 28 while SDK write changed qword 22 | **Yes, proven:** native mcontext starts at ucontext+64 rather than SDK +16, and its reported length is 1152. The exact mask/padding semantics of the extra prefix are not established. `faults.c` uses an opaque native prefix and preserves the full raw context, without treating the incompatible FP tail as SDK fields. |
| 6: PS5-adapted SIGSEGV frame, si_addr/si_code, trap/error, full registers, RIP skip and resume | PASS | 1 | [faults][faults]: `signal=11 si_code=2 ... trap=12 err=4 ... native_mc_len=1152 alt=1`; `PASS SIGSEGV_frame_RIP_resume` | Native trap number 12 is BSD `T_PAGEFLT`, not hardware vector number 14. Register words and named GPRs are retained in the log. |
| 6: sigaltstack | PASS | 1 | `PASS sigaltstack rc=0 errno=0`; both captured fault frames report `alt=1` | No fallback required. |
| 6: truncate shared backing then fault produces SIGBUS | FAIL: observed SIGSEGV instead | 1 | `FAIL SIGBUS_truncated_shm signal=11 code=2 address=200c88000` | **Yes for fault transport:** the same adapted handler captures and resumes that event as SIGSEGV with matching address/trap/error. A Linux SIGBUS policy must use guest VMA/backing provenance rather than blindly copy the host signal number. Actual asynchronous SIGBUS delivery was not separately forced. |
| 6: SIGUSR2 asynchronously interrupts a spinning worker LWP | PASS | 1 | `tid=101895 rc=0 delivered=1` | Native targeted pause/interrupt primitive works. |
| 7: CPUID policy-table census | PASS | 0 | [cpu][cpu]: AVX/AVX2/BMI1/BMI2/FMA/AES/SHA all `1`; all 61 bit/name entries are dumped with leaf/word/bit | Host features are not automatically the guest translator policy. |
| 7: XGETBV / XSAVE / XRSTOR / XSAVEC | PASS | 0 | `xcr0=7`; `xsave_required=832`; each instruction `signal=0` | No fallback required. |
| 7: RDTSC / RDTSCP / invariant TSC / frequency estimate | PASS | 0 | `TSC_Hz=1596287876 seconds=0.200074`; `RDTSCP aux=e`; invariant bit 8 set in `edx=00006799` | Approximately 1.596 GHz over one short native-clock sample; not a calibrated long-term timing guarantee. |

[execmem]: ../../build/ps5/evidence/lkl/20261016T171416-execmem.log
[backing]: ../../build/ps5/evidence/lkl/20261016T171649-backing.log
[process]: ../../build/ps5/evidence/lkl/20261016T170404-process.log
[threads]: ../../build/ps5/evidence/lkl/20261016T170427-threads.log
[umtx]: ../../build/ps5/evidence/lkl/20261016T170445-umtx.log
[faults]: ../../build/ps5/evidence/lkl/20261016T170926-faults.log
[fault-timeout]: ../../build/ps5/evidence/lkl/20261016T170504-faults.log
[fault-abi]: ../../build/ps5/evidence/lkl/20261016T170734-faults.log
[cpu]: ../../build/ps5/evidence/lkl/20261016T170950-cpu.log

## Measured capability results: tracing and resource limits

| Item / capability | Status | Payload EXIT | Key log excerpt / boundary |
|---|---|---:|---|
| 8: own child PT_TRACE_ME without credential changes | PASS | 1 | [ptrace capture][ptrace]: `PT_TRACE_ME_plain pid=185 rc=0 errno=0`. The captured version tested trace-me registration; the subsequently added plain stop/GETREGS/SETREGS/PT_IO path is compiled but UNTESTED. |
| 8: PT_ATTACH without credential changes | FAIL | 1 | `PT_ATTACH_plain pid=186 rc=-1 errno=1` (EPERM), with syscall carry correctly decoded. |
| 8: authorized elfldr-pattern own-child attach / GETREGS / SETREGS / PT_IO / detach | PASS | 1 | `PT_ATTACH_elfldr ... rc=0`; GETREGS/SETREGS 0; PT_IO read `123456789abcdef0`, write/read-back `fedcba9876543210`, 8 bytes; detach 0. Every call logs successful immediate restoration of the calling process's original authid and caps. |
| 8: ptrace privilege boundary | Adapter requirement | 1 | Main explicitly authorized the exact `launcher/vendor/pt.c` pattern: temporarily set **this payload's** authid `0x4800000000010003` and all caps around the syscall, restore immediately. No compositor/system process was a tracee, and only children 185/186 were targeted. Plain attach failure is preserved separately. |
| 9: RLIMIT_AS / DATA / NPROC / NOFILE | PASS | 0 | [limits capture][limits]: soft=hard respectively `9223372036854775807`, `34359738368`, `1604`, `13952`. AS is FreeBSD RLIM_INFINITY; DATA is 32 GiB. |
| 9: anonymous resident memory actually touched | PASS through safety cap; absolute maximum UNTESTED | 0 | 8/16/32/64/128 MiB mappings touched one byte in **every native page** and released; `resident_lower_bound=134217728 safety_cap=134217728 absolute_limit_untested=1`. No global-OOM attempt on the shared console. |
| 9: thread creation until failure, bounded at 2048/eight seconds, then join | PASS | 0 | `thread_count=510 create_rc=35 bound=2048 time_cap_seconds=8`; EAGAIN at 510 newly created workers, all joined before `EXIT 0`. Main and logger threads already existed. This is the observed process context, not a global console thread maximum. |
| 9: max shared object / contiguous map | PASS 8-GiB object; FAIL 8-GiB contiguous mapping | 1 | Item 2's separate object/windowed and contiguous-map measurements are authoritative; no absolute object-size maximum beyond the requested 8-GiB test bound is claimed. |

[ptrace]: ../../build/ps5/evidence/lkl/20261016T171342-ptrace.log
[limits]: ../../build/ps5/evidence/lkl/20261016T171321-limits.log

## Built but unexecuted coverage

The following checks have real bounded implementations, but no console
execution/exit/log evidence. The exclusive-console hold and then the user's
wrap-up request ended hardware coverage. They must remain UNTESTED.

| Item / capability | Status | EXIT | Implementation / evidence boundary |
|---|---|---|---|
| 2: raw SYS_mmap / SYS_mprotect bypass at rejected high VA and 4-KiB addresses/offsets | UNTESTED | — | `backing.c` contains a native-page raw positive control, direct 4-KiB address/offset/protect calls, and raw high-VA reservation/projection alternatives. Latest build succeeded; the linked backing capture predates these additions. No raw-bypass verdict is inferred from libc EINVAL/ENOMEM. |
| 8: GETREGS/SETREGS/PT_IO after a plain PT_TRACE_ME child stops | UNTESTED | — | `ptrace.c` contains this path as well as the captured privileged path. The plain path was added after the last ptrace capture, without claiming that registration alone proves register access. |
| 10: genuinely unprivileged TCP/UDP loopback clients, TCP listen, data exchange | UNTESTED | — | `network.c` first uses normal `seteuid(getuid())`, requires nonzero effective uid, then runs bounded loopback tests. No LAN/gateway reachability claim. |
| 10: unprivileged raw/datagram ICMP socket attempts | UNTESTED | — | `network.c` records each descriptor/errno and actual uid/euid after dropping effective uid. |
| 11: /data free space, ≤10%-of-free multi-GiB sparse file, sequential/random pread/pwrite 4K/64K throughput, deletion | UNTESTED | — | `storage.c`: 4-GiB apparent size capped by 10% available space, unlinked while open, ≤32-MiB working set, eight two-second cached-I/O measurements under a 20-second total bound. No invented throughput numbers. |
| 12: self KERN_PROC_PID RSS and KERN_PROC_VMMAP entries | UNTESTED | — | `introspection.c` queries self only and logs sized/validated entries. No VM-map/RSS result inferred from generic process-list health checks. |
| 13: memfd_create, copy_file_range, pdfork, getrandom/getentropy | UNTESTED | — | `abi.c` performs native libkernel symbol lookup with a getpid positive control plus RTLD_DEFAULT/library-origin reporting. Known SDK BSD syscall candidates use invalid arguments and distinguish ENOSYS/SIGSYS; unsupported modern numbers are not guessed. |
| 13: posix_spawn / posix_spawnp and file-actions init/open/dup2/close/destroy | UNTESTED full census/behavior; one symbol presence observed | — | Item 3 recorded non-null RTLD_DEFAULT posix_spawn. `abi.c` separately tests file-action construction/destruction and bounded unsigned-runner spawn, but has not run. Presence in another library is not equated with a libkernel export. |
| 13: cap_enter/getmode/rights/ioctls/fcntls families | UNTESTED | — | Symbol census includes limit/get operations; safe raw get candidates are implemented. Sandbox mutation is excluded. FreeBSD syscall 533 collides with PS5 JIT create, so cap_rights_limit's modern number is never called as a capability syscall. |
| 13: native eventfd/eventfd2 and timerfd create/settime | UNTESTED | — | Symbol census implemented; no verified native PS5 syscall number, so raw modern BSD/Linux guesses are explicitly excluded. Existing epoll-shim evidence elsewhere is an adapter result, not a native kernel ABI PASS. |
| 13: native pipe2 and accept4 symbol/raw candidates | UNTESTED | — | Census distinguishes native libkernel exports from the already linked compatibility shim's RTLD_DEFAULT symbols. Raw SDK BSD-number dispatch alone will not be interpreted as semantic support on the PS5 fork. |

## Source correspondence

Read-only source: `~/lkl-freebsd/IMPLEMENTATION_PLAN.md` and its `probes/`.
`execmem.c` carries the shared-backing/RX execution invariant from
`freebsd_translation_memory`, `freebsd_translation_enforcement` and
`freebsd_shared_instruction`; `backing.c` tests the native map/protect/backing
requirements in `freebsd_shared_vm` and `freebsd_runtime_memory`.
`process.c` covers `freebsd_native_worker_clone` lifecycle prerequisites;
`threads.c`, `faults.c`, and `ptrace.c` isolate the native primitives behind
`freebsd_worker_cpu_state`, `freebsd_worker_faults`, `freebsd_fault_frame_context`,
`freebsd_native_memory_cpu` and the `freebsd_ptrace_*` probes. `umtx.c` retains the
shared control-page WAIT_UINT/WAKE protocol, with PRIVATE as a negative control.
`cpu_features.h` ports the hardware bit/name policy table used by
`host/freebsd/x86_decoder.c` and `freebsd_x86_decoder_reuse`; it reports native
hardware bits, not the decoder's filtered guest advertisement. No LKL runtime
or guest syscall emulation is linked into these probes.

The SDK's [raw syscall wrappers](https://github.com/ps5-payload-dev/sdk/blob/v0.44/libc/syscalls.c)
do not translate carry/errno or fork's second return register. `cap.h` handles
both explicitly. Modern FreeBSD/Linux syscall numbers must not be guessed:
PS5 repurposes the 532+ range (notably FreeBSD cap_rights_limit 533 versus PS5
JIT create). Unknown raw syscall ABIs remain explicitly UNTESTED. A successful
invalid-argument dispatch is not proof that a modern BSD semantic ABI exists.

## Existing display, input and audio evidence (not re-probed)

| Capability | Status | Exit | Existing evidence / boundary |
|---|---|---|---|
| Foreground VideoOut | PASS | Existing bounded native-app clean exit, no LKL payload | [backend README](../../compositor/src/backend/ps5/README.md#L26): PID 98, 899 flips in 15 seconds, **59.94 Hz**, teardown completed. PIDs 109/114/120/132 provide longer clean-lifecycle evidence. Plain background payload VideoOut is unsafe and is not attempted. |
| scePad / sceKeyboard / sceMouse integration | PASS for software imports/open path; physical events UNTESTED | Existing compositor run | [Existing compositor log](../../build/ps5/evidence/compositor-fd-fixed.log): `keyboard_init=0 pad_init=0`, Keyboard handle `0x10a0700`, Pad handle `0x30b0700`, Mouse Init 0 / handle `0xc0700`; the adapter polls at 250 Hz. [Backend README](../../compositor/src/backend/ps5/README.md#L41) explicitly excludes proof of physical keyboard/mouse/pad events. |
| AudioOut on this console | UNTESTED | — | No local AudioOut execution evidence exists in PLAN.md or compositor/probes. External references only: [prospero-win audio adapter](https://github.com/mpereiraesaa/prospero-win/blob/main/native/pw_audio_ps5.c) calls Init/Open/SetVolume/Output/Close with SYSTEM user `0xff`, main output, S16 stereo; [PS5_Vulkan AudioOut recipe](https://github.com/mihawk-99/PS5_Vulkan/blob/main/docs/RECIPES.md#audioout) describes a hardware-proven 48-kHz, 256-frame baseline. This is not a PASS on this firmware/session. |
