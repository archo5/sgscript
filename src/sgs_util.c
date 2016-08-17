

#include <time.h>
#include <math.h>
#if !defined( _MSC_VER ) && !defined( __GNUC__ )
#  include <assert.h>
#endif
#include <limits.h>

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


int sgs_isoneof( int chr, const char* str )
{
	while( *str )
	{
		if( *str == chr )
			return 1;
		str++;
	}
	return 0;
}

int sgs_hexchar( int c )
{
	return ( (c) >= '0' && (c) <= '9' ) ||
	( (c) >= 'a' && (c) <= 'f' ) || ( (c) >= 'A' && (c) <= 'F' );
}

int sgs_gethex( int c )
{
	return ( (c) >= '0' && (c) <= '9' ) ? ( (c) - '0' ) :
	( ( (c) >= 'a' && (c) <= 'f' ) ? ( (c) - 'a' + 10 ) : ( (c) - 'A' + 10 ) );
}

int sgs_tolower( int c )
{
	return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}


void sgs_print_safe( FILE* fp, const char* buf, size_t size )
{
	size_t i;
	for( i = 0; i < size; ++i )
	{
		if( sgs_isgraph( buf[ i ] ) || buf[ i ] == ' ' )
			fputc( buf[ i ], fp );
		else
			fprintf( fp, "\\x%02X", (int) (unsigned char) buf[ i ] );
	}
}


sgs_MemBuf sgs_membuf_create( void )
{
	sgs_MemBuf sb = { NULL, 0, 0 };
	return sb;
}

void sgs_membuf_destroy( sgs_MemBuf* sb, SGS_CTX )
{
	if( sb->ptr )
		sgs_Dealloc( sb->ptr );
	sb->ptr = NULL;
}

sgs_MemBuf sgs_membuf_partial( char* ch, size_t size )
{
	sgs_MemBuf sb;
	sb.ptr = ch;
	sb.size = size;
	sb.mem = size;
	return sb;
}

void sgs_membuf_reserve( sgs_MemBuf* mb, SGS_CTX, size_t size )
{
	if( size <= mb->mem )
		return;

	mb->mem = size;
	mb->ptr = (char*) sgs_Realloc( C, mb->ptr, size );
}

void sgs_membuf_resize( sgs_MemBuf* mb, SGS_CTX, size_t size )
{
	sgs_membuf_reserve( mb, C, size );
	mb->size = size;
}

void sgs_membuf_resize_opt( sgs_MemBuf* mb, SGS_CTX, size_t size )
{
	if( size > mb->mem )
		sgs_membuf_reserve( mb, C, mb->mem * 2 < size ? size : mb->mem * 2 );
	if( size > mb->size )
		mb->size = size;
}

void sgs_membuf_insbuf( sgs_MemBuf* mb, SGS_CTX, size_t pos, const void* buf, size_t size )
{
	sgs_membuf_reserve( mb, C, mb->mem < mb->size + size ? SGS_MAX( mb->mem * 2, mb->size + size ) : 0 );
	memmove( mb->ptr + pos + size, mb->ptr + pos, mb->size - pos );
	memcpy( mb->ptr + pos, buf, size );
	mb->size += size;
}

void sgs_membuf_erase( sgs_MemBuf* mb, size_t from, size_t to )
{
	sgs_BreakIf( from > mb->size );
	sgs_BreakIf( to > mb->size );
	sgs_BreakIf( from > to );
	if( mb->size - to > 0 )
		memmove( mb->ptr + from, mb->ptr + to, mb->size - to );
	mb->size -= to - from;
}

void sgs_membuf_appbuf( sgs_MemBuf* mb, SGS_CTX, const void* buf, size_t size )
{
	sgs_membuf_reserve( mb, C, mb->mem < mb->size + size ? SGS_MAX( mb->mem * 2, mb->size + size ) : 0 );
	memcpy( mb->ptr + mb->size, buf, size );
	mb->size += size;
}


sgs_Hash sgs_HashFunc( const char* str, size_t size )
{
	size_t i, adv = size / 127 + 1;
	sgs_Hash h = 2166136261u;
	for( i = 0; i < size; i += adv )
	{
		h ^= (sgs_Hash) (uint8_t) str[ i ];
		h *= 16777619u;
	}
	return h;
}

sgs_Hash sgs_HashVar( const sgs_Variable* v )
{
	size_t size;
	switch( v->type )
	{
	/* special */
	case SGS_VT_NULL: return 0;
	case SGS_VT_BOOL: return ( v->data.B != 0 );
	case SGS_VT_STRING: return v->data.S->hash;
	/* data */
	case SGS_VT_INT: size = sizeof( sgs_Int ); break;
	case SGS_VT_REAL: size = sizeof( sgs_Real ); break;
	case SGS_VT_FUNC:
	case SGS_VT_CFUNC:
	case SGS_VT_OBJECT:
	case SGS_VT_PTR:
	case SGS_VT_THREAD:
		size = sizeof( void* ); break;
	default:
		return 0;
	}
	return sgs_HashFunc( (const char*) &v->data, size );
}


// http://stackoverflow.com/a/5694432 implementation 5
static int sgs_is_prime( size_t x )
{
	size_t i, o = 4;
	for( i = 5; 1; i += o )
	{
		size_t q = x / i;
		if (q < i)
			return 1;
		if (x == q * i)
			return 0;
		o ^= 6;
	}
	return 1;
}
static size_t sgs_next_prime( size_t x )
{
	switch( x )
	{
	case 0:
	case 1:
	case 2: return 2;
	case 3: return 3;
	case 4:
	case 5: return 5;
	}
	size_t k = x / 6;
	size_t i = x - 6 * k;
	size_t o = i < 2 ? 1 : 5;
	x = 6 * k + o;
	for( i = ( 3 + o ) / 2; !sgs_is_prime( x ); x += i )
		i ^= 6;
	return x;
}



static int equal_variables( sgs_Variable* v1, sgs_Variable* v2 )
{
	if( v1->type != v2->type )
		return 0;
	switch( v1->type )
	{
	case SGS_VT_NULL: return 1;
	case SGS_VT_BOOL: return v1->data.B == v2->data.B;
	case SGS_VT_INT: return v1->data.I == v2->data.I;
	case SGS_VT_REAL: return v1->data.R == v2->data.R;
	case SGS_VT_STRING:
#if SGS_STRINGTABLE_MAXLEN >= 0x7fffffff
		return v1->data.S == v2->data.S;
#else
		if( v1->data.S == v2->data.S ) return 1;
		return v1->data.S->size == v2->data.S->size &&
			memcmp( sgs_var_cstr( v1 ), sgs_var_cstr( v2 ), v1->data.S->size ) == 0;
#endif
	case SGS_VT_FUNC: return v1->data.F == v2->data.F;
	case SGS_VT_CFUNC: return v1->data.C == v2->data.C;
	case SGS_VT_OBJECT: return v1->data.O == v2->data.O;
	case SGS_VT_PTR: return v1->data.P == v2->data.P;
	case SGS_VT_THREAD: return v1->data.T == v2->data.T;
	}
	return v1->data.P == v2->data.P;
}


#define SGSCFG_VHT_PROBE_DIST 2
#define SGSCFG_VHT_ENABLE_ROBIN_HOOD_HASHING 1

#define SGS_VHT_PROBE_ADV( i, size ) i += SGSCFG_VHT_PROBE_DIST; if( i >= (size) ) i -= (size);
/****  ^ slightly faster on x86 than:  i = ( i + SGSCFG_VHT_PROBE_DIST ) % size;  ****/
#define SGS_VHT_PAIR_MEM_( T ) ((unsigned)(T)->pair_mem)
#define SGS_VHT_PROBE_LEN_( T, pos_idx, pos_hash ) (((SGS_VHT_PAIR_MEM_(T) + (unsigned)(pos_idx) - (pos_hash) % SGS_VHT_PAIR_MEM_(T)) % SGS_VHT_PAIR_MEM_(T))/SGSCFG_VHT_PROBE_DIST)

void sgs_vht_init( sgs_VHTable* T, SGS_CTX, sgs_VHTIdx initial_pair_mem, sgs_VHTIdx initial_var_mem )
{
	sgs_BreakIf( initial_pair_mem < 1 );
	sgs_BreakIf( initial_var_mem < 1 );
	
	T->pairs = sgs_Alloc_n( sgs_VHTIdx, (size_t) initial_pair_mem );
	T->pair_mem = initial_pair_mem;
	T->vars = sgs_Alloc_n( sgs_VHTVar, (size_t) initial_var_mem );
	T->var_mem = initial_var_mem;
	T->size = 0;
	T->num_rem = 0;
	
	memset( T->pairs, SGS_VHTIDX_EMPTY, sizeof(sgs_VHTIdx) * (size_t) initial_pair_mem );
}

void sgs_vht_free( sgs_VHTable* T, SGS_CTX )
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

void sgs_vht_analyze( sgs_VHTable* T, sgs_VHTStats* io )
{
	unsigned numcols = 0, numused = 0, numempty = 0, numrem = 0, distsum = 0, worstdist = 0;
	
	sgs_VHTIdx* p = T->pairs;
	sgs_VHTIdx* pend = p + T->pair_mem;
	
	if( io->print )
	{
		printf( "Hash table %p [size=%d, pair_mem=%d var_mem=%d] ---\n", T,
			T->size, T->pair_mem, T->var_mem );
	}
	
	while( p < pend )
	{
		if( *p == SGS_VHTIDX_EMPTY )
			numempty++;
		else if( *p == SGS_VHTIDX_REMOVED )
			numrem++;
		else
		{
			sgs_VHTVar* v = T->vars + *p;
			unsigned dist = SGS_VHT_PROBE_LEN_( T, p - T->pairs, v->hash );
			
			numused++;
			distsum += dist + 1;
			if( worstdist < dist + 1 )
				worstdist = dist + 1;
			
			if( v->hash % (unsigned) T->pair_mem != ( p - T->pairs ) )
			{
				numcols++;
				if( io->print && io->print_cols )
				{
					printf( "collision: hash=0x%08X mod=%u loc=%u dist=%u\n",
						v->hash, v->hash % (unsigned) T->pair_mem, (unsigned) ( p - T->pairs ), dist );
				}
			}
		}
		++p;
	}
	
	if( io->print )
	{
		if( io->print_ubmp )
		{
			printf( "--- usage bitmap ---\n" );
			p = T->pairs;
			while( p < pend )
			{
				if( *p == SGS_VHTIDX_EMPTY )
					printf( " " );
				else if( *p == SGS_VHTIDX_REMOVED )
					printf( "r" );
				else
				{
					unsigned dist = SGS_VHT_PROBE_LEN_( T, p - T->pairs, T->vars[ *p ].hash );
					if( dist < 10 )
						printf( "%u", dist );
					else
						printf( "#" );
				}
				++p;
			}
			printf( "\n" );
		}
		
		printf( "--- summary ---\n" );
		printf( "# used: %u\n", numused );
		printf( "# empty: %u\n", numempty );
		printf( "# removed: %u\n", numrem );
		printf( "# collisions: %u\n", numcols );
		printf( "> average probe length: %.2f\n", (float) distsum / (float) numused );
		printf( "> worst probe length: %u\n", worstdist );
		{
			float fbkts = (float) T->pair_mem, fins = (float) numused, fcols = (float) numcols;
			printf( "%% collisions: %.2f%% (expected=%.2f%%)\n", fcols / fbkts * 100, (fins - fbkts * (1 - powf((fbkts-1)/fbkts, fins))) / fbkts * 100);
		}
		printf( "---\n" );
	}
	
	io->buckets = (unsigned) T->pair_mem;
	io->used = numused;
	io->empty = numempty;
	io->removed = numrem;
	io->collisions = numcols;
	io->worst_probe_length = worstdist;
	io->avg_probe_length = (float) distsum / (float) numused;
}

static void sgs_vht_rehash( sgs_VHTable* T, SGS_CTX, sgs_VHTIdx size )
{
	sgs_Hash h;
	sgs_VHTIdx i, si, sp, idx, *np;
	sgs_BreakIf( size < T->size );
	
	if( size < 4 )
		size = 4;
	size = (sgs_VHTIdx) sgs_next_prime( (size_t) size );
	if( size == T->pair_mem )
		return;
	
	np = sgs_Alloc_n( sgs_VHTIdx, (size_t) size );
	memset( np, SGS_VHTIDX_EMPTY, sizeof(sgs_VHTIdx) * (size_t) size );
	
#if 0
	printf( "rehash %d -> %d (size = %d, mem = %d kB)\n", T->pair_mem, size, T->size,
		(size * sizeof(sgs_VHTIdx) + T->var_mem * sizeof(sgs_VHTVar)) / 1024 );
#endif
	
	for( si = 0; si < T->pair_mem; ++si )
	{
		idx = T->pairs[ si ];
		if( idx >= 0 )
		{
			h = T->vars[ idx ].hash;
			sp = i = (sgs_VHTIdx)( h % (sgs_Hash) size );
			do
			{
				sgs_VHTIdx nidx = np[ i ];
				if( nidx == SGS_VHTIDX_EMPTY )
				{
					np[ i ] = idx;
					break;
				}
				SGS_VHT_PROBE_ADV( i, size );
			}
			while( i != sp );
		}
	}
	
	sgs_Dealloc( T->pairs );
	T->pairs = np;
	T->pair_mem = size;
	T->num_rem = 0;
}

static void sgs_vht_reserve( sgs_VHTable* T, SGS_CTX, sgs_VHTIdx size )
{
	sgs_VHTVar* p;
	
	sgs_BreakIf( size < T->size );
	
	if( size == T->var_mem )
		return;
	if( size < 4 )
		size = 4;
	
#if 0
	printf( "reserve %d -> %d (size = %d, mem = %d kB)\n", T->var_mem, size, T->size,
		(T->pair_mem * sizeof(sgs_VHTIdx) + size * sizeof(sgs_VHTVar)) / 1024 );
#endif
	
	/* WP: hash table limit */
	p = sgs_Alloc_n( sgs_VHTVar, (size_t) size );
	memcpy( p, T->vars, sizeof(sgs_VHTVar) * (size_t) T->size );
	sgs_Dealloc( T->vars );
	T->vars = p;
	T->var_mem = size;
}

sgs_VHTIdx sgs_vht_pair_id( sgs_VHTable* T, sgs_Variable* K, sgs_Hash hash )
{
	sgs_VHTIdx i, sp = (sgs_VHTIdx)( hash % (sgs_Hash) T->pair_mem );
	i = sp;
	do
	{
		sgs_VHTIdx idx = T->pairs[ i ];
		if( idx == SGS_VHTIDX_EMPTY )
			break;
		if( idx != SGS_VHTIDX_REMOVED && equal_variables( K, &T->vars[ idx ].key ) )
			return i;
		SGS_VHT_PROBE_ADV( i, T->pair_mem );
	}
	while( i != sp );
	return -1;
}

sgs_VHTVar* sgs_vht_get( sgs_VHTable* T, sgs_Variable* K )
{
	sgs_VHTIdx i = sgs_vht_pair_id( T, K, sgs_HashVar( K ) );
	if( i >= 0 )
		return T->vars + T->pairs[ i ];
	else
		return NULL;
}

sgs_VHTVar* sgs_vht_get_str( sgs_VHTable* T, const char* str, uint32_t size, sgs_Hash hash )
{
	sgs_VHTIdx i, sp = (sgs_VHTIdx)( hash % (sgs_Hash) T->pair_mem );
	i = sp;
	do
	{
		sgs_VHTIdx idx = T->pairs[ i ];
		if( idx == SGS_VHTIDX_EMPTY )
			return NULL;
		else if( idx != SGS_VHTIDX_REMOVED )
		{
			sgs_Variable* var = &T->vars[ idx ].key;
			if( var->type == SGS_VT_STRING )
			{
				sgs_iStr* S = var->data.S;
				if( S->size == size && memcmp( sgs_str_cstr( S ), str, size ) == 0 )
					return T->vars + idx;
			}
		}
		SGS_VHT_PROBE_ADV( i, T->pair_mem );
	}
	while( i != sp );
	return NULL;
}

sgs_VHTVar* sgs_vht_set( sgs_VHTable* T, SGS_CTX, sgs_Variable* K, sgs_Variable* V )
{
	sgs_Hash h = sgs_HashVar( K );
	sgs_VHTIdx sp, i = sgs_vht_pair_id( T, K, h );
	if( i >= 0 )
	{
		sgs_VHTVar* p = T->vars + T->pairs[ i ];
		if( V )
			sgs_Acquire( C, V );
		sgs_Release( C, &p->val );
		if( V )
			p->val = *V;
		else
			p->val.type = SGS_VT_NULL;
		return p;
	}
	else
	{
		unsigned curdist = 0; /* current probe length for item to be inserted */
		sgs_VHTIdx
			osize = T->size, /* original hash table item count */
			ipos = T->size; /* currently inserted item index */
		SGS_UNUSED( osize );
		
		/* prefer to rehash if too many removed (num_rem) items are found */
		if( T->size + T->num_rem + 1.0 >= T->pair_mem * 0.7 )
			sgs_vht_rehash( T, C, (sgs_VHTIdx) SGS_MAX( T->pair_mem * 1.5, T->size + 16 ) );
		if( T->size >= T->var_mem )
			sgs_vht_reserve( T, C, (sgs_VHTIdx) SGS_MAX( T->size * 1.5, T->size + 16 ) );
		
		{
			sgs_VHTVar* p = T->vars + T->size;
			p->key = *K;
			p->hash = h;
			sgs_Acquire( C, K );
			if( V )
			{
				p->val = *V;
				sgs_Acquire( C, V );
			}
			else
				p->val.type = SGS_VT_NULL;
		}
		
		sp = i = (sgs_VHTIdx)( h % (sgs_Hash) T->pair_mem );
		do
		{
			sgs_VHTIdx idx = T->pairs[ i ];
			if( idx == SGS_VHTIDX_EMPTY || idx == SGS_VHTIDX_REMOVED )
			{
				if( idx == SGS_VHTIDX_REMOVED )
					T->num_rem--;
				T->pairs[ i ] = ipos;
				T->size++;
				break;
			}
#if SGSCFG_VHT_ENABLE_ROBIN_HOOD_HASHING
			else
			{
				/* occupied, try Robin Hood Hashing */
				
				/* probe length for existing item */
				unsigned exdist = SGS_VHT_PROBE_LEN_( T, i, T->vars[ idx ].hash );
				
				if( exdist < curdist )
				{
					/* success, swap current / new items */
					T->pairs[ i ] = ipos;
					ipos = idx;
					curdist = exdist;
				}
			}
#endif
			SGS_VHT_PROBE_ADV( i, T->pair_mem );
			curdist++;
		}
		while( i != sp );
		
		sgs_BreakIf( T->size == osize );
		
		return T->vars + T->size - 1;
	}
}

void sgs_vht_unset( sgs_VHTable* T, SGS_CTX, sgs_Variable* K )
{
	sgs_Hash h = sgs_HashVar( K );
	sgs_VHTIdx i = sgs_vht_pair_id( T, K, h );
	if( i >= 0 )
	{
		sgs_VHTIdx idx = T->pairs[ i ];
		sgs_VHTVar* p = T->vars + idx;
		sgs_VHTVar bp = *p;
		
		T->pairs[ i ] = SGS_VHTIDX_REMOVED;
		
		T->num_rem++;
		T->size--;
		if( p < T->vars + T->size )
		{
			sgs_VHTVar* ep = T->vars + T->size;
			i = sgs_vht_pair_id( T, &ep->key, ep->hash );
			sgs_BreakIf( i == -1 );
			*p = *ep;
			T->pairs[ i ] = idx;
		}
		
		sgs_Release( C, &bp.key );
		sgs_Release( C, &bp.val );
	}
	
	if( T->num_rem > T->var_mem * 0.25 + 16 )
	{
		sgs_vht_reserve( T, C, (sgs_VHTIdx) ( T->size * 0.75 + T->var_mem * 0.25 ) );
		sgs_vht_rehash( T, C, (sgs_VHTIdx) ( T->size * 0.5 + T->var_mem * 0.5 ) );
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
	while( str < end && sgs_hexchar( *str ) )
	{
		val *= 16;
		val += sgs_gethex( *str );
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
	while( str < end && sgs_octchar( *str ) )
	{
		val *= 8;
		val += sgs_getoct( *str );
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
	while( str < end && sgs_binchar( *str ) )
	{
		val *= 2;
		val += sgs_getbin( *str );
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
	while( str < end && sgs_decchar( *str ) )
	{
		val *= 10;
		val += sgs_getdec( *str );
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
		while( str < end && sgs_decchar( *str ) )
		{
			mult /= 10;
			val += sgs_getdec( *str ) * mult;
			str++;
		}
	}
	if( str < end && ( *str == 'e' || *str == 'E' ) )
	{
		sgs_Real sign, e = 0;
		str++;
		if( str >= end || ( *str != '+' && *str != '-' ) )
			goto done;
		sign = *str++ == '-' ? -1 : 1;
		while( str < end && sgs_decchar( *str ) )
		{
			e *= 10;
			e += sgs_getdec( *str );
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
	while( str < end && sgs_decchar( *str ) )
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
		
		while( str < end && sgs_decchar( *str ) )
		{
			val *= 10;
			val += sgs_getdec( *str );
			str++;
		}
		if( invsign ) val = -val;
		*outi = val;
		*at = str;
		return 1;
	}
}

int sgs_util_strtonum( CCH** at, CCH* end, sgs_Int* outi, sgs_Real* outf )
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


sgs_Int sgs_util_atoi( const char* str, size_t len )
{
	sgs_Int vi = 0;
	sgs_Real vr = 0;
	const char* p = str;
	int ret = sgs_util_strtonum( &p, str + len, &vi, &vr );
	if( p == str ) return 0;
	if( ret == 1 ) return vi;
	else if( ret == 2 ) return (sgs_Int) vr;
	else return 0;
}

sgs_Real sgs_util_atof( const char* str, size_t len )
{
	sgs_Int vi = 0;
	sgs_Real vr = 0;
	const char* p = str;
	int ret = sgs_util_strtonum( &p, str + len, &vi, &vr );
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

void sgs_quicksort( void *array, size_t length, size_t size,
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
	const ptrdiff_t threshold = (ptrdiff_t) size << INSERTION_SORT_THRESHOLD_SHIFT;
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
		SWAP(left + ((size_t)((right - left) >> 1) / size * size),right,size)

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
		*outchar = (uint32_t) c;
		return 1;
	}
	
	if( ( c & 0xE0 ) == 0xC0 )
	{
		if( size < 2 || U8NFL( buf[1] ) )
			return - (int) SGS_MIN(size,2);
		*outchar = (uint32_t) ( ( ((int)(buf[0]&0x1f)) << 6 ) | ((int)(buf[1]&0x3f)) );
		return 2;
	}
	
	if( ( c & 0xF0 ) == 0xE0 )
	{
		if( size < 3 || U8NFL( buf[1] ) || U8NFL( buf[2] ) )
			return - (int) SGS_MIN(size,3);
		*outchar = (uint32_t) ( ( ((int)(buf[0]&0x0f)) << 12 ) | ( ((int)(buf[1]&0x3f)) << 6 )
			| ((int)(buf[2]&0x3f)) );
		return 3;
	}
	
	if( ( c & 0xF8 ) == 0xF0 )
	{
		if( size < 4 || U8NFL( buf[1] ) || U8NFL( buf[2] ) || U8NFL( buf[3] ) )
			return - (int) SGS_MIN(size,4);
		*outchar = (uint32_t) ( ( ((int)(buf[0]&0x07)) << 18 ) | ( ((int)(buf[1]&0x3f)) << 12 )
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
		out[ 0 ] = (char)( 0xc0 | ( ( ch >> 6 ) & 0x1f ) );
		out[ 1 ] = (char)( 0x80 | ( ch & 0x3f ) );
		return 2;
	}
	if( ch <= 0xffff )
	{
		out[ 0 ] = (char)( 0xe0 | ( ( ch >> 12 ) & 0x0f ) );
		out[ 1 ] = (char)( 0x80 | ( ( ch >> 6 ) & 0x3f ) );
		out[ 2 ] = (char)( 0x80 | ( ch & 0x3f ) );
		return 3;
	}
	if( ch <= 0x10ffff )
	{
		out[ 0 ] = (char)( 0xf0 | ( ( ch >> 18 ) & 0x07 ) );
		out[ 1 ] = (char)( 0x80 | ( ( ch >> 12 ) & 0x3f ) );
		out[ 2 ] = (char)( 0x80 | ( ( ch >> 6 ) & 0x3f ) );
		out[ 3 ] = (char)( 0x80 | ( ch & 0x3f ) );
		return 4;
	}

	return 0;
}


