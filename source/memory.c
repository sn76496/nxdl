/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "memory.h"
#include <stdlib.h>
#ifdef NXDL_HOST_TEST
#include <sys/mman.h>
#include <unistd.h>

int nxdlMemoryCreate(NxdlMemory* m, size_t size) {
    char path[] = "/tmp/nxdl-memory-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) {
        nxdlSetError("temporary backing file failed");
        return -1;
    }
    unlink(path);
    if (ftruncate(fd, size)) {
        close(fd);
        nxdlSetError("backing resize failed");
        return -1;
    }
    m->size = size;
    m->source = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    m->base = mmap(NULL, size, PROT_NONE, MAP_SHARED, fd, 0);
    close(fd);
    if (m->source == MAP_FAILED || m->base == MAP_FAILED) {
        if (m->source != MAP_FAILED)
            munmap(m->source, size);
        if (m->base != MAP_FAILED)
            munmap(m->base, size);
        m->source = m->base = NULL;
        nxdlSetError("mmap failed");
        return -1;
    }
    return 0;
}

int nxdlMemoryPublish(NxdlMemory* m, const uint8_t* perms) {
    for (size_t p = 0; p < m->size / 4096; p++) {
        int f = ((perms[p] & 4) ? PROT_READ : 0) | ((perms[p] & 2) ? PROT_WRITE : 0) | ((perms[p] & 1) ? PROT_EXEC : 0);
        if (mprotect((char*)m->base + p * 4096, 4096, f)) {
            nxdlSetError("mprotect failed");
            return -1;
        }
    }
    if (mprotect(m->source, m->size, PROT_NONE)) {
        nxdlSetError("source protection failed");
        return -1;
    }
    m->mapped = 1;
    return 0;
}

int nxdlMemoryClose(NxdlMemory* m) {
    if (m->base && munmap(m->base, m->size))
        return -1;
    if (m->source && munmap(m->source, m->size))
        return -1;
    return 0;
}

#else
#include <switch.h>

int nxdlMemoryCreate(NxdlMemory* m, size_t size) {
    if (envGetOwnProcessHandle() == INVALID_HANDLE || !envIsSyscallHinted(0x73) || !envIsSyscallHinted(0x77) ||
        !envIsSyscallHinted(0x78)) {
        nxdlSetError("process handle and SVCs 0x73/0x77/0x78 required");
        return -1;
    }
    m->source = aligned_alloc(4096, size);
    if (!m->source) {
        nxdlSetError("backing allocation failed");
        return -1;
    }
    m->size = size;
    /* Reserve the eventual load bias while the backing allocation stays writable. */
    virtmemLock();
    m->base = virtmemFindCodeMemory(size, 4096);
    if (m->base)
        m->reservation = virtmemAddReservation(m->base, size);
    virtmemUnlock();
    if (!m->reservation) {
        free(m->source);
        m->source = NULL;
        m->base = NULL;
        nxdlSetError("code address reservation failed");
        return -1;
    }
    return 0;
}

int nxdlMemoryPublish(NxdlMemory* m, const uint8_t* perms) {
    armDCacheFlush(m->source, m->size);
    virtmemLock();
    Result r = svcMapProcessCodeMemory(envGetOwnProcessHandle(), (u64)m->base, (u64)m->source, m->size);
    if (R_SUCCEEDED(r)) {
        m->mapped = 1;
        virtmemRemoveReservation(m->reservation);
        m->reservation = NULL;
    }
    virtmemUnlock();
    if (R_FAILED(r)) {
        nxdlSetError("map failed: 0x%08x", r);
        return -1;
    }
    for (size_t p = 0; p < m->size / 4096;) {
        size_t end = p + 1;
        while (end < m->size / 4096 && perms[end] == perms[p])
            end++;
        uint32_t perm = ((perms[p] & 4) ? Perm_R : 0) | ((perms[p] & 2) ? Perm_W : 0) | ((perms[p] & 1) ? Perm_X : 0);
        Result r =
            svcSetProcessMemoryPermission(envGetOwnProcessHandle(), (u64)m->base + p * 4096, (end - p) * 4096, perm);
        if (R_FAILED(r)) {
            MemoryInfo info = {0};
            u32 page_info = 0;
            svcQueryMemory(&info, &page_info, (u64)m->base + p * 4096);
            nxdlSetError("segment permission failed: 0x%08x offset=0x%zx size=0x%zx perm=%u state=0x%x oldperm=%u", r,
                         p * 4096, (end - p) * 4096, perm, info.type, info.perm);
            return -1;
        }
        p = end;
    }
    /* Cache maintenance faults on Perm_None gaps between ELF segments. */
    for (size_t p = 0; p < m->size / 4096;) {
        if (!(perms[p] & 1)) {
            ++p;
            continue;
        }
        size_t end = p + 1;
        while (end < m->size / 4096 && (perms[end] & 1))
            end++;
        armICacheInvalidate((char*)m->base + p * 4096, (end - p) * 4096);
        p = end;
    }
    return 0;
}

int nxdlMemoryClose(NxdlMemory* m) {
    if (m->mapped) {
        Result r = svcUnmapProcessCodeMemory(envGetOwnProcessHandle(), (u64)m->base, (u64)m->source, m->size);
        if (R_FAILED(r)) {
            nxdlSetError("unmap failed: 0x%08x", r);
            return -1;
        }
    }
    if (m->reservation) {
        virtmemLock();
        virtmemRemoveReservation(m->reservation);
        virtmemUnlock();
    }
    free(m->source);
    return 0;
}

#endif
