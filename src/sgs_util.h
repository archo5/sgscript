
#ifndef SGS_UTIL_H_INCLUDED
#define SGS_UTIL_H_INCLUDED

#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sgscript.h"


/* useful shortcut definitions */
#define TRUE 1
#define FALSE 0
#define MAX( a, b ) ((a)>(b)?(a):(b))
#define MIN( a, b ) ((a)<(b)?(a):(b))
#define ARRAY_SIZE( a ) (sizeof(a)/sizeof(a[0]))

#define isoneof( chr, str ) (!!strchr( str, chr ))


/* memory handling */
#if SGS_DEBUG && SGS_DEBUG_MEMORY && SGS_DEBUG_EXTRA
void sgs_MemCheckDbg( void* ptr );
#else
#  define sgs_MemCheckDbg( ptr )
#endif
#if SGS_DEBUG && SGS_DEBUG_MEMORY && SGS_DEBUG_CHECK_LEAKS
int _memdbghash( const char* str );
void* sgs_MemFuncDbg( void* ptr, size_t size, const char* file, int line, int bucket );
void sgs_DumpMemoryInfo();
#  define sgs_Malloc( sz )		sgs_MemFuncDbg( NULL, sz, __FILE__, __LINE__, _memdbghash( __FILE__ ) + __LINE__ )
#  define sgs_Free( what )		sgs_MemFuncDbg( what, 0, NULL, 0, -1 )
#else
#  define sgs_Malloc( sz )		sgs_MemFunc( NULL, sz )
#  define sgs_Free( what )		sgs_MemFunc( what, 0 )
#  define sgs_DumpMemoryInfo()
#endif
#define sgs_Alloc( what )		(what*) sgs_Malloc( sizeof( what ) )
#define sgs_Alloc_n( what, n )	(what*) sgs_Malloc( sizeof( what ) * ( n ) )
#define sgs_Alloc_a( what, n )	(what*) sgs_Malloc( sizeof( what ) + ( n ) )


void sgs_BreakIfFunc( const char* code, const char* file, int line );
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
#  define sgs_BreakIf( expr ) { if( expr ){ sgs_BreakIfFunc( #expr, __FILE__, __LINE__ ); } }
#else
#  define sgs_BreakIf( expr )
#endif


/* conversions */
static SGS_INLINE int hexchar( char c ){ return ( (c) >= '0' && (c) <= '9' ) || ( (c) >= 'a' && (c) <= 'f' ) || ( (c) >= 'A' && (c) <= 'F' ); }
static SGS_INLINE int gethex( char c ){ return ( (c) >= '0' && (c) <= '9' ) ? ( (c) - '0' ) : ( ( (c) >= 'a' && (c) <= 'f' ) ? ( (c) - 'a' + 10 ) : ( (c) - 'A' + 10 ) ); }
static SGS_INLINE int decchar( char c ){ return c >= '0' && c <= '9'; }
static SGS_INLINE int getdec( char c ){ return c - '0'; }
static SGS_INLINE int octchar( char c ){ return c >= '0' && c <= '7'; }
static SGS_INLINE int getoct( char c ){ return c - '0'; }
static SGS_INLINE int binchar( char c ){ return c == '0' || c == '1'; }
static SGS_INLINE int getbin( char c ){ return c - '0'; }


#define AS_( ptr, wat ) (*(wat*)(ptr))
#define AS_INT8( ptr ) AS_( ptr, int8_t )
#define AS_UINT8( ptr ) AS_( ptr, uint8_t )
#define AS_INT16( ptr ) AS_( ptr, int16_t )
#define AS_UINT16( ptr ) AS_( ptr, uint16_t )
#define AS_INT32( ptr ) AS_( ptr, int32_t )
#define AS_UINT32( ptr ) AS_( ptr, uint32_t )
#define AS_INT64( ptr ) AS_( ptr, int64_t )
#define AS_UINT64( ptr ) AS_( ptr, uint64_t )
#define AS_FLOAT( ptr ) AS_( ptr, float )
#define AS_DOUBLE( ptr ) AS_( ptr, double )

#define AS_INTEGER( ptr ) AS_( ptr, sgs_Integer )
#define AS_REAL( ptr ) AS_( ptr, sgs_Real )


/* flow/data debugging */
#if SGS_DEBUG && SGS_DEBUG_FLOW
#  define FUNC_HIT( what ) printf( "Hit \"%s\" line %d in function \"%s\"\n", what, __LINE__, __FUNCTION__ );
#  ifdef _MSC_VER
#    define FUNC_INFO( ... ) printf( __VA_ARGS__ );
#  else
#    define FUNC_INFO( what... ) printf( what );
#  endif
#  define FUNC_ENTER printf( "Entering a function from \"%s\" at line %d\n", __FUNCTION__, __LINE__ );
#  define FUNC_BEGIN printf( "Inside \"%s\"\n", __FUNCTION__ );
#  define FUNC_END printf( "Out of \"%s\" at line %d\n", __FUNCTION__, __LINE__ );
#else
#  define FUNC_HIT( what )
#  ifdef _MSC_VER
#    define FUNC_INFO( ... )
#  else
#    define FUNC_INFO( what... )
#  endif
#  define FUNC_ENTER
#  define FUNC_BEGIN
#  define FUNC_END
#endif

void print_safe( const char* buf, int32_t size );


/* string buffer */
typedef
struct _StrBuf
{
	char*   ptr;
	int32_t size;
	int32_t mem;
}
StrBuf;

StrBuf strbuf_create( void );
void strbuf_destroy( StrBuf* sb );
StrBuf strbuf_partial( char* ch, int32_t size );
void strbuf_reserve( StrBuf* sb, int32_t size );
void strbuf_resize( StrBuf* sb, int32_t size );
void strbuf_inschr( StrBuf* sb, int32_t pos, char ch );
void strbuf_insbuf( StrBuf* sb, int32_t pos, const void* buf, int32_t size );
void strbuf_insstr( StrBuf* sb, int32_t pos, const char* str );
void strbuf_appchr( StrBuf* sb, char ch );
void strbuf_appbuf( StrBuf* sb, const void* buf, int32_t size );
void strbuf_appstr( StrBuf* sb, const char* str );


/* data buffer */
#define MemBuf StrBuf
#define membuf_create strbuf_create
#define membuf_destroy strbuf_destroy
#define membuf_partial strbuf_partial
void membuf_reserve( MemBuf* mb, int32_t size );
void membuf_resize( MemBuf* mb, int32_t size );
void membuf_resize_opt( MemBuf* mb, int32_t size );
void membuf_insbuf( MemBuf* mb, int32_t pos, const void* buf, int32_t size );
void membuf_erase( MemBuf* mb, int32_t from, int32_t to );
void membuf_appbuf( MemBuf* mb, const void* buf, int32_t size );
static SGS_INLINE void membuf_appchr( MemBuf* mb, char chr ){ membuf_appbuf( mb, &chr, 1 ); }


/* hash table */
typedef uint32_t Hash;

typedef
struct _HTPair
{
	char* str;
	int   size;
	Hash  hash;
	void* ptr;
}
HTPair;

typedef
struct _HashTable
{
	HTPair* pairs;
	int32_t size;
	int32_t load;
}
HashTable;

void ht_init( HashTable* T, int size );
void ht_free( HashTable* T );
void ht_dump( HashTable* T );
void ht_rehash( HashTable* T, int size );
void ht_check( HashTable* T, int inc );
HTPair* ht_find( HashTable* T, const char* str, int size );
void* ht_get( HashTable* T, const char* str, int size );
void ht_setpair( HTPair* P, const char* str, int size, Hash h, void* ptr );
HTPair* ht_set( HashTable* T, const char* str, int size, void* ptr );
void ht_unset_pair( HashTable* T, HTPair* p );
void ht_unset( HashTable* T, const char* str, int size );

static SGS_INLINE HashTable* ht_create(){ HashTable* T = sgs_Alloc( HashTable ); ht_init( T, 4 ); return T; }
static SGS_INLINE void ht_destroy( HashTable* T ){ ht_free( T ); sgs_Free( T ); }


double sgs_GetTime();


/* returns 0 on failure, 1/2 on integer/real */
int util_strtonum( const char** at, const char* end, sgs_Integer* outi, sgs_Real* outf );
sgs_Integer util_atoi( const char* str, int len );
sgs_Real util_atof( const char* str, int len );


#endif /* SGS_UTIL_H_INCLUDED */
