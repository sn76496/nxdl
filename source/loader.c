/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "nxdl.h"
#include "memory.h"

#include <elf.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define NXDL_PAGE_SIZE 4096u
#define NXDL_MAX_IMAGE_SIZE (256u * 1024u * 1024u)

typedef struct NXDLModule NXDLModule;
struct NXDLModule {
    NXDLModule* next;
    NxdlMemory mem;
    uint8_t* perms;
    Elf64_Sym* syms;
    const char* strings;
    size_t nsyms, strsz;
    uint64_t init, initsz, fini, finisz;
    int initialized;
};

static NXDLModule* g_Modules;
static char g_Error[256];
static int g_ErrorPending;

void nxdlSetError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_Error, sizeof(g_Error), fmt, ap);
    va_end(ap);
    g_ErrorPending = 1;
}

const char* nxdlError(void) {
    if (!g_ErrorPending)
        return NULL;
    g_ErrorPending = 0;
    return g_Error;
}

static int nxdl_isRangeValid(size_t size, uint64_t off, uint64_t len) {
    return off <= size && len <= size - off;
}

static void* nxdl_getPointer(NXDLModule* m, uint64_t off, uint64_t len) {
    if (!nxdl_isRangeValid(m->mem.size, off, len))
        return NULL;
    if (len)
        for (size_t p = off / NXDL_PAGE_SIZE; p <= (off + len - 1) / NXDL_PAGE_SIZE; p++)
            if (!(m->perms[p] & PF_R))
                return NULL;
    return (char*)(m->mem.mapped ? m->mem.base : m->mem.source) + off;
}

static int nxdl_isValidHandle(NXDLModule* m) {
    for (NXDLModule* p = g_Modules; p; p = p->next)
        if (p == m)
            return 1;
    return 0;
}

static const char* nxdl_getSymbolName(NXDLModule* m, const Elf64_Sym* s) {
    if (s->st_name >= m->strsz || !memchr(m->strings + s->st_name, 0, m->strsz - s->st_name))
        return NULL;
    return m->strings + s->st_name;
}

static void* nxdl_getSymbolAddress(NXDLModule* m, const Elf64_Sym* s) {
    if (s->st_shndx == SHN_ABS)
        return (void*)(uintptr_t)s->st_value;
    if (!nxdl_getPointer(m, s->st_value, s->st_size ? s->st_size : 1))
        return NULL;
    return (char*)m->mem.base + s->st_value;
}

static void* nxdl_lookupSymbol(NXDLModule* m, const char* name) {
    for (size_t i = 1; i < m->nsyms; i++) {
        Elf64_Sym* s = m->syms + i;
        unsigned b = ELF64_ST_BIND(s->st_info), v = ELF64_ST_VISIBILITY(s->st_other);
        const char* n = nxdl_getSymbolName(m, s);
        if (s->st_shndx != SHN_UNDEF && (b == STB_GLOBAL || b == STB_WEAK) && v != STV_HIDDEN && v != STV_INTERNAL &&
            n && !strcmp(n, name))
            return nxdl_getSymbolAddress(m, s);
    }
    return NULL;
}

void* nxdlSym(void* h, const char* name) {
    if (!nxdl_isValidHandle(h) || !name) {
        nxdlSetError("invalid handle or symbol name");
        return NULL;
    }
    void* p = nxdl_lookupSymbol(h, name);
    if (!p)
        nxdlSetError("symbol not found: %s", name);
    return p;
}

static int nxdl_applyRelocations(NXDLModule* m, uint64_t off, uint64_t size, NxdlResolver resolver, void* user) {
    if (off % 8 || size % sizeof(Elf64_Rela))
        return -1;
    Elf64_Rela* table = nxdl_getPointer(m, off, size);
    if (!table && size)
        return -1;
    for (size_t i = 0; i < size / sizeof(*table); i++) {
        Elf64_Rela r = table[i];
        uint32_t type = ELF64_R_TYPE(r.r_info), index = ELF64_R_SYM(r.r_info);
        if (type == R_AARCH64_NONE)
            continue;
        uint64_t* target = nxdl_getPointer(m, r.r_offset, 8);
        if (!target || r.r_offset % 8 || !(m->perms[r.r_offset / NXDL_PAGE_SIZE] & PF_W))
            return -1;
        uintptr_t value = 0;
        if (type == R_AARCH64_RELATIVE) {
            if (index)
                return -1;
            value = (uintptr_t)m->mem.base;
        } else if (type == R_AARCH64_ABS64 || type == R_AARCH64_GLOB_DAT || type == R_AARCH64_JUMP_SLOT) {
            if (index >= m->nsyms)
                return -1;
            if (index) {
                Elf64_Sym* s = m->syms + index;
                const char* name = nxdl_getSymbolName(m, s);
                if (!name)
                    return -1;
                if (s->st_shndx != SHN_UNDEF)
                    value = (uintptr_t)nxdl_getSymbolAddress(m, s);
                else {
                    value = resolver ? (uintptr_t)resolver(name, user) : 0;
                    if (!value && ELF64_ST_BIND(s->st_info) != STB_WEAK) {
                        nxdlSetError("unresolved import: %s", name);
                        return -2;
                    }
                }
            }
        } else {
            nxdlSetError("unsupported AArch64 relocation: %u", type);
            return -2;
        }
        *target = value + (uint64_t)r.r_addend;
    }
    return 0;
}

static int nxdl_validateFunctions(NXDLModule* m, uint64_t off, uint64_t size) {
    if (off % 8 || size % 8)
        return -1;
    uintptr_t* a = nxdl_getPointer(m, off, size);
    if (!a && size)
        return -1;
    for (size_t i = 0; i < size / 8; i++) {
        uintptr_t v = a[i], base = (uintptr_t)m->mem.base;
        if (!v || v == UINTPTR_MAX)
            continue;
        if (v < base || v - base >= m->mem.size || (v & 3) || !(m->perms[(v - base) / NXDL_PAGE_SIZE] & PF_X))
            return -1;
    }
    return 0;
}

static void nxdl_callInitFiniArray(NXDLModule* m, uint64_t off, uint64_t size, int reverse) {
    uintptr_t* a = (uintptr_t*)((char*)m->mem.base + off);
    for (size_t i = 0; i < size / 8; i++) {
        uintptr_t v = a[reverse ? size / 8 - 1 - i : i];
        if (v && v != UINTPTR_MAX)
            ((void (*)(void))v)();
    }
}

void* nxdlMap(const void* input, size_t size, int flags, NxdlResolver resolver, void* user) {
    Elf64_Ehdr eh;
    if (flags != NXDL_NOW || !input || size < sizeof(eh)) {
        nxdlSetError("expected ELF and NXDL_NOW|NXDL_LOCAL");
        return NULL;
    }
    memcpy(&eh, input, sizeof(eh));
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB || eh.e_ident[EI_VERSION] != EV_CURRENT || eh.e_version != EV_CURRENT ||
        eh.e_type != ET_DYN || eh.e_machine != EM_AARCH64 || eh.e_ehsize != sizeof(eh) ||
        eh.e_phentsize != sizeof(Elf64_Phdr) || !eh.e_phnum ||
        !nxdl_isRangeValid(size, eh.e_phoff, (uint64_t)eh.e_phnum * sizeof(Elf64_Phdr))) {
        nxdlSetError("expected little-endian AArch64 ET_DYN ELF");
        return NULL;
    }
    NXDLModule* m = calloc(1, sizeof(*m));
    if (!m) {
        nxdlSetError("allocation failed");
        return NULL;
    }
    size_t extent = 0;
    Elf64_Phdr dyn = {0};
    unsigned ndyn = 0;
    for (unsigned i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr p;
        memcpy(&p, (const char*)input + eh.e_phoff + i * sizeof(p), sizeof(p));
        if (p.p_type == PT_TLS || p.p_type == PT_INTERP)
            goto malformed;
        if (p.p_type == PT_DYNAMIC) {
            dyn = p;
            ndyn++;
        }
        if (p.p_type != PT_LOAD)
            continue;
        if (!p.p_memsz || p.p_memsz < p.p_filesz || !nxdl_isRangeValid(size, p.p_offset, p.p_filesz) ||
            !nxdl_isRangeValid(NXDL_MAX_IMAGE_SIZE, p.p_vaddr, p.p_memsz) || !(p.p_flags & PF_R) || (p.p_flags & ~7u) ||
            (p.p_flags & (PF_W | PF_X)) == (PF_W | PF_X) ||
            (p.p_align > 1 && ((p.p_align & (p.p_align - 1)) || p.p_vaddr % p.p_align != p.p_offset % p.p_align)))
            goto malformed;
        size_t end = (p.p_vaddr + p.p_memsz + NXDL_PAGE_SIZE - 1) & ~(size_t)(NXDL_PAGE_SIZE - 1);
        if (end > extent)
            extent = end;
    }
    if (!extent || ndyn != 1 || dyn.p_memsz % sizeof(Elf64_Dyn) || dyn.p_vaddr % 8)
        goto malformed;
    m->perms = calloc(extent / NXDL_PAGE_SIZE, 1);
    if (!m->perms)
        goto malformed;
    /* Reject page-sharing segments: permissions must be unambiguous. */
    for (unsigned i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr p;
        memcpy(&p, (const char*)input + eh.e_phoff + i * sizeof(p), sizeof(p));
        if (p.p_type != PT_LOAD)
            continue;
        for (size_t j = p.p_vaddr / NXDL_PAGE_SIZE; j < (p.p_vaddr + p.p_memsz + NXDL_PAGE_SIZE - 1) / NXDL_PAGE_SIZE;
             j++) {
            if (m->perms[j])
                goto malformed;
            m->perms[j] = p.p_flags;
        }
    }
    if (nxdlMemoryCreate(&m->mem, extent))
        goto fail;
    memset(m->mem.source, 0, extent);
    for (unsigned i = 0; i < eh.e_phnum; i++) {
        Elf64_Phdr p;
        memcpy(&p, (const char*)input + eh.e_phoff + i * sizeof(p), sizeof(p));
        if (p.p_type == PT_LOAD)
            memcpy((char*)m->mem.source + p.p_vaddr, (const char*)input + p.p_offset, p.p_filesz);
    }
    Elf64_Dyn* d = nxdl_getPointer(m, dyn.p_vaddr, dyn.p_memsz);
    if (!d)
        goto malformed;
    uint64_t sym = 0, str = 0, hash = 0, rela = 0, relasz = 0, jmp = 0, jmpsz = 0;
    int terminated = 0;
    uint64_t seen = 0;
    for (size_t i = 0; i < dyn.p_memsz / sizeof(*d); i++) {
        int64_t tag = d[i].d_tag;
        uint64_t v = d[i].d_un.d_val;
        if (tag == DT_NULL) {
            terminated = 1;
            break;
        }
        if (tag >= 0 && tag < 64) {
            if (seen & (1ULL << tag))
                goto malformed;
            seen |= 1ULL << tag;
        }
        switch (tag) {
            case DT_SYMTAB:
                sym = v;
                break;
            case DT_STRTAB:
                str = v;
                break;
            case DT_STRSZ:
                m->strsz = v;
                break;
            case DT_HASH:
                hash = v;
                break;
            case DT_SYMENT:
                if (v != sizeof(Elf64_Sym))
                    goto malformed;
                break;
            case DT_RELA:
                rela = v;
                break;
            case DT_RELASZ:
                relasz = v;
                break;
            case DT_RELAENT:
                if (v != sizeof(Elf64_Rela))
                    goto malformed;
                break;
            case DT_JMPREL:
                jmp = v;
                break;
            case DT_PLTRELSZ:
                jmpsz = v;
                break;
            case DT_PLTREL:
                if (v != DT_RELA)
                    goto malformed;
                break;
            case DT_INIT_ARRAY:
                m->init = v;
                break;
            case DT_INIT_ARRAYSZ:
                m->initsz = v;
                break;
            case DT_FINI_ARRAY:
                m->fini = v;
                break;
            case DT_FINI_ARRAYSZ:
                m->finisz = v;
                break;
            case DT_FLAGS:
                if (v & ~8ULL)
                    goto unsupported;
                break; /* DF_BIND_NOW */
            case DT_NEEDED:
            case DT_REL:
            case DT_RELSZ:
            case DT_RELENT:
            case 35:
            case 36:
            case 37: /* RELR */
            case DT_INIT:
            case DT_FINI:
            case DT_TEXTREL:
            case DT_SYMBOLIC:
            case 0x6ffffff0:
            case 0x6ffffffc:
            case 0x6ffffffe:
                goto unsupported; /* symbol versions */
            default:
                break;
        }
    }
    if (!terminated || !sym || !str || !hash || !m->strsz || sym % 8 || hash % 4)
        goto malformed;
    uint32_t* h = nxdl_getPointer(m, hash, 8);
    if (!h || !h[0] || !h[1])
        goto malformed;
    m->nsyms = h[1];
    if (!nxdl_getPointer(m, hash, 8 + ((uint64_t)h[0] + h[1]) * 4))
        goto malformed;
    m->syms = nxdl_getPointer(m, sym, (uint64_t)m->nsyms * sizeof(Elf64_Sym));
    m->strings = nxdl_getPointer(m, str, m->strsz);
    if (!m->syms || !m->strings)
        goto malformed;
    for (size_t i = 0; i < m->nsyms; i++) {
        Elf64_Sym* s = m->syms + i;
        if (!nxdl_getSymbolName(m, s) || ELF64_ST_TYPE(s->st_info) == STT_TLS || ELF64_ST_TYPE(s->st_info) == 10 ||
            (s->st_shndx != SHN_UNDEF && s->st_shndx != SHN_ABS && !nxdl_getSymbolAddress(m, s)))
            goto malformed;
    }
    int r = nxdl_applyRelocations(m, rela, relasz, resolver, user);
    if (r == -2)
        goto fail;
    if (r)
        goto malformed;
    if (jmpsz && relasz && jmp < rela + relasz && rela < jmp + jmpsz)
        goto malformed;
    r = nxdl_applyRelocations(m, jmp, jmpsz, resolver, user);
    if (r == -2)
        goto fail;
    if (r)
        goto malformed;
    if (nxdl_validateFunctions(m, m->init, m->initsz) || nxdl_validateFunctions(m, m->fini, m->finisz))
        goto malformed;
    if (nxdlMemoryPublish(&m->mem, m->perms))
        goto fail;
    m->syms = (Elf64_Sym*)((char*)m->mem.base + sym);
    m->strings = (const char*)m->mem.base + str;
    m->next = g_Modules;
    g_Modules = m;
    nxdl_callInitFiniArray(m, m->init, m->initsz, 0);
    m->initialized = 1;
    return m;
unsupported:
    nxdlSetError("unsupported ELF feature (dependencies, TLS, versions, REL/RELR or legacy init/fini)");
    goto fail;
malformed:
    nxdlSetError("malformed or unsupported ELF layout");
fail:
    if (nxdlMemoryClose(&m->mem)) {
        /* Keep ownership reachable if the kernel refuses to unmap. */
        m->next = g_Modules;
        g_Modules = m;
        nxdlSetError("cleanup failed; retained mapping; restart application");
        return NULL;
    }
    free(m->perms);
    free(m);
    return NULL;
}

int nxdlClose(void* handle) {
    NXDLModule* m = handle;
    if (!nxdl_isValidHandle(m)) {
        nxdlSetError("invalid handle");
        return -1;
    }
    if (m->initialized) {
        m->initialized = 0;
        nxdl_callInitFiniArray(m, m->fini, m->finisz, 1);
    }
    if (nxdlMemoryClose(&m->mem))
        return -1;
    NXDLModule** p = &g_Modules;
    while (*p != m)
        p = &(*p)->next;
    *p = m->next;
    free(m->perms);
    free(m);
    return 0;
}

void* nxdlOpen(const char* path, int flags, NxdlResolver resolver, void* user) {
    if (!path) {
        nxdlSetError("path required");
        return NULL;
    }
    FILE* f = fopen(path, "rb");
    if (!f) {
        nxdlSetError("cannot open: %s", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END)) {
        fclose(f);
        nxdlSetError("seek failed");
        return NULL;
    }
    long n = ftell(f);
    if (n <= 0 || (unsigned long)n > NXDL_MAX_IMAGE_SIZE || fseek(f, 0, SEEK_SET)) {
        fclose(f);
        nxdlSetError("invalid file size");
        return NULL;
    }
    void* data = malloc(n);
    if (!data) {
        fclose(f);
        nxdlSetError("allocation failed");
        return NULL;
    }
    size_t got = fread(data, 1, n, f);
    fclose(f);
    void* h = NULL;
    if (got == (size_t)n)
        h = nxdlMap(data, n, flags, resolver, user);
    else
        nxdlSetError("short read");
    free(data);
    return h;
}
