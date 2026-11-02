# NXDL: direct ELF loader prototype for Nintendo Switch

An independent implementation inspired by CTRDL's architecture, using devkitPro
libnx and devkitA64. This is a prototype build. The v2 standalone smoke test passed on a Switch
on 2026-10-30 after correcting the mapping sequence.
It does not use Nintendo's proprietary SDK. Source in the adjacent CTRDL/CTRL
checkouts is unchanged, as is the existing libnx-dlink project.

## Build and verify

```
make
make check
make smoke
make install
```

`DEVKITPRO` defaults to `/opt/devkitpro`. `make install` installs `lib/libdl.a`,
`include/dlfcn.h`, and `include/nxdl.h` under `$DEVKITPRO/portlibs/switch`.
License and attribution files are installed in `share/licenses/nxdl/`.
Override `PREFIX` to change the destination, or use `DESTDIR` for staged installs.
Installed applications can use `-ldl -lnx` with the usual Switch portlibs include
and library search paths. The project name and directory remain NXDL and `nxdl/`.
 `make check` compiles the parser and
relocator for the host with ASan/UBSan and a dual-address mmap backend, and loads a real
AArch64 shared object produced by devkitA64. It checks exports, mutable data,
ABS64, GLOB_DAT, RELATIVE, unresolved strong imports, unresolved weak imports,
close, invalid handles, truncated headers, invalid machine/offset/segment ranges,
and byte mutations. It does not execute AArch64 code or exercise Horizon SVCs.
LeakSanitizer is disabled because it cannot run under this environment's tracing;
address and undefined-behavior sanitizers remain enabled.

## Standalone console test

Each console test must be its own application, print results to the Switch
screen, and save those results to an output file. No previous test runner is used.

Copy these two files to the SD card:

- `build/smoke.nro` -> `sdmc:/switch/nxdl/smoke.nro`
- `build/plugin-console.so` -> `sdmc:/switch/nxdl/plugin-console.so`

Launch smoke.nro through homebrew. It prints PASS/FAIL, flushes each result to
`sdmc:/switch/nxdl/smoke-output.txt`, then waits for +. The log is overwritten on
each launch. File creation failure is itself a failure displayed on screen.
The test checks constructor execution, host calls through the PLT, mutable data,
relative/absolute pointers, unresolved weak imports, and destructor execution.
A crash during loading may leave only the initial line in the flushed log.

The environment must provide a real own-process handle and SVC permissions for
0x73, 0x77, and 0x78. Without them loading fails with an explanation. No ldr:ro
service or NRR registration is used. The v1 console log reported a segment permission failure (0x0000d401).
The v2 console run passed: host calls, mutable data, relative pointers, weak
imports, constructors, finalizers, and close. See results/smoke-v2-output.txt.

## Host API

Include `dlfcn.h`, link `build/libdl.a` before `-lnx`, and expose host imports:

```c
void *nxdlProgramResolver(const char *name) {
    return strcmp(name, "my_function") == 0 ? (void *)my_function : NULL;
}

void *module = dlopen("sdmc:/switch/myapp/plugin.so", RTLD_NOW | RTLD_LOCAL);
if (!module) printf("%s\n", dlerror());
/* On success: dlsym(module, "entry"), call it, then dlclose(module). */
```

The namespaced API in `nxdl.h` additionally supplies nxdlMap and a per-load
resolver/context. Each open creates an independent object. dlsym searches that
object's exports. dlerror consumes the last recorded error. Stop all module
execution and discard module pointers before closing. Serialize loader calls;
resolvers, initializers, and finalizers must not reenter the loader. Link either
this library or libnx-dlink, since both export the POSIX dl* names.

## Module contract and current limits

Use the module rule and export map in the Makefile as a starting point. Modules
are little-endian ELF64 AArch64 ET_DYN, PIC, `-shared -nostdlib`, with a SysV hash
(`--hash-style=sysv`), explicit exports, and separate pages for each PT_LOAD.
The export map also hides the devkitA64 default linker script's `_stack` symbol,
which otherwise advertises an address outside the loaded image. Module imports
are supplied by the host callback. Do not statically link another libnx runtime
inside a module. Host/module interfaces should use plain C and shared ownership
rules for allocations.

Implemented: eager RELA NONE/RELATIVE/ABS64/GLOB_DAT/JUMP_SLOT, undefined weak
symbols, SHN_ABS, init arrays in forward order, fini arrays in reverse order,
zero-filled BSS, PT_LOAD permissions, file and memory input, symbol lookup,
failed-load cleanup, and retryable close when unmapping fails.

Only RTLD_NOW|RTLD_LOCAL is accepted. There is no dependency graph, global symbol
scope, dlopen(NULL), shared-handle reference counting, dladdr, stream API, GNU-only
hash, lazy binding, TLS, symbol versions, IFUNC, REL/RELR, legacy DT_INIT/DT_FINI,
RELRO tightening, or C++ exception/unwind registration. Some unsupported features
are rejected explicitly; this is a constrained module ABI, not a general-purpose
ELF compatibility or security boundary. Load trusted modules only. Maximum input
and mapped image size is 256 MiB; images retain zero-based virtual layout.
Unexpected kernel unmap failures retain backing storage to avoid freeing mapped
memory; a failed-load cleanup failure requires restarting the application.

See ANALYSIS.md for the original library analysis and port design.

## License and contributions

NXDL is released under **MPL-2.0**, matching CTRDL's license, without the
secondary-license incompatibility opt-out. See [LICENSE](LICENSE) and
[licensing and upstream acknowledgments](NOTICE.md), including the MPL section
3.3 route for combining NXDL with GPL-licensed Rockbox. Binary releases must
identify where recipients can obtain the corresponding NXDL source.

The library is inspired by [CTRDL](https://github.com/kynex7510/CTRDL) by
Kynex7510. Its C formatting follows CTRDL's style and is captured in
`.clang-format`. See [CONTRIBUTING.md](CONTRIBUTING.md) for formatting commands
and build checks. Public API names, the `nxdl/` directory, the `libdl.a` archive,
and the portlibs installation layout are unchanged by the style cleanup.
# nxdl
