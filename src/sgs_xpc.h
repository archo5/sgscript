
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
#  define PRId64 "lld"
#else
#  include <inttypes.h>
#  define SGS_INLINE inline
#endif

#define UNUSED( x ) (void)(x)


#ifdef __cplusplus
extern "C" {
#endif

#define SGS_XPC_NOFILE -1
#define SGS_XPC_NOPROC -2
#define SGS_XPC_NOTSUP -3
int sgs_GetProcAddress( const char* file, const char* proc, void** out );

#ifdef __cplusplus
}
#endif


#endif /* SGS_XPC_H_INCLUDED */
