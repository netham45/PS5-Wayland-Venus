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
No target executable is run by Meson.

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
