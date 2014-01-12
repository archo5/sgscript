
#ifndef SGS_UTIL_H_INCLUDED
#define SGS_UTIL_H_INCLUDED

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef SGS_INTERNAL
#  ifdef SGS_REALLY_INTERNAL
#    ifndef TRUE
#      define TRUE sgs_TRUE
#    endif
#    ifndef FALSE
#      define FALSE sgs_FALSE
#    endif
#    define MIN sgs_MIN
#    define MAX sgs_MAX
#    define ARRAY_SIZE sgs_ARRAY_SIZE
#    define HAS_FLAG sgs_HAS_FLAG
#  endif
#  define LineNum sgs_LineNum
#  define isoneof sgs_isoneof
#  define hexchar sgs_hexchar
#  define gethex sgs_gethex
#  define decchar sgs_decchar
#  define getdec sgs_getdec
#  define octchar sgs_octchar
#  define getoct sgs_getoct
#  define binchar sgs_binchar
#  define getbin sgs_getbin
#  define AS_ SGS_AS_
#  define AS_INT8 SGS_AS_INT8
#  define AS_UINT8 SGS_AS_UINT8
#  define AS_INT16 SGS_AS_INT16
#  define AS_UINT16 SGS_AS_UINT16
#  define AS_INT32 SGS_AS_INT32
#  define AS_UINT32 SGS_AS_UINT32
#  define AS_INT64 SGS_AS_INT64
#  define AS_UINT64 SGS_AS_UINT64
#  define AS_FLOAT SGS_AS_FLOAT
#  define AS_DOUBLE SGS_AS_DOUBLE
#  define AS_INTEGER SGS_AS_INTEGER
#  define AS_REAL SGS_AS_REAL

#  define FUNC_HIT SGS_FUNC_HIT
#  define FUNC_ENTER SGS_FUNC_ENTER
#  define FUNC_BEGIN SGS_FUNC_BEGIN
#  define FUNC_END SGS_FUNC_END

#  define MemBuf sgs_MemBuf
#  define membuf_create sgs_membuf_create
#  define membuf_destroy sgs_membuf_destroy
#  define membuf_partial sgs_membuf_partial
#  define membuf_reserve sgs_membuf_reserve
#  define membuf_resize sgs_membuf_resize
#  define membuf_resize_opt sgs_membuf_resize_opt
#  define membuf_insbuf sgs_membuf_insbuf
#  define membuf_erase sgs_membuf_erase
#  define membuf_appbuf sgs_membuf_appbuf
#  define membuf_setstr sgs_membuf_setstr
#  define membuf_setstrbuf sgs_membuf_setstrbuf
#  define membuf_appchr sgs_membuf_appchr

#  define VHTIdx sgs_VHTIdx
#  define VHTVar sgs_VHTVar
#  define VHTable sgs_VHTable
#  define vht_init sgs_vht_init
#  define vht_free sgs_vht_free
#  define vht_get sgs_vht_get
#  define vht_get_str sgs_vht_get_str
#  define vht_set sgs_vht_set
#  define vht_unset sgs_vht_unset
#  define vht_size sgs_vht_size

#  define print_safe sgs_print_safe
#  define util_strtonum sgs_util_strtonum
#  define util_atoi sgs_util_atoi
#  define util_atof sgs_util_atof
#  define quicksort sgs_quicksort
#endif

#include "sgscript.h"


/* useful shortcut definitions */
#define sgs_TRUE 1
#define sgs_FALSE 0
#define sgs_MAX( a, b ) ((a)>(b)?(a):(b))
#define sgs_MIN( a, b ) ((a)<(b)?(a):(b))
#define sgs_ARRAY_SIZE( a ) (sizeof(a)/sizeof(a[0]))
#define sgs_HAS_FLAG( val, flag ) (((val)&(flag))==(flag))

#define sgs_isoneof( chr, str ) (!!strchr( str, chr ))


typedef int16_t sgs_LineNum;

SGS_APIFUNC void sgs_BreakIfFunc( const char* code, const char* file, int line );
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
#  define sgs_BreakIf( expr ) { if( expr ){ sgs_BreakIfFunc( #expr, __FILE__, __LINE__ ); } }
#else
#  define sgs_BreakIf( expr )
#endif


/* conversions */
static SGS_INLINE int sgs_hexchar( char c )
	{ return ( (c) >= '0' && (c) <= '9' ) ||
	( (c) >= 'a' && (c) <= 'f' ) || ( (c) >= 'A' && (c) <= 'F' ); }
static SGS_INLINE int sgs_gethex( char c )
	{ return ( (c) >= '0' && (c) <= '9' ) ? ( (c) - '0' ) :
	( ( (c) >= 'a' && (c) <= 'f' ) ? ( (c) - 'a' + 10 ) : ( (c) - 'A' + 10 ) ); }
static SGS_INLINE int sgs_decchar( char c ){ return c >= '0' && c <= '9'; }
static SGS_INLINE int sgs_getdec( char c ){ return c - '0'; }
static SGS_INLINE int sgs_octchar( char c ){ return c >= '0' && c <= '7'; }
static SGS_INLINE int sgs_getoct( char c ){ return c - '0'; }
static SGS_INLINE int sgs_binchar( char c ){ return c == '0' || c == '1'; }
static SGS_INLINE int sgs_getbin( char c ){ return c - '0'; }


/*
#define SGS_AS_( ptr, wat ) (*(wat*)(ptr))
#define SGS_AS_INT8( ptr ) SGS_AS_( ptr, int8_t )
#define SGS_AS_UINT8( ptr ) SGS_AS_( ptr, uint8_t )
#define SGS_AS_INT16( ptr ) SGS_AS_( ptr, int16_t )
#define SGS_AS_UINT16( ptr ) SGS_AS_( ptr, uint16_t )
#define SGS_AS_INT32( ptr ) SGS_AS_( ptr, int32_t )
#define SGS_AS_UINT32( ptr ) SGS_AS_( ptr, uint32_t )
#define SGS_AS_INT64( ptr ) SGS_AS_( ptr, int64_t )
#define SGS_AS_UINT64( ptr ) SGS_AS_( ptr, uint64_t )
#define SGS_AS_FLOAT( ptr ) SGS_AS_( ptr, float )
#define SGS_AS_DOUBLE( ptr ) SGS_AS_( ptr, double )

#define SGS_AS_INTEGER( ptr ) SGS_AS_( ptr, sgs_Int )
#define SGS_AS_REAL( ptr ) SGS_AS_( ptr, sgs_Real )
*/

#define SGS_AS_( tgt, ptr, wat ) do{ memcpy( &(tgt), (ptr), sizeof(wat) ); }while(0)
#define SGS_AS_INT8( tgt, ptr ) SGS_AS_( tgt, ptr, int8_t )
#define SGS_AS_UINT8( tgt, ptr ) SGS_AS_( tgt, ptr, uint8_t )
#define SGS_AS_INT16( tgt, ptr ) SGS_AS_( tgt, ptr, int16_t )
#define SGS_AS_UINT16( tgt, ptr ) SGS_AS_( tgt, ptr, uint16_t )
#define SGS_AS_INT32( tgt, ptr ) SGS_AS_( tgt, ptr, int32_t )
#define SGS_AS_UINT32( tgt, ptr ) SGS_AS_( tgt, ptr, uint32_t )
#define SGS_AS_INT64( tgt, ptr ) SGS_AS_( tgt, ptr, int64_t )
#define SGS_AS_UINT64( tgt, ptr ) SGS_AS_( tgt, ptr, uint64_t )
#define SGS_AS_FLOAT( tgt, ptr ) SGS_AS_( tgt, ptr, float )
#define SGS_AS_DOUBLE( tgt, ptr ) SGS_AS_( tgt, ptr, double )

#define SGS_AS_INTEGER( tgt, ptr ) SGS_AS_( tgt, ptr, sgs_Int )
#define SGS_AS_REAL( tgt, ptr ) SGS_AS_( tgt, ptr, sgs_Real )


/* flow/data debugging */
#if SGS_DEBUG && SGS_DEBUG_FLOW
#  define SGS_FUNC_HIT( what ) \
	printf( "Hit \"%s\" line %d in function \"%s\"\n", what, __LINE__, __FUNCTION__ );
#  define SGS_FUNC_ENTER \
	printf( "Entering a function from \"%s\" at line %d\n", __FUNCTION__, __LINE__ );
#  define SGS_FUNC_BEGIN \
	printf( "Inside \"%s\"\n", __FUNCTION__ );
#  define SGS_FUNC_END \
	printf( "Out of \"%s\" at line %d\n", __FUNCTION__, __LINE__ );
#else
#  define SGS_FUNC_HIT( what )
#  define SGS_FUNC_ENTER
#  define SGS_FUNC_BEGIN
#  define SGS_FUNC_END
#endif

SGS_APIFUNC void sgs_print_safe( FILE* fp, const char* buf, int32_t size );


/* string buffer */
typedef
struct _sgs_MemBuf
{
	char*   ptr;
	int32_t size;
	int32_t mem;
}
sgs_MemBuf;


/* data buffer */
SGS_APIFUNC sgs_MemBuf sgs_membuf_create( void );
SGS_APIFUNC void sgs_membuf_destroy( sgs_MemBuf* sb, SGS_CTX );
SGS_APIFUNC sgs_MemBuf sgs_membuf_partial( char* ch, int32_t size );
SGS_APIFUNC void sgs_membuf_reserve( sgs_MemBuf* mb, SGS_CTX, int32_t size );
SGS_APIFUNC void sgs_membuf_resize( sgs_MemBuf* mb, SGS_CTX, int32_t size );
SGS_APIFUNC void sgs_membuf_resize_opt( sgs_MemBuf* mb, SGS_CTX, int32_t size );
SGS_APIFUNC void sgs_membuf_insbuf( sgs_MemBuf* mb, SGS_CTX, int32_t pos, const void* buf, int32_t size );
SGS_APIFUNC void sgs_membuf_erase( sgs_MemBuf* mb, int32_t from, int32_t to );
SGS_APIFUNC void sgs_membuf_appbuf( sgs_MemBuf* mb, SGS_CTX, const void* buf, int32_t size );
static SGS_INLINE void sgs_membuf_setstr( sgs_MemBuf* mb, SGS_CTX, const char* str )
	{ mb->size = 0; sgs_membuf_appbuf( mb, C, str, strlen( str ) + 1 ); mb->size--; }
static SGS_INLINE void sgs_membuf_setstrbuf( sgs_MemBuf* mb, SGS_CTX, const char* str, int32_t size )
	{ sgs_membuf_reserve( mb, C, size + 1 ); mb->size = 0;
		sgs_membuf_appbuf( mb, C, str, size ); mb->ptr[ mb->size ] = 0; }
static SGS_INLINE void sgs_membuf_appchr( sgs_MemBuf* mb, SGS_CTX, char chr )
	{ sgs_membuf_appbuf( mb, C, &chr, 1 ); }


/* hashing functions */
typedef uint32_t sgs_Hash;
SGS_APIFUNC sgs_Hash sgs_HashFunc( const char* str, int size );
#define SGS_HASH_COMPUTED( h ) ((h) != 0)
SGS_APIFUNC sgs_Hash sgs_HashVar( const sgs_Variable* v );


/* hash table */
typedef sgs_SizeVal sgs_VHTIdx;

#define SGS_VHTIDX_EMPTY -1
#define SGS_VHTIDX_REMOVED -2

typedef
struct _sgs_VHTVar
{
	sgs_Variable key;
	sgs_Variable val;
	sgs_Hash     hash;
}
sgs_VHTVar;

typedef
struct _sgs_VHTable
{
	sgs_VHTIdx* pairs;
	sgs_VHTVar* vars;
	sgs_VHTIdx  pair_mem;
	sgs_VHTIdx  var_mem;
	sgs_VHTIdx  size;
	sgs_VHTIdx  num_rem;
}
sgs_VHTable;

SGS_APIFUNC void sgs_vht_init( sgs_VHTable* T, SGS_CTX, int32_t initial_pair_mem, int32_t initial_var_mem );
SGS_APIFUNC void sgs_vht_free( sgs_VHTable* T, SGS_CTX );
SGS_APIFUNC sgs_VHTIdx sgs_vht_pair_id( sgs_VHTable* T, sgs_Variable* K, sgs_Hash hash );
SGS_APIFUNC sgs_VHTVar* sgs_vht_get( sgs_VHTable* T, sgs_Variable* K );
SGS_APIFUNC sgs_VHTVar* sgs_vht_get_str( sgs_VHTable* T, const char* str, sgs_SizeVal size, sgs_Hash hash );
SGS_APIFUNC sgs_VHTVar* sgs_vht_set( sgs_VHTable* T, SGS_CTX, sgs_Variable* K, sgs_Variable* V );
SGS_APIFUNC void sgs_vht_unset( sgs_VHTable* T, SGS_CTX, sgs_Variable* K );

#define sgs_vht_size( T ) ((T)->size)


SGS_APIFUNC double sgs_GetTime();


/* returns 0 on failure, 1/2 on integer/real */
SGS_APIFUNC int sgs_util_strtonum( const char** at, const char* end, sgs_Int* outi, sgs_Real* outf );
SGS_APIFUNC sgs_Int sgs_util_atoi( const char* str, int len );
SGS_APIFUNC sgs_Real sgs_util_atof( const char* str, int len );



SGS_APIFUNC void sgs_quicksort( void *array, size_t length, size_t size,
	int(*compare)(const void *, const void *, void*), void* userdata);

/*
	UNICODE helper functions
	- utf8_decode: returns number of bytes parsed (negated if input was invalid)
	- utf8_encode: returns number of bytes written (up to 4, make sure there's space)
*/
#define SGS_UNICODE_INVCHAR 0xfffd
#define SGS_UNICODE_INVCHAR_STR "\xef\xbf\xbd"
#define SGS_UNICODE_INVCHAR_LEN 3
SGS_APIFUNC int sgs_utf8_decode( char* buf, size_t size, uint32_t* outchar );
SGS_APIFUNC int sgs_utf8_encode( uint32_t ch, char* out );


#endif /* SGS_UTIL_H_INCLUDED */
