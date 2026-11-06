# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Build the Setu shell with mingw-w64. Output goes to ../build/shell/.
#   make -C shell            # setu-shell.exe and the test helpers
#   make -C shell clean
# The toolchain from tools/bootstrap-toolchain.sh is used if it exists.

TOOLCHAIN ?= $(HOME)/.local/setu-toolchain/bin
export PATH := $(TOOLCHAIN):$(PATH)

CC      := x86_64-w64-mingw32-gcc
OUT     := ../build/shell
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -municode -std=gnu11
LDFLAGS := -static-libgcc -s
# system DLLs only
SHELL_LIBS := -luser32 -lgdi32 -lshell32 -lole32 -luuid -ladvapi32 -lkernel32
PROBE_LIBS := -lshell32 -lole32 -luuid -luser32
XPROC_LIBS := -luser32 -lgdi32

all: $(OUT)/setu-shell.exe $(OUT)/setu-probe.exe $(OUT)/setu-xproc.exe $(OUT)/setu-testapp.exe

$(OUT)/setu-shell.exe: src/setu-shell.c | $(OUT)
	$(CC) $(CFLAGS) -mwindows -o $@ $< $(LDFLAGS) $(SHELL_LIBS)

$(OUT)/setu-probe.exe: tests/setu-probe.c | $(OUT)
	$(CC) $(CFLAGS) -mconsole -o $@ $< $(LDFLAGS) $(PROBE_LIBS)

$(OUT)/setu-xproc.exe: tests/setu-xproc.c | $(OUT)
	$(CC) $(CFLAGS) -mconsole -o $@ $< $(LDFLAGS) $(XPROC_LIBS)

$(OUT)/setu-testapp.exe: tests/setu-testapp.c | $(OUT)
	$(CC) $(CFLAGS) -mwindows -o $@ $< $(LDFLAGS) -luser32 -lgdi32 -lshell32

$(OUT):
	mkdir -p $@

clean:
	rm -f $(OUT)/setu-shell.exe $(OUT)/setu-probe.exe $(OUT)/setu-xproc.exe $(OUT)/setu-testapp.exe

.PHONY: all clean
