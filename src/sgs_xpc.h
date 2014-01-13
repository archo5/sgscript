
#ifndef SGS_XPC_H_INCLUDED
#define SGS_XPC_H_INCLUDED

#ifdef _MSC_VER
#  pragma warning( disable: 4996 )
#  if _MSC_VER >= 1600
#    include <stdint.h>
#  else
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#  endif
#  define SGS_INLINE
#  define PRId32 "d"
#  define PRId64 "lld"
#  define snprintf _snprintf
#  define SGS_VSPRINTF_LEN( str, args ) _vscprintf( str, args )
#else
#  include <inttypes.h>
#  define SGS_INLINE inline
#  define SGS_VSPRINTF_LEN( str, args ) vsnprintf( NULL, 0, str, args )
#endif

#define UNUSED( x ) (void)(x)

#ifdef __GNUC__
#  define SGS_ASSUME_ALIGNED __builtin_assume_aligned
#else
#  define SGS_ASSUME_ALIGNED( x, a ) (x)
#endif

#ifdef __cplusplus
#define SGS_DECLARE extern
#else
#define SGS_DECLARE static
#endif


#if SGS_DLL && !defined( __GNUC__ )
#  if BUILDING_SGS
#    define SGS_APIFUNC __declspec(dllexport)
#  else
#    define SGS_APIFUNC __declspec(dllimport)
#  endif
#else
#  define SGS_APIFUNC
#endif


#ifdef SGS_USE_FILESYSTEM
#  include <sys/types.h>
#  include <sys/stat.h>

#  ifdef _WIN32
#    include <direct.h>
#    define getcwd _getcwd
#    define mkdir _mkdir
#    define rmdir _rmdir
#    define stat _stat
#    include "../ext/msvc/dirent.h"
#  else
#    include <unistd.h>
#    include <dirent.h>
#  endif
#endif

#ifdef _WIN32
#  define SGS_MODULE_EXT ".dll"
#else
#  define SGS_MODULE_EXT ".so"
#endif

/* basic platform info */
#if _WIN32 || __WIN32__ || __WINDOWS__
#  define SGS_OS_TYPE "Windows"
#elif __APPLE__
#  define SGS_OS_TYPE "Mac OS X"
#elif __ANDROID__
#  define SGS_OS_TYPE "Android"
#elif __linux || __linux__
#  define SGS_OS_TYPE "Linux"
#elif __unix || __unix__
#  define SGS_OS_TYPE "Unix"
#elif __posix
#  define SGS_OS_TYPE "POSIX"
#else
#  define SGS_OS_TYPE "Unknown"
#endif

/* simplified architecture info */
#if __arm__ || __thumb__ || _M_ARM || _M_ARMT
#  define SGS_ARCH_ARM 1
#endif
#if i386 || __i386 || __i386 || _M_IX86 || _X86_ || __X86__
#  define SGS_ARCH_X86 1
#endif
#if __amd64__ || __amd64 || __x86_64__ || __x86_64 || _M_X64 || _M_AMD64
#  define SGS_ARCH_AMD64 1
#endif
#if __ia64__ || _IA64 || __IA64__ || _M_IA64
#  define SGS_ARCH_IA64 1
#endif
#if __powerpc || __powerpc__ || __powerpc64__ || \
	__ppc__ || __ppc64 || _M_PPC || _XENON
#  define SGS_ARCH_PPC 1
#endif


/* http://stackoverflow.com/a/2103095/1648140 */

enum
{
    O32_LITTLE_ENDIAN = 0x03020100ul,
    O32_BIG_ENDIAN = 0x00010203ul
};

static const union { unsigned char bytes[4]; uint32_t value; } o32_host_order =
    { { 0, 1, 2, 3 } };

#define O32_HOST_ORDER (o32_host_order.value)


#ifdef __cplusplus
extern "C" {
#endif

#define SGS_XPC_NOFILE -1
#define SGS_XPC_NOPROC -2
#define SGS_XPC_NOTSUP -3
#define SGS_XPC_NOTLIB -4
#define SGS_XPC_LDFAIL -5
int sgs_GetProcAddress( const char* file, const char* proc, void** out );

#ifdef __cplusplus
}
#endif


#endif /* SGS_XPC_H_INCLUDED */
