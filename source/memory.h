/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void* source;
    void* base;
    void* reservation;
    size_t size;
    int mapped;
} NxdlMemory;

int nxdlMemoryCreate(NxdlMemory* m, size_t size);
int nxdlMemoryPublish(NxdlMemory* m, const uint8_t* perms);
int nxdlMemoryClose(NxdlMemory* m);
void nxdlSetError(const char* format, ...);
