/* libarchive configuration for Wine's PE build (read side only).
 * Written by hand from what libarchive's configure reports for
 * --host=x86_64-w64-mingw32, keeping only what Wine's msvcrt headers
 * provide, plus the bundled decoders (libs/zlib, libs/bzip2, libs/lzma,
 * libs/zstd). No iconv: names are converted with the Win32 code page
 * functions (archive_string.c). No crypto: encrypted entries are reported
 * as such and not decrypted. */

#define HAVE_CTIME_S 1
#define HAVE_CTYPE_H 1
#define HAVE_DECL_INT32_MAX 1
#define HAVE_DECL_INT32_MIN 1
#define HAVE_DECL_INT64_MAX 1
#define HAVE_DECL_INT64_MIN 1
#define HAVE_DECL_INTMAX_MAX 1
#define HAVE_DECL_INTMAX_MIN 1
#define HAVE_DECL_SIZE_MAX 1
#define HAVE_DECL_STRERROR_R 0
#define HAVE_DECL_UINT32_MAX 1
#define HAVE_DECL_UINT64_MAX 1
#define HAVE_DECL_UINTMAX_MAX 1
#define HAVE_DIRECT_H 1
#define HAVE_EILSEQ 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_GMTIME_S 1
#define HAVE_INTMAX_T 1
#define HAVE_INTTYPES_H 1
#define HAVE_IO_H 1
#define HAVE_LIMITS_H 1
#define HAVE_LOCALE_H 1
#define HAVE_LONG_LONG_INT 1
#define HAVE_MBRTOWC 1
#define HAVE_MEMMOVE 1
#define HAVE_MEMSET 1
#define HAVE_SETLOCALE 1
#define HAVE_SIGNAL_H 1
#define HAVE_STDARG_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRCHR 1
#define HAVE_STRDUP 1
#define HAVE_STRERROR 1
#define HAVE_STRFTIME 1
#define HAVE_STRING_H 1
#define HAVE_STRNCPY_S 1
#define HAVE_STRNLEN 1
#define HAVE_STRRCHR 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIME_H 1
#define HAVE_TZSET 1
#define HAVE_UINTMAX_T 1
#define HAVE_UNSIGNED_LONG_LONG 1
#define HAVE_UNSIGNED_LONG_LONG_INT 1
#define HAVE_VPRINTF 1
#define HAVE_WCHAR_H 1
#define HAVE_WCHAR_T 1
#define HAVE_WCRTOMB 1
#define HAVE_WCSCMP 1
#define HAVE_WCSCPY 1
#define HAVE_WCSLEN 1
#define HAVE_WCTOMB 1
#define HAVE_WCTYPE_H 1
#define HAVE_WINDOWS_H 1
#define HAVE_WINIOCTL_H 1
#define HAVE_WMEMCMP 1
#define HAVE_WMEMCPY 1
#define HAVE_WMEMMOVE 1
#define HAVE__FSEEKI64 1
#define HAVE__MKGMTIME 1
#define HAVE_VISIBILITY 0
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_WCHAR_T 2
#define STDC_HEADERS 1

/* the bundled decoders */
#define HAVE_ZLIB_H 1
#define HAVE_LIBZ 1
#define HAVE_BZLIB_H 1
#define HAVE_LIBBZ2 1
#define HAVE_LZMA_H 1
#define HAVE_LIBLZMA 1
#define HAVE_ZSTD_H 1
#define HAVE_LIBZSTD 1

#define LIBARCHIVE_VERSION_NUMBER "3008011"
#define LIBARCHIVE_VERSION_STRING "3.8.11"
#define PACKAGE "libarchive"
#define PACKAGE_NAME "libarchive"
#define PACKAGE_STRING "libarchive 3.8.11"
#define PACKAGE_TARNAME "libarchive"
#define PACKAGE_VERSION "3.8.11"
#define VERSION "3.8.11"

#define gid_t short
#define id_t unsigned long
#define uid_t short

/* Wine's zlib is built with Z_SOLO, which has no default allocator, and
 * libarchive leaves zalloc/zfree NULL. Route its inflate initialisation
 * through zlib_alloc.c, which fills them in. */
#define inflateInit_ libarchive_inflateInit_
#define inflateInit2_ libarchive_inflateInit2_

/* archive_random.c uses BCryptGenRandom; Wine's bcrypt.h has no
 * BCRYPT_SUCCESS and its fallback needs STATUS_SUCCESS from ntstatus.h. */
#define BCRYPT_SUCCESS(status) (((LONG)(status)) >= 0)
