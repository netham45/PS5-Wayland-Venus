# LLVM for standalone lavapipe payloads

`env/run python3 deps/llvm/build.py host` builds pinned LLVM 14.0.6, its native
`llvm-tblgen` and `llvm-config`, and static position-independent X86 libraries.
`env/run python3 deps/llvm/build.py ps5` also cross-builds the same libraries
through `cross/bin/prospero-clang{,++}`. LLVM is private to `build/llvm/`; it does
not replace the container compiler or the normal dependency sysroots. Source
and checksum come from `deps/versions.lock.json`. Mesa's separate variant is
`env/run python3 deps/mesa/lavapipe.py {host,ps5}`.

## Pacbrew recipe evaluation

The starting reference is [pacbrew's LLVM recipe at our pinned commit
722187f0](https://github.com/ps5-payload-dev/pacbrew-repo/blob/722187f0ee246c0d7d57401c8a97de1c7f4e2ff0/llvm/PKGBUILD).
It selects LLVM 14.0.6, static X86 libraries, a native table generator and the
PS5 SDK toolchain. We reuse the source checksum and its four missing-`cstdint`
patches. We do **not** copy its blanket disabling of `SmallVector.cpp` static
assertions, its implicit-project text substitution, or its SDK-provided
`llvm-config`: those are not a demonstrated ABI or dependency-discovery fix.
This build disables tools/projects explicitly and uses the repository compiler
wrappers so the fd/flag compatibility archive is linked.

The PS5-specific `0001-ps5-libc-math.patch` removes LLVM's unconditional `-lm`
from feature probes and LLVMSupport on this SDK target only. The SDK supplies
math through its default libc link and has no separate libm archive; the
unmodified configure otherwise misreports headers/functions as missing because
each probe fails to link `-lm`.

`0002-smallvector-explicit-alignment.patch` retains those assertions and states
the element and header alignment requirements on `SmallVector` itself. The
unmodified PS5 cross compile failed both over-aligned zero-capacity assertions
(`alignof` 8 rather than 16/32). The change addresses the empty-base layout,
instead of hiding the compiler's diagnostic.

The pinned Mesa 26.2.0 `meson.build:2039-2050` accepts LLVM >=8 for X86 MCJIT.
ORCJIT requires >=15; this variant deliberately selects `llvm-orcjit=false` on
both platforms. LLVM still builds its ORC support libraries, but lavapipe uses
MCJIT. No AMDGPU target, clang frontend, SPIR-V translator or external LLVM
runtime `.so` is required. RTTI, exceptions, plugins, compression and terminal
libraries are disabled on both platforms.

For cross dependency discovery, `deps/mesa/lavapipe.py` generates a build-host
`llvm-config` adapter: component closures come from the identical native X86
build, include/library paths point at the PS5 install and target system
libraries are `-lpthread` (math comes from the SDK's default libc link).
Combined queries such as `--libs --ldflags --system-libs` retain the complete
LLVM component closure while replacing only the target system libraries.
No target executable is run by Meson.
The adapter is selected through Meson's machine file, not `LLVM_CONFIG` in
the process environment: SDK compiler wrappers use that environment variable
to locate their own compiler binaries and must retain the container's value.

## Executable memory source review

The pinned source chain is:

- Mesa `src/gallium/auxiliary/gallivm/lp_bld_misc.cpp:664-668` constructs
  `llvm::SectionMemoryManager`.
- LLVM `llvm/lib/ExecutionEngine/SectionMemoryManager.cpp:94-106` requests RW
  pages; `:149-155` finalizes code pages as RX and leaves writable data separate.
- LLVM `llvm/lib/Support/Unix/Memory.inc:82-117` uses anonymous private `mmap`
  and `:167-201` uses `mprotect` for the transition. Both allocation and
  protection round to the runtime page size.
- LLVM `llvm/lib/Support/Unix/Process.inc:77-88` obtains that page size through
  `getpagesize()` (or `sysconf` where necessary), rather than assuming 4 KiB.

This is already the anonymous RW-to-RX pattern proven by
`probes/lkl/RESULTS.md:115`, with the runtime PS5 16 KiB granularity. No custom
JIT syscalls, alias mappings, executable file mappings, MAP_FIXED placement or
RWX override is introduced. The upstream manager is retained rather than
adding a second allocator. Payload-specific execution remains a console
acceptance test; source review is not runtime evidence.

## Observed host acceptance (2026-10-16)

`env/run python3 deps/llvm/build.py host` completed (exit 0).
`env/run python3 deps/mesa/lavapipe.py host` completed (exit 0), producing
`build/mesa/host-lavapipe/sysroot/lib/libvulkan_lvp.so`. `readelf -d` shows no
LLVM shared-library dependency: LLVM is linked from its X86 static archives.

After `env/run clients/vkcube/build.sh host --driver lavapipe`, the command
`env/run python3 clients/vkcube/smoke.py --driver lavapipe` exited 0. It selected
`llvmpipe (LLVM 14.0.6, 256 bits), type: Cpu`, logged
`vkcube presented frame=120` and `vkcube exit frames=120 validation_error=0`.
The independently captured headless ps5wl log reported `Created shm allocator`
and `client surface committed 500x500 count=120`. No GPU server was launched.


## Observed PS5 LLVM cross build (2026-10-16)

`env/run python3 deps/llvm/build.py ps5` completed (exit 0), including all
upstream `SmallVector.cpp` layout assertions. The static archives are installed
under `build/llvm/ps5/sysroot/lib`; both `libLLVMMCJIT.a` and `libLLVMOrcJIT.a`
were built. CMake's target compiler is the repository's
`cross/bin/prospero-clang++`, not the unwrapped SDK compiler. The runtime page
size configuration found `getpagesize`; no 4 KiB override was added.

`env/run python3 deps/mesa/lavapipe.py ps5` and
`env/run clients/vkcube/build.sh ps5 --driver lavapipe` also completed (exit 0).
The payload driver is
`build/mesa/ps5-lavapipe/sysroot/lib/libvulkan_lvp.so`; its only `DT_NEEDED`
entries are `libkernel_web.sprx` and `libSceLibcInternal.sprx`, not LLVM, RADV,
AGC or a GPU server library. Its dynamic exports include `vkCreateInstance`,
`vkGetInstanceProcAddr`, `vkGetDeviceProcAddr`, `vkCreateSwapchainKHR` and
`vkQueuePresentKHR`.

The standalone client is
`build/ps5/vkcube-lavapipe/ps5wl-vkcube-lavapipe.elf`, with its driver embedded
and a default of 600 frames; the readback client is
`build/ps5/vkcube-lavapipe/ps5wl-vktest-lavapipe.elf`. Both link through
`cross/bin/prospero-clang`. The cube extracts its own driver to
`/data/ps5wl/libvulkan-lavapipe.so.1` and logs on port 9590.
The PS5 compositor's xdg configure maximizes the cube to 1920x1080; the
500x500 host acceptance is not a PS5 runtime or timing result.

## Console acceptance gate

The first authorized L5 attempt failed at `vkCreateInstance`, before physical
device discovery or JIT execution. `build/ps5/evidence/gpu/l5-client.log`
records client PID 88 extracting its 70,117,688-byte driver, connecting to
Wayland and reporting `vkCreateInstance failed`. The gatekeeper reported that
the client exited and the compositor remained responsive. No 600 presents,
matching compositor commit count or visible-cube screenshot were obtained.

Offline inspection identified a C11 thread ABI collision. The SDK's
`threads.h` defines `thrd_success=4`; Mesa's emulation defines success as 0,
and `vk_instance_init` tests `mtx_init(...) != 0`. Disassembly of the first
payload driver showed its `mtx_init` resolving to the SDK implementation,
which returns 4 on success. Pacbrew's existing `C11_THREADS_RENAME` protection
was enabled for RADV, and separately for Venus, but not lavapipe.
`deps/mesa/lavapipe-patches/0002-ps5-c11-symbols.patch` extends that existing
protection to the FreeBSD-targeted lavapipe payload; it does not change the
SDK ABI, suppress the error, or replace the C11 implementation.
The corrected public PS5 Mesa build and both client links completed (exit 0).
`objdump --disassemble=vk_instance_init` now shows all three mutex initializers
calling `mesa_mtx_init`, whose plain-mutex branch returns 0. The authorized
corrected run passed instance and surface initialization, then failed with
SIGSEGV before device initialization or any recorded presents
(`build/ps5/evidence/gpu/l5-corrected-client.log`).

The separately authorized fault-capture run records RIP `0x82882448d`, a null
fault address, trap 12, page-fault error 4 and native mcontext length 1152 in
`build/ps5/evidence/gpu/l5-fault-client.log`. The opaque-prefix PS5 signal-frame
adapter captured those fields successfully. The fault instruction is outside
the loaded driver's static code region, inferred from its logged
`vkGetInstanceProcAddr` address and that symbol's zero ELF offset. SDK import
stubs cannot attribute firmware offsets. PS5 LLVM and Mesa lavapipe builds
therefore retain frame pointers for register, instruction and stack capture.
The frame-pointer build completed through the public PS5 Mesa recipe, and
disassembly confirms `push %rbp; mov %rsp,%rbp` prologues in both Mesa's
`lvp_CreateDevice` and LLVM's `LLVMInitializeX86TargetInfo`.

The authorized extended capture in
`build/ps5/evidence/gpu/l5-extended-client.log` identified libc `strlen(NULL)`.
Its complete symbolized frame chain and ELF-binding evidence are preserved in
`l5-extended-symbolized.log` alongside the capture. MCJIT initialization calls
`DynamicLibrary::getPermanentLibrary(NULL)`, whose `dlopen` fails and whose
`dlerror` returns NULL; assigning that error to `std::string` reaches the
captured null read. This happens synchronously while llvmpipe initializes its
sampler matrix during device creation, not in a raster worker.

The ICD's `-Bsymbolic` binding selected its own SDK static-libc `dlopen` wrapper,
whose private weak `__dlopen` hook is not exported by the payload. The payload
exports the public `dlopen`, `dlsym`, `dlclose` and `dlerror` entrypoints instead.
The canonical `cross/compiler.sh` DSO link contract applies
`cross/public-loader.dyn`, making public `dl*` calls interposable on PS5 while
retaining symbolic binding for other ICD/LLVM definitions. It neither exports
private CRT hooks nor suppresses LLVM's loader error. The original
lavapipe-specific dynamic-list patch was removed when the shared compatibility
shim also needed the same public loader contract. A pre-fix relocation audit
finds none of the four public-loader relocations required by this path.
The corrected host build, 120-present/120-commit Wayland smoke and PS5 driver
build completed successfully. The post-fix audit finds all four
`R_X86_64_GLOB_DAT` loader relocations, and MCJIT now calls the public loader GOT
slots instead of its local SDK wrappers. Dynamic flags remain
`SYMBOLIC BIND_NOW`; the driver still needs only the kernel and libc modules.
PS5 configuration also fingerprints the cross/native machine files, compiler
proxy, canonical dynamic list and injected compatibility archive after rebuilding
that archive, so central shim changes cannot silently reuse an old ICD link.
The final rebuild with the central `ppoll` correction completed successfully;
its audit retains all four public-loader relocations and all three
`vk_instance_init` calls to `mesa_mtx_init`. Disassembly also confirms Wayland's
timeout helper calls the central `__wrap_ppoll`, not the SDK's incompatible stub.
The latest central-shim rebuild retains the cached native-poll module for the
process lifetime. Post-link inspection confirms its resolver calls `dlopen` and
`dlsym` without `dlclose`; loader bindings, C11 targets and Wayland's wrapper
callsite remain intact. The symbolized evidence log records the final hashes.

The final-loader console run (`build/ps5/evidence/gpu/l5-final-loader-client.log`,
PID166) progressed to MCJIT memory finalization, then captured RIP0. The stack
return at driver+0xf4b2 is the SDK `mprotect` wrapper's call through its weak,
unbound `kernel_mprotect` GOT slot. The frame chain continues through
`llvm::sys::Memory::protectMappedMemory`, `SectionMemoryManager::finalizeMemory`,
MCJIT, and sampler JIT initialization. The analogous SDK `mmap(PROT_EXEC)`
branch uses the same private hook. The canonical public binding list therefore
includes both `mmap` and `mprotect`, already exported by the payload, while
preserving the SDK's existing privileged executable-page implementation.
No raw-syscall permission bypass or private-hook export was added. This
supersedes the prior driver hash; console acceptance of the rebuilt driver is
recorded separately in the GPU probe results.

The final PS5 driver SHA256
`f613858b6ff6d82f09d57249f13e6f01c009925e0052596438ca6438c10e5097`
passed actual ICD runtime-binding audit and console acceptance: separate
client95 rendered600 frames, compositor93 committed600 named1920x1080 cube
buffers, and the inspected scanout PNG shows the textured cube. Both client
cleanup and the compositor's bounded shutdown completed; gpud stopped0.
Evidence is `build/ps5/evidence/gpu/l5-final-public-runtime-*` and the final
section of `gpu-server/probes/RESULTS.md`. Additional console runs still require
coordination with the console owner.
