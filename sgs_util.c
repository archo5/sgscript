

#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <math.h>

#include "sgs_util.h"



#if SGS_DEBUG && SGS_DEBUG_MEMORY
 #if SGS_DEBUG_CHECK_LEAKS
#if SGS_DEBUG && SGS_DEBUG_EXTRA
void dbg_memcheck( void* );
typedef struct allocitem_s allocitem_t;
struct allocitem_s
{
	void* ptr;
	char* file;
	int line;
	allocitem_t* next;
};
allocitem_t* theallocs = NULL;
void ac_checkall()
{
	allocitem_t* p = theallocs;
	while( p )
	{
		dbg_memcheck( p->ptr );
		p = p->next;
	}
}
void ac_addalloc( void* ptr, const char* file, int line )
{
	allocitem_t* p;
#define klajhfdafa	fprintf( stderr, "+ %p\n", ptr );
	ac_checkall();
	p = (allocitem_t*) malloc( sizeof( allocitem_t ) );
	p->next = theallocs;
	p->ptr = ptr;
	p->file = (char*) file;
	p->line = line;
	theallocs = p;
}
void ac_remalloc( void* ptr )
{
	allocitem_t *p, *pp = NULL;
#define akdhkjadhs	fprintf( stderr, "- %p\n", ptr );
	ac_checkall();
	p = theallocs;
	while( p )
	{
		if( p->ptr == ptr )
		{
			if( pp )
				pp->next = p->next;
			else
				theallocs = p->next;
			free( p );
			return;
		}
		pp = p;
		p = p->next;
	}
	p = theallocs;
	fprintf( stderr, "LOOKING FOR: %p\n", ptr );
	while( p )
	{
		fprintf( stderr, "PTR: %p\n", p->ptr );
		p = p->next;
	}
	sgs_BreakIf( "pointer was not registered here" );
}
#endif
int _memdbghash( const char* str )
{
	uint32_t hash = 2166136261u;
	while( *str )
	{
		hash *= 16777619;
		hash ^= *str;
		str++;
	}
	return hash % ( 1 << 16 );
}
#define NUMBUCKETS 32
int buckets[ NUMBUCKETS ] = {0};
const char* files[ NUMBUCKETS ] = {0};
int lines[ NUMBUCKETS ] = {0};
void* sgs_MemFuncDbg( void* ptr, size_t size, const char* file, int line, int bucket )
{
	if( ptr )
	{
		int* ln = (int*) ptr;
		ln--;
		sgs_BreakIf( *ln >= NUMBUCKETS || *ln < 0 );
		buckets[ *ln ]--;
		ptr = ln;
#if SGS_DEBUG && SGS_DEBUG_EXTRA
		ac_remalloc( ptr );
#endif
	}
	if( size )
	{
		int* mem;
		sgs_BreakIf( bucket < 0 );
		bucket %= NUMBUCKETS;
		buckets[ bucket ]++;
		files[ bucket ] = file;
		lines[ bucket ] = line;
		size += sizeof( int );
#if SGS_DEBUG && SGS_DEBUG_EXTRA
		ac_checkall();
#endif
		mem = (int*) sgs_MemFunc( ptr, size );
#if SGS_DEBUG && SGS_DEBUG_EXTRA
		ac_addalloc( mem, file, line );
#endif
		*mem++ = bucket;
#if SGS_DEBUG && SGS_DEBUG_EXTRA
		sgs_MemCheckDbg( mem );
#endif
		return mem;
	}
	else
		return sgs_MemFunc( ptr, size );
}
void sgs_DumpMemoryInfo()
{
	int numlb = 0, i;
	printf( "- Memory info dump -\n" );
	printf( "leaked buckets: " );
	for( i = 0; i < NUMBUCKETS; ++i )
	{
		if( buckets[ i ] )
		{
			printf( i ? ", %d[%d]" : "%d[%d]", i, buckets[ i ] );
			numlb++;
		}
	}
	if( !numlb )
		printf( "none\n" );
	else
	{
		printf( "\n" );
		printf( "file/line info written to leaked buffers (may be incorrect):\n" );
		for( i = 0; i < NUMBUCKETS; ++i )
		{
			if( buckets[ i ] )
			{
				printf( "bucket %3d: %s, line %d\n", i, files[ i ], lines[ i ] );
			}
		}
	}
	printf( "- end of dump -\n" );
}
 #endif
#define _SBSZ 32
#define _SSZ sizeof( size_t )
#define BINCHR(a,b,c,d,e,f,g,h) ((uint8_t)((a<<7)|(b<<6)|(c<<5)|(d<<4)|(e<<3)|(f<<2)|(g<<1)|(h)))
#define GUARDBYTE BINCHR(1,0,1,0,1,0,1,1)
void dbg_memcheck( void* ptr )
{
	char* p;
	int i;
	size_t size;

	p = ((char*)ptr) - _SBSZ - _SSZ;
	memcpy( &size, p, _SSZ );
	for( i = 0; i < _SBSZ; ++i )
		sgs_BreakIf( ( (uint8_t) p[ _SSZ + i ] ) != GUARDBYTE );
	for( i = 0; i < _SBSZ; ++i )
		sgs_BreakIf( ( (uint8_t) p[ size - _SBSZ + i ] ) != GUARDBYTE );
}
#undef sgs_MemCheckDbg
#ifdef SGS_DEBUG_CHECK_LEAKS
void sgs_MemCheckDbg( void* ptr )
{
	if( !ptr ) return;
	dbg_memcheck( ((int*)ptr) - 1 );
}
#else
void sgs_MemCheckDbg( void* ptr )
{
	if( !ptr ) return;
	dbg_memcheck( ptr );
}
#endif
static void* dbg_malloc( size_t size )
{
	char* ptr;
	size += _SBSZ * 2 + _SSZ;
	ptr = malloc( size );
	memcpy( ptr, &size, _SSZ );
	memset( ptr + _SSZ, GUARDBYTE, _SBSZ );
	memset( ptr + size - _SBSZ, GUARDBYTE, _SBSZ );
#if SGS_DEBUG && SGS_DEBUG_EXTRA
	dbg_memcheck( ptr + _SBSZ + _SSZ );
#endif
	return ptr + _SBSZ + _SSZ;
}
static void dbg_free( void* ptr )
{
	char* p = ((char*)ptr) - _SBSZ - _SSZ;
	dbg_memcheck( ptr );
	free( p );
}
static void* sgs_DefaultMemFunc( void* ptr, size_t size )
{
	if( ptr )	dbg_free( ptr );
	if( size )	return dbg_malloc( size );
	else		return NULL;
}
#else
static void* sgs_DefaultMemFunc( void* ptr, size_t size )
{
	if( ptr )	free( ptr );
	if( size )	return malloc( size );
	else		return NULL;
}
#endif

void* (*sgs_MemFunc) ( void*, size_t ) = sgs_DefaultMemFunc;



void sgs_BreakIfFunc( const char* code, const char* file, int line )
{
	fprintf( stderr, "\n== Error detected: \"%s\", file: %s, line %d ==\n", code, file, line );
#if defined( _MSC_VER )
	__asm{ int 3 };
#elif defined( __GNUC__ )
	asm( "int $0x3" );
#else
	assert( 0 );
#endif
}


void print_safe( const char* buf, int32_t size )
{
	int32_t i;
	for( i = 0; i < size; ++i )
	{
		if( !buf[ i ] )
			break;
		if( isgraph( buf[ i ] ) )
			putchar( buf[ i ] );
		else
			printf( "\\x%02X", (int) buf[ i ] );
	}
}


StrBuf strbuf_create( void )
{
	StrBuf sb = { NULL, 0, 0 };
	return sb;
}

void strbuf_destroy( StrBuf* sb )
{
	if( sb->ptr )
		sgs_Free( sb->ptr );
	sb->ptr = NULL;
}

StrBuf strbuf_partial( char* ch, int32_t size )
{
	StrBuf sb = { ch, size, size };
	return sb;
}

void strbuf_reserve( StrBuf* sb, int32_t size )
{
	char* str;
	if( size < sb->mem )
		return;

	sb->mem = size;
	str = sgs_Alloc_n( char, size + 1 );
	if( sb->ptr )
	{
		memcpy( str, sb->ptr, sb->size + 1 );
		sgs_Free( sb->ptr );
	}
	sb->ptr = str;
}

void strbuf_resize( StrBuf* sb, int32_t size )
{
	strbuf_reserve( sb, size );
	sb->size = size;
	sb->ptr[ size ] = 0;
}

void strbuf_inschr( StrBuf* sb, int32_t pos, char ch )
{
	strbuf_reserve( sb, sb->mem < sb->size + 1 ? MAX( sb->mem * 2, sb->size + 1 ) : 0 );
	memcpy( sb->ptr + pos + 1, sb->ptr + pos, sb->size - pos );
	sb->ptr[ pos ] = ch;
	sb->ptr[ ++sb->size ] = 0;
}

void strbuf_insbuf( StrBuf* sb, int32_t pos, const void* buf, int32_t size )
{
	strbuf_reserve( sb, sb->mem < sb->size + size ? MAX( sb->mem * 2, sb->size + size ) : 0 );
	memcpy( sb->ptr + pos + size, sb->ptr + pos, sb->size - pos );
	memcpy( sb->ptr + pos, buf, size );
	sb->size += size;
	sb->ptr[ sb->size ] = 0;
}

void strbuf_insstr( StrBuf* sb, int32_t pos, const char* str )
{
	strbuf_insbuf( sb, pos, str, strlen( str ) );
}

void strbuf_appchr( StrBuf* sb, char ch )
{
	strbuf_reserve( sb, sb->mem < sb->size + 1 ? MAX( sb->mem * 2, sb->size + 1 ) : 0 );
	sb->ptr[ sb->size++ ] = ch;
	sb->ptr[ sb->size ] = 0;
}

void strbuf_appbuf( StrBuf* sb, const void* buf, int32_t size )
{
	strbuf_reserve( sb, sb->mem < sb->size + size ? MAX( sb->mem * 2, sb->size + size ) : 0 );
	memcpy( sb->ptr + sb->size, buf, size );
	sb->size += size;
	sb->ptr[ sb->size ] = 0;
}

void strbuf_appstr( StrBuf* sb, const char* str )
{
	strbuf_appbuf( sb, str, strlen( str ) );
}


void membuf_reserve( MemBuf* mb, int32_t size )
{
	char* str;
	if( size < mb->mem )
		return;

	mb->mem = size;
	str = sgs_Alloc_n( char, size );
	if( mb->ptr )
	{
		memcpy( str, mb->ptr, mb->size );
		sgs_Free( mb->ptr );
	}
	mb->ptr = str;
}

void membuf_resize( MemBuf* mb, int32_t size )
{
	membuf_reserve( mb, size );
	mb->size = size;
}

void membuf_resize_opt( MemBuf* mb, int32_t size )
{
	if( size > mb->mem )
		membuf_reserve( mb, mb->mem * 2 < size ? size : mb->mem * 2 );
	if( size > mb->size )
		mb->size = size;
}

void membuf_insbuf( MemBuf* mb, int32_t pos, const void* buf, int32_t size )
{
	membuf_reserve( mb, mb->mem < mb->size + size ? MAX( mb->mem * 2, mb->size + size ) : 0 );
	memmove( mb->ptr + pos + size, mb->ptr + pos, mb->size - pos );
	memcpy( mb->ptr + pos, buf, size );
	mb->size += size;
}

void membuf_erase( MemBuf* mb, int32_t from, int32_t to )
{
	sgs_BreakIf( from < 0 || from >= mb->size );
	sgs_BreakIf( to < 0 || to >= mb->size );
	if( mb->size - to > 1 )
		memmove( mb->ptr + from, mb->ptr + to + 1, mb->size - to - 1 );
	mb->size -= to - from + 1;
}

void membuf_appbuf( MemBuf* mb, const void* buf, int32_t size )
{
	membuf_reserve( mb, mb->mem < mb->size + size ? MAX( mb->mem * 2, mb->size + size ) : 0 );
	memcpy( mb->ptr + mb->size, buf, size );
	mb->size += size;
}


Hash hashFunc( char* str, int size )
{
	int i;
	Hash h = 2166136261u;
	for( i = 0; i < size; ++i )
	{
		h ^= str[ i ];
		h *= 16777619u;
	}
	return h;
}

char* UNALLOCATED_STRING = "";

void ht_init( HashTable* T, int size )
{
	HTPair *p, *pend;

	T->pairs = sgs_Alloc_n( HTPair, size );
	T->size = size;
	T->load = 0;

	p = T->pairs;
	pend = p + size;
	while( p < pend )
	{
		p->str = NULL;
		p++;
	}
}

void ht_free( HashTable* T )
{
	HTPair* p = T->pairs, *pend = T->pairs + T->size;
	while( p < pend )
	{
		if( p->str ) sgs_Free( p->str );
		p++;
	}
	sgs_Free( T->pairs );
	T->pairs = NULL;
	T->size = 0;
	T->load = 0;
}

void ht_dump( HashTable* T )
{
	int i = 0;
	HTPair* p = T->pairs, *pend = T->pairs + T->size;
	printf( "contents of hash table %p (size=%d, load=%d):\n", T, T->size, T->load );
	while( p < pend )
	{
		if( p->str )
		{
			printf( "- \"%.*s\"=%p (size=%d, hash=%08X, bucket=%d)\n", p->size, p->str, p->ptr, p->size, p->hash, p->hash % T->size );
			i++;
		}
		p++;
		if( i > 50 )
		{
			printf( "- too many items found, ending... -\n" );
			break;
		}
	}
}

void ht_rehash( HashTable* T, int size )
{
	HTPair *np, *p, *pend;

	sgs_BreakIf( size < T->load );

	np = sgs_Alloc_n( HTPair, size );
	p = np;
	pend = np + size;
	while( p < pend )
		(p++)->str = NULL;

	p = T->pairs;
	pend = T->pairs + T->size;
	while( p < pend )
	{
		if( p->str )
		{
			HTPair *tp = np + ( p->hash % size ), *tpend = np + size;
			HTPair *tpp = tp;
			do
			{
				if( tp->str == NULL )
				{
					memcpy( tp, p, sizeof( HTPair ) );
					break;
				}
				tp++;
				if( tp >= tpend )
					tp = np;
			}
			while( tp != tpp );
		}
		p++;
	}

	sgs_Free( T->pairs );
	T->pairs = np;
	T->size = size;
}

void ht_check( HashTable* T, int inc )
{
	if( T->load + inc > T->size * 0.75 )
	{
		int newsize = (int)( T->size * 0.6 + ( T->load + inc ) * 0.6 );
		ht_rehash( T, newsize );
	}
	else if( T->load + inc < T->size * 0.25 )
	{
		int newsize = (int)( T->size * 0.5 + ( T->load + inc ) * 0.5 );
		ht_rehash( T, newsize );
	}
}

HTPair* ht_find( HashTable* T, char* str, int size )
{
	Hash h = hashFunc( str, size );
	HTPair* p = T->pairs + ( h % T->size );
	HTPair* pp = p, *pend = T->pairs + T->size;
	if( p->str == NULL )
		return NULL;
	do
	{
		if( h == p->hash && size == p->size && strncmp( str, p->str, size ) == 0 )
			return p;
		p++;
		if( p >= pend ) p = T->pairs;
	}
	while( p != pp && p->str != NULL );
	return NULL;
}

void* ht_get( HashTable* T, char* str, int size )
{
	HTPair* p = ht_find( T, str, size );
	return p ? p->ptr : NULL;
}

void ht_setpair( HTPair* P, char* str, int size, Hash h, void* ptr )
{
	P->str = size ? sgs_Alloc_n( char, size ) : UNALLOCATED_STRING;
	if( size ) memcpy( P->str, str, size );
	P->size = size;
	P->ptr = ptr;
	P->hash = h;
}

void ht_set( HashTable* T, char* str, int size, void* ptr )
{
	HTPair* p = ht_find( T, str, size );
	if( p )
		p->ptr = ptr;
	else
	{
		Hash h;
		HTPair *pp, *pend;

		ht_check( T, 1 );
		h = hashFunc( str, size );
		T->load++;

		p = T->pairs + ( h % T->size );
		if( p->str == NULL )
		{
			ht_setpair( p, str, size, h, ptr );
			return;
		}

		pp = p++, pend = T->pairs + T->size;
		if( p >= pend )
			p = T->pairs;
		while( p != pp )
		{
			if( p->str == NULL )
			{
				ht_setpair( p, str, size, h, ptr );
				return;
			}
			p++;
			if( p >= pend )
				p = T->pairs;
		}
		sgs_BreakIf( "reached end of loop" );
	}
}

int ht_trymove( HashTable* T, HTPair* phole, HTPair* P )
{
	HTPair* p = T->pairs + ( P->hash % T->size ), *pend = T->pairs + T->size;
	while( p->str && p != P && p != phole )
	{
		p++;
		if( p >= pend )
			p = T->pairs;
	}
	if( p == P )
		return 0;
	sgs_BreakIf( p != phole );
	memcpy( p, P, sizeof( HTPair ) );
	P->str = NULL;
	return 1;
}

void ht_fillhole( HashTable* T, HTPair* P )
{
	HTPair *pp = P, *phole = P, *pend = T->pairs + T->size;

	P++;
	if( P >= pend )
		P = T->pairs;

	while( P->str && P != pp )
	{
		if( ht_trymove( T, phole, P ) )
			phole = P;
		P++;
		if( P >= pend )
			P = T->pairs;
	}
}

void ht_unset( HashTable* T, char* str, int size )
{
	HTPair* p = ht_find( T, str, size );
	if( p )
	{
		int osz = T->size;
		if( p->str != UNALLOCATED_STRING )
			sgs_Free( p->str );
		p->str = NULL;
		T->load--;
		ht_check( T, 0 );
		if( T->size == osz )
			ht_fillhole( T, p );
	}
}



double sgs_GetTime()
{
	clock_t clk = clock();
	return (double)( clk ) / (double)( CLOCKS_PER_SEC );
}


/* string -> number conversion */

typedef const char CCH;

static int strtonum_hex( CCH** at, CCH* end, sgs_Integer* outi )
{
	sgs_Integer val = 0;
	CCH* str = *at + 2;
	while( str < end && hexchar( *str ) )
	{
		val *= 16;
		val += gethex( *str );
		str++;
	}
	*at = str;
	*outi = val;
	return 1;
}

static int strtonum_oct( CCH** at, CCH* end, sgs_Integer* outi )
{
	sgs_Integer val = 0;
	CCH* str = *at + 2;
	while( str < end && octchar( *str ) )
	{
		val *= 8;
		val += getoct( *str );
		str++;
	}
	*at = str;
	*outi = val;
	return 1;
}

static int strtonum_bin( CCH** at, CCH* end, sgs_Integer* outi )
{
	sgs_Integer val = 0;
	CCH* str = *at + 2;
	while( str < end && binchar( *str ) )
	{
		val *= 2;
		val += getbin( *str );
		str++;
	}
	*at = str;
	*outi = val;
	return 1;
}

static int strtonum_real( CCH** at, CCH* end, sgs_Real* outf )
{
	sgs_Real val = 0;
	sgs_Real vsign = 1;
	CCH* str = *at;

	if( *str == '+' ) str++;
	else if( *str == '-' ){ vsign = -1; str++; }

	while( str < end && decchar( *str ) )
	{
		val *= 10;
		val += getdec( *str );
		str++;
	}
	if( str >= end )
		goto done;
	if( *str == '.' )
	{
		sgs_Real mult = 1.0;
		str++;
		while( str < end && decchar( *str ) )
		{
			mult /= 10;
			val += getdec( *str ) * mult;
			str++;
		}
	}
	if( str + 3 >= end )
		goto done;
	if( *str == 'e' || *str == 'E' )
	{
		sgs_Real sign, e = 0;
		str++;
		if( *str != '+' && *str != '-' )
			goto done;
		sign = *str++ == '-' ? -1 : 1;
		while( str < end && decchar( *str ) )
		{
			e *= 10;
			e += getdec( *str );
			str++;
		}
		val *= pow( 10, e * sign );
	}

done:
	*outf = val * vsign;
	*at = str;
	return 2;
}

static int strtonum_dec( CCH** at, CCH* end, sgs_Integer* outi, sgs_Real* outf )
{
	CCH* str = *at;
	if( *str == '+' || *str == '-' ) str++;
	while( str < end && decchar( *str ) )
		str++;
	if( *str == '.' || *str == 'E' || *str == 'e' )
		return strtonum_real( at, end, outf );
	else
	{
		sgs_Integer val = 0;
		int invsign = 0;

		str = *at;
		if( *str == '+' ) str++;
		else if( *str == '-' ){ invsign = 1; str++; }

		while( decchar( *str ) )
		{
			val *= 10;
			val += getdec( *str );
			str++;
		}
		if( invsign ) val = -val;
		*outi = val;
		*at = str;
		return 1;
	}
}

int util_strtonum( CCH** at, CCH* end, sgs_Integer* outi, sgs_Real* outf )
{
	CCH* str = *at;
	if( str == end )
		return 0;
	if( end - str >= 3 && *str == '0' )
	{
		if( str[1] == 'x' ) return strtonum_hex( at, end, outi );
		else if( str[1] == 'o' ) return strtonum_oct( at, end, outi );
		else if( str[1] == 'b' ) return strtonum_bin( at, end, outi );
	}
	return strtonum_dec( at, end, outi, outf );
}


sgs_Integer util_atoi( const char* str, int len )
{
	sgs_Integer vi;
	sgs_Real vr;
	int ret = util_strtonum( &str, str + len, &vi, &vr );
	if( ret == 1 ) return vi;
	else if( ret == 2 ) return (sgs_Integer) vr;
	else return 0;
}

sgs_Real util_atof( const char* str, int len )
{
	sgs_Integer vi;
	sgs_Real vr;
	int ret = util_strtonum( &str, str + len, &vi, &vr );
	if( ret == 1 ) return (sgs_Real) vi;
	else if( ret == 2 ) return vr;
	else return 0;
}


