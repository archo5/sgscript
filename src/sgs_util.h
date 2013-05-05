
#ifndef SGS_UTIL_H_INCLUDED
#define SGS_UTIL_H_INCLUDED

#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _sgs_iStr
#  define _sgs_iStr string_s
#  define _sgs_iFunc func_s
#endif

#ifdef SGS_INTERNAL
#  ifdef SGS_REALLY_INTERNAL
#    define TRUE sgs_TRUE
#    define FALSE sgs_FALSE
#    define MIN sgs_MIN
#    define MAX sgs_MAX
#    define ARRAY_SIZE sgs_ARRAY_SIZE
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
#  define FUNC_INFO SGS_FUNC_INFO
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

#  define HTPair sgs_HTPair
#  define HashTable sgs_HashTable
#  define ht_init sgs_ht_init
#  define ht_free sgs_ht_free
#  define ht_dump sgs_ht_dump
#  define ht_rehash sgs_ht_rehash
#  define ht_check sgs_ht_check
#  define ht_find sgs_ht_find
#  define ht_get sgs_ht_get
#  define ht_setpair sgs_ht_setpair
#  define ht_set sgs_ht_set
#  define ht_unset_pair sgs_ht_unset_pair
#  define ht_unset sgs_ht_unset


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

#define sgs_isoneof( chr, str ) (!!strchr( str, chr ))


typedef int16_t sgs_LineNum;

void sgs_BreakIfFunc( const char* code, const char* file, int line );
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

#define SGS_AS_INTEGER( ptr ) SGS_AS_( ptr, sgs_Integer )
#define SGS_AS_REAL( ptr ) SGS_AS_( ptr, sgs_Real )


/* flow/data debugging */
#if SGS_DEBUG && SGS_DEBUG_FLOW
#  define SGS_FUNC_HIT( what ) \
	printf( "Hit \"%s\" line %d in function \"%s\"\n", what, __LINE__, __FUNCTION__ );
#  ifdef _MSC_VER
#    define SGS_FUNC_INFO( ... ) printf( __VA_ARGS__ );
#  else
#    define SGS_FUNC_INFO( what... ) printf( what );
#  endif
#  define SGS_FUNC_ENTER \
	printf( "Entering a function from \"%s\" at line %d\n", __FUNCTION__, __LINE__ );
#  define SGS_FUNC_BEGIN \
	printf( "Inside \"%s\"\n", __FUNCTION__ );
#  define SGS_FUNC_END \
	printf( "Out of \"%s\" at line %d\n", __FUNCTION__, __LINE__ );
#else
#  define SGS_FUNC_HIT( what )
#  ifdef _MSC_VER
#    define SGS_FUNC_INFO( ... )
#  else
#    define SGS_FUNC_INFO( what... )
#  endif
#  define SGS_FUNC_ENTER
#  define SGS_FUNC_BEGIN
#  define SGS_FUNC_END
#endif

void sgs_print_safe( FILE* fp, const char* buf, int32_t size );


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
sgs_MemBuf sgs_membuf_create( void );
void sgs_membuf_destroy( sgs_MemBuf* sb, SGS_CTX );
sgs_MemBuf sgs_membuf_partial( char* ch, int32_t size );
void sgs_membuf_reserve( sgs_MemBuf* mb, SGS_CTX, int32_t size );
void sgs_membuf_resize( sgs_MemBuf* mb, SGS_CTX, int32_t size );
void sgs_membuf_resize_opt( sgs_MemBuf* mb, SGS_CTX, int32_t size );
void sgs_membuf_insbuf( sgs_MemBuf* mb, SGS_CTX, int32_t pos, const void* buf, int32_t size );
void sgs_membuf_erase( sgs_MemBuf* mb, int32_t from, int32_t to );
void sgs_membuf_appbuf( sgs_MemBuf* mb, SGS_CTX, const void* buf, int32_t size );
static SGS_INLINE void sgs_membuf_setstr( sgs_MemBuf* mb, SGS_CTX, const char* str )
	{ mb->size = 0; sgs_membuf_appbuf( mb, C, str, strlen( str ) + 1 ); mb->size--; }
static SGS_INLINE void sgs_membuf_setstrbuf( sgs_MemBuf* mb, SGS_CTX, const char* str, int32_t size )
	{ sgs_membuf_reserve( mb, C, size + 1 ); mb->size = 0;
		sgs_membuf_appbuf( mb, C, str, size ); mb->ptr[ mb->size ] = 0; }
static SGS_INLINE void sgs_membuf_appchr( sgs_MemBuf* mb, SGS_CTX, char chr )
	{ sgs_membuf_appbuf( mb, C, &chr, 1 ); }


/* hash table */
typedef uint32_t sgs_Hash;
sgs_Hash sgs_HashFunc( const char* str, int size );

typedef
struct _sgs_HTPair
{
	char*    str;
	int      size;
	sgs_Hash hash;
	void*    ptr;
}
sgs_HTPair;

typedef
struct _sgs_HashTable
{
	sgs_HTPair* pairs;
	int32_t size;
	int32_t load;
}
sgs_HashTable;

void sgs_ht_init( sgs_HashTable* T, SGS_CTX, int size );
void sgs_ht_free( sgs_HashTable* T, SGS_CTX );
void sgs_ht_dump( sgs_HashTable* T );
void sgs_ht_rehash( sgs_HashTable* T, SGS_CTX, int size );
void sgs_ht_check( sgs_HashTable* T, SGS_CTX, int inc );
sgs_HTPair* sgs_ht_find( sgs_HashTable* T, const char* str, int size );
void* sgs_ht_get( sgs_HashTable* T, const char* str, int size );
void sgs_ht_setpair( sgs_HTPair* P, SGS_CTX, const char* str, int size, sgs_Hash h, void* ptr );
sgs_HTPair* sgs_ht_set( sgs_HashTable* T, SGS_CTX, const char* str, int size, void* ptr );
void sgs_ht_unset_pair( sgs_HashTable* T, SGS_CTX, sgs_HTPair* p );
void sgs_ht_unset( sgs_HashTable* T, SGS_CTX, const char* str, int size );


double sgs_GetTime();


/* returns 0 on failure, 1/2 on integer/real */
int sgs_util_strtonum( const char** at, const char* end, sgs_Integer* outi, sgs_Real* outf );
sgs_Integer sgs_util_atoi( const char* str, int len );
sgs_Real sgs_util_atof( const char* str, int len );



void sgs_quicksort( void *array, size_t length, size_t size,
	int(*compare)(const void *, const void *, void*), void* userdata);

/*
	UNICODE helper functions
	- utf8_decode: returns number of bytes parsed (negated if input was invalid)
	- utf8_encode: returns number of bytes written (up to 4, make sure there's space)
*/
#define SGS_UNICODE_INVCHAR 0xfffd
#define SGS_UNICODE_INVCHAR_STR "\xff\xfd"
#define SGS_UNICODE_INVCHAR_LEN 2
int sgs_utf8_decode( char* buf, size_t size, uint32_t* outchar );
int sgs_utf8_encode( uint32_t ch, char* out );


#endif /* SGS_UTIL_H_INCLUDED */
