#!/usr/bin/env bash
# SPDX-License-Identifier: MIT OR Apache-2.0
#
# Import the read side of libarchive and the decoders it needs into the Wine
# fork as bundled static PE libraries, the way Wine bundles zlib, libjpeg and
# libxml2 under personality/wine/libs:
#
#   libs/libarchive  libarchive 3.8.11  (BSD-2-Clause)  read side only
#   libs/lzma        liblzma (XZ Utils 5.8.4, 0BSD)     decoders only
#   libs/bzip2       bzip2 1.0.8  (bzip2 licence, BSD-style)
#   libs/zstd        Zstandard 1.5.7  (BSD-3-Clause)    decoder only
#
# zlib is Wine's own libs/zlib. Only zipfldr.dll links these libraries.
#
# The tarballs are pinned by SHA-256 below. The script downloads them into a
# temporary directory, checks the hashes, and copies the listed files over
# the existing trees (the hand-written config.h files and Makefile.in files
# are kept). Run it again after changing a version; nothing is patched.
#
# Usage: tools/import-libarchive.sh [download-dir]
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
LIBS="$REPO/personality/wine/libs"
DL="${1:-$(mktemp -d)}"

LIBARCHIVE_URL=https://github.com/libarchive/libarchive/releases/download/v3.8.11/libarchive-3.8.11.tar.xz
LIBARCHIVE_SHA256=a3dc0872b1b39da34dc18ef1b944f0f11ffd37425d70ae04be679279f1cf43d2
XZ_URL=https://github.com/tukaani-project/xz/releases/download/v5.8.4/xz-5.8.4.tar.xz
XZ_SHA256=4ce24038fd4221e0d13bc1a2de7a4db56e90b92b3bf75321f6c14be73f65de4b
BZIP2_URL=https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
BZIP2_SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
ZSTD_URL=https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
ZSTD_SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3

fetch() # url sha256 -> unpacked directory on stdout
{
  local url="$1" sum="$2" file="$DL/${1##*/}" dir
  [ -f "$file" ] || curl -fsSL --retry 5 -o "$file" "$url"
  echo "$sum  $file" | sha256sum -c --quiet - >&2
  dir="$DL/src-${sum:0:12}"
  if [ ! -d "$dir" ]; then mkdir -p "$dir"; tar -C "$dir" --strip-components=1 -xf "$file"; fi
  echo "$dir"
}

copy() # srcdir dstdir files...
{
  local src="$1" dst="$2" f; shift 2
  for f in "$@"; do
    mkdir -p "$dst/$(dirname "$f")"
    cp "$src/$f" "$dst/$f"
  done
}

# ---- bzip2 ----
src="$(fetch "$BZIP2_URL" "$BZIP2_SHA256")"
copy "$src" "$LIBS/bzip2" LICENSE blocksort.c bzlib.c bzlib.h bzlib_private.h compress.c crctable.c \
  decompress.c huffman.c randtable.c

# ---- zstd (decompression) ----
src="$(fetch "$ZSTD_URL" "$ZSTD_SHA256")"
copy "$src" "$LIBS/zstd" LICENSE lib/zstd.h lib/zstd_errors.h \
  lib/common/allocations.h lib/common/bits.h lib/common/bitstream.h lib/common/compiler.h \
  lib/common/cpu.h lib/common/debug.c lib/common/debug.h lib/common/entropy_common.c \
  lib/common/error_private.c lib/common/error_private.h lib/common/fse.h lib/common/fse_decompress.c \
  lib/common/huf.h lib/common/mem.h lib/common/portability_macros.h lib/common/xxhash.c \
  lib/common/xxhash.h lib/common/zstd_common.c lib/common/zstd_deps.h lib/common/zstd_internal.h \
  lib/common/zstd_trace.h \
  lib/decompress/huf_decompress.c lib/decompress/zstd_ddict.c lib/decompress/zstd_ddict.h \
  lib/decompress/zstd_decompress.c lib/decompress/zstd_decompress_block.c \
  lib/decompress/zstd_decompress_block.h lib/decompress/zstd_decompress_internal.h

# ---- liblzma (decoders) ----
src="$(fetch "$XZ_URL" "$XZ_SHA256")"
copy "$src" "$LIBS/lzma" COPYING COPYING.0BSD \
  src/common/mythread.h src/common/sysdefs.h src/common/tuklib_common.h src/common/tuklib_config.h \
  src/common/tuklib_integer.h \
  src/liblzma/api/lzma.h \
  $(cd "$src" && ls src/liblzma/api/lzma/*.h) \
  src/liblzma/check/check.c src/liblzma/check/check.h src/liblzma/check/crc32_fast.c \
  src/liblzma/check/crc32_table_le.h src/liblzma/check/crc32_table_be.h src/liblzma/check/crc64_fast.c \
  src/liblzma/check/crc64_table_le.h src/liblzma/check/crc64_table_be.h src/liblzma/check/crc_common.h \
  src/liblzma/check/crc_x86_clmul.h src/liblzma/check/crc32_arm64.h src/liblzma/check/crc32_loongarch.h \
  src/liblzma/check/sha256.c \
  src/liblzma/common/alone_decoder.c src/liblzma/common/alone_decoder.h src/liblzma/common/auto_decoder.c \
  src/liblzma/common/block_decoder.c src/liblzma/common/block_decoder.h \
  src/liblzma/common/block_header_decoder.c src/liblzma/common/block_util.c src/liblzma/common/common.c \
  src/liblzma/common/common.h src/liblzma/common/easy_preset.h src/liblzma/common/filter_common.c \
  src/liblzma/common/filter_common.h src/liblzma/common/filter_decoder.c \
  src/liblzma/common/filter_decoder.h src/liblzma/common/filter_flags_decoder.c \
  src/liblzma/common/index.c src/liblzma/common/index.h src/liblzma/common/index_decoder.c \
  src/liblzma/common/index_decoder.h src/liblzma/common/index_hash.c src/liblzma/common/lzip_decoder.c \
  src/liblzma/common/lzip_decoder.h src/liblzma/common/memcmplen.h src/liblzma/common/stream_decoder.c \
  src/liblzma/common/stream_decoder.h src/liblzma/common/stream_flags_common.c \
  src/liblzma/common/stream_flags_common.h src/liblzma/common/stream_flags_decoder.c \
  src/liblzma/common/vli_decoder.c src/liblzma/common/vli_size.c \
  src/liblzma/delta/delta_common.c src/liblzma/delta/delta_common.h src/liblzma/delta/delta_decoder.c \
  src/liblzma/delta/delta_decoder.h src/liblzma/delta/delta_private.h \
  src/liblzma/lz/lz_decoder.c src/liblzma/lz/lz_decoder.h \
  src/liblzma/lzma/lzma2_decoder.c src/liblzma/lzma/lzma2_decoder.h src/liblzma/lzma/lzma_common.h \
  src/liblzma/lzma/lzma_decoder.c src/liblzma/lzma/lzma_decoder.h \
  src/liblzma/rangecoder/range_common.h src/liblzma/rangecoder/range_decoder.h \
  src/liblzma/simple/arm.c src/liblzma/simple/arm64.c src/liblzma/simple/armthumb.c \
  src/liblzma/simple/ia64.c src/liblzma/simple/powerpc.c src/liblzma/simple/riscv.c \
  src/liblzma/simple/simple_coder.c src/liblzma/simple/simple_coder.h \
  src/liblzma/simple/simple_decoder.c src/liblzma/simple/simple_decoder.h \
  src/liblzma/simple/simple_private.h src/liblzma/simple/sparc.c src/liblzma/simple/x86.c

# ---- libarchive (read side) ----
src="$(fetch "$LIBARCHIVE_URL" "$LIBARCHIVE_SHA256")"
copy "$src" "$LIBS/libarchive" COPYING $(cd "$src" && ls libarchive/*.h | grep -v -e config_freebsd) \
  libarchive/archive_acl.c libarchive/archive_blake2s_ref.c libarchive/archive_blake2sp_ref.c \
  libarchive/archive_check_magic.c libarchive/archive_crc32.c libarchive/archive_entry.c \
  libarchive/archive_entry_sparse.c libarchive/archive_entry_xattr.c libarchive/archive_options.c \
  libarchive/archive_ppmd7.c libarchive/archive_random.c libarchive/archive_rb.c libarchive/archive_read.c \
  libarchive/archive_read_add_passphrase.c libarchive/archive_read_set_options.c \
  libarchive/archive_read_support_filter_bzip2.c libarchive/archive_read_support_filter_uu.c \
  libarchive/archive_read_support_filter_xz.c libarchive/archive_read_support_filter_zstd.c \
  libarchive/archive_read_support_format_7zip.c libarchive/archive_read_support_format_cab.c \
  libarchive/archive_read_support_format_cpio.c libarchive/archive_read_support_format_rar.c \
  libarchive/archive_read_support_format_rar5.c libarchive/archive_read_support_format_raw.c \
  libarchive/archive_string.c libarchive/archive_string_sprintf.c libarchive/archive_time.c \
  libarchive/archive_util.c libarchive/archive_virtual.c libarchive/archive_windows.c
# archive.h includes it on Android only, but makedep wants every include to exist
cp "$src/contrib/android/include/android_lf.h" "$LIBS/libarchive/libarchive/"

echo "imported into $LIBS"
