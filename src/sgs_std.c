

#include <stdio.h>
#include <errno.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"

#define STDLIB_WARN( warn ) return sgs_Msg( C, SGS_WARNING, warn );
#define STDLIB_ERR( err ) return sgs_Msg( C, SGS_ERROR, err );


/* Containers */


/*
	ARRAY
*/

#define SGSARR_UNIT sizeof( sgs_Variable )
#define SGSARR_HDR sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data
#define SGSARR_HDR_OI sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data->data
#define SGSARR_HDRSIZE sizeof(sgsstd_array_header_t)
#define SGSARR_ALLOCSIZE( cnt ) ((size_t)(cnt)*SGSARR_UNIT)
#define SGSARR_PTR( base ) (((sgsstd_array_header_t*)base)->data)

static void sgsstd_array_reserve( SGS_CTX, sgsstd_array_header_t* hdr, sgs_SizeVal size )
{
	if( size <= hdr->mem )
		return;

	hdr->data = (sgs_Variable*) sgs_Realloc( C, hdr->data, SGSARR_ALLOCSIZE( size ) );
	hdr->mem = size;
}

static void sgsstd_array_clear( SGS_CTX, sgsstd_array_header_t* hdr )
{
	sgs_Variable *var, *vend;
	var = SGSARR_PTR( hdr );
	vend = var + hdr->size;
	while( var < vend )
	{
		sgs_Release( C, var );
		var++;
	}
	hdr->size = 0;
}

/* off = offset in stack to start inserting from (1 = the first argument in methods) */
static void sgsstd_array_insert( SGS_CTX, sgsstd_array_header_t* hdr, sgs_SizeVal pos, int off )
{
	int i;
	sgs_SizeVal cnt = sgs_StackSize( C ) - off;
	sgs_SizeVal nsz = hdr->size + cnt;
	sgs_Variable* ptr = SGSARR_PTR( hdr );

	if( !cnt ) return;

	if( nsz > hdr->mem )
	{
		sgsstd_array_reserve( C, hdr, MAX( nsz, hdr->mem * 2 ) );
		ptr = SGSARR_PTR( hdr );
	}
	if( pos < hdr->size )
		memmove( ptr + pos + cnt, ptr + pos, SGSARR_ALLOCSIZE( hdr->size - pos ) );
	for( i = off; i < sgs_StackSize( C ); ++i )
	{
		sgs_Variable* var = ptr + pos + i - off;
		sgs_GetStackItem( C, i, var );
		sgs_Acquire( C, var );
	}
	hdr->size = nsz;
}

static void sgsstd_array_erase( SGS_CTX, sgsstd_array_header_t* hdr, sgs_SizeVal from, sgs_SizeVal to )
{
	sgs_SizeVal i;
	sgs_SizeVal cnt = to - from + 1, to1 = to + 1;
	sgs_Variable* ptr = SGSARR_PTR( hdr );

	sgs_BreakIf( from < 0 || from >= hdr->size || to < 0 || to >= hdr->size || from > to );

	for( i = from; i <= to; ++i )
		sgs_Release( C, ptr + i );
	if( to1 < hdr->size )
		memmove( ptr + from, ptr + to1, SGSARR_ALLOCSIZE( hdr->size - to1 ) );
	hdr->size -= cnt;
}


#define SGSARR_IHDR( name ) \
	sgsstd_array_header_t* hdr; \
	if( !SGS_PARSE_METHOD( C, sgsstd_array_iface, hdr, array, name ) ) return 0; \
	UNUSED( hdr );


static int sgsstd_arrayI_push( SGS_CTX )
{
	SGSARR_IHDR( push );
	sgsstd_array_insert( C, hdr, hdr->size, 0 );
	
	SGS_RETURN_THIS( C );
}
static int sgsstd_arrayI_pop( SGS_CTX )
{
	sgs_Variable* ptr;
	SGSARR_IHDR( pop );
	ptr = SGSARR_PTR( hdr );
	if( !hdr->size )
		STDLIB_WARN( "array is empty, cannot pop" );

	sgs_PushVariable( C, ptr + hdr->size - 1 );
	sgsstd_array_erase( C, hdr, hdr->size - 1, hdr->size - 1 );
	return 1;
}
static int sgsstd_arrayI_shift( SGS_CTX )
{
	sgs_Variable* ptr;
	SGSARR_IHDR( shift );
	ptr = SGSARR_PTR( hdr );
	if( !hdr->size )
		STDLIB_WARN( "array is empty, cannot shift" );

	sgs_PushVariable( C, ptr );
	sgsstd_array_erase( C, hdr, 0, 0 );
	return 1;
}
static int sgsstd_arrayI_unshift( SGS_CTX )
{
	SGSARR_IHDR( unshift );
	sgsstd_array_insert( C, hdr, 0, 0 );
	
	SGS_RETURN_THIS( C );
}

static int sgsstd_arrayI_insert( SGS_CTX )
{
	sgs_Int at;
	SGSARR_IHDR( insert );
	
	if( !sgs_LoadArgs( C, "i?v", &at ) )
		return 0;

	if( at < 0 )
		at += hdr->size + 1;
	if( at < 0 || at > hdr->size )
		STDLIB_WARN( "index out of bounds" )

	sgsstd_array_insert( C, hdr, (sgs_SizeVal) at, 1 );
	
	SGS_RETURN_THIS( C );
}
static int sgsstd_arrayI_erase( SGS_CTX )
{
	int cnt = sgs_StackSize( C );
	sgs_Int at, at2;
	SGSARR_IHDR( erase );
	
	if( !sgs_LoadArgs( C, "i|i", &at, &at2 ) )
		return 0;

	if( at < 0 )
		at += hdr->size;
	if( at < 0 || at >= hdr->size )
		STDLIB_WARN( "index out of bounds" )

	if( cnt == 1 )
		at2 = at;
	else
	{
		if( at2 < 0 )
			at2 += hdr->size;
		if( at2 < 0 || at2 >= hdr->size )
			STDLIB_WARN( "index out of bounds" )

		if( at2 < at )
			STDLIB_WARN( "after resolving,"
				" index #1 must be smaller or equal than index #2" )
	}

	sgsstd_array_erase( C, hdr, (sgs_SizeVal) at, (sgs_SizeVal) at2 );
	
	SGS_RETURN_THIS( C );
}
static int sgsstd_arrayI_part( SGS_CTX )
{
	sgs_SizeVal from, max = 0x7fffffff, to, i;
	SGSARR_IHDR( part );
	
	if( !sgs_LoadArgs( C, "l|l", &from, &max ) )
		return 0;
	
	if( max < 0 )
		STDLIB_WARN( "argument 2 (count) cannot be negative" )
	
	if( from < 0 )
		from += hdr->size;
	to = from + max;
	if( to < from )
		to = hdr->size;
	
	sgs_PushArray( C, 0 );
	if( from < hdr->size && 0 < to )
	{
		from = MAX( from, 0 );
		to = MIN( to, hdr->size );
		
		for( i = from; i < to; ++i )
		{
			sgs_PushVariable( C, SGSARR_PTR( hdr ) + i );
			sgs_ObjectAction( C, -2, SGS_ACT_ARRAY_PUSH, 1 );
		}
	}
	
	return 1;
}

static int sgsstd_arrayI_clear( SGS_CTX )
{
	SGSARR_IHDR( clear );
	sgsstd_array_clear( C, hdr );
	
	SGS_RETURN_THIS( C );
}

static int sgsstd_arrayI_reverse( SGS_CTX )
{
	SGSARR_IHDR( reverse );
	
	/* emit a warning if any arguments are passed */
	if( !sgs_LoadArgs( C, "." ) )
		return 0;
	
	{
		sgs_Variable tmp;
		sgs_Variable* P = SGSARR_PTR( hdr );
		sgs_SizeVal i, j, hsz = hdr->size / 2;
		for( i = 0, j = hdr->size - 1; i < hsz; ++i, --j )
		{
			tmp = P[ i ];
			P[ i ] = P[ j ];
			P[ j ] = tmp;
		}
	}
	
	SGS_RETURN_THIS( C );
}

static void sgsstd_array_adjust( SGS_CTX, sgsstd_array_header_t* hdr, sgs_SizeVal cnt )
{
	while( hdr->size > cnt )
	{
		sgs_Release( C, SGSARR_PTR( hdr ) + hdr->size - 1 );
		hdr->size--;
	}
	while( hdr->size < cnt )
	{
		( SGSARR_PTR( hdr ) + (hdr->size++) )->type = SVT_NULL;
	}
}
static int sgsstd_arrayI_resize( SGS_CTX )
{
	sgs_SizeVal sz;
	SGSARR_IHDR( resize );
	
	if( !sgs_LoadArgs( C, "l", &sz ) )
		return 0;
	
	if( sz < 0 )
		STDLIB_WARN( "argument 1 (size) must be bigger than or equal to 0" )

	sgsstd_array_reserve( C, hdr, sz );
	sgsstd_array_adjust( C, hdr, sz );
	
	SGS_RETURN_THIS( C );
}
static int sgsstd_arrayI_reserve( SGS_CTX )
{
	sgs_SizeVal sz;
	SGSARR_IHDR( reserve );
	
	if( !sgs_LoadArgs( C, "l", &sz ) )
		return 0;
	
	if( sz < 0 )
		STDLIB_WARN( "argument 1 (size) must be bigger than or equal to 0" )

	sgsstd_array_reserve( C, hdr, sz );
	
	SGS_RETURN_THIS( C );
}

static SGS_INLINE int sgsarrcomp_basic( const void* p1, const void* p2, void* userdata )
{
	SGS_CTX = (sgs_Context*) userdata;
	sgs_Variable v1 = *(const sgs_Variable*) p1;
	sgs_Variable v2 = *(const sgs_Variable*) p2;
	return sgs_Compare( C, &v1, &v2 );
}
static SGS_INLINE int sgsarrcomp_basic_rev( const void* p1, const void* p2, void* userdata )
{ return sgsarrcomp_basic( p2, p1, userdata ); }
static int sgsstd_arrayI_sort( SGS_CTX )
{
	int rev = 0;
	SGSARR_IHDR( sort );
	
	if( !sgs_LoadArgs( C, "|b", &rev ) )
		return 0;
	
	/* WP: array limit */
	quicksort( SGSARR_PTR( hdr ), (size_t) hdr->size, sizeof( sgs_Variable ),
		rev ? sgsarrcomp_basic_rev : sgsarrcomp_basic, C );
	
	SGS_RETURN_THIS( C );
}

typedef struct sgsarrcomp_cl2_s
{
	SGS_CTX;
	sgs_Variable sortfunc;
}
sgsarrcomp_cl2;
static SGS_INLINE int sgsarrcomp_custom( const void* p1, const void* p2, void* userdata )
{
	sgsarrcomp_cl2* u = (sgsarrcomp_cl2*) userdata;
	sgs_Variable v1 = *(const sgs_Variable*) p1;
	sgs_Variable v2 = *(const sgs_Variable*) p2;
	SGS_CTX = u->C;
	sgs_PushVariable( C, &v1 );
	sgs_PushVariable( C, &v2 );
	if( sgs_CallP( C, &u->sortfunc, 2, 1 ) != SGS_SUCCESS )
		return 0;
	else
	{
		sgs_Real r = sgs_GetReal( C, -1 );
		sgs_Pop( C, 1 );
		return r == 0 ? 0 : ( r < 0 ? -1 : 1 );
	}
}
static SGS_INLINE int sgsarrcomp_custom_rev( const void* p1, const void* p2, void* userdata )
{ return sgsarrcomp_custom( p2, p1, userdata ); }
static int sgsstd_arrayI_sort_custom( SGS_CTX )
{
	int rev = 0;
	sgsarrcomp_cl2 u;
	SGSARR_IHDR( sort_custom );
	
	u.C = C;
	u.sortfunc.type = SVT_NULL;
	
	if( !sgs_LoadArgs( C, "?p<v|b", &u.sortfunc, &rev ) )
		return 0;
	
	/* WP: array limit */
	quicksort( SGSARR_PTR( hdr ), (size_t) hdr->size,
		sizeof( sgs_Variable ), rev ? sgsarrcomp_custom_rev : sgsarrcomp_custom, &u );
	
	SGS_RETURN_THIS( C );
}

typedef struct sgsarr_smi_s
{
	sgs_Real value;
	sgs_SizeVal pos;
}
sgsarr_smi;
static SGS_INLINE int sgsarrcomp_smi( const void* p1, const void* p2, void* userdata )
{
	const sgsarr_smi *v1 = (const sgsarr_smi*) p1;
	const sgsarr_smi *v2 = (const sgsarr_smi*) p2;
	if( v1->value < v2->value )
		return -1;
	return v1->value > v2->value ? 1 : 0;
}
static SGS_INLINE int sgsarrcomp_smi_rev( const void* p1, const void* p2, void* userdata )
{ return sgsarrcomp_smi( p2, p1, userdata ); }
static int sgsstd_arrayI_sort_mapped( SGS_CTX )
{
	sgs_SizeVal i, asize = 0;
	int rev = 0;
	
	SGSARR_IHDR( sort_mapped );
	if( !sgs_LoadArgs( C, "a|b", &asize, &rev ) )
		return 0;
	
	if( asize != hdr->size )
		STDLIB_WARN( "array sizes must match" )
	
	{
		/* WP: array limit */
		sgsarr_smi* smis = sgs_Alloc_n( sgsarr_smi, (size_t) asize );
		for( i = 0; i < asize; ++i )
		{
			if( sgs_PushNumIndex( C, 0, i ) )
			{
				sgs_Dealloc( smis );
				STDLIB_WARN( "error in mapping array" )
			}
			smis[ i ].value = sgs_GetReal( C, -1 );
			smis[ i ].pos = i;
			sgs_Pop( C, 1 );
		}
		quicksort( smis, (size_t) asize, sizeof( sgsarr_smi ),
			rev ? sgsarrcomp_smi_rev : sgsarrcomp_smi, NULL );
		
		{
			sgs_Variable *p1, *p2;
			p1 = SGSARR_PTR( hdr );
			p2 = sgs_Alloc_n( sgs_Variable, (size_t) hdr->mem );
			memcpy( p2, p1, SGSARR_ALLOCSIZE( hdr->mem ) );
			for( i = 0; i < asize; ++i )
				p1[ i ] = p2[ smis[ i ].pos ];
			sgs_Dealloc( p2 );
		}
		
		sgs_Dealloc( smis );
		
		SGS_RETURN_THIS( C );
	}
}

static int sgsstd_arrayI_find( SGS_CTX )
{
	sgs_Variable comp;
	int strict = FALSE;
	sgs_SizeVal off = 0;
	
	SGSARR_IHDR( find );
	if( !sgs_LoadArgs( C, "v|bl", &comp, &strict, &off ) )
		return 0;
	
	while( off < hdr->size )
	{
		sgs_Variable* p = SGSARR_PTR( hdr ) + off;
		if( ( !strict || sgs_EqualTypes( C, p, &comp ) )
			&& sgs_CompareF( C, p, &comp ) == 0 )
		{
			sgs_PushInt( C, off );
			return 1;
		}
		off++;
	}
	return 0;
}

static int sgsstd_arrayI_remove( SGS_CTX )
{
	sgs_Variable comp;
	int strict = FALSE, all = FALSE, rmvd = 0;
	sgs_SizeVal off = 0;
	
	SGSARR_IHDR( remove );
	if( !sgs_LoadArgs( C, "v|bbl", &comp, &strict, &all, &off ) )
		return 0;
	
	while( off < hdr->size )
	{
		sgs_Variable* p = SGSARR_PTR( hdr ) + off;
		if( ( !strict || sgs_EqualTypes( C, p, &comp ) )
			&& sgs_CompareF( C, p, &comp ) == 0 )
		{
			sgsstd_array_erase( C, hdr, off, off );
			rmvd++;
			if( !all )
				break;
		}
		else
			off++;
	}
	sgs_PushInt( C, rmvd );
	return 1;
}

static int _in_array( SGS_CTX, void* data, int strconv )
{
	int found = 0;
	sgs_SizeVal off = 0;
	SGSARR_HDR;
	
	while( off < hdr->size )
	{
		sgs_PushVariable( C, SGSARR_PTR( hdr ) + off );
		if( strconv )
		{
			sgs_PushItem( C, -2 );
			sgs_ToString( C, -2 );
			sgs_ToString( C, -1 );
		}
		
		/* the comparison */
		{
			sgs_Variable A, B;
			sgs_GetStackItem( C, -2, &A );
			sgs_GetStackItem( C, -1, &B );
			found = sgs_EqualTypes( C, &A, &B )
				&& sgs_CompareF( C, &A, &B ) == 0;
		}
		
		sgs_Pop( C, strconv ? 2 : 1 );
		if( found )
			break;
		off++;
	}
	return found;
}

static int sgsstd_arrayI_unique( SGS_CTX )
{
	int strconv = FALSE;
	sgs_SizeVal off = 0, asz = 0;
	void* nadata;
	
	SGSARR_IHDR( unique );
	if( !sgs_LoadArgs( C, "|b", &strconv ) )
		return 0;
	
	sgs_SetStackSize( C, 1 );
	sgs_PushArray( C, 0 );
	nadata = sgs_GetObjectData( C, -1 );
	while( off < hdr->size )
	{
		sgs_PushVariable( C, SGSARR_PTR( hdr ) + off );
		if( !_in_array( C, nadata, strconv ) )
		{
			sgsstd_array_insert( C, (sgsstd_array_header_t*) nadata, asz, 1 );
			asz++;
		}
		sgs_Pop( C, 1 );
		off++;
	}
	
	return 1;
}

static int sgsstd_arrayI_random( SGS_CTX )
{
	sgs_Int num;
	sgs_SizeVal asz = 0;
	sgsstd_array_header_t* nadata;
	
	SGSARR_IHDR( random );
	if( !sgs_LoadArgs( C, "i", &num ) )
		return 0;
	
	if( num < 0 )
		STDLIB_WARN( "argument 1 (count) cannot be negative" )
	
	sgs_SetStackSize( C, 1 );
	sgs_PushArray( C, 0 );
	nadata = (sgsstd_array_header_t*) sgs_GetObjectData( C, -1 );
	while( num-- )
	{
		sgs_PushVariable( C, SGSARR_PTR( hdr ) + ( rand() % hdr->size ) );
		sgsstd_array_insert( C, nadata, asz, 1 );
		asz++;
		sgs_Pop( C, 1 );
	}
	
	return 1;
}

static int sgsstd_arrayI_shuffle( SGS_CTX )
{
	sgs_Variable tmp;
	sgs_SizeVal i, j;
	
	SGSARR_IHDR( shuffle );
	if( !sgs_LoadArgs( C, "." ) )
		return 0;
	
	for( i = hdr->size - 1; i >= 1; i-- )
	{
		j = rand() % ( i + 1 );
		tmp = SGSARR_PTR( hdr )[ i ];
		SGSARR_PTR( hdr )[ i ] = SGSARR_PTR( hdr )[ j ];
		SGSARR_PTR( hdr )[ j ] = tmp;
	}
	
	return 1;
}

static int sgsstd_array_getprop( SGS_CTX, void* data, sgs_Variable* key )
{
	char* name;
	SGSARR_HDR;
	if( sgs_ParseStringP( C, key, &name, NULL ) )
	{
		sgs_CFunc func;
		if( 0 == strcmp( name, "size" ) )
		{
			sgs_PushInt( C, hdr->size );
			return SGS_SUCCESS;
		}
		else if( 0 == strcmp( name, "capacity" ) )
		{
			sgs_PushInt( C, hdr->mem );
			return SGS_SUCCESS;
		}
		else if( 0 == strcmp( name, "first" ) )
		{
			if( hdr->size )
				sgs_PushVariable( C, SGSARR_PTR( hdr ) );
			else
			{
				sgs_PushNull( C );
				sgs_Msg( C, SGS_WARNING, "array is empty, cannot get first item" );
			}
			return SGS_SUCCESS;
		}
		else if( 0 == strcmp( name, "last" ) )
		{
			if( hdr->size )
				sgs_PushVariable( C, SGSARR_PTR( hdr ) + hdr->size - 1 );
			else
			{
				sgs_PushNull( C );
				sgs_Msg( C, SGS_WARNING, "array is empty, cannot get last item" );
			}
			return SGS_SUCCESS;
		}
		else if( 0 == strcmp( name, "push" ) )      func = sgsstd_arrayI_push;
		else if( 0 == strcmp( name, "pop" ) )       func = sgsstd_arrayI_pop;
		else if( 0 == strcmp( name, "shift" ) )     func = sgsstd_arrayI_shift;
		else if( 0 == strcmp( name, "unshift" ) )   func = sgsstd_arrayI_unshift;
		else if( 0 == strcmp( name, "insert" ) )    func = sgsstd_arrayI_insert;
		else if( 0 == strcmp( name, "erase" ) )     func = sgsstd_arrayI_erase;
		else if( 0 == strcmp( name, "part" ) )      func = sgsstd_arrayI_part;
		else if( 0 == strcmp( name, "clear" ) )     func = sgsstd_arrayI_clear;
		else if( 0 == strcmp( name, "reverse" ) )   func = sgsstd_arrayI_reverse;
		else if( 0 == strcmp( name, "resize" ) )    func = sgsstd_arrayI_resize;
		else if( 0 == strcmp( name, "reserve" ) )   func = sgsstd_arrayI_reserve;
		else if( 0 == strcmp( name, "sort" ) )      func = sgsstd_arrayI_sort;
		else if( 0 == strcmp( name, "sort_custom" ) ) func = sgsstd_arrayI_sort_custom;
		else if( 0 == strcmp( name, "sort_mapped" ) ) func = sgsstd_arrayI_sort_mapped;
		else if( 0 == strcmp( name, "find" ) )      func = sgsstd_arrayI_find;
		else if( 0 == strcmp( name, "remove" ) )    func = sgsstd_arrayI_remove;
		else if( 0 == strcmp( name, "unique" ) )    func = sgsstd_arrayI_unique;
		else if( 0 == strcmp( name, "random" ) )    func = sgsstd_arrayI_random;
		else if( 0 == strcmp( name, "shuffle" ) )   func = sgsstd_arrayI_shuffle;
		else return SGS_ENOTFND;

		sgs_PushCFunction( C, func );
		return SGS_SUCCESS;
	}
	return SGS_ENOTFND;
}

int sgsstd_array_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	if( prop )
		return sgsstd_array_getprop( C, data->data, key );
	else
	{
		SGSARR_HDR_OI;
		sgs_Variable* ptr = SGSARR_PTR( hdr );
		sgs_Int i = sgs_GetIntP( C, key );
		if( i < 0 || i >= hdr->size )
		{
			sgs_Msg( C, SGS_WARNING, "array index out of bounds" );
			return SGS_EBOUNDS;
		}
		sgs_PushVariable( C, ptr + i );
		return SGS_SUCCESS;
	}
}

static int sgsstd_array_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* val, int prop )
{
	if( prop )
		return SGS_ENOTSUP;
	else
	{
		SGSARR_HDR_OI;
		sgs_Variable* ptr = SGSARR_PTR( hdr );
		sgs_Int i = sgs_GetIntP( C, key );
		if( i < 0 || i >= hdr->size )
		{
			sgs_Msg( C, SGS_WARNING, "array index out of bounds" );
			return SGS_EBOUNDS;
		}
		sgs_Assign( C, ptr + i, val );
		return SGS_SUCCESS;
	}
}

static int sgsstd_array_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	char bfr[ 32 ];
	int i, ssz = sgs_StackSize( C );
	SGSARR_HDR_OI;
	sprintf( bfr, "array (%"PRId32")\n[", hdr->size );
	sgs_PushString( C, bfr );
	if( depth )
	{
		if( hdr->size )
		{
			for( i = 0; i < hdr->size; ++i )
			{
				sgs_PushString( C, "\n" );
				sgs_PushVariable( C, SGSARR_PTR( hdr ) + i );
				if( sgs_DumpVar( C, depth ) )
					return SGS_EINPROC;
			}
			if( sgs_StringConcat( C, hdr->size * 2 ) || sgs_PadString( C ) )
				return SGS_EINPROC;
		}
	}
	else
	{
		sgs_PushString( C, "\n..." );
		if( sgs_PadString( C ) )
			return SGS_EINPROC;
	}
	sgs_PushString( C, "\n]" );
	return sgs_StringConcat( C, sgs_StackSize( C ) - ssz );
}

static int sgsstd_array_gcmark( SGS_CTX, sgs_VarObj* data )
{
	SGSARR_HDR_OI;
	sgs_Variable* var = SGSARR_PTR( data->data );
	sgs_Variable* vend = var + hdr->size;
	while( var < vend )
	{
		int ret = sgs_GCMark( C, var++ );
		if( ret != SGS_SUCCESS )
			return ret;
	}
	return SGS_SUCCESS;
}

/* iterator */

static int sgsstd_array_iter_destruct( SGS_CTX, sgs_VarObj* data )
{
	sgs_Release( C, &((sgsstd_array_iter_t*) data->data)->ref );
	return SGS_SUCCESS;
}

static int sgsstd_array_iter_getnext( SGS_CTX, sgs_VarObj* data, int mask )
{
	sgsstd_array_iter_t* iter = (sgsstd_array_iter_t*) data->data;
	sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) iter->ref.data.O->data;
	if( iter->size != hdr->size )
		return SGS_EINPROC;

	if( !mask )
	{
		iter->off++;
		return iter->off < iter->size;
	}
	else
	{
		if( mask & SGS_GETNEXT_KEY )
			sgs_PushInt( C, iter->off );
		if( mask & SGS_GETNEXT_VALUE )
			sgs_PushVariable( C, SGSARR_PTR( hdr ) + iter->off );
		return SGS_SUCCESS;
	}
}

static int sgsstd_array_iter_gcmark( SGS_CTX, sgs_VarObj* data )
{
	sgsstd_array_iter_t* iter = (sgsstd_array_iter_t*) data->data;
	sgs_GCMark( C, &iter->ref );
	return SGS_SUCCESS;
}

sgs_ObjInterface sgsstd_array_iter_iface[1] =
{{
	"array_iterator",
	sgsstd_array_iter_destruct, sgsstd_array_iter_gcmark,
	NULL, NULL,
	NULL, NULL, NULL, sgsstd_array_iter_getnext,
	NULL, NULL
}};

static int sgsstd_array_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	SGSARR_HDR_OI;
	if( type == SGS_CONVOP_TOITER )
	{
		sgsstd_array_iter_t* iter = (sgsstd_array_iter_t*)
			sgs_PushObjectIPA( C, sizeof(*iter), sgsstd_array_iter_iface );
		
		sgs_InitObjectPtr( &iter->ref, data );
		sgs_Acquire( C, &iter->ref );
		iter->size = hdr->size;
		iter->off = -1;
		
		return SGS_SUCCESS;
	}
	else if( type == SVT_BOOL )
	{
		sgs_PushBool( C, !!hdr->size );
		return SGS_SUCCESS;
	}
	else if( type == SVT_STRING )
	{
		sgs_Variable* var = SGSARR_PTR( data->data );
		sgs_Variable* vend = var + hdr->size;

		sgs_PushString( C, "[" );
		while( var < vend )
		{
			sgs_PushVariable( C, var );
			sgs_ToStringFast( C, -1 );
			var++;
			if( var < vend )
				sgs_PushString( C, "," );
		}
		sgs_PushString( C, "]" );
		return sgs_StringConcat( C, hdr->size * 2 + 1 + !hdr->size );
	}
	else if( type == SGS_CONVOP_CLONE )
	{
		sgsstd_array_header_t* hdr2 = (sgsstd_array_header_t*)
			sgs_PushObjectIPA( C, sizeof( sgsstd_array_header_t ), sgsstd_array_iface );
		memcpy( hdr2, hdr, sizeof( sgsstd_array_header_t ) );
		hdr2->data = sgs_Alloc_n( sgs_Variable, (size_t) hdr->mem );
		memcpy( hdr2->data, hdr->data, SGSARR_ALLOCSIZE( hdr->mem ) );
		{
			sgs_Variable* ptr = SGSARR_PTR( hdr );
			sgs_Variable* pend = ptr + hdr->size;
			while( ptr < pend )
				sgs_Acquire( C, ptr++ );
		}
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_array_serialize( SGS_CTX, sgs_VarObj* data )
{
	int ret;
	sgs_Variable* pos, *posend;
	SGSARR_HDR_OI;
	pos = SGSARR_PTR( hdr );
	posend = pos + hdr->size;
	while( pos < posend )
	{
		sgs_PushVariable( C, pos++ );
		ret = sgs_Serialize( C );
		if( ret != SGS_SUCCESS )
			return ret;
	}
	return sgs_SerializeObject( C, hdr->size, "array" );
}

static int sgsstd_array_destruct( SGS_CTX, sgs_VarObj* data )
{
	SGSARR_HDR_OI;
	sgsstd_array_clear( C, hdr );
	sgs_Dealloc( hdr->data );
	return 0;
}

sgs_ObjInterface sgsstd_array_iface[1] =
{{
	"array",
	sgsstd_array_destruct, sgsstd_array_gcmark,
	sgsstd_array_getindex, sgsstd_array_setindex,
	sgsstd_array_convert, sgsstd_array_serialize, sgsstd_array_dump, NULL,
	NULL, NULL
}};

static int sgsstd_array( SGS_CTX )
{
	int i = 0, objcnt = sgs_StackSize( C );
	void* data = sgs_Malloc( C, SGSARR_ALLOCSIZE( objcnt ) );
	sgs_Variable *p, *pend;
	sgsstd_array_header_t* hdr = (sgsstd_array_header_t*)
		sgs_PushObjectIPA( C, sizeof( sgsstd_array_header_t ), sgsstd_array_iface );
	hdr->size = objcnt;
	hdr->mem = objcnt;
	p = hdr->data = (sgs_Variable*) data;
	pend = p + objcnt;
	while( p < pend )
	{
		sgs_GetStackItem( C, i++, p );
		sgs_Acquire( C, p++ );
	}
	return 1;
}


/*
	VHT containers
*/

typedef
struct _DictHdr
{
	VHTable ht;
}
DictHdr;

#define HTHDR DictHdr* dh = (DictHdr*) data->data; VHTable* ht = &dh->ht;

static int sgsstd_vht_serialize( SGS_CTX, sgs_VarObj* data, const char* initfn )
{
	int ret;
	VHTVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + vht_size( ht );
	while( pair < pend )
	{
		sgs_PushVariable( C, &pair->key );
		ret = sgs_Serialize( C );
		if( ret != SGS_SUCCESS )
			return ret;
		sgs_PushVariable( C, &pair->val );
		ret = sgs_Serialize( C );
		if( ret != SGS_SUCCESS )
			return ret;
		pair++;
	}
	return sgs_SerializeObject( C, vht_size( ht ) * 2, initfn );
}

static int sgsstd_vht_dump( SGS_CTX, sgs_VarObj* data, int depth, const char* name )
{
	int ssz;
	char bfr[ 32 ];
	VHTVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + vht_size( ht );
	ssz = sgs_StackSize( C );
	sprintf( bfr, "%s (%"PRId32")\n{", name, vht_size( ht ) );
	sgs_PushString( C, bfr );
	if( depth )
	{
		if( vht_size( ht ) )
		{
			while( pair < pend )
			{
				sgs_PushString( C, "\n" );
				sgs_PushVariable( C, &pair->key );
				if( pair->key.type == SVT_STRING )
					sgs_ToPrintSafeString( C );
				else
				{
					if( sgs_DumpVar( C, depth ) )
						return SGS_EINPROC;
				}
				sgs_PushString( C, " = " );
				sgs_PushVariable( C, &pair->val );
				if( sgs_DumpVar( C, depth ) )
					return SGS_EINPROC;
				pair++;
			}
			/* WP: stack limit */
			if( sgs_StringConcat( C, (StkIdx) ( pend - ht->vars ) * 4 ) || sgs_PadString( C ) )
				return SGS_EINPROC;
		}
	}
	else
	{
		sgs_PushString( C, "\n..." );
		if( sgs_PadString( C ) )
			return SGS_EINPROC;
	}
	sgs_PushString( C, "\n}" );
	return sgs_StringConcat( C, sgs_StackSize( C ) - ssz );
}


/*
	DICT
*/

static DictHdr* mkdict( SGS_CTX, sgs_Variable* out )
{
	DictHdr* dh = (DictHdr*) ( out
		? sgs_InitObjectIPA( C, out, sizeof( DictHdr ), sgsstd_dict_iface )
		: sgs_PushObjectIPA( C, sizeof( DictHdr ), sgsstd_dict_iface ) );
	vht_init( &dh->ht, C, 4, 4 );
	return dh;
}


static int sgsstd_dict_destruct( SGS_CTX, sgs_VarObj* data )
{
	HTHDR;
	vht_free( ht, C );
	return SGS_SUCCESS;
}

static int sgsstd_dict_gcmark( SGS_CTX, sgs_VarObj* data )
{
	VHTVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + vht_size( ht );
	while( pair < pend )
	{
		int ret = sgs_GCMark( C, &pair->key );
		if( ret != SGS_SUCCESS )
			return ret;
		ret = sgs_GCMark( C, &pair->val );
		if( ret != SGS_SUCCESS )
			return ret;
		pair++;
	}
	return SGS_SUCCESS;
}

static int sgsstd_dict_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	return sgsstd_vht_dump( C, data, depth, "dict" );
}

static int sgsstd_dict_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	VHTVar* pair = NULL;
	HTHDR;
	
	if( prop && key->type == SVT_INT )
	{
		int32_t off = (int32_t) key->data.I;
		if( off < 0 || off > vht_size( ht ) )
			return SGS_EBOUNDS;
		sgs_PushVariable( C, &ht->vars[ off ].val );
		return SGS_SUCCESS;
	}
	
	if( sgs_ParseStringP( C, key, NULL, NULL ) )
	{
		pair = vht_get( ht, key );
		if( !pair )
			return SGS_ENOTFND;

		sgs_PushVariable( C, &pair->val );
		return SGS_SUCCESS;
	}
	return SGS_EINVAL;
}

static int sgsstd_dict_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* val, int prop )
{
	char* str;
	HTHDR;
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		vht_set( ht, C, key, val );
		return SGS_SUCCESS;
	}
	return SGS_EINVAL;
}

/* iterator */

typedef struct sgsstd_dict_iter_s
{
	sgs_Variable ref;
	int32_t size;
	int32_t off;
}
sgsstd_dict_iter_t;

static int sgsstd_dict_iter_destruct( SGS_CTX, sgs_VarObj* data )
{
	sgs_Release( C, &((sgsstd_dict_iter_t*) data->data)->ref );
	return SGS_SUCCESS;
}

static int sgsstd_dict_iter_getnext( SGS_CTX, sgs_VarObj* data, int flags )
{
	sgsstd_dict_iter_t* iter = (sgsstd_dict_iter_t*) data->data;
	VHTable* ht = (VHTable*) iter->ref.data.O->data;
	if( iter->size != vht_size( ht ) )
		return SGS_EINVAL;

	if( !flags )
	{
		iter->off++;
		return iter->off < iter->size;
	}
	else
	{
		if( flags & SGS_GETNEXT_KEY )
			sgs_PushVariable( C, &ht->vars[ iter->off ].key );
		if( flags & SGS_GETNEXT_VALUE )
			sgs_PushVariable( C, &ht->vars[ iter->off ].val );
		return SGS_SUCCESS;
	}
}

static int sgsstd_dict_iter_gcmark( SGS_CTX, sgs_VarObj* data )
{
	sgsstd_dict_iter_t* iter = (sgsstd_dict_iter_t*) data->data;
	sgs_GCMark( C, &iter->ref );
	return SGS_SUCCESS;
}

sgs_ObjInterface sgsstd_dict_iter_iface[1] =
{{
	"dict_iterator",
	sgsstd_dict_iter_destruct, sgsstd_dict_iter_gcmark,
	NULL, NULL,
	NULL, NULL, NULL, sgsstd_dict_iter_getnext,
	NULL, NULL
}};


static int sgsstd_dict_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	HTHDR;
	if( type == SGS_CONVOP_TOITER )
	{
		sgsstd_dict_iter_t* iter = (sgsstd_dict_iter_t*)
			sgs_PushObjectIPA( C, sizeof(*iter), sgsstd_dict_iter_iface );
		
		sgs_InitObjectPtr( &iter->ref, data );
		sgs_Acquire( C, &iter->ref );
		iter->size = vht_size( ht );
		iter->off = -1;
		
		return SGS_SUCCESS;
	}
	else if( type == SVT_BOOL )
	{
		sgs_PushBool( C, vht_size( ht ) != 0 );
		return SGS_SUCCESS;
	}
	else if( type == SVT_STRING )
	{
		VHTVar *pair = ht->vars, *pend = ht->vars + vht_size( ht );
		int cnt = 0;
		sgs_PushString( C, "{" );
		while( pair < pend )
		{
			if( cnt )
				sgs_PushString( C, "," );
			sgs_PushVariable( C, &pair->key );
			sgs_PushString( C, "=" );
			sgs_PushVariable( C, &pair->val );
			sgs_ToStringFast( C, -1 );
			cnt++;
			pair++;
		}
		sgs_PushString( C, "}" );
		return sgs_StringConcat( C, cnt * 4 + 1 + !cnt );
	}
	else if( type == SGS_CONVOP_CLONE )
	{
		int i, htsize = vht_size( ht );
		DictHdr* ndh = mkdict( C, NULL );
		for( i = 0; i < htsize; ++i )
		{
			vht_set( &ndh->ht, C, &ht->vars[ i ].key, &ht->vars[ i ].val );
		}
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_dict_serialize( SGS_CTX, sgs_VarObj* data )
{
	return sgsstd_vht_serialize( C, data, "dict" );
}

sgs_ObjInterface sgsstd_dict_iface[1] =
{{
	"dict",
	sgsstd_dict_destruct, sgsstd_dict_gcmark,
	sgsstd_dict_getindex, sgsstd_dict_setindex,
	sgsstd_dict_convert, sgsstd_dict_serialize, sgsstd_dict_dump, NULL,
	NULL, NULL
}};

static int sgsstd_dict( SGS_CTX )
{
	DictHdr* dh;
	VHTable* ht;
	int i, objcnt = sgs_StackSize( C );
	
	SGSFN( "dict" );

	if( objcnt % 2 != 0 )
		STDLIB_WARN( "function expects 0 or an even number of arguments" )

	dh = mkdict( C, NULL );
	ht = &dh->ht;

	for( i = 0; i < objcnt; i += 2 )
	{
		sgs_Variable val;
		if( !sgs_ParseString( C, i, NULL, NULL ) )
			return sgs_FuncArgError( C, i, SVT_STRING, 0 );

		sgs_GetStackItem( C, i + 1, &val );
		vht_set( ht, C, (C->stack_off+i), &val );
	}
	
	return 1;
}


/* MAP */

static DictHdr* mkmap( SGS_CTX, sgs_Variable* out )
{
	DictHdr* dh = (DictHdr*) ( out
		? sgs_InitObjectIPA( C, out, sizeof( DictHdr ), sgsstd_map_iface )
		: sgs_PushObjectIPA( C, sizeof( DictHdr ), sgsstd_map_iface ) );
	vht_init( &dh->ht, C, 4, 4 );
	return dh;
}

#define sgsstd_map_destruct sgsstd_dict_destruct
#define sgsstd_map_gcmark sgsstd_dict_gcmark

static int sgsstd_map_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	return sgsstd_vht_dump( C, data, depth, "map" );
}

static int sgsstd_map_serialize( SGS_CTX, sgs_VarObj* data )
{
	return sgsstd_vht_serialize( C, data, "map" );
}

static int sgsstd_map_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	HTHDR;
	if( type == SGS_CONVOP_TOITER )
	{
		sgsstd_dict_iter_t* iter = (sgsstd_dict_iter_t*)
			sgs_PushObjectIPA( C, sizeof(*iter), sgsstd_dict_iter_iface );
		
		sgs_InitObjectPtr( &iter->ref, data );
		sgs_Acquire( C, &iter->ref );
		iter->size = vht_size( ht );
		iter->off = -1;
		
		return SGS_SUCCESS;
	}
	else if( type == SVT_BOOL )
	{
		sgs_PushBool( C, vht_size( ht ) != 0 );
		return SGS_SUCCESS;
	}
	else if( type == SVT_STRING )
	{
		VHTVar *pair = ht->vars, *pend = ht->vars + vht_size( ht );
		int cnt = 0;
		sgs_PushString( C, "{" );
		while( pair < pend )
		{
			if( cnt )
				sgs_PushString( C, "," );
			sgs_PushVariable( C, &pair->key );
			sgs_ToStringFast( C, -1 );
			sgs_PushString( C, "=" );
			sgs_PushVariable( C, &pair->val );
			sgs_ToStringFast( C, -1 );
			cnt++;
			pair++;
		}
		sgs_PushString( C, "}" );
		return sgs_StringConcat( C, cnt * 4 + 1 + !cnt );
	}
	else if( type == SGS_CONVOP_CLONE )
	{
		int i, htsize = vht_size( ht );
		DictHdr* ndh = mkmap( C, NULL );
		for( i = 0; i < htsize; ++i )
		{
			vht_set( &ndh->ht, C, &ht->vars[ i ].key, &ht->vars[ i ].val );
		}
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_map_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	VHTVar* pair = NULL;
	HTHDR;
	
	pair = vht_get( ht, key );
	if( !pair )
		return SGS_ENOTFND;

	sgs_PushVariable( C, &pair->val );
	return SGS_SUCCESS;
}

static int sgsstd_map_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* val, int prop )
{
	HTHDR;
	vht_set( ht, C, key, val );
	return SGS_SUCCESS;
}

sgs_ObjInterface sgsstd_map_iface[1] =
{{
	"map",
	sgsstd_map_destruct, sgsstd_map_gcmark,
	sgsstd_map_getindex, sgsstd_map_setindex,
	sgsstd_map_convert, sgsstd_map_serialize, sgsstd_map_dump, NULL,
	NULL, NULL
}};

static int sgsstd_map( SGS_CTX )
{
	DictHdr* dh;
	VHTable* ht;
	int i, objcnt = sgs_StackSize( C );
	
	SGSFN( "map" );

	if( objcnt % 2 != 0 )
		STDLIB_WARN( "function expects 0 or an even number of arguments" )
	
	dh = mkmap( C, NULL );
	ht = &dh->ht;

	for( i = 0; i < objcnt; i += 2 )
	{
		vht_set( ht, C, C->stack_off+i, C->stack_off+i+1 );
	}
	
	return 1;
}


/* CLASS */

typedef struct sgsstd_class_header_s
{
	sgs_Variable data;
	sgs_Variable inh;
}
sgsstd_class_header_t;


#define SGSCLASS_HDR sgsstd_class_header_t* hdr = (sgsstd_class_header_t*) data->data;

static int sgsstd_class_getmethod( SGS_CTX, sgs_VarObj* data, const char* method, sgs_Variable* out )
{
	int ret;
	sgs_Variable key;
	SGSCLASS_HDR;
	sgs_InitString( C, &key, method );
	ret = sgs_GetIndexPPP( C, &hdr->inh, &key, out, TRUE );
	sgs_Release( C, &key );
	return ret == SGS_SUCCESS;
}

static int sgsstd_class_destruct( SGS_CTX, sgs_VarObj* data )
{
	SGSCLASS_HDR;
	sgs_Variable method;
	if( sgsstd_class_getmethod( C, data, "__destruct", &method ) )
	{
		data->refcount++;
		sgs_PushObjectPtr( C, data );
		sgs_ThisCallP( C, &method, 0, 0 );
		sgs_Release( C, &method );
		data->refcount--;
	}
	sgs_Release( C, &hdr->data );
	sgs_Release( C, &hdr->inh );
	return SGS_SUCCESS;
}

static int sgsstd_class_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int isprop )
{
	int ret;
	SGSCLASS_HDR;
	if( isprop && sgs_ItemType( C, 0 ) == SVT_STRING && !strcmp( sgs_ToStringP( C, key ), "_super" ) )
	{
		sgs_PushVariable( C, &hdr->inh );
		return SGS_SUCCESS;
	}
	
	if( SGS_SUCCEEDED( sgs_PushIndexPP( C, &hdr->data, key, isprop ) ) )
		return SGS_SUCCESS;
	
	ret = sgs_PushIndexPP( C, &hdr->inh, key, isprop );
	if( SGS_SUCCEEDED( ret ) )
		return SGS_SUCCESS;
	
	return ret;
}

static int sgsstd_class_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* val, int isprop )
{
	SGSCLASS_HDR;
	if( isprop && sgs_ItemType( C, 0 ) == SVT_STRING && !strcmp( sgs_ToString( C, 0 ), "_super" ) )
	{
		sgs_Assign( C, &hdr->inh, val );
		return SGS_SUCCESS;
	}
	return sgs_SetIndexPPP( C, &hdr->data, key, val, isprop );
}

static int sgsstd_class_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	SGSCLASS_HDR;
	sgs_PushString( C, "class\n{" );
	sgs_PushString( C, "\ndata: " );
	sgs_PushVariable( C, &hdr->data );
	if( sgs_DumpVar( C, depth ) )
	{
		sgs_Pop( C, 1 );
		sgs_PushString( C, "<error>" );
	}
	sgs_PushString( C, "\nsuper: " );
	sgs_PushVariable( C, &hdr->inh );
	if( sgs_DumpVar( C, depth ) )
	{
		sgs_Pop( C, 1 );
		sgs_PushString( C, "<error>" );
	}
	if( sgs_StringConcat( C, 4 ) || sgs_PadString( C ) )
		return SGS_EINPROC;
	sgs_PushString( C, "\n}" );
	return sgs_StringConcat( C, 3 );
}

static int sgsstd_class( SGS_CTX );

SGS_DECLARE sgs_ObjInterface sgsstd_class_iface[1];

static int sgsstd_class_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	int otype = type;
	sgs_Variable method;
	
	const char* op = NULL;
	switch( type )
	{
	case SVT_BOOL: op = "__tobool"; break;
	case SVT_INT: op = "__toint"; break;
	case SVT_REAL: op = "__toreal"; break;
	case SVT_STRING: op = "__tostr"; break;
	case SVT_PTR: op = "__toptr"; break;
	case SGS_CONVOP_CLONE: op = "__clone"; break;
	case SGS_CONVOP_TYPEOF: op = "__typeof"; break;
	}
	
	if( op == NULL )
		return SGS_ENOTFND;
	
	if( sgsstd_class_getmethod( C, data, op, &method ) )
	{
		int ret;
		sgs_PushObjectPtr( C, data );
		ret = sgs_ThisCallP( C, &method, 0, 1 );
		sgs_Release( C, &method );
		return ret;
	}
	
	if( otype == SGS_CONVOP_CLONE )
	{
		sgsstd_class_header_t* hdr2;
		SGSCLASS_HDR;
		sgs_PushVariable( C, &hdr->data );
		sgs_CloneItem( C, -1 );
		
		hdr2 = (sgsstd_class_header_t*) sgs_PushObjectIPA( C,
			sizeof( sgsstd_class_header_t ), sgsstd_class_iface );
		sgs_GetStackItem( C, -1, &hdr2->data );
		hdr2->inh = hdr->inh;
		sgs_Acquire( C, &hdr2->data );
		sgs_Acquire( C, &hdr2->inh );
		return SGS_SUCCESS;
	}
	
	return SGS_ENOTSUP;
}

static int sgsstd_class_serialize( SGS_CTX, sgs_VarObj* data )
{
	int ret;
	SGSCLASS_HDR;
	
	sgs_PushVariable( C, &hdr->data );
	ret = sgs_Serialize( C );
	if( ret != SGS_SUCCESS )
		return ret;
	
	sgs_PushVariable( C, &hdr->inh );
	ret = sgs_Serialize( C );
	if( ret != SGS_SUCCESS )
		return ret;
	
	return sgs_SerializeObject( C, 2, "class" );
}

static int sgsstd_class_gcmark( SGS_CTX, sgs_VarObj* data )
{
	int ret;
	SGSCLASS_HDR;
	ret = sgs_GCMark( C, &hdr->data );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_GCMark( C, &hdr->inh );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

static int sgsstd_class_expr( SGS_CTX, sgs_VarObj* data, sgs_Variable* A, sgs_Variable* B, int type )
{
	sgs_Variable method;
	static const char* ops[] = { "__add", "__sub", "__mul", "__div", "__mod", "__compare", "__negate" };
	if( sgsstd_class_getmethod( C, data, ops[type], &method ) )
	{
		int ret;
		if( type == SGS_EOP_NEGATE )
		{
			sgs_PushObjectPtr( C, data );
			ret = sgs_FCallP( C, &method, 0, 1, 1 );
		}
		else
		{
			sgs_PushVariable( C, A );
			sgs_PushVariable( C, B );
			ret = sgs_FCallP( C, &method, 2, 1, 0 );
		}
		sgs_Release( C, &method );
		return ret;
	}
	
	return SGS_ENOTFND;
}

static int sgsstd_class_call( SGS_CTX, sgs_VarObj* data )
{
	sgs_Variable method;
	if( sgsstd_class_getmethod( C, data, "__call", &method ) )
	{
		int ret, i;
		sgs_PushObjectPtr( C, data );
		for( i = 0; i < C->sf_last->argcount; ++i )
			sgs_PushItem( C, i );
		ret = sgsVM_VarCall( C, &method, C->sf_last->argcount, 0, C->sf_last->expected, TRUE );
		sgs_Release( C, &method );
		return ret;
	}
	return SGS_ENOTFND;
}

static sgs_ObjInterface sgsstd_class_iface[1] =
{{
	"class",
	sgsstd_class_destruct, sgsstd_class_gcmark,
	sgsstd_class_getindex, sgsstd_class_setindex,
	sgsstd_class_convert, sgsstd_class_serialize, sgsstd_class_dump, NULL,
	sgsstd_class_call, sgsstd_class_expr,
}};

static int sgsstd_class( SGS_CTX )
{
	sgsstd_class_header_t* hdr;
	
	SGSFN( "class" );
	
	if( !sgs_LoadArgs( C, "?v?v." ) )
		return 0;
	
	hdr = (sgsstd_class_header_t*) sgs_PushObjectIPA( C,
		sizeof( sgsstd_class_header_t ), sgsstd_class_iface );
	sgs_GetStackItem( C, 0, &hdr->data );
	sgs_GetStackItem( C, 1, &hdr->inh );
	sgs_Acquire( C, &hdr->data );
	sgs_Acquire( C, &hdr->inh );
	return 1;
}


/* CLOSURE */

typedef struct sgsstd_closure_s
{
	sgs_Variable func;
	sgs_Variable data;
}
sgsstd_closure_t;


#define SGSCLOSURE_HDR sgsstd_closure_t* hdr = (sgsstd_closure_t*) data->data;

static int sgsstd_closure_destruct( SGS_CTX, sgs_VarObj* data )
{
	SGSCLOSURE_HDR;
	sgs_Release( C, &hdr->func );
	sgs_Release( C, &hdr->data );
	return SGS_SUCCESS;
}

static int sgsstd_closure_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int isprop )
{
	char* str;
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( !strcmp( str, "thiscall" ) )
		{
			sgs_PushCFunction( C, sgs_thiscall_method );
			return SGS_SUCCESS;
		}
	}
	return SGS_ENOTFND;
}

static int sgsstd_closure_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	SGSCLOSURE_HDR;
	sgs_PushString( C, "closure\n{" );
	sgs_PushString( C, "\nfunc: " );
	sgs_PushVariable( C, &hdr->func );
	if( sgs_DumpVar( C, depth ) )
	{
		sgs_Pop( C, 1 );
		sgs_PushString( C, "<error>" );
	}
	sgs_PushString( C, "\ndata: " );
	sgs_PushVariable( C, &hdr->data );
	if( sgs_DumpVar( C, depth ) )
	{
		sgs_Pop( C, 1 );
		sgs_PushString( C, "<error>" );
	}
	if( sgs_StringConcat( C, 4 ) || sgs_PadString( C ) )
		return SGS_EINPROC;
	sgs_PushString( C, "\n}" );
	return sgs_StringConcat( C, 3 );
}

static int sgsstd_closure_gcmark( SGS_CTX, sgs_VarObj* data )
{
	int ret;
	SGSCLOSURE_HDR;
	ret = sgs_GCMark( C, &hdr->func );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_GCMark( C, &hdr->data );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

static int sgsstd_closure_call( SGS_CTX, sgs_VarObj* data )
{
	int ismethod = sgs_Method( C );
	SGSCLOSURE_HDR;
	sgs_InsertVariable( C, -C->sf_last->argcount - 1, &hdr->data );
	return sgsVM_VarCall( C, &hdr->func, C->sf_last->argcount + 1,
		0, C->sf_last->expected, ismethod ) * C->sf_last->expected;
}

static sgs_ObjInterface sgsstd_closure_iface[1] =
{{
	"closure",
	sgsstd_closure_destruct, sgsstd_closure_gcmark,
	sgsstd_closure_getindex, NULL,
	NULL, NULL, sgsstd_closure_dump, NULL,
	sgsstd_closure_call, NULL
}};

static int sgsstd_closure( SGS_CTX )
{
	sgsstd_closure_t* hdr;
	
	SGSFN( "closure" );
	
	if( !sgs_LoadArgs( C, "?p?v." ) )
		return 0;
	
	hdr = (sgsstd_closure_t*) sgs_PushObjectIPA( C,
		sizeof( sgsstd_closure_t ), sgsstd_closure_iface );
	sgs_GetStackItem( C, 0, &hdr->func );
	sgs_GetStackItem( C, 1, &hdr->data );
	sgs_Acquire( C, &hdr->func );
	sgs_Acquire( C, &hdr->data );
	return 1;
}



/*
	real closure memory layout:
	- sgs_Variable: function
	- int32: closure count
	- sgs_Closure* x ^^: closures
*/

static int sgsstd_realclsr_destruct( SGS_CTX, sgs_VarObj* data )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, cc = *(int32_t*) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));
	
	sgs_Release( C, (sgs_Variable*) (void*) ASSUME_ALIGNED( cl, sizeof(void*) ) );
	
	for( i = 0; i < cc; ++i )
	{
		if( --cls[ i ]->refcount < 1 )
		{
			sgs_Release( C, &cls[ i ]->var );
			sgs_Dealloc( cls[i] );
		}
	}
	
	return SGS_SUCCESS;
}

static int sgsstd_realclsr_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int isprop )
{
	char* str;
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( !strcmp( str, "thiscall" ) )
		{
			sgs_PushCFunction( C, sgs_thiscall_method );
			return SGS_SUCCESS;
		}
	}
	return SGS_ENOTFND;
}

static int sgsstd_realclsr_call( SGS_CTX, sgs_VarObj* data )
{
	int ismethod = sgs_Method( C ), expected = C->sf_last->expected;
	uint8_t* cl = (uint8_t*) data->data;
	int32_t cc = *(int32_t*) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));
	
	sgsVM_PushClosures( C, cls, cc );
	return sgsVM_VarCall( C, (sgs_Variable*) (void*) ASSUME_ALIGNED( cl, sizeof(void*) ), C->sf_last->argcount,
		cc, C->sf_last->expected, ismethod ) * expected;
}

static int sgsstd_realclsr_gcmark( SGS_CTX, sgs_VarObj* data )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, cc = *(int32_t*) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));
	
	sgs_GCMark( C, (sgs_Variable*) (void*) ASSUME_ALIGNED( cl, sizeof(void*) ) );
	
	for( i = 0; i < cc; ++i )
	{
		sgs_GCMark( C, &cls[ i ]->var );
	}
	
	return SGS_SUCCESS;
}

static int sgsstd_realclsr_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, ssz, cc = *(int32_t*) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));

	sgs_PushString( C, "sys.closure\n{" );

	ssz = sgs_StackSize( C );
	sgs_PushString( C, "\nfunc: " );
	sgs_PushVariable( C, (sgs_Variable*) (void*) ASSUME_ALIGNED( cl, sizeof(void*) ) ); /* function */
	if( sgs_DumpVar( C, depth ) )
	{
		sgs_Pop( C, 1 );
		sgs_PushString( C, "<error>" );
	}
	for( i = 0; i < cc; ++i )
	{
		char intro[ 64 ];
		sprintf( intro, "\n#%"PRId32" (rc=%"PRId32"): ", i, cls[ i ]->refcount );
		sgs_PushString( C, intro );
		sgs_PushVariable( C, &cls[ i ]->var );
		if( sgs_DumpVar( C, depth ) )
		{
			sgs_Pop( C, 1 );
			sgs_PushString( C, "<error>" );
		}
	}
	if( sgs_StringConcat( C, sgs_StackSize( C ) - ssz ) || sgs_PadString( C ) )
		return SGS_EINPROC;

	sgs_PushString( C, "\n}" );
	return sgs_StringConcat( C, 3 );
}

static sgs_ObjInterface sgsstd_realclsr_iface =
{
	"sys.closure",
	sgsstd_realclsr_destruct, sgsstd_realclsr_gcmark,
	sgsstd_realclsr_getindex, NULL,
	NULL, NULL, sgsstd_realclsr_dump, NULL,
	sgsstd_realclsr_call, NULL
};

int sgsSTD_MakeClosure( SGS_CTX, sgs_Variable* func, uint32_t clc )
{
	/* WP: range not affected by conversion */
	uint32_t i, clsz = (uint32_t) sizeof(sgs_Closure*) * clc;
	uint32_t memsz = clsz + (uint32_t) ( sizeof(sgs_Variable) + sizeof(int32_t) );
	uint8_t* cl = (uint8_t*) sgs_PushObjectIPA( C, memsz, &sgsstd_realclsr_iface );
	
	memcpy( cl, func, sizeof(sgs_Variable) );
	sgs_Acquire( C, func );
	
	memcpy( cl + sizeof(sgs_Variable), &clc, sizeof(clc) );
	
	memcpy( cl + sizeof(sgs_Variable) + sizeof(clc), C->clstk_top - clc, clsz );
	for( i = 0; i < clc; ++i )
		(*(C->clstk_top - clc + i))->refcount++;
	
	return SGS_SUCCESS;
}


/* UTILITIES */

static int sgsstd_array_filter( SGS_CTX )
{
	SGSBOOL cset = 0, use;
	sgs_SizeVal asz, off = 0, nasz = 0;
	void *data;
	sgsstd_array_header_t* nadata;
	
	SGSFN( "array_filter" );
	if( !sgs_LoadArgs( C, "a|p", &asz, &cset ) )
		return 0;
	
	sgs_PushArray( C, 0 );
	data = sgs_GetObjectData( C, 0 );
	nadata = (sgsstd_array_header_t*) sgs_GetObjectData( C, -1 );
	
	{
		SGSARR_HDR;
		
		while( off < asz )
		{
			sgs_PushVariable( C, SGSARR_PTR( hdr ) + off );
			if( cset )
			{
				sgs_PushInt( C, off );
				sgs_PushItem( C, 1 );
				if( sgs_Call( C, 2, 1 ) )
					STDLIB_WARN( "failed to call the filter function" )
			}
			use = sgs_GetBool( C, -1 );
			sgs_Pop( C, 1 );
			sgs_PushVariable( C, SGSARR_PTR( hdr ) + off );
			if( use )
			{
				sgsstd_array_insert( C, nadata, nasz, sgs_StackSize( C ) - 1 );
				nasz++;
			}
			sgs_Pop( C, 1 );
			
			off++;
		}
	}
	
	return 1;
}

static int sgsstd_array_process( SGS_CTX )
{
	sgs_SizeVal asz, off = 0;
	void *data;
	
	SGSFN( "array_process" );
	if( !sgs_LoadArgs( C, "a?p", &asz ) )
		return 0;
	
	data = sgs_GetObjectData( C, 0 );
	{
		SGSARR_HDR;
		
		while( off < asz )
		{
			sgs_PushVariable( C, SGSARR_PTR( hdr ) + off );
			sgs_PushInt( C, off );
			sgs_PushItem( C, 1 );
			if( sgs_Call( C, 2, 1 ) )
				STDLIB_WARN( "failed to call the processing function" )
			sgs_StoreNumIndex( C, 0, off );
			off++;
		}
	}
	
	sgs_SetStackSize( C, 1 );
	return 1;
}

static int sgsstd_dict_filter( SGS_CTX )
{
	SGSBOOL cset = 0, use;
	
	SGSFN( "dict_filter" );
	if( !sgs_LoadArgs( C, "?t|p", &cset ) )
		return 0;
	
	sgs_PushDict( C, 0 );
	sgs_PushIterator( C, 0 );
	while( sgs_IterAdvance( C, -1 ) > 0 )
	{
		if( sgs_IterPushData( C, -1, 1, 1 ) )
			STDLIB_WARN( "failed to read iterator (was dict changed in callback?)" )
		if( cset )
		{
			sgs_PushItem( C, -1 );
			sgs_PushItem( C, -3 );
			sgs_PushItem( C, 1 );
			if( sgs_Call( C, 2, 1 ) )
				STDLIB_WARN( "failed to call the filter function" )
		}
		use = sgs_GetBool( C, -1 );
		if( cset )
			sgs_Pop( C, 1 );
		if( use )
		{
			/* src-dict, ... dest-dict, iterator, key, value */
			sgs_StoreIndexII( C, -4, -2, FALSE );
		}
		sgs_Pop( C, use ? 1 : 2 );
	}
	sgs_Pop( C, 1 );
	return 1;
}

static int sgsstd_dict_process( SGS_CTX )
{
	SGSFN( "dict_process" );
	if( !sgs_LoadArgs( C, "?t?p" ) )
		return 0;
	
	sgs_PushIterator( C, 0 );
	while( sgs_IterAdvance( C, -1 ) > 0 )
	{
		if( sgs_IterPushData( C, -1, 1, 1 ) )
			STDLIB_WARN( "failed to read iterator (was dict changed in callback?)" )
		sgs_PushItem( C, -2 );
		sgs_PushItem( C, 1 );
		if( sgs_Call( C, 2, 1 ) )
			STDLIB_WARN( "failed to call the processing function" )
		/* src-dict, callable, ... iterator, key, proc.val. */
		sgs_StoreIndexII( C, 0, -2, FALSE );
		sgs_Pop( C, 1 );
	}
	
	sgs_SetStackSize( C, 1 );
	return 1;
}


static int sgsstd_dict_size( SGS_CTX )
{
	sgs_SizeVal size;
	
	SGSFN( "dict_size" );
	if( !sgs_LoadArgs( C, "t.", &size ) )
		return 0;
	
	sgs_PushInt( C, size );
	return 1;
}

static int sgsstd_map_size( SGS_CTX )
{
	sgs_SizeVal size;
	
	SGSFN( "map_size" );
	if( !sgs_LoadArgs( C, "h.", &size ) )
		return 0;
	
	sgs_PushInt( C, size );
	return 1;
}

static int sgsstd_isset( SGS_CTX )
{
	int oml, ret;
	
	SGSFN( "isset" );
	
	if( !sgs_LoadArgs( C, "?v?m." ) )
		return 0;

	oml = C->minlev;
	C->minlev = INT32_MAX;
	ret = sgs_PushIndexII( C, -2, -1, TRUE );
	C->minlev = oml;
	sgs_PushBool( C, ret == SGS_SUCCESS );
	return 1;
}

static int sgsstd_unset( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	DictHdr* dh;
	
	SGSFN( "unset" );
	
	if( !sgs_IsObject( C, 0, sgsstd_dict_iface ) &&
		!sgs_IsObject( C, 0, sgsstd_map_iface ) )
		return sgs_ArgErrorExt( C, 0, 0, "dict / map", "" );
	
	dh = (DictHdr*) sgs_GetObjectData( C, 0 );
	
	if( sgs_IsObject( C, 0, sgsstd_dict_iface ) )
	{
		if( !sgs_LoadArgs( C, ">m.", &str, &size ) )
			return 0;
	}
	else
	{
		if( !sgs_LoadArgs( C, ">?v." ) )
			return 0;
	}
	vht_unset( &dh->ht, C, (C->stack_off+1) );

	return 0;
}

static int sgsstd_clone( SGS_CTX )
{
	int ret;
	
	SGSFN( "clone" );
	
	if( !sgs_LoadArgs( C, "?v." ) )
		return 0;

	ret = sgs_CloneItem( C, 0 );
	if( ret != SGS_SUCCESS )
		STDLIB_WARN( "failed to clone variable" )
	return 1;
}


static int _foreach_lister( SGS_CTX, int vnk )
{
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;
	
	if( sgs_PushIterator( C, 0 ) != SGS_SUCCESS )
		return sgs_ArgErrorExt( C, 0, 0, "iterable", "" );

	sgs_PushArray( C, 0 );
	/* arg1, arg1 iter, output */
	while( sgs_IterAdvance( C, 1 ) > 0 )
	{
		sgs_IterPushData( C, 1, !vnk, vnk );
		sgs_ObjectAction( C, 2, SGS_ACT_ARRAY_PUSH, 1 );
	}
	return 1;
}
static int sgsstd_get_keys( SGS_CTX )
{
	SGSFN( "get_keys" );
	return _foreach_lister( C, 0 );
}
static int sgsstd_get_values( SGS_CTX )
{
	SGSFN( "get_values" );
	return _foreach_lister( C, 1 );
}

static int sgsstd_get_concat( SGS_CTX )
{
	int i, ssz = sgs_StackSize( C );
	SGSFN( "get_concat" );
	if( ssz < 2 )
	{
		return sgs_Msg( C, SGS_WARNING,
			"function expects at least 2 arguments, got %d",
			sgs_StackSize( C ) );
	}

	sgs_PushArray( C, 0 );
	for( i = 0; i < ssz; ++i )
	{
		if( sgs_PushIterator( C, i ) != SGS_SUCCESS )
			return sgs_ArgErrorExt( C, i, 0, "iterable", "" );
		while( sgs_IterAdvance( C, -1 ) > 0 )
		{
			/* ..., output, arg2 iter */
			sgs_IterPushData( C, -1, FALSE, TRUE );
			sgs_ObjectAction( C, -3, SGS_ACT_ARRAY_PUSH, 1 );
		}
		sgs_Pop( C, 1 );
	}
	return 1;
}

static int sgsstd__get_merged__common( SGS_CTX, sgs_SizeVal ssz )
{
	sgs_SizeVal i;
	for( i = 0; i < ssz; ++i )
	{
		if( sgs_PushIterator( C, i ) != SGS_SUCCESS )
			return sgs_ArgErrorExt( C, i, 0, "iterable", "" );
		while( sgs_IterAdvance( C, -1 ) > 0 )
		{
			int ret;
			/* ..., output, arg2 iter */
			ret = sgs_IterPushData( C, -1, TRUE, TRUE );
			if( ret != SGS_SUCCESS ) STDLIB_WARN( "failed to retrieve data from iterator" )
			ret = sgs_StoreIndexII( C, -4, -2, FALSE );
			if( ret != SGS_SUCCESS ) STDLIB_WARN( "failed to store data" )
			sgs_Pop( C, 1 );
		}
		sgs_Pop( C, 1 );
	}
	return 1;
}

static int sgsstd_get_merged( SGS_CTX )
{
	sgs_SizeVal ssz = sgs_StackSize( C );
	SGSFN( "get_merged" );
	if( ssz < 2 )
	{
		return sgs_Msg( C, SGS_WARNING,
			"function expects at least 2 arguments, got %d",
			sgs_StackSize( C ) );
	}

	sgs_PushDict( C, 0 );
	return sgsstd__get_merged__common( C, ssz );
}

static int sgsstd_get_merged_map( SGS_CTX )
{
	sgs_SizeVal ssz = sgs_StackSize( C );
	SGSFN( "get_merged_map" );
	if( ssz < 2 )
	{
		return sgs_Msg( C, SGS_WARNING,
			"function expects at least 2 arguments, got %d",
			sgs_StackSize( C ) );
	}

	sgs_PushMap( C, 0 );
	return sgsstd__get_merged__common( C, ssz );
}


static int sgsstd_tobool( SGS_CTX )
{
	SGSFN( "tobool" );
	if( sgs_ItemType( C, 0 ) == SVT_BOOL )
	{
		sgs_SetStackSize( C, 1 );
		return 1;
	}
	sgs_PushBool( C, sgs_GetBool( C, 0 ) );
	return 1;
}

static int sgsstd_toint( SGS_CTX )
{
	SGSFN( "toint" );
	if( sgs_ItemType( C, 0 ) == SVT_INT )
	{
		sgs_SetStackSize( C, 1 );
		return 1;
	}
	sgs_PushInt( C, sgs_GetInt( C, 0 ) );
	return 1;
}

static int sgsstd_toreal( SGS_CTX )
{
	SGSFN( "toreal" );
	if( sgs_ItemType( C, 0 ) == SVT_REAL )
	{
		sgs_SetStackSize( C, 1 );
		return 1;
	}
	sgs_PushReal( C, sgs_GetReal( C, 0 ) );
	return 1;
}

static int sgsstd_tostring( SGS_CTX )
{
	char* str;
	sgs_SizeVal sz;
	SGSFN( "tostring" );
	if( sgs_ItemType( C, 0 ) == SVT_STRING )
	{
		sgs_SetStackSize( C, 1 );
		return 1;
	}
	str = sgs_ToStringBuf( C, 0, &sz );
	if( str )
		sgs_PushStringBuf( C, str, sz );
	else
		sgs_PushStringBuf( C, "", 0 );
	return 1;
}

static int sgsstd_toptr( SGS_CTX )
{
	SGSFN( "toptr" );
	if( sgs_ItemType( C, 0 ) == SVT_PTR )
	{
		sgs_SetStackSize( C, 1 );
		return 1;
	}
	sgs_PushPtr( C, sgs_GetPtr( C, 0 ) );
	return 1;
}


static int sgsstd_parseint( SGS_CTX )
{
	sgs_Int i;
	SGSFN( "parseint" );
	if( sgs_ItemType( C, 0 ) == SVT_INT )
	{
		sgs_SetStackSize( C, 1 );
		return 1;
	}
	if( sgs_ParseInt( C, 0, &i ) )
		sgs_PushInt( C, i );
	else
		sgs_PushNull( C );
	return 1;
}

static int sgsstd_parsereal( SGS_CTX )
{
	sgs_Real r;
	SGSFN( "parsereal" );
	if( sgs_ItemType( C, 0 ) == SVT_REAL )
	{
		sgs_SetStackSize( C, 1 );
		return 1;
	}
	if( sgs_ParseReal( C, 0, &r ) )
		sgs_PushReal( C, r );
	else
		sgs_PushNull( C );
	return 1;
}


static int sgsstd_typeof( SGS_CTX )
{
	SGSFN( "typeof" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;
	sgs_TypeOf( C );
	return 1;
}

static int sgsstd_typeid( SGS_CTX )
{
	SGSFN( "typeid" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;
	sgs_PushInt( C, sgs_ItemType( C, 0 ) );
	return 1;
}

static int sgsstd_is_numeric( SGS_CTX )
{
	int res;
	uint32_t ty = sgs_ItemType( C, 0 );
	
	SGSFN( "is_numeric" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;

	if( ty == SVT_NULL || ty == SVT_FUNC ||
		ty == SVT_CFUNC || ty == SVT_OBJECT )
		res = FALSE;
	else
		res = ty != SVT_STRING || sgs_IsNumericString(
			sgs_GetStringPtr( C, 0 ), sgs_GetStringSize( C, 0 ) );

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_is_callable( SGS_CTX )
{
	SGSFN( "is_callable" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;
	
	sgs_PushBool( C, sgs_IsCallable( C, 0 ) );
	return 1;
}


/* I/O */

static int sgsstd_print( SGS_CTX )
{
	int i, ssz;
	SGSBASEFN( "print" );
	ssz = sgs_StackSize( C );
	for( i = 0; i < ssz; ++i )
	{
		sgs_SizeVal size;
		char* buf = sgs_ToStringBuf( C, i, &size );
		/* WP: string limit */
		sgs_Write( C, buf, (size_t) size );
	}
	return 0;
}

static int sgsstd_println( SGS_CTX )
{
	SGSFN( "println" );
	sgsstd_print( C );
	sgs_Write( C, "\n", 1 );
	return 0;
}

static int sgsstd_printlns( SGS_CTX )
{
	int i, ssz;
	ssz = sgs_StackSize( C );
	
	SGSFN( "printlns" );
	
	for( i = 0; i < ssz; ++i )
	{
		sgs_SizeVal size;
		char* buf = sgs_ToStringBuf( C, i, &size );
		/* WP: string limit */
		sgs_Write( C, buf, (size_t) size );
		sgs_Write( C, "\n", 1 );
	}
	return 0;
}

static int sgsstd_printvar( SGS_CTX )
{
	int i, ssz, res;
	ssz = sgs_StackSize( C );
	
	SGSFN( "printvar" );
	
	for( i = 0; i < ssz; ++i )
	{
		sgs_PushItem( C, i );
		res = sgs_DumpVar( C, 5 );
		if( res == SGS_SUCCESS )
		{
			sgs_SizeVal bsz;
			char* buf = sgs_ToStringBuf( C, -1, &bsz );
			/* WP: string limit */
			sgs_Write( C, buf, (size_t) bsz );
			sgs_Write( C, "\n", 1 );
		}
		else
		{
			char ebuf[ 64 ];
			sprintf( ebuf, "unknown error while dumping variable #%d", i + 1 );
			STDLIB_ERR( ebuf );
		}
		sgs_Pop( C, 1 );
	}
	return 0;
}
static int sgsstd_printvar_ext( SGS_CTX )
{
	sgs_Int depth = 5;
	
	SGSFN( "printvar_ext" );
	
	if( !sgs_LoadArgs( C, ">|i.", &depth ) )
		return 0;

	sgs_SetStackSize( C, 1 );
	if( sgs_DumpVar( C, (int) depth ) == SGS_SUCCESS )
	{
		sgs_SizeVal bsz;
		char* buf = sgs_ToStringBuf( C, -1, &bsz );
		/* WP: string limit */
		sgs_Write( C, buf, (size_t) bsz );
		sgs_Write( C, "\n", 1 );
	}
	else
		STDLIB_ERR( "unknown error while dumping variable" );
	return 0;
}

static int sgsstd_read_stdin( SGS_CTX )
{
	MemBuf B;
	char bfr[ 1024 ];
	int all = 0;
	
	SGSFN( "read_stdin" );
	
	if( !sgs_LoadArgs( C, "|b", &all ) )
		return 0;

	B = membuf_create();
	while( fgets( bfr, 1024, stdin ) )
	{
		size_t len = strlen( bfr );
		membuf_appbuf( &B, C, bfr, len );
		if( len && !all && bfr[ len - 1 ] == '\n' )
		{
			B.size--;
			break;
		}
	}
	if( B.size > 0x7fffffff )
	{
		membuf_destroy( &B, C );
		STDLIB_WARN( "read more bytes than allowed to store" );
	}
	/* WP: error condition */
	sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	membuf_destroy( &B, C );
	return 1;
}


/* OS */

static int sgsstd_ftime( SGS_CTX )
{
	SGSFN( "ftime" );
	sgs_PushReal( C, sgs_GetTime() );
	return 1;
}


/* utils */

static int sgsstd_rand( SGS_CTX )
{
	SGSFN( "rand" );
	sgs_PushInt( C, rand() );
	return 1;
}

static int sgsstd_randf( SGS_CTX )
{
	SGSFN( "randf" );
	sgs_PushReal( C, (double) rand() / (double) RAND_MAX );
	return 1;
}

static int sgsstd_srand( SGS_CTX )
{
	uint32_t s;
	SGSFN( "srand" );
	if( !sgs_LoadArgs( C, "+l", &s ) )
		return 0;
	srand( s );
	return 0;
}


/* internal utils */

struct pcall_printinfo
{
	sgs_MsgFunc pfn;
	void* pctx;
	sgs_Variable handler;
	int depth;
};

static void sgsstd_pcall_print( void* data, SGS_CTX, int type, const char* message )
{
	int ret = 0;
	struct pcall_printinfo* P = (struct pcall_printinfo*) data;
	P->depth++;
	
	if( P->depth > 1 )
		ret = type; /* don't handle errors thrown inside handlers */
	else if( P->handler.type != SVT_NULL )
	{
		sgs_PushInt( C, type );
		sgs_PushString( C, message );
		if( sgs_CallP( C, &P->handler, 2, 1 ) )
		{
			P->pfn( P->pctx, C, SGS_ERROR, "Error detected while attempting to call error handler" );
		}
		else
		{
			ret = (int) sgs_GetInt( C, -1 );
			sgs_Pop( C, 1 );
		}
	}
	
	if( ret > 0 )
		P->pfn( P->pctx, C, ret, message );
	
	P->depth--;
}

static int sgsstd_pcall( SGS_CTX )
{
	struct pcall_printinfo P;
	int b = 0;
	
	SGSFN( "pcall" );
	
	if( !sgs_LoadArgs( C, "?p|p", &b ) )
		return 0;
	
	P.pfn = C->msg_fn;
	P.pctx = C->msg_ctx;
	P.handler.type = SVT_NULL;
	P.depth = 0;
	if( b )
	{
		sgs_GetStackItem( C, 1, &P.handler );
		sgs_Acquire( C, &P.handler );
	}
	
	C->msg_fn = sgsstd_pcall_print;
	C->msg_ctx = &P;
	
	sgs_PushItem( C, 0 );
	sgs_Call( C, 0, 0 );
	
	C->msg_fn = P.pfn;
	C->msg_ctx = P.pctx;
	if( b )
		sgs_Release( C, &P.handler );
	
	return 0;
}

static int sgsstd_assert( SGS_CTX )
{
	char* str = NULL;
	
	SGSFN( "assert" );
	
	if( !sgs_LoadArgs( C, "?v|s", &str ) )
		return 0;
	
	SGSFN( NULL );
	if( !sgs_GetBool( C, 0 ) )
		sgs_Msg( C, SGS_ERROR, !str ? "assertion failed" :
			"assertion failed: %s", str );
	return 0;
}

static int sgsstd_eval( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	int rvc = 0;
	
	SGSFN( "eval" );
	
	if( !sgs_LoadArgs( C, "m", &str, &size ) )
		return 0;
	
	/* WP: string limit */
	sgs_EvalBuffer( C, str, (size_t) size, &rvc );
	return rvc;
}

static int sgsstd_eval_file( SGS_CTX )
{
	int ret, retcnt = 0;
	char* str;
	
	SGSFN( "eval_file" );
	
	if( !sgs_LoadArgs( C, "s", &str ) )
		return 0;

	ret = sgs_EvalFile( C, str, &retcnt );
	if( ret == SGS_ENOTFND )
		STDLIB_WARN( "file not found" )
	return retcnt;
}


static void _sgsstd_compile_pfn( void* data, SGS_CTX, int type, const char* msg )
{
	int ret;
	sgs_Variable* pvar = (sgs_Variable*) data;
	
	sgs_PushVariable( C, pvar );
	
	sgs_PushString( C, "type" );
	sgs_PushInt( C, type );
	sgs_PushString( C, "msg" );
	sgs_PushString( C, msg );
	sgs_PushDict( C, 4 );
	
	ret = sgs_ObjectAction( C, -2, SGS_ACT_ARRAY_PUSH, 1 );
	UNUSED( ret );
	sgs_BreakIf( ret < 0 );
	
	sgs_Pop( C, 1 );
}

static int sgsstd_compile_sgs( SGS_CTX )
{
	int ret;
	char* buf = NULL, *outbuf = NULL;
	sgs_SizeVal size = 0;
	size_t outsize = 0;
	sgs_Variable var;
	
	sgs_MsgFunc oldpfn;
	void* oldpctx;
	
	SGSFN( "compile_sgs" );
	
	if( !sgs_LoadArgs( C, "m", &buf, &size ) )
		return 0;
	
	sgs_PushArray( C, 0 );
	sgs_GetStackItem( C, -1, &var );
	sgs_Acquire( C, &var );
	sgs_Pop( C, 1 );
	
	oldpfn = C->msg_fn;
	oldpctx = C->msg_ctx;
	sgs_SetMsgFunc( C, _sgsstd_compile_pfn, &var );
	SGSFN( NULL );
	
	/* WP: string limit */
	ret = sgs_Compile( C, buf, (size_t) size, &outbuf, &outsize );
	
	SGSFN( "compile_sgs" );
	C->msg_fn = oldpfn;
	C->msg_ctx = oldpctx;
	
	if( ret < 0 )
		sgs_PushNull( C );
	else
	{
		if( outsize > 0x7fffffff )
			sgs_PushStringBuf( C, outbuf, (sgs_SizeVal) outsize );
		else
		{
			sgs_PushNull( C );
			sgs_Msg( C, SGS_WARNING, "size of compiled code is bigger than allowed to store" );
		}
		sgs_Dealloc( outbuf );
	}
	sgs_PushVariable( C, &var );
	sgs_Release( C, &var );
	
	return 2;
}


static int sgsstd__inclib( SGS_CTX, const char* name, int override )
{
	char buf[ 16 ];
	int ret = SGS_ENOTFND;

	sprintf( buf, "-%.14s", name );
	if( !override && sgs_PushGlobal( C, buf ) == SGS_SUCCESS )
	{
		sgs_Pop( C, 1 );
		return SGS_SUCCESS;
	}

	if( strcmp( name, "fmt" ) == 0 ) ret = sgs_LoadLib_Fmt( C );
	else if( strcmp( name, "io" ) == 0 ) ret = sgs_LoadLib_IO( C );
	else if( strcmp( name, "math" ) == 0 ) ret = sgs_LoadLib_Math( C );
	else if( strcmp( name, "os" ) == 0 ) ret = sgs_LoadLib_OS( C );
	else if( strcmp( name, "re" ) == 0 ) ret = sgs_LoadLib_RE( C );
	else if( strcmp( name, "string" ) == 0 ) ret = sgs_LoadLib_String( C );

	if( ret == SGS_SUCCESS )
	{
		sgs_PushBool( C, TRUE );
		sgs_StoreGlobal( C, buf );
	}

	return ret;
}

static int sgsstd__chkinc( SGS_CTX, int argid )
{
	sgs_PushString( C, "+" );
	sgs_PushItem( C, argid );
	sgs_StringConcat( C, 2 );
	sgs_PushEnv( C );
	if( sgs_PushIndexII( C, -1, -2, 0 ) != SGS_SUCCESS )
		return FALSE;
	/* sgs_Pop( C, 2 ); [.., string, bool] - will return last */
	return TRUE;
}

static void sgsstd__setinc( SGS_CTX, int argid )
{
	sgs_PushEnv( C );
	sgs_PushString( C, "+" );
	sgs_PushItem( C, argid );
	sgs_StringConcat( C, 2 );
	sgs_PushBool( C, TRUE );
	sgs_SetIndexIII( C, -3, -2, -1, 0 );
	sgs_Pop( C, 3 );
}

static int sgsstd_include_library( SGS_CTX )
{
	int ret, over = FALSE;
	char* str;

	SGSBASEFN( "include_library" );
	
	if( !sgs_LoadArgs( C, "s|b", &str, &over ) )
		return 0;

	ret = sgsstd__inclib( C, str, over );

	if( ret == SGS_ENOTFND )
		STDLIB_WARN( "library not found" )
	sgs_PushBool( C, ret == SGS_SUCCESS );
	return 1;
}

static int sgsstd_include_file( SGS_CTX )
{
	int ret, over = FALSE;
	char* str;

	SGSBASEFN( "include_file" );
	
	if( !sgs_LoadArgs( C, "s|b", &str, &over ) )
		return 0;

	if( !over && sgsstd__chkinc( C, 0 ) )
		return 1;

	ret = sgs_ExecFile( C, str );
	if( ret == SGS_ENOTFND )
		return sgs_Msg( C, SGS_WARNING, "file '%s' was not found", str );
	if( ret == SGS_SUCCESS )
		sgsstd__setinc( C, 0 );
	sgs_PushBool( C, ret == SGS_SUCCESS );
	return 1;
}

static int sgsstd_include_shared( SGS_CTX )
{
	char* fnstr;
	int ret, over = FALSE;
	sgs_CFunc func;
	
	SGSBASEFN( "include_shared" );
	
	if( !sgs_LoadArgs( C, "s|b", &fnstr, &over ) )
		return 0;

	if( !over && sgsstd__chkinc( C, 0 ) )
		return 1;

	ret = sgsXPC_GetProcAddress( fnstr, SGS_LIB_ENTRY_POINT, (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE )
			return sgs_Msg( C, SGS_WARNING, "file '%s' was not found", fnstr );
		else if( ret == SGS_XPC_NOPROC )
			return sgs_Msg( C, SGS_WARNING, "procedure '" SGS_LIB_ENTRY_POINT "' was not found" );
		else if( ret == SGS_XPC_NOTSUP )
			STDLIB_WARN( "feature is not supported on this platform" )
		else STDLIB_WARN( "unknown error occured" )
	}
	
	ret = func( C );
	if( ret != SGS_SUCCESS )
		STDLIB_WARN( "module failed to initialize" )
	
	sgsstd__setinc( C, 0 );
	sgs_PushBool( C, TRUE );
	return 1;
}

static int _push_curdir( SGS_CTX )
{
	const char* file, *fend;
	sgs_StackFrame* sf;

	sf = sgs_GetFramePtr( C, 1 )->prev;
	if( !sf )
		return 0;

	sgs_StackFrameInfo( C, sf, NULL, &file, NULL );
	if( file )
	{
		fend = file + strlen( file );
		while( fend > file && *fend != '/' && *fend != '\\' )
			fend--;
		if( fend == file )
		{
#ifdef _WIN32
			sgs_PushString( C, "." );
#else
			if( *file == '/' )
				sgs_PushString( C, "" );
			else
				sgs_PushString( C, "." );
#endif
		}
		else
		{
			ptrdiff_t len = fend - file;
			if( len > 0x7fffffff )
				return 0;
			else
				/* WP: error condition */
				sgs_PushStringBuf( C, file, (sgs_SizeVal) len );
		}
		return 1;
	}
	
	return 0;
}

static int _find_includable_file( SGS_CTX, MemBuf* tmp, char* ps,
	size_t pssize, char* fn, size_t fnsize, char* dn, size_t dnsize )
{
	if( ( fnsize > 2 && *fn == '.' && ( fn[1] == '/' || fn[1] == '\\' ) ) ||
#ifdef _WIN32
		( fnsize > 2 && fn[1] == ':' ) )
#else
		( fnsize > 1 && *fn == '/' ) )
#endif
	{
		FILE* f;
		membuf_setstrbuf( tmp, C, fn, fnsize );
		if( ( f = fopen( tmp->ptr, "rb" ) ) != NULL )
		{
			fclose( f );
			return 1;
		}
	}
	else
	{
		char* pse = ps + pssize;
		char* psc = ps;
		while( ps <= pse )
		{
			if( ps == pse || *ps == ';' )
			{
				FILE* f;
				membuf_resize( tmp, C, 0 );
				while( psc < ps )
				{
					if( *psc == '?' )
						membuf_appbuf( tmp, C, fn, fnsize );
					else if( *psc == '|' )
					{
						if( dn )
							membuf_appbuf( tmp, C, dn, dnsize );
						else
						{
							psc = ps;
							goto notthispath;
						}
					}
					else
						membuf_appchr( tmp, C, *psc );
					psc++;
				}
				membuf_appchr( tmp, C, 0 );
				if( ( f = fopen( tmp->ptr, "rb" ) ) != NULL )
				{
					fclose( f );
					return 1;
				}
notthispath:
				psc++;
			}
			ps++;
		}
	}
	return 0;
}

static int sgsstd_include( SGS_CTX )
{
	char* fnstr, *dnstr = NULL;
	sgs_SizeVal fnsize, dnsize = 0;
	int over = 0, ret;
	
	SGSFN( "include" );
	
	if( !sgs_LoadArgs( C, "m|b", &fnstr, &fnsize, &over ) )
		return 0;
	
	if( !over && sgsstd__chkinc( C, 0 ) )
		goto success;
	
	ret = sgsstd__inclib( C, fnstr, over );
	if( ret == SGS_SUCCESS )
		goto success;
	else
	{
		char* ps;
		sgs_SizeVal pssize;
		sgs_CFunc func;
		MemBuf mb = membuf_create();
		
		ret = sgs_PushGlobal( C, "SGS_PATH" );
		if( ret != SGS_SUCCESS ||
			( ps = sgs_ToStringBuf( C, -1, &pssize ) ) != NULL )
		{
			ps = SGS_INCLUDE_PATH;
			pssize = (sgs_SizeVal) strlen( ps );
		}
		
		if( _push_curdir( C ) )
		{
			dnstr = sgs_GetStringPtr( C, -1 );
			dnsize = sgs_GetStringSize( C, -1 );
		}
		/* WP: string limit */
		ret = _find_includable_file( C, &mb, ps, (size_t) pssize, fnstr, (size_t) fnsize, dnstr, (size_t) dnsize );
		if( ret == 0 || mb.size == 0 )
		{
			membuf_destroy( &mb, C );
			return sgs_Msg( C, SGS_WARNING, "could not find '%.*s' "
				"with include path '%.*s'", fnsize, fnstr, pssize, ps );
		}
		
		ret = sgsXPC_GetProcAddress( mb.ptr, SGS_LIB_ENTRY_POINT, (void**) &func );
		if( ret == SGS_SUCCESS )
		{
			ret = func( C );
			if( ret == SGS_SUCCESS )
			{
				membuf_destroy( &mb, C );
				goto success;
			}
		}
		else if( ret != SGS_XPC_NOTLIB )
		{
			membuf_destroy( &mb, C );
			return sgs_Msg( C, SGS_WARNING, "failed to load native module '%.*s'", fnsize, fnstr );
		}
		
		sgs_PushString( C, mb.ptr );
		sgs_PushString( C, " - include" );
		sgs_StringConcat( C, 2 );
		SGSFN( sgs_GetStringPtr( C, -1 ) );
		ret = sgs_ExecFile( C, mb.ptr );
		SGSFN( "include" );
		membuf_destroy( &mb, C );
		if( ret == SGS_SUCCESS )
			goto success;
	}
	
	sgs_Msg( C, SGS_WARNING, "could not load '%.*s'", fnsize, fnstr );
	sgs_PushBool( C, 0 );
	return 1;
	
success:
	sgsstd__setinc( C, 0 );
	sgs_PushBool( C, 1 );
	return 1;
}

static int sgsstd_import_cfunc( SGS_CTX )
{
	char* fnstr, *pnstr;
	int ret;
	sgs_CFunc func;
	
	SGSFN( "import_cfunc" );
	
	if( !sgs_LoadArgs( C, "ss", &fnstr, &pnstr ) )
		return 0;

	ret = sgsXPC_GetProcAddress( fnstr, pnstr, (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE )
			return sgs_Msg( C, SGS_WARNING, "file '%s' was not found", fnstr );
		else if( ret == SGS_XPC_NOPROC )
			return sgs_Msg( C, SGS_WARNING, "procedure '%s' was not found", pnstr );
		else if( ret == SGS_XPC_NOTSUP )
			STDLIB_WARN( "feature is not supported on this platform" )
		else STDLIB_WARN( "unknown error occured" )
	}
	
	sgs_PushCFunction( C, func );
	return 1;
}

static int sgsstd_sys_curfile( SGS_CTX )
{
	const char* file;
	sgs_StackFrame* sf;
	
	SGSFN( "sys_curfile" );
	
	if( sgs_StackSize( C ) )
		STDLIB_WARN( "function expects 0 arguments" )

	sf = sgs_GetFramePtr( C, 1 )->prev;
	if( !sf )
		return 0;

	sgs_StackFrameInfo( C, sf, NULL, &file, NULL );
	if( file )
	{
		sgs_PushString( C, file );
		return 1;
	}
	return 0;
}

static int sgsstd_sys_curfiledir( SGS_CTX )
{
	SGSFN( "sys_curfiledir" );
	
	if( sgs_StackSize( C ) )
		STDLIB_WARN( "function expects 0 arguments" )
	
	return _push_curdir( C );
}

static int sgsstd_sys_print( SGS_CTX )
{
	char* errmsg;
	sgs_Int errcode;
	
	SGSFN( "sys_print" );
	
	if( !sgs_LoadArgs( C, "is", &errcode, &errmsg ) )
		return 0;
	
	SGSFN( NULL );

	sgs_Msg( C, (int) errcode, errmsg );
	return 0;
}

static int sgsstd__printwrapper( SGS_CTX, const char* fn, int code )
{
	char* msg;
	SGSFN( fn );
	if( sgs_LoadArgs( C, "s", &msg ) )
	{
		SGSFN( NULL );
		sgs_Msg( C, code, msg );
	}
	return 0;
}

static int sgsstd_INFO( SGS_CTX ){ return sgsstd__printwrapper( C, "INFO", SGS_INFO ); }
static int sgsstd_WARNING( SGS_CTX ){ return sgsstd__printwrapper( C, "WARNING", SGS_WARNING ); }
static int sgsstd_ERROR( SGS_CTX ){ return sgsstd__printwrapper( C, "ERROR", SGS_ERROR ); }

static int sgsstd_sys_abort( SGS_CTX )
{
	sgs_Abort( C );
	return 0;
}
static int sgsstd_app_abort( SGS_CTX ){ abort(); return 0; }
static int sgsstd_app_exit( SGS_CTX )
{
	sgs_Int ret = 0;
	
	if( !sgs_LoadArgs( C, "|i", &ret ) )
		return 0;
	
	exit( (int) ret );
	return 0;
}

static int sgsstd_sys_replevel( SGS_CTX )
{
	int lev = C->minlev;
	
	SGSFN( "sys_replevel" );
	
	if( sgs_StackSize( C ) )
	{
		sgs_Int i;
		if( !sgs_LoadArgs( C, "i", &i ) )
			return 0;
		C->minlev = (int) i;
		return 0;
	}
	sgs_PushInt( C, lev );
	return 1;
}

static int sgsstd_sys_stat( SGS_CTX )
{
	sgs_Int type;
	
	SGSFN( "sys_stat" );
	
	if( !sgs_LoadArgs( C, "i", &type ) )
		return 0;

	sgs_PushInt( C, sgs_Stat( C, (int) type ) );
	return 1;
}

static int sgsstd_errno( SGS_CTX )
{
	int retstr = 0;
	
	SGSFN( "errno" );
	
	if( !sgs_LoadArgs( C, "|b", &retstr ) )
		return 0;
	
	if( retstr )
		sgs_PushString( C, strerror( C->last_errno ) );
	else
		sgs_PushInt( C, C->last_errno );
	
	return 1;
}

static int sgsstd_errno_string( SGS_CTX )
{
	sgs_Int e;
	
	SGSFN( "errno_string" );
	
	if( !sgs_LoadArgs( C, "i", &e ) )
		return 0;
	
	sgs_PushString( C, strerror( (int) e ) );
	
	return 1;
}


#define AE( x ) #x, (const char*) x
static const char* errno_key_table[] =
{
	AE( E2BIG ), AE( EACCES ), AE( EAGAIN ), AE( EBADF ), AE( EBUSY ),
	AE( ECHILD ), AE( EDEADLK ), AE( EDOM ), AE( EEXIST ), AE( EFAULT ),
	AE( EFBIG ), AE( EILSEQ ), AE( EINTR ), AE( EINVAL ), AE( EIO ),
	AE( EISDIR ), AE( EMFILE ), AE( EMLINK ), AE( ENAMETOOLONG ), AE( ENFILE ),
	AE( ENODEV ), AE( ENOENT ), AE( ENOEXEC ), AE( ENOLCK ), AE( ENOMEM ),
	AE( ENOSPC ), AE( ENOSYS ), AE( ENOTDIR ), AE( ENOTEMPTY ), AE( ENOTTY ),
	AE( ENXIO ), AE( EPERM ), AE( EPIPE ), AE( ERANGE ), AE( EROFS ),
	AE( ESPIPE ), AE( ESRCH ), AE( EXDEV ),
	
	NULL,
};
#undef AE

static int sgsstd_errno_value( SGS_CTX )
{
	const char** ekt = errno_key_table;
	char* str;
	
	SGSFN( "errno_value" );
	
	if( !sgs_LoadArgs( C, "s", &str ) )
		return 0;
	
	while( *ekt )
	{
		if( strcmp( *ekt, str ) == 0 )
		{
			sgs_PushInt( C, (int) (size_t) ekt[1] );
			return 1;
		}
		ekt += 2;
	}
	
	sgs_Msg( C, SGS_ERROR, "this errno value is unsupported" );
	return 0;
}

static int sgsstd_dumpvar( SGS_CTX )
{
	int i, ssz, res, rc = 0;
	ssz = sgs_StackSize( C );
	
	SGSFN( "dumpvar" );
	
	for( i = 0; i < ssz; ++i )
	{
		sgs_PushItem( C, i );
		res = sgs_DumpVar( C, 5 );
		if( res == SGS_SUCCESS )
		{
			sgs_PushString( C, "\n" );
			rc += 2;
		}
		else
		{
			char ebuf[ 64 ];
			sprintf( ebuf, "unknown error while dumping variable #%d", i + 1 );
			STDLIB_ERR( ebuf );
			sgs_Pop( C, 1 );
		}
	}
	if( rc )
	{
		if( sgs_StringConcat( C, rc ) != SGS_SUCCESS )
			STDLIB_ERR( "failed to concatenate the output" )
		return 1;
	}
	return 0;
}
static int sgsstd_dumpvar_ext( SGS_CTX )
{
	sgs_Int depth = 5;
	
	SGSFN( "dumpvar_ext" );
	
	if( !sgs_LoadArgs( C, ">|i.", &depth ) )
		return 0;
	
	sgs_SetStackSize( C, 1 );
	if( sgs_DumpVar( C, (int) depth ) == SGS_SUCCESS )
		return 1;
	else
		STDLIB_ERR( "unknown error while dumping variable" );
	return 0;
}

static int sgsstd_gc_collect( SGS_CTX )
{
	int ret;
	ptrdiff_t orvc = sgs_Stat( C, SGS_STAT_OBJCOUNT );
	
	SGSFN( "gc_collect" );
	
	ret = sgs_GCExecute( C );
	
	if( ret == SGS_SUCCESS )
		sgs_PushInt( C, orvc - sgs_Stat( C, SGS_STAT_OBJCOUNT ) );
	else
		sgs_PushBool( C, FALSE );
	return 1;
}


static int sgsstd_serialize( SGS_CTX )
{
	int ret;
	
	SGSFN( "serialize" );
	
	if( !sgs_LoadArgs( C, "?v." ) )
		return 0;

	ret = sgs_Serialize( C );
	if( ret == SGS_SUCCESS )
		return 1;
	else
		STDLIB_ERR( "failed to serialize" )
}


static int check_arrayordict_fn( SGS_CTX, int argid, va_list args, int flags )
{
	uint32_t ty = sgs_ItemType( C, argid );
	if( ty != SVT_OBJECT || (
		!sgs_IsObject( C, argid, sgsstd_array_iface ) &&
		!sgs_IsObject( C, argid, sgsstd_dict_iface ) ) )
		return sgs_ArgErrorExt( C, argid, 0, "array or dict", "" );
	return 1;
}

static int sgsstd_unserialize( SGS_CTX )
{
	int ret, ssz = sgs_StackSize( C ), dictpos;
	
	SGSFN( "unserialize" );
	
	if( !sgs_LoadArgs( C, "?s|x", check_arrayordict_fn ) )
		return 0;
	
	if( ssz >= 2 )
	{
		if( sgs_IsObject( C, 1, sgsstd_array_iface ) )
		{
			dictpos = sgs_StackSize( C );
			sgs_PushDict( C, 0 );
			sgs_PushIterator( C, 1 );
			while( sgs_IterAdvance( C, -1 ) > 0 )
			{
				if( !sgs_IterPushData( C, -1, 0, 1 ) )
				{
					char* nm = NULL;
					if( ( nm = sgs_ToString( C, -1 ) ) != NULL )
					{
						if( sgs_PushGlobal( C, nm ) )
							sgs_PushNull( C );
						sgs_StoreProperty( C, 2, nm );
					}
					sgs_Pop( C, 1 );
				}
			}
			sgs_Pop( C, 1 );
		}
		else
			dictpos = 1;
		
		if( sgs_PushGlobal( C, "_G" ) )
			STDLIB_ERR( "failed to retrieve the environment data" )
		
		if( sgs_PushItem( C, dictpos ) || sgs_StoreGlobal( C, "_G" ) )
			STDLIB_ERR( "failed to change the environment" )
		
		sgs_PushItem( C, 0 );
	}

	ret = sgs_Unserialize( C );
	
	if( ssz >= 2 )
	{
		if( sgs_PushItem( C, dictpos + 1 ) || sgs_StoreGlobal( C, "_G" ) )
			STDLIB_ERR( "failed to restore the environment" )
	}
	
	if( ret == SGS_SUCCESS )
		return 1;
	else if( ret == SGS_EINPROC )
		STDLIB_ERR( "error in data" )
	else if( ret == SGS_ENOTFND )
		STDLIB_ERR( "could not find something (like recreation function)" )
	else
		STDLIB_ERR( "unknown error" )
}


/* register all */
#define FN( name ) { #name, sgsstd_##name }
static sgs_RegFuncConst regfuncs[] =
{
	/* containers */
	FN( array ), FN( dict ), FN( map ), { "class", sgsstd_class }, FN( closure ),
	FN( array_filter ), FN( array_process ),
	FN( dict_filter ), FN( dict_process ),
	FN( dict_size ), FN( map_size ), FN( isset ), FN( unset ), FN( clone ),
	FN( get_keys ), FN( get_values ), FN( get_concat ),
	FN( get_merged ), FN( get_merged_map ),
	/* types */
	FN( tobool ), FN( toint ), FN( toreal ), FN( tostring ), FN( toptr ),
	FN( parseint ), FN( parsereal ),
	FN( typeof ), FN( typeid ),
	FN( is_numeric ), FN( is_callable ),
	/* I/O */
	FN( print ), FN( println ), FN( printlns ),
	FN( printvar ), FN( printvar_ext ),
	FN( read_stdin ),
	/* OS */
	FN( ftime ),
	/* utils */
	FN( rand ), FN( randf ), FN( srand ),
	/* internal utils */
	FN( pcall ), FN( assert ),
	FN( eval ), FN( eval_file ), FN( compile_sgs ),
	FN( include_library ), FN( include_file ),
	FN( include_shared ), FN( import_cfunc ),
	FN( include ),
	FN( sys_curfile ), FN( sys_curfiledir ),
	FN( sys_print ), FN( INFO ), FN( WARNING ), FN( ERROR ),
	FN( sys_abort ), FN( app_abort ), FN( app_exit ),
	FN( sys_replevel ), FN( sys_stat ),
	FN( errno ), FN( errno_string ), FN( errno_value ),
	FN( dumpvar ), FN( dumpvar_ext ),
	FN( gc_collect ),
	FN( serialize ), FN( unserialize ),
};

static const sgs_RegIntConst regiconsts[] =
{
	{ "SGS_INFO", SGS_INFO },
	{ "SGS_WARNING", SGS_WARNING },
	{ "SGS_ERROR", SGS_ERROR },
	
	{ "VT_NULL", SVT_NULL },
	{ "VT_BOOL", SVT_BOOL },
	{ "VT_INT", SVT_INT },
	{ "VT_REAL", SVT_REAL },
	{ "VT_STRING", SVT_STRING },
	{ "VT_FUNC", SVT_FUNC },
	{ "VT_CFUNC", SVT_CFUNC },
	{ "VT_OBJECT", SVT_OBJECT },
	{ "VT_PTR", SVT_PTR },
	
	{ "RAND_MAX", RAND_MAX },
};


int sgsSTD_PostInit( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, regiconsts, ARRAY_SIZE( regiconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, regfuncs, ARRAY_SIZE( regfuncs ) );
	if( ret != SGS_SUCCESS ) return ret;
	
	sgs_PushString( C, SGS_INCLUDE_PATH );
	ret = sgs_StoreGlobal( C, "SGS_PATH" );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "array", sgsstd_array_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "array_iterator", sgsstd_array_iter_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "dict", sgsstd_dict_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "dict_iterator", sgsstd_dict_iter_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "map", sgsstd_map_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "class", sgsstd_class_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "closure", sgsstd_closure_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	return SGS_SUCCESS;
}

int sgsSTD_MakeArray( SGS_CTX, sgs_Variable* out, sgs_SizeVal cnt )
{
	StkIdx i = 0, ssz = sgs_StackSize( C );
	
	if( ssz < cnt )
		return SGS_EINVAL;
	else
	{
		sgs_Variable *p, *pend;
		void* data = sgs_Malloc( C, SGSARR_ALLOCSIZE( cnt ) );
		sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) sgs_InitObjectIPA( C,
			out, sizeof( sgsstd_array_header_t ), sgsstd_array_iface );
		
		hdr->size = cnt;
		hdr->mem = cnt;
		p = hdr->data = (sgs_Variable*) data;
		pend = p + cnt;
		while( p < pend )
		{
			sgs_GetStackItem( C, i++ - cnt, p );
			sgs_Acquire( C, p++ );
		}
		
		sgs_Pop( C, cnt );
		return SGS_SUCCESS;
	}
}

int sgsSTD_MakeDict( SGS_CTX, sgs_Variable* out, sgs_SizeVal cnt )
{
	DictHdr* dh;
	VHTable* ht;
	StkIdx i, ssz = sgs_StackSize( C );
	
	if( cnt > ssz || cnt % 2 != 0 )
		return SGS_EINVAL;
	
	dh = mkdict( C, out );
	ht = &dh->ht;
	
	for( i = 0; i < cnt; i += 2 )
	{
		if( !sgs_ParseString( C, i - cnt, NULL, NULL ) )
		{
			sgs_Release( C, out );
			return SGS_EINVAL;
		}
		
		vht_set( ht, C, (C->stack_top+i-cnt), (C->stack_top+i+1-cnt) );
	}
	
	sgs_Pop( C, cnt );
	return SGS_SUCCESS;
}

int sgsSTD_MakeMap( SGS_CTX, sgs_Variable* out, sgs_SizeVal cnt )
{
	DictHdr* dh;
	VHTable* ht;
	StkIdx i, ssz = sgs_StackSize( C );
	
	if( cnt > ssz || cnt % 2 != 0 )
		return SGS_EINVAL;
	
	dh = mkmap( C, out );
	ht = &dh->ht;
	
	for( i = 0; i < cnt; i += 2 )
	{
		vht_set( ht, C, (C->stack_top+i-cnt), (C->stack_top+i+1-cnt) );
	}
	
	sgs_Pop( C, cnt );
	return SGS_SUCCESS;
}


#define GLBP C->_G

int sgsSTD_GlobalInit( SGS_CTX )
{
	sgs_Variable var;
	if( SGS_FAILED( sgsSTD_MakeDict( C, &var, 0 ) ) )
		return SGS_EINPROC;
	GLBP = var.data.O;
	return SGS_SUCCESS;
}

int sgsSTD_GlobalFree( SGS_CTX )
{
	VHTVar *p, *pend;
	sgs_VarObj* data = GLBP;
	HTHDR;

	p = ht->vars;
	pend = p + vht_size( ht );
	while( p < pend )
	{
		sgs_Release( C, &p->val );
		p++;
	}
	
	sgs_ObjRelease( C, GLBP );
	
	GLBP = NULL;
	
	return SGS_SUCCESS;
}

int sgsSTD_GlobalGet( SGS_CTX, sgs_Variable* out, sgs_Variable* idx, int apicall )
{
	VHTVar* pair;
	sgs_VarObj* data = GLBP;
	HTHDR;
	
	/* `out` is expected to point at an initialized variable */

	if( idx->type != SVT_STRING )
	{
		sgs_Release( C, out );
		return SGS_ENOTSUP;
	}

	if( strcmp( str_cstr( idx->data.S ), "_G" ) == 0 )
	{
		sgs_Release( C, out );
		out->type = SVT_OBJECT;
		out->data.O = GLBP;
		sgs_Acquire( C, out );
		return SGS_SUCCESS;
	}
	else if( ( pair = vht_get( ht, idx ) ) != NULL )
	{
		sgs_Release( C, out );
		*out = pair->val;
		sgs_Acquire( C, out );
		return SGS_SUCCESS;
	}
	else
	{
		sgs_Release( C, out );
		if( !apicall )
			sgs_Msg( C, SGS_WARNING, "variable '%s' was not found", str_cstr( idx->data.S ) );
		return SGS_ENOTFND;
	}
}

int sgsSTD_GlobalSet( SGS_CTX, sgs_Variable* idx, sgs_Variable* val, int apicall )
{
	sgs_VarObj* data = GLBP;
	HTHDR;

	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	if( strcmp( var_cstr( idx ), "_G" ) == 0 )
	{
		int ret = sgs_SetEnv( C, val );
		if( !apicall && SGS_FAILED( ret ) )
			sgs_Msg( C, SGS_ERROR, "_G only accepts 'dict' values" );
		return ret;
	}

	vht_set( ht, C, idx, val );
	return SGS_SUCCESS;
}

int sgsSTD_GlobalGC( SGS_CTX )
{
	sgs_VarObj* data = GLBP;
	if( data )
	{
		if( sgs_ObjGCMark( C, data ) < 0 )
			return SGS_EINPROC;
	}
	return SGS_SUCCESS;
}

int sgsSTD_GlobalIter( SGS_CTX, VHTVar** outp, VHTVar** outpend )
{
	sgs_VarObj* data = GLBP;

	if( data )
	{
		HTHDR;
		*outp = ht->vars;
		*outpend = ht->vars + vht_size( ht );
		return SGS_SUCCESS;
	}
	else
		return SGS_EINPROC;
}




SGSRESULT sgs_RegFuncConsts( SGS_CTX, const sgs_RegFuncConst* list, int size )
{
	int ret;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_PushCFunction( C, list->value );
		ret = sgs_StoreGlobal( C, list->name );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_RegIntConsts( SGS_CTX, const sgs_RegIntConst* list, int size )
{
	int ret;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_PushInt( C, list->value );
		ret = sgs_StoreGlobal( C, list->name );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_RegRealConsts( SGS_CTX, const sgs_RegRealConst* list, int size )
{
	int ret;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_PushReal( C, list->value );
		ret = sgs_StoreGlobal( C, list->name );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}


SGSBOOL sgs_IncludeExt( SGS_CTX, const char* name, const char* searchpath )
{
	int ret = 0, sz;
	int pathrep = 0;
	sgs_Variable incfn;
	if( searchpath )
	{
		pathrep = sgs_PushGlobal( C, "SGS_PATH" ) == SGS_SUCCESS ? 1 : 2;
		sgs_PushString( C, searchpath );
		sgs_StoreGlobal( C, "SGS_PATH" );
	}
	
	sz = sgs_StackSize( C );
	sgs_PushString( C, name );
	sgs_InitCFunction( &incfn, sgsstd_include );
	ret = sgs_CallP( C, &incfn, 1, 1 );
	if( ret == SGS_SUCCESS )
		ret = sgs_GetBool( C, -1 );
	else
		ret = 0;
	sgs_SetStackSize( C, sz );
	
	if( pathrep == 1 )
		sgs_StoreGlobal( C, "SGS_PATH" );
	else if( pathrep == 2 )
	{
		sgs_PushGlobal( C, "_G" );
		sgs_PushString( C, "SGS_PATH" );
		sgs_ObjectAction( C, -2, SGS_ACT_DICT_UNSET, -1 );
	}
	
	sgs_SetStackSize( C, 0 );
	return ret;
}

SGSMIXED sgs_ObjectAction( SGS_CTX, int item, int act, int arg )
{
	sgs_SizeVal i, j;
	switch( act )
	{
	case SGS_ACT_ARRAY_PUSH:
		i = sgs_ArraySize( C, item );
		j = sgs_StackSize( C );
		if( i < 0 || arg > j || arg < 0 )
			return SGS_EINVAL;
		if( arg )
		{
			sgsstd_array_insert( C, (sgsstd_array_header_t*)
				sgs_GetObjectData( C, item ), i, j - arg );
			sgs_Pop( C, arg );
		}
		return SGS_SUCCESS;
		
	case SGS_ACT_ARRAY_POP:
	case SGS_ACT_ARRAY_POPRET:
		i = sgs_ArraySize( C, item );
		if( i < 0 || arg > i || arg < 0 )
			return SGS_EINVAL;
		if( arg )
		{
			if( act == SGS_ACT_ARRAY_POPRET )
			{
				for( j = i - arg; j < i; ++j )
					sgs_PushNumIndex( C, item, j );
			}
			sgsstd_array_erase( C, (sgsstd_array_header_t*)
				sgs_GetObjectData( C, item ), i - arg, i - 1 );
		}
		return SGS_SUCCESS;
	
	case SGS_ACT_ARRAY_FIND:
		if( sgs_ArraySize( C, item ) < 0 )
			return SGS_EINVAL;
		else
		{
			sgs_SizeVal off = 0;
			sgsstd_array_header_t* hdr = (sgsstd_array_header_t*)
				sgs_GetObjectData( C, item );
			
			sgs_Variable comp;
			if( !sgs_GetStackItem( C, arg, &comp ) )
				return SGS_EBOUNDS;
			
			while( off < hdr->size )
			{
				sgs_Variable* p = SGSARR_PTR( hdr ) + off;
				if( sgs_EqualTypes( C, p, &comp )
					&& sgs_CompareF( C, p, &comp ) == 0 )
				{
					return off;
				}
				off++;
			}
		}
		return SGS_ENOTFND;
		
	case SGS_ACT_ARRAY_RM_ONE:
	case SGS_ACT_ARRAY_RM_ALL:
		if( sgs_ArraySize( C, item ) < 0 )
			return SGS_EINVAL;
		else
		{
			sgs_SizeVal off = 0, rmvd = 0;
			sgsstd_array_header_t* hdr = (sgsstd_array_header_t*)
				sgs_GetObjectData( C, item );
			
			sgs_Variable comp;
			if( !sgs_GetStackItem( C, arg, &comp ) )
				return SGS_EBOUNDS;
			
			while( off < hdr->size )
			{
				sgs_Variable* p = SGSARR_PTR( hdr ) + off;
				if( sgs_EqualTypes( C, p, &comp )
					&& sgs_CompareF( C, p, &comp ) == 0 )
				{
					sgsstd_array_erase( C, hdr, off, off );
					rmvd++;
					if( act == SGS_ACT_ARRAY_RM_ONE )
						break;
				}
				off++;
			}
			return rmvd;
		}
		return 0;
	
	case SGS_ACT_DICT_UNSET:
		if( !sgs_IsObject( C, item, sgsstd_dict_iface ) ||
			sgs_ItemType( C, arg ) != SVT_STRING )
			return SGS_EINVAL;
		else
		{
			sgs_Variable str;
			sgs_GetStackItem( C, arg, &str );
			vht_unset(
				&((DictHdr*)sgs_GetObjectData( C, item ))->ht, C,
				&str );
			return SGS_SUCCESS;
		}
	
	case SGS_ACT_MAP_UNSET:
		if( !sgs_IsObject( C, item, sgsstd_map_iface ) ||
			!sgs_IsValidIndex( C, arg ) )
			return SGS_EINVAL;
		else
		{
			sgs_Variable key;
			sgs_GetStackItem( C, arg, &key );
			vht_unset(
				&((DictHdr*)sgs_GetObjectData( C, item ))->ht, C,
				&key );
			return SGS_SUCCESS;
		}
		
	}
	return SGS_ENOTFND;
}

