
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

#define SGS_UNUSED( x ) (void)(x)

#if defined(__GNUC__) && ( __GNUC__ > 4 || ( __GNUC__ == 4 && __GNUC_MINOR__ >= 7 ) )
#  define SGS_ASSUME_ALIGNED __builtin_assume_aligned
#else
#  define SGS_ASSUME_ALIGNED( x, a ) (x)
#endif

#ifdef __cplusplus
#define SGS_DECLARE extern
#else
#define SGS_DECLARE static
#endif


#ifdef SGS_COMPILE_MODULE
#  define BUILDING_SGS 1
#endif

#if SGS_DLL && defined( _WIN32 )
#  if BUILDING_SGS
#    define SGS_APIFUNC __declspec(dllexport)
#  else
#    define SGS_APIFUNC __declspec(dllimport)
#  endif
#else
#  define SGS_APIFUNC
#endif

#if SGS_DLL
#  define SGS_IF_DLL( dll, nodll ) dll
#else
#  define SGS_IF_DLL( dll, nodll ) nodll
#endif


#ifdef SGS_USE_FILESYSTEM
#  include <sys/types.h>
#  include <sys/stat.h>

#  ifdef _WIN32
#    if defined(WINAPI_FAMILY) && (WINAPI_FAMILY == WINAPI_FAMILY_PC_APP || WINAPI_FAMILY == WINAPI_FAMILY_PHONE_APP)
#      include <windows.h>
#    else
#      include <direct.h>
#      define getcwd _getcwd
#      define mkdir _mkdir
#      define rmdir _rmdir
#      define stat _stat
#      include "sgs_msvc_dirent.h"
#    endif
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
#if defined __arm__ || defined __thumb__ || defined _M_ARM || defined _M_ARMT
#  define SGS_ARCH_ARM 1
#endif
#if defined i386 || defined __i386 || defined __i386 || defined _M_IX86 || defined _X86_ || defined __X86__
#  define SGS_ARCH_X86 1
#endif
#if defined __amd64__ || defined __amd64 || defined __x86_64__ || defined __x86_64 || defined _M_X64 || defined _M_AMD64
#  define SGS_ARCH_AMD64 1
#endif
#if defined __ia64__ || defined _IA64 || defined __IA64__ || defined _M_IA64
#  define SGS_ARCH_IA64 1
#endif
#if defined __powerpc || defined __powerpc__ || defined __powerpc64__ || \
	defined __ppc__ || defined __ppc64 || defined _M_PPC || defined _XENON
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
int sgsXPC_GetProcAddress( const char* file, const char* proc, void** out );

char* sgsXPC_GetCurrentDirectory();
int sgsXPC_SetCurrentDirectory( char* path );
char* sgsXPC_GetModuleFileName();

#ifdef __cplusplus
}
#endif


#endif /* SGS_XPC_H_INCLUDED */
