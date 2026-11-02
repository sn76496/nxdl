/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef NXDL_DLFCN_H
#define NXDL_DLFCN_H
#include "nxdl.h"
#define RTLD_NOW NXDL_NOW
#define RTLD_LOCAL NXDL_LOCAL
#ifdef __cplusplus
extern "C" {
#endif
void* dlopen(const char* path, int flags);
void* dlsym(void* handle, const char* name);
int dlclose(void* handle);
char* dlerror(void);
/* Override this weak function to expose statically linked host symbols. */
void* nxdlProgramResolver(const char* name);
#ifdef __cplusplus
}

#endif
#endif
