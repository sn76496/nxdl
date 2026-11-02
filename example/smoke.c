/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <switch.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include "dlfcn.h"

static FILE* output;
static int event;
static void log_result(const char* format, ...) {
    va_list ap;
    va_start(ap, format);
    vprintf(format, ap);
    va_end(ap);
    if (output) {
        va_start(ap, format);
        vfprintf(output, format, ap);
        va_end(ap);
        fflush(output);
    }
}

static int host_add(int a, int b) {
    return a + b;
}

static void host_event(int value) {
    event = value;
}

void* nxdlProgramResolver(const char* name) {
    if (!strcmp(name, "host_add"))
        return (void*)host_add;
    if (!strcmp(name, "host_event"))
        return (void*)host_event;
    return NULL;
}

int main(void) {
    consoleInit(NULL);
    PadState pad;
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
    mkdir("sdmc:/switch/nxdl", 0777);
    output = fopen("sdmc:/switch/nxdl/smoke-output.txt", "w");
    if (!output)
        log_result("FAIL: cannot create smoke-output.txt\n");
    log_result("NXDL standalone ELF smoke test v2 (prepare before map)\n");
    int passed = output != NULL;
    void* h = dlopen("sdmc:/switch/nxdl/plugin-console.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        log_result("FAIL load: %s\n", dlerror());
        passed = 0;
    } else {
        int (*fn)(int, int) = dlsym(h, "plugin_add");
        int* counter = dlsym(h, "counter");
        int** private_address = dlsym(h, "private_address");
        int** weak_address = dlsym(h, "weak_address");
        int a = fn ? fn(2, 3) : -1, b = fn ? fn(2, 3) : -1;
        int ok = a == 12 && b == 13 && counter && *counter == 9 && private_address && **private_address == 13 &&
                 weak_address && !*weak_address && event == 1;
        passed &= ok;
        log_result("%s: calls=%d,%d counter=%d relative=%d weak=%s init=%d\n", ok ? "PASS" : "FAIL", a, b,
                   counter ? *counter : -1, private_address ? **private_address : -1,
                   weak_address && !*weak_address ? "NULL" : "FAIL", event);
        if (dlclose(h)) {
            log_result("FAIL close: %s\n", dlerror());
            passed = 0;
        } else {
            log_result("%s: close and finalizer event=%d\n", event == 2 ? "PASS" : "FAIL", event);
            passed &= event == 2;
        }
    }
    log_result("RESULT: %s\n", passed ? "PASS" : "FAIL");
    log_result("Output: sdmc:/switch/nxdl/smoke-output.txt\nPress + to exit\n");
    if (output) {
        fclose(output);
        output = NULL;
    }
    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus)
            break;
        consoleUpdate(NULL);
    }
    consoleExit(NULL);
    return passed ? 0 : 1;
}
