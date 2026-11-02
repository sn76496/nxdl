# CTRDL / CTRL analysis and Switch design

Inspected local revisions:

- CTRDL: `2fb8fa1f40be5e6a340da59ec9f8f25b7b2f8a14`
- CTRL: `626a356e96f263cb3d88052a6756afab2f61da60`

The request's dependency name is interpreted as CTRDL depending on CTRL.

## Original architecture

CTRDL's API layer validates flags, implements the dl* facade, and provides file,
stream, and memory input. Handle.c manages shared handles, reference counts,
global flags, dependencies, and a recursive lock. Stream.c abstracts seek/read.
ELFUtil.c parses ELF32 metadata and SysV hash tables. Symbol.c implements name
and address lookup. Relocs.c resolves symbols and applies ARM relocations.
Loader.c orchestrates dependencies, PT_LOAD allocation, relocation, permissions,
constructors, finalizers, and unloading. Error.c stores error messages.

The resolver callback is essential: a statically linked homebrew host has no
ordinary runtime dynamic symbol table. CTRDL supports a weak program resolver,
per-open callback/context, and generated resolvers through its separate ResGen
host tool. Resolution proceeds through the callback, host, global objects, self,
and dependency scope. Its relocation set is R_ARM_RELATIVE, ABS32, GLOB_DAT,
and JUMP_SLOT, with REL and RELA tables.

CTRL supplies more than CTRDL requires: application layout/environment detection,
heap and virtual-page allocation, aliases, permissions, cache maintenance, remote
memory access, hooks, patches, code generation, and exception support. CTRDL
primarily uses Heap, Code, and Memory. These depend on 3DS address ranges,
32-bit page/address arithmetic, real process handles, and Luma/Citra-specific
behavior. Porting all of CTRL is unnecessary for a loader.

Several details should not be mechanically translated: allocation assumes a
particular ELF virtual layout; permission alignment should cover both ends of
unaligned segments; RTLD_LOCAL equals zero so testing `flags & RTLD_LOCAL` cannot
identify local scope; weak undefined relocation targets should be explicitly
written; failure paths must release handles and allocations consistently. The
Switch prototype establishes a narrower contract instead of copying these paths.

## Switch mapping

| 3DS component | Switch implementation |
|---|---|
| Elf32 / EM_ARM | Elf64 / EM_AARCH64 |
| ARM REL/RELA | AArch64 RELA with explicit 64-bit addends |
| CTRL heap reservation | page-aligned libnx heap allocation |
| CTRL code reservation | virtmemFindCodeMemory under virtmemLock |
| CTRL alias/un-alias | svcMapProcessCodeMemory / svcUnmapProcessCodeMemory |
| CTRL permissions | svcSetProcessMemoryPermission using real process handle |
| CTRL cache operations | armDCacheFlush / armICacheInvalidate |
| Generated/program resolver | explicit callback or weak nxdlProgramResolver |

The memory backend reserves the final code address, builds and relocates the
image through its writable backing allocation, and only then maps it and applies
R, RW, or RX once per segment page run. Relocation values use the reserved runtime
address; writes use the backing address. After mapping, symbol metadata pointers
are moved to the destination because the backing address becomes inaccessible.
Holes become inaccessible. Writable+executable segments and page-sharing PT_LOADs
are rejected. The first version keeps zero-based ELF offsets, including leading holes.

libnx's JIT implementation documents the same process-code-memory fallback and
checks the same process handle/SVC availability. A single Jit buffer would make
its whole execution alias RX, which is insufficient for ordinary module data.
The loader therefore uses the underlying APIs and applies segment permissions.
Reference: https://github.com/switchbrew/libnx/blob/master/nx/source/kernel/jit.c
Installed headers under /opt/devkitpro/libnx/include/switch/kernel/{svc,virtmem}.h
were used to compile this implementation.

## Existing Switch loader in this workspace

libnx-dlink already implements a different viable route: DNRO files, MOD0 dynamic
metadata, ldr:ro registration with an NRR, AArch64 relocation and host exports.
That can be used when custom module packaging and ldr:ro access are acceptable.
NXDL is separate to explore CTRDL's direct standard-ELF route, without changing
that loader or conflating test results. Neither route removes the need for an
authorized executable mapping mechanism supplied by the homebrew environment.

## Validation and next work

The static archive, normal ELF fixture, constructor/destructor fixture, and
standalone smoke NRO all cross-compile with devkitA64. Host parser/relocation
checks pass with ASan and UBSan. The host fixture exercises all four nontrivial
supported relocation kinds; foreign code is never intentionally called by the
host test. Console execution, Horizon permissions, cache behavior, and unmapping
across differently protected ranges remain unverified until the smoke program
runs on a Switch. No hardware pass is claimed.

The next decision should follow the console log: establish the mapping/lifetime
backend first, then add dependency traversal and reference counting, global scope,
GNU hash, and richer API compatibility. TLS and C++ unwind registration need
separate designs. ResGen could be adapted later, but explicit import lists suffice
for validating the first working module ABI.

## Console failure and reference-loader comparison (2026-10-30)

The first console run failed during publication with 0x0000d401. The original
NXDL backend changed the entire destination to RW to copy and relocate it,
then attempted to apply R/RX/RW. This was a design mistake: setting writable
permissions changes the code memory state. Atmosphere's SetProcessMemoryPermission
checks FlagCode and transitions AliasCode to AliasCodeData for writable requests.
The exact failed range was not logged by v1, so the v2 console run is still needed
to confirm the diagnosis.

libtransistor/lib/ld/loader/elf.c prepares segment clones before mapping, maps
segments through svcMapProcessCodeMemory, then applies each final permission.
Its ELF permission conversion also confirms PF_X/PF_R have different bit values
from Horizon permissions; NXDL already converted those correctly.
libtransistor's nro_via_svc.c and nro_via_ldr_ro.c demonstrate its two NRO paths.
libnx-ext/libnx-ld/source/ld.c uses ldro.c to register an NRR and map an NRO through
ldr:ro, then resolves imports and invokes lifecycle functions. This service-based
NRO path does not require NXDL's intermediate writable code mapping.

NXDL v2 uses prepare-before-map, adds offset/size/requested permission/current
state diagnostics on permission failures, and keeps the standalone screen/file
logging test. The host backend now uses two distinct shared mappings, protects
the backing mapping after publication, and thereby checks that exported and
relocated pointers use the runtime address rather than the backing address.
The host sanitizer checks and Switch cross-build pass after this change.

Kernel reference inspected for the state-transition diagnosis:
https://github.com/Atmosphere-NX/Atmosphere/blob/master/libraries/libmesosphere/source/kern_k_page_table_base.cpp
No source was copied from these reference projects.

## V2 console result (2026-10-30)

Fetched results/smoke-v2-output.txt from the Switch FTP server. RESULT: PASS.
Calls returned 12 and 13, counter reached 9, the relative pointer read 13,
the weak import was NULL, constructor event was 1, and closing invoked
finalizer event 2 successfully. This verifies the prepare-before-map sequence
for this fixture on the tested console. Earlier pending-hardware statements
above describe the state before this run. Broader layouts and failure paths
still require additional console tests.
