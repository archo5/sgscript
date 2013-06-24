

#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <stddef.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_util.h"


void sgs_BreakIfFunc( const char* code, const char* file, int line )
{
	fprintf( stderr, "\n== Error detected: \"%s\", file: %s, line %d ==\n", code, file, line );
#if defined( _MSC_VER )
	__asm{ int 3 };
#elif defined( __GNUC__ )
	__asm__( "int $3" );
#else
	assert( 0 );
#endif
}


void print_safe( FILE* fp, const char* buf, int32_t size )
{
	int32_t i;
	for( i = 0; i < size; ++i )
	{
		if( !buf[ i ] )
			break;
		if( isgraph( buf[ i ] ) || buf[ i ] == ' ' )
			fputc( buf[ i ], fp );
		else
			fprintf( fp, "\\x%02X", (int) buf[ i ] );
	}
}


MemBuf membuf_create( void )
{
	MemBuf sb = { NULL, 0, 0 };
	return sb;
}

void membuf_destroy( MemBuf* sb, SGS_CTX )
{
	if( sb->ptr )
		sgs_Dealloc( sb->ptr );
	sb->ptr = NULL;
}

MemBuf membuf_partial( char* ch, int32_t size )
{
	MemBuf sb = { ch, size, size };
	return sb;
}

void membuf_reserve( MemBuf* mb, SGS_CTX, int32_t size )
{
	if( size <= mb->mem )
		return;

	mb->mem = size;
	mb->ptr = (char*) sgs_Realloc( C, mb->ptr, size );
}

void membuf_resize( MemBuf* mb, SGS_CTX, int32_t size )
{
	membuf_reserve( mb, C, size );
	mb->size = size;
}

void membuf_resize_opt( MemBuf* mb, SGS_CTX, int32_t size )
{
	if( size > mb->mem )
		membuf_reserve( mb, C, mb->mem * 2 < size ? size : mb->mem * 2 );
	if( size > mb->size )
		mb->size = size;
}

void membuf_insbuf( MemBuf* mb, SGS_CTX, int32_t pos, const void* buf, int32_t size )
{
	membuf_reserve( mb, C, mb->mem < mb->size + size ? MAX( mb->mem * 2, mb->size + size ) : 0 );
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

void membuf_appbuf( MemBuf* mb, SGS_CTX, const void* buf, int32_t size )
{
	membuf_reserve( mb, C, mb->mem < mb->size + size ? MAX( mb->mem * 2, mb->size + size ) : 0 );
	memcpy( mb->ptr + mb->size, buf, size );
	mb->size += size;
}


#define hashFunc sgs_HashFunc
sgs_Hash sgs_HashFunc( const char* str, int size )
{
	int i;
	sgs_Hash h = 2166136261u;
	for( i = 0; i < size; ++i )
	{
		h ^= str[ i ];
		h *= 16777619u;
	}
	return h;
}


#if 0

void ht_init( HashTable* T, SGS_CTX, int size )
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

void ht_free( HashTable* T, SGS_CTX )
{
	HTPair* p = T->pairs, *pend = T->pairs + T->size;
	while( p < pend )
	{
		if( p->str ) sgs_Dealloc( p->str );
		p++;
	}
	sgs_Dealloc( T->pairs );
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

void ht_rehash( HashTable* T, SGS_CTX, int size )
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

	sgs_Dealloc( T->pairs );
	T->pairs = np;
	T->size = size;
}

void ht_check( HashTable* T, SGS_CTX, int inc )
{
	if( T->load + inc > T->size * 0.75 )
	{
		int newsize = (int)( T->size * 0.6 + ( T->load + inc ) * 0.6 );
		ht_rehash( T, C, newsize );
	}
	else if( T->load + inc < T->size * 0.25 )
	{
		int newsize = (int)( T->size * 0.5 + ( T->load + inc ) * 0.5 );
		ht_rehash( T, C, newsize );
	}
}

HTPair* ht_find( HashTable* T, const char* str, int size, sgs_Hash h )
{
	HTPair* p = T->pairs + ( h % T->size );
	HTPair* pp = p, *pend = T->pairs + T->size;
	if( p->str == NULL )
		return NULL;
	do
	{
		if( h == p->hash && size == p->size && memcmp( str, p->str, size ) == 0 )
			return p;
		p++;
		if( p >= pend ) p = T->pairs;
	}
	while( p != pp && p->str != NULL );
	return NULL;
}

void* ht_get( HashTable* T, const char* str, int size )
{
	HTPair* p = ht_find( T, str, size, hashFunc( str, size ) );
	return p ? p->ptr : NULL;
}

void ht_setpair( HTPair* P, SGS_CTX, const char* str, int size, sgs_Hash h, void* ptr )
{
	P->str = sgs_Alloc_n( char, size ? size : 1 );
	if( size ) memcpy( P->str, str, size );
	P->size = size;
	P->ptr = ptr;
	P->hash = h;
}

HTPair* ht_set( HashTable* T, SGS_CTX, const char* str, int size, void* ptr )
{
	HTPair* p = ht_find( T, str, size, hashFunc( str, size ) );
	if( p )
	{
		p->ptr = ptr;
		return p;
	}
	else
	{
		sgs_Hash h;
		HTPair *pp, *pend;

		ht_check( T, C, 1 );
		h = hashFunc( str, size );
		T->load++;

		p = T->pairs + ( h % T->size );
		if( p->str == NULL )
		{
			ht_setpair( p, C, str, size, h, ptr );
			return p;
		}

		pp = p++, pend = T->pairs + T->size;
		if( p >= pend )
			p = T->pairs;
		while( p != pp )
		{
			if( p->str == NULL )
			{
				ht_setpair( p, C, str, size, h, ptr );
				return p;
			}
			p++;
			if( p >= pend )
				p = T->pairs;
		}
		sgs_BreakIf( "reached end of loop" );
	}
	return NULL;
}

static int ht_trymove( HashTable* T, HTPair* phole, HTPair* P )
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

static void ht_fillhole( HashTable* T, HTPair* P )
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

void ht_unset_pair( HashTable* T, SGS_CTX, HTPair* p )
{
	int osz = T->size;
	sgs_Dealloc( p->str );
	p->str = NULL;
	T->load--;
	ht_check( T, C, 0 );
	if( T->size == osz )
		ht_fillhole( T, p );
}

void ht_unset( HashTable* T, SGS_CTX, const char* str, int size )
{
	HTPair* p = ht_find( T, str, size, hashFunc( str, size ) );
	if( p )
	{
		ht_unset_pair( T, C, p );
	}
}

#else

static void htp_remove( HTPair** p, SGS_CTX )
{
	HTPair* pn = (*p)->next;
	sgs_Dealloc( (*p)->str );
	sgs_Dealloc( *p );
	*p = pn;
}

void ht_init( HashTable* T, SGS_CTX, int size )
{
	T->pairs = sgs_Alloc_n( HTPair*, size );
	T->size = size;
	T->load = 0;
	memset( T->pairs, 0, sizeof( HTPair* ) * size );
}

void ht_clear( sgs_HashTable* T, SGS_CTX )
{
	HTPair** p = T->pairs, **pend = T->pairs + T->size;
	while( p < pend )
	{
		while( *p )
			htp_remove( p, C );
		p++;
	}
	T->load = 0;
}

void ht_free( HashTable* T, SGS_CTX )
{
	ht_clear( T, C );
	sgs_Dealloc( T->pairs );
	T->pairs = NULL;
	T->size = 0;
}

void ht_rehash( HashTable* T, SGS_CTX, int size )
{
	HTPair** np, **p = T->pairs, **pend = T->pairs + T->size;
	
	if( size < 1 )
		size = 1;
	
	sgs_BreakIf( size < T->load );
	np = sgs_Alloc_n( HTPair*, size );
	memset( np, 0, sizeof( HTPair* ) * size );
	
	while( p < pend )
	{
		HTPair* pc = *p;
		while( pc )
		{
			HTPair* pn = pc->next;
			int hm = pc->hash % size;
			pc->next = np[ hm ];
			np[ hm ] = pc;
			pc = pn;
		}
		p++;
	}
	
	sgs_Dealloc( T->pairs );
	T->pairs = np;
	T->size = size;
}

void ht_check( HashTable* T, SGS_CTX, int inc )
{
	if( T->load + inc > T->size * 0.75 )
	{
		int newsize = (int)( T->size * 0.6 + ( T->load + inc ) * 0.6 );
		ht_rehash( T, C, newsize );
	}
	else if( T->load + inc < T->size * 0.25 )
	{
		int newsize = (int)( T->size * 0.5 + ( T->load + inc ) * 0.5 );
		ht_rehash( T, C, newsize );
	}
}

HTPair* ht_find( HashTable* T, const char* str, int size, sgs_Hash h )
{
	HTPair* p = T->pairs[ ( h % T->size ) ];
	while( p )
	{
		if( p->size == size && !memcmp( str, p->str, size ) )
			return p;
		p = p->next;
	}
	return NULL;
}

HTPair* ht_set( HashTable* T, SGS_CTX, const char* str, int size, void* ptr )
{
	sgs_Hash h = hashFunc( str, size );
	HTPair* p = ht_find( T, str, size, h );
	if( p )
		p->ptr = ptr;
	else
	{
		HTPair* np = sgs_Alloc( HTPair );
		
		ht_check( T, C, 1 );
		
		np->str = sgs_Alloc_n( char, size );
		np->ptr = ptr;
		memcpy( np->str, str, size );
		np->size = size;
		np->hash = h;
		
		np->next = T->pairs[ h % T->size ];
		T->pairs[ h % T->size ] = np;
		T->load++;
		p = np;
	}
	return p;
}

void ht_unset( sgs_HashTable* T, SGS_CTX, const char* str, int size )
{
	sgs_Hash h = hashFunc( str, size );
	HTPair** p = T->pairs + ( h % T->size );
	while( *p )
	{
		if( (*p)->size == size && !memcmp( str, (*p)->str, size ) )
		{
			htp_remove( p, C );
			T->load--;
			ht_check( T, C, -1 );
			return;
		}
		p = &(*p)->next;
	}
}

void ht_unset_pair( sgs_HashTable* T, SGS_CTX, sgs_HTPair* pair )
{
	sgs_Hash h = pair->hash;
	HTPair** p = T->pairs + ( h % T->size );
	while( *p )
	{
		if( *p == pair )
		{
			htp_remove( p, C );
			T->load--;
			ht_check( T, C, -1 );
			return;
		}
		p = &(*p)->next;
	}
}

void ht_iterate( sgs_HashTable* T, sgs_HTIterFunc func, void* userdata )
{
	HTPair** p = T->pairs, **pend = T->pairs + T->size;
	while( p < pend )
	{
		HTPair* sp = *p;
		while( sp )
		{
			func( sp, userdata );
			sp = sp->next;
		}
		p++;
	}
}

#endif



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
	if( str < end && ( *str == '.' || *str == 'E' || *str == 'e' ) )
		return strtonum_real( at, end, outf );
	else
	{
		sgs_Integer val = 0;
		int invsign = 0;

		str = *at;
		if( *str == '+' ) str++;
		else if( *str == '-' ){ invsign = 1; str++; }

		while( str < end && decchar( *str ) )
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
	if( str >= end )
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




/**** BEGIN CUSTOM QSORT CODE ****/

/*******************************************************************************
*
*  Author:  Remi Dufour - remi.dufour@gmail.com
*  ! code is modified !, for original, refer to:
*    http://www.codeproject.com/Articles/426706/A-simple-portable-yet-efficient-Quicksort-implemen
*  Date:    July 23rd, 2012
*
*  Name:        Quicksort
*
*  Description: This is a well-known sorting algorithm developed by C. A. R. 
*               Hoare. It is a comparison sort and in this implementation,
*               is not a stable sort.
*
*  Note:        This is public-domain C implementation written from
*               scratch.  Use it at your own risk.
*
*******************************************************************************/

/* Insertion sort threshold shift
 *
 * This macro defines the threshold shift (power of 2) at which the insertion
 * sort algorithm replaces the Quicksort.  A zero threshold shift disables the
 * insertion sort completely.
 *
 * The value is optimized for Linux and MacOS on the Intel x86 platform.
 */
#ifndef INSERTION_SORT_THRESHOLD_SHIFT
# if defined( __APPLE__ ) && defined( __MACH__ )
#  define INSERTION_SORT_THRESHOLD_SHIFT 0
# else
#  define INSERTION_SORT_THRESHOLD_SHIFT 2
# endif
#endif

/* Macro SWAP
 *
 * Swaps the elements of two arrays.
 *
 * The length of the swap is determined by the value of "SIZE".  While both
 * arrays can't overlap, the case in which both pointers are the same works.
 */
#define SWAP(A,B,SIZE)                               \
	{                                                \
		register char       *a_byte = A;             \
		register char       *b_byte = B;             \
		register const char *a_end  = a_byte + SIZE; \
		while (a_byte < a_end)                       \
		{                                            \
			register const char swap_byte = *b_byte; \
			*b_byte++ = *a_byte;                     \
			*a_byte++ = swap_byte;                   \
		}                                            \
	}

/* Macro SWAP_NEXT
 *
 * Swaps the elements of an array with its next value.
 *
 * The length of the swap is determined by the value of "size".  This macro
 * must be used at the beginning of a scope and "A" shouldn't be an expression.
 */
#define SWAP_NEXT(A,SIZE)                                 \
	register char       *a_byte = A;                      \
	register const char *a_end  = A + SIZE;               \
	while (a_byte < a_end)                                \
	{                                                     \
		register const char swap_byte = *(a_byte + SIZE); \
		*(a_byte + SIZE) = *a_byte;                       \
		*a_byte++ = swap_byte;                            \
	}

void quicksort( void *array, size_t length, size_t size,
	int(*compare)(const void *, const void *, void*), void* userdata)
{
	struct stackframe
	{
		void *left;
		void *right;
	} stack[CHAR_BIT * sizeof(void *)];

	/* Recursion level */
	struct stackframe *recursion = stack;

#if INSERTION_SORT_THRESHOLD_SHIFT != 0
	/* Insertion sort threshold */
	const int threshold = size << INSERTION_SORT_THRESHOLD_SHIFT;
#endif

	/* Assign the first recursion level of the sorting */
	recursion->left = array;
	recursion->right = (char *)array + size * (length - 1);

	do
	{
	    /* Partition the array */
		register char *index = (char*) recursion->left;
		register char *right = (char*) recursion->right;
		char          *left  = index;

	    /* Assigning store to the left */
		register char *store = index;

	    /* Pop the stack */
		--recursion;

	    /* Determine a pivot (in the middle) and move it to the end */
	    /* @modification@ changed the left address to something that works */
		SWAP(left + (((right - left) >> 1) / size * size),right,size)

	    /* From left to right */
		while (index < right)
		{
	        /* If item is smaller than pivot */
			if (compare(right, index, userdata) > 0)
			{
	            /* Swap item and store */
				SWAP(index,store,size)

	            /* We increment store */
				store += size;
			}

			index += size;
		}

	    /* Move the pivot to its final place */
		SWAP(right,store,size)

/* Performs a recursion to the left */
#define RECURSE_LEFT                     \
	if (left < store - size)             \
	{                                    \
		(++recursion)->left = left;      \
		recursion->right = store - size; \
	}

/* Performs a recursion to the right */
#define RECURSE_RIGHT                       \
	if (store + size < right)               \
	{                                       \
		(++recursion)->left = store + size; \
		recursion->right = right;           \
	}

/* Insertion sort inner-loop */
#define INSERTION_SORT_LOOP(LEFT)                                 \
	{                                                             \
		register char *trail = index - size;                      \
		while (trail >= LEFT && compare(trail, trail + size, userdata) > 0) \
		{                                                         \
			SWAP_NEXT(trail,size)                                 \
			trail -= size;                                        \
		}                                                         \
	}

/* Performs insertion sort left of the pivot */
#define INSERTION_SORT_LEFT                                \
	for (index = left + size; index < store; index +=size) \
		INSERTION_SORT_LOOP(left)

/* Performs insertion sort right of the pivot */
#define INSERTION_SORT_RIGHT                                        \
	for (index = store + (size << 1); index <= right; index +=size) \
		INSERTION_SORT_LOOP(store + size)

/* Sorts to the left */
#if INSERTION_SORT_THRESHOLD_SHIFT == 0
# define SORT_LEFT RECURSE_LEFT
#else
# define SORT_LEFT                 \
	if (store - left <= threshold) \
	{                              \
		INSERTION_SORT_LEFT        \
	}                              \
	else                           \
	{                              \
		RECURSE_LEFT               \
	}
#endif

/* Sorts to the right */
#if INSERTION_SORT_THRESHOLD_SHIFT == 0
# define SORT_RIGHT RECURSE_RIGHT
#else
# define SORT_RIGHT                 \
	if (right - store <= threshold) \
	{                               \
		INSERTION_SORT_RIGHT        \
	}                               \
	else                            \
	{                               \
		RECURSE_RIGHT               \
	}
#endif

		/* Recurse into the smaller partition first */
		if (store - left < right - store)
		{
		/* Left side is smaller */
			SORT_RIGHT
			SORT_LEFT

			continue;
		}

		/* Right side is smaller */
		SORT_LEFT
		SORT_RIGHT

#undef RECURSE_LEFT
#undef RECURSE_RIGHT
#undef INSERTION_SORT_LOOP
#undef INSERTION_SORT_LEFT
#undef INSERTION_SORT_RIGHT
#undef SORT_LEFT
#undef SORT_RIGHT
	}
	while (recursion >= stack);
}

#undef INSERTION_SORT_THRESHOLD_SHIFT
#undef SWAP
#undef SWAP_NEXT

/**** END CUSTOM QSORT CODE ****/


#define U8NFL( x ) ((x&0xC0)!=0x80)

int sgs_utf8_decode( char* buf, size_t size, uint32_t* outchar )
{
	char c;
	if( size == 0 )
		return 0;

	c = *buf;
	if( !( c & 0x80 ) )
	{
		*outchar = c;
		return 1;
	}

	if( ( c & 0xE0 ) == 0xC0 )
	{
		if( size < 2 || U8NFL( buf[1] ) )
			return - (int) MIN(size,2);
		*outchar = ( ( ((int)(buf[0]&0x1f)) << 6 ) | ((int)(buf[1]&0x3f)) );
		return 2;
	}

	if( ( c & 0xF0 ) == 0xE0 )
	{
		if( size < 3 || U8NFL( buf[1] ) || U8NFL( buf[2] ) )
			return - (int) MIN(size,3);
		*outchar = ( ( ((int)(buf[0]&0x0f)) << 12 ) | ( ((int)(buf[1]&0x3f)) << 6 )
			| ((int)(buf[2]&0x3f)) );
		return 3;
	}

	if( ( c & 0xF8 ) == 0xF0 )
	{
		if( size < 4 || U8NFL( buf[1] ) || U8NFL( buf[2] ) || U8NFL( buf[3] ) )
			return - (int) MIN(size,4);
		*outchar = ( ( ((int)(buf[0]&0x07)) << 18 ) | ( ((int)(buf[1]&0x3f)) << 12 )
				| ( ((int)(buf[2]&0x3f)) << 6 ) | ((int)(buf[3]&0x3f)) );
		return 4;
	}

	return -1;
}

int sgs_utf8_encode( uint32_t ch, char* out )
{
	if( ch <= 0x7f )
	{
		*out = (char) ch;
		return 1;
	}
	if( ch <= 0x7ff )
	{
		out[ 0 ] = 0xc0 | ( ( ch >> 6 ) & 0x1f );
		out[ 1 ] = 0x80 | ( ch & 0x3f );
		return 2;
	}
	if( ch <= 0xffff )
	{
		out[ 0 ] = 0xe0 | ( ( ch >> 12 ) & 0x0f );
		out[ 1 ] = 0x80 | ( ( ch >> 6 ) & 0x3f );
		out[ 2 ] = 0x80 | ( ch & 0x3f );
		return 3;
	}
	if( ch <= 0x10ffff )
	{
		out[ 0 ] = 0xf0 | ( ( ch >> 18 ) & 0x07 );
		out[ 1 ] = 0x80 | ( ( ch >> 12 ) & 0x3f );
		out[ 2 ] = 0x80 | ( ( ch >> 6 ) & 0x3f );
		out[ 3 ] = 0x80 | ( ch & 0x3f );
		return 4;
	}

	return 0;
}


