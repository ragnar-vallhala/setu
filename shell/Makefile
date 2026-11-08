# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Build the Setu shell with mingw-w64. Output goes to ../build/shell/.
#   make -C shell            # setu-shell.exe, setu-desktop.exe, setu-files.exe, setu-theme.exe and the test helpers
#   make -C shell clean
# The toolchain from tools/bootstrap-toolchain.sh is used if it exists.

TOOLCHAIN ?= $(HOME)/.local/setu-toolchain/bin
export PATH := $(TOOLCHAIN):$(PATH)

CC      := x86_64-w64-mingw32-gcc
OUT     := ../build/shell
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -municode -std=gnu11
LDFLAGS := -static-libgcc -s
# system DLLs only
SHELL_LIBS := -luser32 -lgdi32 -lcomctl32 -lshell32 -lole32 -luuid -ladvapi32 -lkernel32
PROBE_LIBS := -lshell32 -lole32 -luuid -luser32
XPROC_LIBS := -luser32 -lgdi32
FILES_LIBS := -luser32 -lgdi32 -lcomctl32 -lcomdlg32 -lshell32 -lshlwapi -lole32 -luuid -ladvapi32 -lkernel32
FPROBE_LIBS := -luser32 -lshell32 -lshlwapi

all: $(OUT)/setu-shell.exe $(OUT)/setu-probe.exe $(OUT)/setu-xproc.exe $(OUT)/setu-testapp.exe \
     $(OUT)/setu-files.exe $(OUT)/setu-files-probe.exe $(OUT)/setu-theme.exe $(OUT)/setu-desktop.exe

STYLE := src/setu-style.h src/setu-icons.h src/setu-theme.h src/setu-theme.c
THEME_LIBS := -luser32 -lgdi32 -lshell32 -lshlwapi -ladvapi32 -lole32 -lwindowscodecs -luuid -lkernel32

$(OUT)/setu-shell.exe: src/setu-shell.c $(STYLE) | $(OUT)
	$(CC) $(CFLAGS) -mwindows -o $@ $< src/setu-theme.c $(LDFLAGS) $(SHELL_LIBS)

$(OUT)/setu-probe.exe: tests/setu-probe.c | $(OUT)
	$(CC) $(CFLAGS) -mconsole -o $@ $< $(LDFLAGS) $(PROBE_LIBS)

$(OUT)/setu-xproc.exe: tests/setu-xproc.c | $(OUT)
	$(CC) $(CFLAGS) -mconsole -o $@ $< $(LDFLAGS) $(XPROC_LIBS)

$(OUT)/setu-testapp.exe: tests/setu-testapp.c | $(OUT)
	$(CC) $(CFLAGS) -mwindows -o $@ $< $(LDFLAGS) -luser32 -lgdi32 -lshell32

$(OUT)/setu-files.exe: src/setu-files.c $(STYLE) | $(OUT)
	$(CC) $(CFLAGS) -mwindows -o $@ $< src/setu-theme.c $(LDFLAGS) $(FILES_LIBS)

DESK_LIBS := -luser32 -lgdi32 -lcomctl32 -lshell32 -lshlwapi -lole32 -luuid -ladvapi32 -lwindowscodecs -lkernel32

$(OUT)/setu-desktop.exe: src/setu-desktop.c $(STYLE) | $(OUT)
	$(CC) $(CFLAGS) -mwindows -o $@ $< src/setu-theme.c $(LDFLAGS) $(DESK_LIBS)

$(OUT)/setu-theme.exe: src/setu-theme-tool.c src/setu-theme.c src/setu-theme.h | $(OUT)
	$(CC) $(CFLAGS) -mconsole -o $@ $< src/setu-theme.c $(LDFLAGS) $(THEME_LIBS)

$(OUT)/setu-files-probe.exe: tests/setu-files-probe.c | $(OUT)
	$(CC) $(CFLAGS) -mconsole -o $@ $< $(LDFLAGS) $(FPROBE_LIBS)

$(OUT):
	mkdir -p $@

clean:
	rm -f $(OUT)/setu-shell.exe $(OUT)/setu-probe.exe $(OUT)/setu-xproc.exe $(OUT)/setu-testapp.exe \
	      $(OUT)/setu-files.exe $(OUT)/setu-files-probe.exe $(OUT)/setu-theme.exe $(OUT)/setu-desktop.exe

.PHONY: all clean
