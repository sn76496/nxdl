/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef NXDL_H
#define NXDL_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NXDL_NOW 2
#define NXDL_LOCAL 0

/* Serialize all loader calls, and stop module execution before closing it. */
typedef void* (*NxdlResolver)(const char* name, void* user);
void* nxdlMap(const void* elf, size_t size, int flags, NxdlResolver resolver, void* user);
void* nxdlOpen(const char* path, int flags, NxdlResolver resolver, void* user);
void* nxdlSym(void* handle, const char* name);
int nxdlClose(void* handle);
const char* nxdlError(void);
#ifdef __cplusplus
}

#endif
#endif
