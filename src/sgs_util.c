

#include <time.h>
#include <signal.h>
#include <ctype.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <stddef.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"


void sgs_BreakIfFunc( const char* code, const char* file, int line )
{
	fprintf( stderr, "\n== Error detected: \"%s\", file: %s, line %d ==\n", code, file, line );
#if defined( _MSC_VER )
	__debugbreak();
#elif defined( __GNUC__ )
#  if SGS_ARCH_X86
	__asm__( "int $3" );
#  else
	__builtin_trap();
#  endif
#else
	assert( 0 );
#endif
}


void print_safe( FILE* fp, const char* buf, int32_t size )
{
	int32_t i;
	for( i = 0; i < size; ++i )
	{
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

MemBuf membuf_partial( char* ch, size_t size )
{
	MemBuf sb;
	sb.ptr = ch;
	sb.size = size;
	sb.mem = size;
	return sb;
}

void membuf_reserve( MemBuf* mb, SGS_CTX, size_t size )
{
	if( size <= mb->mem )
		return;

	mb->mem = size;
	mb->ptr = (char*) sgs_Realloc( C, mb->ptr, size );
}

void membuf_resize( MemBuf* mb, SGS_CTX, size_t size )
{
	membuf_reserve( mb, C, size );
	mb->size = size;
}

void membuf_resize_opt( MemBuf* mb, SGS_CTX, size_t size )
{
	if( size > mb->mem )
		membuf_reserve( mb, C, mb->mem * 2 < size ? size : mb->mem * 2 );
	if( size > mb->size )
		mb->size = size;
}

void membuf_insbuf( MemBuf* mb, SGS_CTX, size_t pos, const void* buf, size_t size )
{
	membuf_reserve( mb, C, mb->mem < mb->size + size ? MAX( mb->mem * 2, mb->size + size ) : 0 );
	memmove( mb->ptr + pos + size, mb->ptr + pos, mb->size - pos );
	memcpy( mb->ptr + pos, buf, size );
	mb->size += size;
}

void membuf_erase( MemBuf* mb, size_t from, size_t to )
{
	sgs_BreakIf( from >= mb->size );
	sgs_BreakIf( to >= mb->size );
	if( mb->size - to > 1 )
		memmove( mb->ptr + from, mb->ptr + to + 1, mb->size - to - 1 );
	mb->size -= to - from + 1;
}

void membuf_appbuf( MemBuf* mb, SGS_CTX, const void* buf, size_t size )
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
	return h | 0x80000000;
}

sgs_Hash sgs_HashVar( const sgs_Variable* v )
{
	switch( BASETYPE( v->type ) )
	{
	case SVT_NULL: return 0;
	case SVT_BOOL: return v->data.B;
	case SVT_STRING: return hashFunc( var_cstr( v ), v->data.S->size );
	}
	return hashFunc( (const char*) &v->data, sizeof(v->data) );
}



static int equal_variables( sgs_Variable* v1, sgs_Variable* v2 )
{
	if( BASETYPE( v1->type ) != BASETYPE( v2->type ) )
		return 0;
	switch( BASETYPE( v1->type ) )
	{
	case SVT_BOOL: return v1->data.B == v2->data.B;
	case SVT_INT: return v1->data.I == v2->data.I;
	case SVT_REAL: return v1->data.R == v2->data.R;
	case SVT_STRING: return v1->data.S->size == v2->data.S->size &&
		memcmp( var_cstr( v1 ), var_cstr( v2 ), v1->data.S->size ) == 0;
	case SVT_FUNC: return v1->data.F == v2->data.F;
	case SVT_CFUNC: return v1->data.C == v2->data.C;
	case SVT_OBJECT: return v1->data.O == v2->data.O;
	case SVT_PTR: return v1->data.P == v2->data.P;
	}
	return 1;
}


void vht_init( sgs_VHTable* T, SGS_CTX, VHTIdx initial_pair_mem, VHTIdx initial_var_mem )
{
	sgs_BreakIf( initial_pair_mem < 1 );
	sgs_BreakIf( initial_var_mem < 1 );
	
	T->pairs = sgs_Alloc_n( VHTIdx, initial_pair_mem );
	T->pair_mem = initial_pair_mem;
	T->vars = sgs_Alloc_n( VHTVar, initial_var_mem );
	T->var_mem = initial_var_mem;
	T->size = 0;
	T->num_rem = 0;
	
	memset( T->pairs, SGS_VHTIDX_EMPTY, sizeof(VHTIdx) * initial_pair_mem );
}

void vht_free( sgs_VHTable* T, SGS_CTX )
{
	sgs_VHTVar* p = T->vars;
	sgs_VHTVar* pend = p + T->size;
	while( p < pend )
	{
		sgs_Release( C, &p->key );
		sgs_Release( C, &p->val );
		p++;
	}
	
	sgs_Dealloc( T->pairs );
	sgs_Dealloc( T->vars );
}

void vht_rehash( VHTable* T, SGS_CTX, VHTIdx size )
{
	sgs_Hash h;
	VHTIdx i, si, sp, idx, *np;
	sgs_BreakIf( size < T->size );
	
	if( size == T->pair_mem )
		return;
	if( size < 4 )
		size = 4;
	
	np = sgs_Alloc_n( VHTIdx, size );
	memset( np, SGS_VHTIDX_EMPTY, sizeof(VHTIdx) * size );
	
//	printf( "rehash %d -> %d (size = %d, mem = %d kB)\n", T->pair_mem, size, T->size,
//		(size * sizeof(VHTIdx) + T->var_mem * sizeof(VHTVar)) / 1024 );
	
	for( si = 0; si < T->pair_mem; ++si )
	{
		idx = T->pairs[ si ];
		if( idx >= 0 )
		{
			h = T->vars[ idx ].hash;
			sp = i = h % size;
			do
			{
				VHTIdx nidx = np[ i ];
				if( nidx == SGS_VHTIDX_EMPTY )
				{
					np[ i ] = idx;
					break;
				}
				i++;
				if( i >= size )
					i = 0;
			}
			while( i != sp );
		}
	}
	
	sgs_Dealloc( T->pairs );
	T->pairs = np;
	T->pair_mem = size;
	T->num_rem = 0;
}

void vht_reserve( VHTable* T, SGS_CTX, VHTIdx size )
{
	VHTVar* p;
	
	sgs_BreakIf( size < T->size );
	
	if( size == T->var_mem )
		return;
	if( size < 4 )
		size = 4;
	
//	printf( "reserve %d -> %d (size = %d, mem = %d kB)\n", T->var_mem, size, T->size,
//		(T->pair_mem * sizeof(VHTIdx) + size * sizeof(VHTVar)) / 1024 );
	
	p = sgs_Alloc_n( VHTVar, size );
	memcpy( p, T->vars, sizeof(VHTVar) * T->size );
	sgs_Dealloc( T->vars );
	T->vars = p;
	T->var_mem = size;
}

VHTIdx vht_pair_id( VHTable* T, sgs_Variable* K, sgs_Hash hash )
{
	VHTIdx i, sp = hash % T->pair_mem;
	i = sp;
	do
	{
		VHTIdx idx = T->pairs[ i ];
		if( idx == SGS_VHTIDX_EMPTY )
			break;
		if( idx != SGS_VHTIDX_REMOVED && equal_variables( K, &T->vars[ idx ].key ) )
			return i;
		i++;
		if( i >= T->pair_mem )
			i = 0;
	}
	while( i != sp );
	return -1;
}

VHTVar* vht_get( VHTable* T, sgs_Variable* K )
{
	VHTIdx i = vht_pair_id( T, K, sgs_HashVar( K ) );
	if( i >= 0 )
		return T->vars + T->pairs[ i ];
	else
		return NULL;
}

VHTVar* vht_get_str( VHTable* T, const char* str, sgs_SizeVal size, sgs_Hash hash )
{
	VHTIdx i, sp = hash % T->pair_mem;
	i = sp;
	do
	{
		VHTIdx idx = T->pairs[ i ];
		if( idx == SGS_VHTIDX_EMPTY )
			return NULL;
		else
		{
			sgs_Variable* var = &T->vars[ idx ].key;
			if( BASETYPE( var->type ) == SVT_STRING )
			{
				string_t* S = var->data.S;
				if( S->size == size && memcmp( str_cstr( S ), str, size ) == 0 )
					return T->vars + idx;
			}
		}
		i++;
		if( i >= T->pair_mem )
			i = 0;
	}
	while( i != sp );
	return NULL;
}

VHTVar* vht_set( VHTable* T, SGS_CTX, sgs_Variable* K, sgs_Variable* V )
{
	sgs_Hash h = sgs_HashVar( K );
	VHTIdx sp, i = vht_pair_id( T, K, h );
	if( i >= 0 )
	{
		VHTVar* p = T->vars + T->pairs[ i ];
		sgs_Release( C, &p->val );
		if( V )
		{
			sgs_Acquire( C, V );
			p->val = *V;
		}
		else
			p->val.type = VTC_NULL;
		return p;
	}
	else
	{
		VHTIdx osize = T->size;
		UNUSED( osize );
		
		/* prefer to rehash if too many removed (num_rem) items are found */
		if( T->size + T->num_rem + 1.0 >= T->pair_mem * 0.7 )
			vht_rehash( T, C, (sgs_VHTIdx) MAX( T->pair_mem * 1.5, T->size + 16 ) );
		if( T->size >= T->var_mem )
			vht_reserve( T, C, (sgs_VHTIdx) MAX( T->size * 1.5, T->size + 16 ) );
		
		{
			VHTVar* p = T->vars + T->size;
			p->key = *K;
			p->hash = h;
			sgs_Acquire( C, K );
			if( V )
			{
				p->val = *V;
				sgs_Acquire( C, V );
			}
			else
				p->val.type = VTC_NULL;
		}
		
		sp = i = h % T->pair_mem;
		do
		{
			VHTIdx idx = T->pairs[ i ];
			if( idx == SGS_VHTIDX_EMPTY || idx == SGS_VHTIDX_REMOVED )
			{
				if( idx == SGS_VHTIDX_REMOVED )
					T->num_rem--;
				T->pairs[ i ] = T->size;
				T->size++;
				break;
			}
			i++;
			if( i >= T->pair_mem )
				i = 0;
		}
		while( i != sp );
		
		sgs_BreakIf( T->size == osize );
		
		return T->vars + T->size - 1;
	}
}

void vht_unset( VHTable* T, SGS_CTX, sgs_Variable* K )
{
	sgs_Hash h = sgs_HashVar( K );
	VHTIdx i = vht_pair_id( T, K, h );
	if( i >= 0 )
	{
		VHTIdx idx = T->pairs[ i ];
		VHTVar* p = T->vars + idx;
		VHTVar bp = *p;
		
		T->pairs[ i ] = SGS_VHTIDX_REMOVED;
		
		T->num_rem++;
		T->size--;
		if( p < T->vars + T->size )
		{
			VHTVar* ep = T->vars + T->size;
			i = vht_pair_id( T, &ep->key, ep->hash );
			sgs_BreakIf( i == -1 );
			*p = *ep;
			T->pairs[ i ] = idx;
		}
		
		sgs_Release( C, &bp.key );
		sgs_Release( C, &bp.val );
	}
	
	if( T->num_rem > T->var_mem * 0.25 + 16 )
	{
		vht_reserve( T, C, (sgs_VHTIdx) ( T->size * 0.75 + T->var_mem * 0.25 ) );
		vht_rehash( T, C, (sgs_VHTIdx) ( T->size * 0.5 + T->var_mem * 0.5 ) );
	}
}


double sgs_GetTime()
{
#ifdef __linux
	struct timespec ts;
	clock_gettime( CLOCK_MONOTONIC, &ts );
	return (double) ts.tv_sec + 0.000000001 * (double) ts.tv_nsec;
#else
	clock_t clk = clock();
	return (double)( clk ) / (double)( CLOCKS_PER_SEC );
#endif
}


/* string -> number conversion */

typedef const char CCH;

static int strtonum_hex( CCH** at, CCH* end, sgs_Int* outi )
{
	sgs_Int val = 0;
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

static int strtonum_oct( CCH** at, CCH* end, sgs_Int* outi )
{
	sgs_Int val = 0;
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

static int strtonum_bin( CCH** at, CCH* end, sgs_Int* outi )
{
	sgs_Int val = 0;
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
	CCH* str = *at, *teststr;

	if( *str == '+' ) str++;
	else if( *str == '-' ){ vsign = -1; str++; }

	teststr = str;
	while( str < end && decchar( *str ) )
	{
		val *= 10;
		val += getdec( *str );
		str++;
	}
	if( str == teststr )
		return 0;
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
	if( str + 2 >= end )
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

static int strtonum_dec( CCH** at, CCH* end, sgs_Int* outi, sgs_Real* outf )
{
	CCH* str = *at, *teststr;
	if( *str == '+' || *str == '-' ) str++;
	teststr = str;
	while( str < end && decchar( *str ) )
		str++;
	if( str == teststr )
		return 0;
	if( str < end && ( *str == '.' || *str == 'E' || *str == 'e' ) )
		return strtonum_real( at, end, outf );
	else
	{
		sgs_Int val = 0;
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

int util_strtonum( CCH** at, CCH* end, sgs_Int* outi, sgs_Real* outf )
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


sgs_Int util_atoi( const char* str, int len )
{
	sgs_Int vi = 0;
	sgs_Real vr = 0;
	const char* p = str;
	int ret = util_strtonum( &p, str + len, &vi, &vr );
	if( p == str ) return 0;
	if( ret == 1 ) return vi;
	else if( ret == 2 ) return (sgs_Int) vr;
	else return 0;
}

sgs_Real util_atof( const char* str, int len )
{
	sgs_Int vi = 0;
	sgs_Real vr = 0;
	const char* p = str;
	int ret = util_strtonum( &p, str + len, &vi, &vr );
	if( p == str ) return 0;
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
	
	if( length <= 1 )
		return;

	/* Assign the first recursion level of the sorting */
	recursion->left = array;
	recursion->right = (char *)array + size * (length - 1);

	do
	{
	    /* Partition the array */
		register char *idx = (char*) recursion->left;
		register char *right = (char*) recursion->right;
		char          *left  = idx;

	    /* Assigning store to the left */
		register char *store = idx;

	    /* Pop the stack */
		--recursion;

	    /* Determine a pivot (in the middle) and move it to the end */
	    /* @modification@ changed the left address to something that works */
		SWAP(left + (((right - left) >> 1) / size * size),right,size)

	    /* From left to right */
		while (idx < right)
		{
	        /* If item is smaller than pivot */
			if (compare(right, idx, userdata) > 0)
			{
	            /* Swap item and store */
				SWAP(idx,store,size)

	            /* We increment store */
				store += size;
			}

			idx += size;
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
		register char *trail = idx - size;                        \
		while (trail >= LEFT && compare(trail, trail + size, userdata) > 0) \
		{                                                         \
			SWAP_NEXT(trail,size)                                 \
			trail -= size;                                        \
		}                                                         \
	}

/* Performs insertion sort left of the pivot */
#define INSERTION_SORT_LEFT                                \
	for (idx = left + size; idx < store; idx +=size)       \
		INSERTION_SORT_LOOP(left)

/* Performs insertion sort right of the pivot */
#define INSERTION_SORT_RIGHT                                        \
	for (idx = store + (size << 1); idx <= right; idx +=size)       \
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


