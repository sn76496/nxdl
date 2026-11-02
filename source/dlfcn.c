/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "dlfcn.h"

__attribute__((weak)) void* nxdlProgramResolver(const char* name) {
    (void)name;
    return 0;
}

static void* resolve(const char* name, void* user) {
    (void)user;
    return nxdlProgramResolver(name);
}

void* dlopen(const char* path, int flags) {
    return nxdlOpen(path, flags, resolve, 0);
}

void* dlsym(void* h, const char* name) {
    return nxdlSym(h, name);
}

int dlclose(void* h) {
    return nxdlClose(h);
}

char* dlerror(void) {
    return (char*)nxdlError();
}
