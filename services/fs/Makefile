# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Drive-letter service (README.md). setu-mountctl.exe builds with mingw-w64;
# the rest are scripts and config files.
#   make -C services/fs                       # build/services/fs/setu-mountctl.exe
#   make -C services/fs install DESTDIR=DIR   # the image's files, under DIR
# distro/build-image.sh runs the install target.

TOOLCHAIN ?= $(HOME)/.local/setu-toolchain/bin
export PATH := $(TOOLCHAIN):$(PATH)

CC      := x86_64-w64-mingw32-gcc
OUT     := ../../build/services/fs
CFLAGS  := -O2 -Wall -Wextra -Werror -std=gnu11
LDFLAGS := -static-libgcc -s

all: $(OUT)/setu-mountctl.exe

$(OUT)/setu-mountctl.exe: setu-mountctl.c | $(OUT)
	$(CC) $(CFLAGS) -mconsole -o $@ $< $(LDFLAGS)

$(OUT):
	mkdir -p $@

install: all
	test -n "$(DESTDIR)"
	install -D -m 0755 setu-drives $(DESTDIR)/usr/local/sbin/setu-drives
	install -D -m 0755 setu-drives-sync $(DESTDIR)/usr/local/bin/setu-drives-sync
	install -D -m 0644 drives.conf $(DESTDIR)/etc/setu/drives.conf
	install -D -m 0644 setu-drives.service $(DESTDIR)/etc/systemd/system/setu-drives.service
	install -D -m 0644 setu-volume@.service $(DESTDIR)/etc/systemd/system/setu-volume@.service
	install -D -m 0644 71-setu-drives.rules $(DESTDIR)/etc/udev/rules.d/71-setu-drives.rules
	install -D -m 0644 $(OUT)/setu-mountctl.exe $(DESTDIR)/opt/setu/bin/setu-mountctl.exe

clean:
	rm -f $(OUT)/setu-mountctl.exe

.PHONY: all install clean
