/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "nxdl.h"
#include <assert.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int add(int a, int b) {
    return a + b;
}

static void* resolve(const char* name, void* user) {
    assert(user == (void*)42);
    return !strcmp(name, "host_add") ? (void*)add : NULL;
}

static void reject(void* data, size_t n) {
    assert(!nxdlMap(data, n, NXDL_NOW, resolve, (void*)42));
    assert(nxdlError());
    assert(!nxdlError());
}

int main(int argc, char** argv) {
    assert(argc == 2);
    FILE* f = fopen(argv[1], "rb");
    assert(f);
    assert(!fseek(f, 0, SEEK_END));
    size_t n = ftell(f);
    rewind(f);
    unsigned char *data = malloc(n), *copy = malloc(n);
    assert(data && copy);
    assert(fread(data, 1, n, f) == n);
    fclose(f);
    void* h = nxdlMap(data, n, NXDL_NOW, resolve, (void*)42);
    if (!h) {
        fprintf(stderr, "%s\n", nxdlError());
        return 1;
    }
    int* counter = nxdlSym(h, "counter");
    assert(counter && *counter == 7);
    int** address = nxdlSym(h, "counter_address");
    assert(address && *address == counter);
    int** private_address = nxdlSym(h, "private_address");
    assert(private_address && **private_address == 13);
    int** weak_address = nxdlSym(h, "weak_address");
    assert(weak_address && !*weak_address);
    *counter = 11;
    assert(**address == 11);
    assert(nxdlSym(h, "plugin_add")); /* AArch64 code cannot execute on this host. */
    assert(!nxdlSym(h, "missing"));
    assert(nxdlError());
    assert(!nxdlClose(h));
    assert(nxdlClose(h) == -1);
    assert(nxdlError());
    assert(!nxdlMap(data, n, NXDL_NOW, NULL, NULL));
    assert(strstr(nxdlError(), "host_add"));
    for (size_t i = 0; i < sizeof(Elf64_Ehdr); i++)
        reject(data, i);
    memcpy(copy, data, n);
    ((Elf64_Ehdr*)copy)->e_machine = EM_386;
    reject(copy, n);
    memcpy(copy, data, n);
    ((Elf64_Ehdr*)copy)->e_phoff = UINT64_MAX;
    reject(copy, n);
    memcpy(copy, data, n);
    Elf64_Ehdr* eh = (Elf64_Ehdr*)copy;
    Elf64_Phdr* ph = (Elf64_Phdr*)(copy + eh->e_phoff);
    for (unsigned i = 0; i < eh->e_phnum; i++)
        if (ph[i].p_type == PT_LOAD) {
            ph[i].p_memsz = UINT64_MAX;
            break;
        }
    reject(copy, n);
    /* Byte mutations probe boundary checks; all valid mappings are closed. */
    for (size_t i = 0; i < n; i += 17) {
        memcpy(copy, data, n);
        copy[i] ^= 0xff;
        h = nxdlMap(copy, n, NXDL_NOW, resolve, (void*)42);
        if (h)
            assert(!nxdlClose(h));
        nxdlError();
    }
    h = nxdlOpen(argv[1], NXDL_NOW, resolve, (void*)42);
    assert(h);
    assert(!nxdlClose(h));
    free(copy);
    free(data);
    puts("ELF load, imports, relative relocations, symbols, close and malformed-input checks passed");
}
