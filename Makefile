# SPDX-License-Identifier: MPL-2.0

DEVKITPRO ?= /opt/devkitpro
PREFIX ?= $(DEVKITPRO)/portlibs/switch
DESTDIR ?=
INSTALL ?= install
CLANG_FORMAT ?= clang-format
FORMAT_FILES := $(wildcard source/*.c source/*.h include/*.h example/*.c tests/*.c)
CC := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-gcc
AR := $(DEVKITPRO)/devkitA64/bin/aarch64-none-elf-ar
CFLAGS := -std=gnu11 -O2 -g -Wall -Wextra -Werror -march=armv8-a -mtp=soft -fPIC -Iinclude -I$(DEVKITPRO)/libnx/include -D__SWITCH__
OBJ := build/loader.o build/memory.o build/dlfcn.o
all: build/libdl.a build/plugin.so
build:
	mkdir -p $@
build/%.o: source/%.c source/memory.h include/nxdl.h include/dlfcn.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/libdl.a: $(OBJ)
	$(AR) rcs $@ $^
build/plugin.so: example/plugin.c example/exports.map | build
	$(CC) -fPIC -shared -nostdlib -Wl,--version-script=example/exports.map,--hash-style=sysv,-z,max-page-size=4096,-z,separate-code $< -o $@
check: build/plugin.so
	cc -std=gnu11 -g -Wall -Wextra -Werror -fsanitize=address,undefined -DNXDL_HOST_TEST -Iinclude source/loader.c source/memory.c tests/test.c -o build/test
	ASAN_OPTIONS=detect_leaks=0 ./build/test build/plugin.so
clean:
	rm -rf build
.PHONY: all check clean
build/smoke.elf: example/smoke.c build/libdl.a
	$(CC) $(CFLAGS) -specs=$(DEVKITPRO)/libnx/switch.specs $< build/libdl.a -L$(DEVKITPRO)/libnx/lib -lnx -o $@
build/smoke.nro: build/smoke.elf
	$(DEVKITPRO)/tools/bin/elf2nro $< $@
build/plugin-console.so: example/plugin.c example/exports.map | build
	$(CC) -DNXDL_LIFECYCLE -fPIC -shared -nostdlib -Wl,--version-script=example/exports.map,--hash-style=sysv,-z,max-page-size=4096,-z,separate-code $< -o $@
smoke: build/smoke.nro build/plugin-console.so
.PHONY: smoke

install: build/libdl.a LICENSE NOTICE.md
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/lib" "$(DESTDIR)$(PREFIX)/include"
	$(INSTALL) -m 644 build/libdl.a "$(DESTDIR)$(PREFIX)/lib/libdl.a"
	$(INSTALL) -m 644 include/dlfcn.h include/nxdl.h "$(DESTDIR)$(PREFIX)/include/"
	$(INSTALL) -d "$(DESTDIR)$(PREFIX)/share/licenses/nxdl"
	$(INSTALL) -m 644 LICENSE NOTICE.md "$(DESTDIR)$(PREFIX)/share/licenses/nxdl/"
.PHONY: install

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)
format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)
.PHONY: format format-check
