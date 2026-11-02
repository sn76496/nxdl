/**
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

extern int host_add(int, int);
#ifdef NXDL_LIFECYCLE
int counter = 100;
extern void host_event(int);
__attribute__((constructor)) static void initialize(void) {
    counter = 7;
    host_event(1);
}

__attribute__((destructor)) static void finalize(void) {
    host_event(2);
}

#else
int counter = 7;
#endif
int* counter_address = &counter;
int plugin_add(int a, int b) {
    return host_add(a, b) + (*counter_address)++;
}

static int private_counter = 13;
int* private_address = &private_counter;
extern int optional_value __attribute__((weak));
int* weak_address = &optional_value;
