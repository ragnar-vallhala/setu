/* liblzma configuration for Wine's PE build: decoders only, no threads.
 * Written by hand from what XZ Utils' configure reports for
 * --host=x86_64-w64-mingw32 --disable-encoders --disable-threads. */

#define ASSUME_RAM 128
#define HAVE_CHECK_CRC32 1
#define HAVE_CHECK_CRC64 1
#define HAVE_CHECK_SHA256 1
#define HAVE_DECODERS 1
#define HAVE_DECODER_ARM 1
#define HAVE_DECODER_ARM64 1
#define HAVE_DECODER_ARMTHUMB 1
#define HAVE_DECODER_DELTA 1
#define HAVE_DECODER_IA64 1
#define HAVE_DECODER_LZMA1 1
#define HAVE_DECODER_LZMA2 1
#define HAVE_DECODER_POWERPC 1
#define HAVE_DECODER_RISCV 1
#define HAVE_DECODER_SPARC 1
#define HAVE_DECODER_X86 1
#define HAVE_LZIP_DECODER 1
#define HAVE_INTTYPES_H 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDINT_H 1
#define HAVE__BOOL 1
#define HAVE_VISIBILITY 0
#define HAVE___BUILTIN_ASSUME_ALIGNED 1
#define HAVE___BUILTIN_BSWAPXX 1
#define NDEBUG 1
#define PACKAGE "xz"
#define PACKAGE_BUGREPORT "xz@tukaani.org"
#define PACKAGE_NAME "XZ Utils"
#define PACKAGE_STRING "XZ Utils 5.8.4"
#define PACKAGE_TARNAME "xz"
#define PACKAGE_URL "https://tukaani.org/xz/"
#define PACKAGE_VERSION "5.8.4"
#ifdef _WIN64
#define SIZEOF_SIZE_T 8
#else
#define SIZEOF_SIZE_T 4
#endif
#define TUKLIB_FAST_UNALIGNED_ACCESS 1
#define VERSION "5.8.4"
