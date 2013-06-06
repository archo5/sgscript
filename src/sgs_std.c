

#include <stdio.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"

#define STDLIB_WARN( warn ) return sgs_Printf( C, SGS_WARNING, warn );


/* Containers */


/*
	ARRAY
*/

typedef struct sgsstd_array_header_s
{
	uint32_t size;
	uint32_t mem;
}
sgsstd_array_header_t;

static void* sgsstd_array_functable[];

#define SGSARR_UNIT sizeof( sgs_Variable )
#define SGSARR_HDRBASE sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data
#define SGSARR_HDR sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data->data
#define SGSARR_HDRUPDATE hdr = (sgsstd_array_header_t*) data->data
#define SGSARR_HDRSIZE sizeof(sgsstd_array_header_t)
#define SGSARR_ALLOCSIZE( cnt ) ((cnt)*sizeof(sgs_Variable)+SGSARR_HDRSIZE)
#define SGSARR_PTR( base ) ((sgs_Variable*)(((char*)base)+SGSARR_HDRSIZE))

static void sgsstd_array_reserve( SGS_CTX, sgs_VarObj* data, uint32_t size )
{
	SGSARR_HDR;
	if( size <= hdr->mem )
		return;

	data->data = sgs_Realloc( C, data->data, SGSARR_ALLOCSIZE( size ) );
	SGSARR_HDRUPDATE;
	hdr->mem = size;
}

static void sgsstd_array_clear( SGS_CTX, sgs_VarObj* data, int dch )
{
	sgs_Variable *var, *vend;
	SGSARR_HDR;
	var = SGSARR_PTR( data->data );
	vend = var + hdr->size;
	while( var < vend )
	{
		sgs_ReleaseOwned( C, var, dch );
		var++;
	}
	hdr->size = 0;
}

/* off = offset in stack to start inserting from (1 = the first argument in methods) */
static void sgsstd_array_insert( SGS_CTX, sgs_VarObj* data, uint32_t pos, int off )
{
	int i;
	uint32_t cnt = sgs_StackSize( C ) - off;
	SGSARR_HDR;
	uint32_t nsz = hdr->size + cnt;
	sgs_Variable* ptr = SGSARR_PTR( data->data );

	if( !cnt ) return;

	if( nsz > hdr->mem )
	{
		sgsstd_array_reserve( C, data, MAX( nsz, hdr->mem * 2 ) );
		SGSARR_HDRUPDATE;
		ptr = SGSARR_PTR( data->data );
	}
	if( pos < hdr->size )
		memmove( ptr + pos + cnt, ptr + pos, ( hdr->size - pos ) * SGSARR_UNIT );
	for( i = off; i < sgs_StackSize( C ); ++i )
	{
		sgs_Variable* var = ptr + pos + i - off;
		sgs_GetStackItem( C, i, var );
		sgs_Acquire( C, var );
	}
	hdr->size = nsz;
}

static void sgsstd_array_erase( SGS_CTX, sgs_VarObj* data, uint32_t from, uint32_t to )
{
	uint32_t i;
	uint32_t cnt = to - from + 1, to1 = to + 1;
	SGSARR_HDR;
	sgs_Variable* ptr = SGSARR_PTR( data->data );

	sgs_BreakIf( from >= hdr->size || to >= hdr->size || from > to );

	for( i = from; i <= to; ++i )
		sgs_Release( C, ptr + i );
	if( to1 < hdr->size )
		memmove( ptr + from, ptr + to1, ( hdr->size - to1 ) * SGSARR_UNIT );
	hdr->size -= cnt;
}

#define SGSARR_IHDR( name ) \
	sgs_VarObj* data; \
	sgsstd_array_header_t* hdr; \
	SGSFN( "array." #name ); \
	if( !sgs_Method( C ) || !sgs_IsObject( C, 0, sgsstd_array_functable ) ) \
		STDLIB_WARN( "not called on an array" ) \
	data = sgs_GetObjectData( C, 0 ); \
	hdr = (sgsstd_array_header_t*) data->data; \
	UNUSED( hdr );
/* after this, the counting starts from 1 because of sgs_Method */


static int sgsstd_arrayI_push( SGS_CTX )
{
	SGSARR_IHDR( push );
	sgsstd_array_insert( C, data, hdr->size, 1 );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}
static int sgsstd_arrayI_pop( SGS_CTX )
{
	sgs_Variable* ptr;
	SGSARR_IHDR( pop );
	ptr = SGSARR_PTR( data->data );
	if( !hdr->size )
		STDLIB_WARN( "array is empty, cannot pop" );

	sgs_PushVariable( C, ptr + hdr->size - 1 );
	sgsstd_array_erase( C, data, hdr->size - 1, hdr->size - 1 );
	return 1;
}
static int sgsstd_arrayI_shift( SGS_CTX )
{
	sgs_Variable* ptr;
	SGSARR_IHDR( shift );
	ptr = SGSARR_PTR( data->data );
	if( !hdr->size )
		STDLIB_WARN( "array is empty, cannot shift" );

	sgs_PushVariable( C, ptr );
	sgsstd_array_erase( C, data, 0, 0 );
	return 1;
}
static int sgsstd_arrayI_unshift( SGS_CTX )
{
	SGSARR_IHDR( unshift );
	sgsstd_array_insert( C, data, 0, 1 );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}

static int sgsstd_arrayI_insert( SGS_CTX )
{
	sgs_Integer at;
	SGSARR_IHDR( insert );
	if( sgs_StackSize( C ) < 3 || !sgs_ParseInt( C, 1, &at ) )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 2+ arguments: int, any+" )

	if( at < 0 )
		at += hdr->size + 1;
	if( at < 0 || at > hdr->size )
		STDLIB_WARN( "index out of bounds" )

	sgsstd_array_insert( C, data, at, 2 );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}
static int sgsstd_arrayI_erase( SGS_CTX )
{
	int cnt = sgs_StackSize( C );
	sgs_Integer at, at2;
	SGSARR_IHDR( erase );
	if( cnt < 1 || cnt > 2 || !sgs_ParseInt( C, 1, &at ) ||
		( cnt == 2 && !sgs_ParseInt( C, 2, &at2 ) ) )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 1-2 arguments: int[, int]" )

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

	sgsstd_array_erase( C, data, at, at2 );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}

static int sgsstd_arrayI_clear( SGS_CTX )
{
	SGSARR_IHDR( clear );
	sgsstd_array_clear( C, data, 1 );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}

static int sgsstd_arrayI_reverse( SGS_CTX )
{
	SGSARR_IHDR( reverse );

	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "unexpected arguments; function expects 0 arguments" )
	
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
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
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
	sgs_Integer sz;
	int cnt = sgs_StackSize( C );
	SGSARR_IHDR( resize );

	if( cnt != 1 || !sgs_ParseInt( C, 1, &sz ) || sz < 0 )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 1 argument: int (>= 0)" )

	sgsstd_array_reserve( C, data, sz );
	SGSARR_HDRUPDATE;
	sgsstd_array_adjust( C, hdr, sz );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}
static int sgsstd_arrayI_reserve( SGS_CTX )
{
	sgs_Integer sz;
	int cnt = sgs_StackSize( C );
	SGSARR_IHDR( reserve );

	if( cnt != 1 || !sgs_ParseInt( C, 1, &sz ) || sz < 0 )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 1 argument: int (>= 0)" )

	sgsstd_array_reserve( C, data, sz );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}

static SGS_INLINE int sgsarrcomp_basic( const void* p1, const void* p2, void* userdata )
{
	SGS_CTX = (sgs_Context*) userdata;
	sgs_Variable *v1 = (sgs_Variable*) p1;
	sgs_Variable *v2 = (sgs_Variable*) p2;
	return sgs_Compare( C, v1, v2 );
}
static SGS_INLINE int sgsarrcomp_basic_rev( const void* p1, const void* p2, void* userdata )
{ return sgsarrcomp_basic( p2, p1, userdata ); }
static int sgsstd_arrayI_sort( SGS_CTX )
{
	int rev = 0, cnt = sgs_StackSize( C );
	SGSARR_IHDR( sort );

	if( cnt > 1 ||
		( cnt == 1 && !sgs_ParseBool( C, 1, &rev ) ) )
		STDLIB_WARN( "unexpected arguments; function expects 0-1 arguments: [bool]" )

	{
		quicksort( SGSARR_PTR( data->data ), hdr->size, sizeof( sgs_Variable ),
			rev ? sgsarrcomp_basic_rev : sgsarrcomp_basic, C );
		if( cnt )
			sgs_Pop( C, 1 );
		return 1;
	}
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
	sgs_Variable *v1 = (sgs_Variable*) p1;
	sgs_Variable *v2 = (sgs_Variable*) p2;
	SGS_CTX = u->C;
	sgs_PushVariable( C, v1 );
	sgs_PushVariable( C, v2 );
	sgs_PushVariable( C, &u->sortfunc );
	if( sgs_Call( C, 2, 1 ) != SGS_SUCCESS )
		return 0;
	else
	{
		int ret = sgs_GetInt( C, -1 );
		sgs_Pop( C, 1 );
		return ret;
	}
}
static SGS_INLINE int sgsarrcomp_custom_rev( const void* p1, const void* p2, void* userdata )
{ return sgsarrcomp_custom( p2, p1, userdata ); }
static int sgsstd_arrayI_sort_custom( SGS_CTX )
{
	int rev = 0, cnt = sgs_StackSize( C );
	SGSARR_IHDR( sort_custom );

	if( cnt < 1 || cnt > 2 ||
		( cnt == 2 && !sgs_ParseBool( C, 2, &rev ) ) )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 1-2 arguments: func[, bool]" )

	{
		sgsarrcomp_cl2 u = { C };
		sgs_GetStackItem( C, 1, &u.sortfunc );
		quicksort( SGSARR_PTR( data->data ), hdr->size,
			sizeof( sgs_Variable ), rev ? sgsarrcomp_custom_rev : sgsarrcomp_custom, &u );
		sgs_Pop( C, cnt );
		return 1;
	}
}

typedef struct sgsarr_smi_s
{
	sgs_Real value;
	sgs_SizeVal pos;
}
sgsarr_smi;
static SGS_INLINE int sgsarrcomp_smi( const void* p1, const void* p2, void* userdata )
{
	sgsarr_smi *v1 = (sgsarr_smi*) p1;
	sgsarr_smi *v2 = (sgsarr_smi*) p2;
	if( v1->value < v2->value )
		return -1;
	return v1->value > v2->value ? 1 : 0;
}
static SGS_INLINE int sgsarrcomp_smi_rev( const void* p1, const void* p2, void* userdata )
{ return sgsarrcomp_smi( p2, p1, userdata ); }
static int sgsstd_arrayI_sort_mapped( SGS_CTX )
{
	sgs_SizeVal i, asize = 0;
	sgs_Variable a2;
	int rev = 0, cnt = sgs_StackSize( C );
	SGSARR_IHDR( sort_mapped );

	if( cnt < 1 || cnt > 2 ||
		!sgs_GetStackItem( C, 1, &a2 ) ||
		!sgs_IsArray( C, &a2 ) ||
		( asize = sgs_ArraySize( C, &a2 ) ) < 0 ||
		( cnt == 2 && !sgs_ParseBool( C, 2, &rev ) ) )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 1-2 arguments: array[, bool]" )

	if( asize != hdr->size )
		STDLIB_WARN( "array sizes must match" )

	{
		sgsarr_smi* smis = sgs_Alloc_n( sgsarr_smi, asize );
		for( i = 0; i < asize; ++i )
		{
			sgs_Variable var;
			if( !sgs_ArrayGet( C, &a2, i, &var ) )
			{
				sgs_Dealloc( smis );
				STDLIB_WARN( "error in mapping array" )
			}
			sgs_PushVariable( C, &var );
			smis[ i ].value = sgs_GetReal( C, -1 );
			smis[ i ].pos = i;
			sgs_Pop( C, 1 );
		}
		quicksort( smis, asize, sizeof( sgsarr_smi ),
			rev ? sgsarrcomp_smi_rev : sgsarrcomp_smi, NULL );

		{
			sgs_Variable *p1, *p2;
			sgsstd_array_header_t* nd = (sgsstd_array_header_t*)
				sgs_Malloc( C, SGSARR_ALLOCSIZE( hdr->mem ) );
			memcpy( nd, hdr, SGSARR_ALLOCSIZE( hdr->mem ) );
			p1 = SGSARR_PTR( hdr );
			p2 = SGSARR_PTR( nd );
			for( i = 0; i < asize; ++i )
				p1[ i ] = p2[ smis[ i ].pos ];
			sgs_Dealloc( nd );
		}

		sgs_Dealloc( smis );

		sgs_Pop( C, cnt );
		return 1;
	}
}

static int sgsstd_arrayI_find( SGS_CTX )
{
	int strict = FALSE;
	int cnt = sgs_StackSize( C );
	sgs_Integer from = 0;
	SGSARR_IHDR( find );
	if( cnt < 1 || cnt > 3 ||
		( cnt >= 2 && !sgs_ParseBool( C, 2, &strict ) ) ||
		( cnt >= 3 && !sgs_ParseInt( C, 3, &from ) ) )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 1-3 arguments: any[, bool[, int]]" )

	{
		sgs_Variable comp;
		sgs_GetStackItem( C, 1, &comp );
		sgs_SizeVal off = from;
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
}

static int sgsstd_arrayI_remove( SGS_CTX )
{
	int strict = FALSE, all = FALSE;
	int cnt = sgs_StackSize( C );
	sgs_Integer from = 0;
	SGSARR_IHDR( remove );
	if( cnt < 1 || cnt > 4 ||
		( cnt >= 2 && !sgs_ParseBool( C, 2, &strict ) ) ||
		( cnt >= 3 && !sgs_ParseBool( C, 3, &all ) ) ||
		( cnt >= 4 && !sgs_ParseInt( C, 4, &from ) ) )
		STDLIB_WARN( "unexpected arguments;"
			" function expects 1-4 arguments: any[, bool[, bool[, int]]]" )

	{
		int rmvd = 0;
		sgs_Variable comp;
		sgs_GetStackItem( C, 1, &comp );
		sgs_SizeVal off = from;
		while( off < hdr->size )
		{
			sgs_Variable* p = SGSARR_PTR( hdr ) + off;
			if( ( !strict || sgs_EqualTypes( C, p, &comp ) )
				&& sgs_CompareF( C, p, &comp ) == 0 )
			{
				sgsstd_array_erase( C, data, off, off );
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
}

static int sgsstd_array_getprop( SGS_CTX, sgs_VarObj* data )
{
	const char* name = sgs_ToString( C, -1 );
	sgs_CFunc func;
	if( 0 == strcmp( name, "size" ) )
	{
		SGSARR_HDR;
		sgs_PushInt( C, hdr->size );
		return SGS_SUCCESS;
	}
	else if( 0 == strcmp( name, "capacity" ) )
	{
		SGSARR_HDR;
		sgs_PushInt( C, hdr->mem );
		return SGS_SUCCESS;
	}
	else if( 0 == strcmp( name, "push" ) )      func = sgsstd_arrayI_push;
	else if( 0 == strcmp( name, "pop" ) )       func = sgsstd_arrayI_pop;
	else if( 0 == strcmp( name, "shift" ) )     func = sgsstd_arrayI_shift;
	else if( 0 == strcmp( name, "unshift" ) )   func = sgsstd_arrayI_unshift;
	else if( 0 == strcmp( name, "insert" ) )    func = sgsstd_arrayI_insert;
	else if( 0 == strcmp( name, "erase" ) )     func = sgsstd_arrayI_erase;
	else if( 0 == strcmp( name, "clear" ) )     func = sgsstd_arrayI_clear;
	else if( 0 == strcmp( name, "reverse" ) )   func = sgsstd_arrayI_reverse;
	else if( 0 == strcmp( name, "resize" ) )    func = sgsstd_arrayI_resize;
	else if( 0 == strcmp( name, "reserve" ) )   func = sgsstd_arrayI_reserve;
	else if( 0 == strcmp( name, "sort" ) )      func = sgsstd_arrayI_sort;
	else if( 0 == strcmp( name, "sort_custom" ) ) func = sgsstd_arrayI_sort_custom;
	else if( 0 == strcmp( name, "sort_mapped" ) ) func = sgsstd_arrayI_sort_mapped;
	else if( 0 == strcmp( name, "find" ) )      func = sgsstd_arrayI_find;
	else if( 0 == strcmp( name, "remove" ) )    func = sgsstd_arrayI_remove;
	else return SGS_ENOTFND;

	sgs_PushCFunction( C, func );
	return SGS_SUCCESS;
}

static int sgsstd_array_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	if( prop )
		return sgsstd_array_getprop( C, data );
	else
	{
		SGSARR_HDR;
		sgs_Variable* ptr = SGSARR_PTR( data->data );
		sgs_Integer i = sgs_ToInt( C, -1 );
		if( i < 0 || i >= hdr->size )
		{
			sgs_Printf( C, SGS_WARNING, "array index out of bounds" );
			return SGS_EBOUNDS;
		}
		sgs_PushVariable( C, ptr + i );
		return SGS_SUCCESS;
	}
}

static int sgsstd_array_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	if( prop )
		return SGS_ENOTSUP;
	else
	{
		SGSARR_HDR;
		sgs_Variable* ptr = SGSARR_PTR( data->data );
		sgs_Integer i = sgs_ToInt( C, -2 );
		if( i < 0 || i >= hdr->size )
		{
			sgs_Printf( C, SGS_WARNING, "array index out of bounds" );
			return SGS_EBOUNDS;
		}
		sgs_Release( C, ptr + i );
		sgs_GetStackItem( C, -1, ptr + i );
		sgs_Acquire( C, ptr + i );
		return SGS_SUCCESS;
	}
}

static int sgsstd_array_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	char bfr[ 32 ];
	int i;
	SGSARR_HDR;
	sprintf( bfr, "array (%d)\n[", hdr->size );
	sgs_PushString( C, bfr );
	if( depth )
	{
		if( hdr->size )
		{
			for( i = 0; i < hdr->size; ++i )
			{
				sgs_PushString( C, "\n" );
				sgs_PushVariable( C, SGSARR_PTR( data->data ) + i );
				if( sgs_DumpVar( C, depth ) )
					return SGS_EINPROC;
			}
			if( sgs_StringMultiConcat( C, hdr->size * 2 ) || sgs_PadString( C ) )
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
	return sgs_StringMultiConcat( C, 2 + !!hdr->size );
}

static int sgsstd_array_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
{
	SGSARR_HDR;
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

typedef struct sgsstd_array_iter_s
{
	sgs_Variable ref;
	uint32_t size;
	uint32_t off;
}
sgsstd_array_iter_t;

static int sgsstd_array_iter_destruct( SGS_CTX, sgs_VarObj* data, int dch )
{
	sgs_ReleaseOwned( C, &((sgsstd_array_iter_t*) data->data)->ref, dch );
	sgs_Dealloc( data->data );
	return SGS_SUCCESS;
}

static int sgsstd_array_iter_getnext( SGS_CTX, sgs_VarObj* data, int mask )
{
	sgsstd_array_iter_t* iter = (sgsstd_array_iter_t*) data->data;
	sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) iter->ref.data.O->data;
	if( iter->size != hdr->size )
		return SGS_EINVAL;

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

static void* sgsstd_array_iter_functable[] =
{
	SOP_DESTRUCT, sgsstd_array_iter_destruct,
	SOP_GETNEXT, sgsstd_array_iter_getnext,
	SOP_END,
};

static int sgsstd_array_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	SGSARR_HDR;
	if( type == SGS_CONVOP_TOITER )
	{
		sgsstd_array_iter_t* iter = sgs_Alloc( sgsstd_array_iter_t );

		iter->ref.type = SVT_OBJECT;
		iter->ref.data.O = data;
		iter->size = hdr->size;
		iter->off = -1;

		sgs_Acquire( C, &iter->ref );
		sgs_PushObject( C, iter, sgsstd_array_iter_functable );
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
		return sgs_StringMultiConcat( C, hdr->size * 2 + 1 + !hdr->size );
	}
	else if( type == SGS_CONVOP_CLONE )
	{
		void* nd = sgs_Malloc( C, SGSARR_ALLOCSIZE( hdr->mem ) );
		memcpy( nd, hdr, SGSARR_ALLOCSIZE( hdr->mem ) );
		{
			sgs_Variable* ptr = SGSARR_PTR( hdr );
			sgs_Variable* pend = ptr + hdr->size;
			while( ptr < pend )
				sgs_Acquire( C, ptr++ );
		}
		sgs_PushObject( C, nd, sgsstd_array_functable );
		return SGS_SUCCESS;
	}
	else if( type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "array" );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_array_serialize( SGS_CTX, sgs_VarObj* data, int unused )
{
	int ret;
	sgs_Variable* pos, *posend;
	SGSARR_HDR;
	UNUSED( unused );
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

static int sgsstd_array_destruct( SGS_CTX, sgs_VarObj* data, int dch )
{
	sgsstd_array_clear( C, data, dch );
	sgs_Dealloc( data->data );
	return 0;
}

static void* sgsstd_array_functable[] =
{
	SOP_DESTRUCT, sgsstd_array_destruct,
	SOP_GETINDEX, sgsstd_array_getindex,
	SOP_SETINDEX, sgsstd_array_setindex,
	SOP_DUMP, sgsstd_array_dump,
	SOP_GCMARK, sgsstd_array_gcmark,
	SOP_CONVERT, sgsstd_array_convert,
	SOP_SERIALIZE, sgsstd_array_serialize,
	SOP_FLAGS, SGS_OP( SGS_OBJ_ARRAY ),
	SOP_END,
};

static int sgsstd_array( SGS_CTX )
{
	int i = 0, objcnt = sgs_StackSize( C );
	void* data = sgs_Malloc( C, SGSARR_ALLOCSIZE( objcnt ) );
	sgs_Variable *p, *pend;
	SGSARR_HDRBASE;
	hdr->size = objcnt;
	hdr->mem = objcnt;
	p = SGSARR_PTR( data );
	pend = p + objcnt;
	while( p < pend )
	{
		sgs_GetStackItem( C, i++, p );
		sgs_Acquire( C, p++ );
	}
	sgs_PushObject( C, data, sgsstd_array_functable );
	return 1;
}


/*
	DICT
*/

#ifdef SGS_DICT_CACHE_SIZE
#define DICT_CACHE_SIZE SGS_DICT_CACHE_SIZE
#endif

typedef
struct _DictHdr
{
	VHTable ht;
#ifdef DICT_CACHE_SIZE
	void* cachekeys[ DICT_CACHE_SIZE ];
	int32_t cachevars[ DICT_CACHE_SIZE ];
#endif
}
DictHdr;

static DictHdr* mkdict( SGS_CTX )
{
#ifdef DICT_CACHE_SIZE
	int i;
#endif
	DictHdr* dh = sgs_Alloc( DictHdr );
	vht_init( &dh->ht, C );
#ifdef DICT_CACHE_SIZE
	for( i = 0; i < DICT_CACHE_SIZE; ++i )
	{
		dh->cachekeys[ i ] = NULL;
		dh->cachevars[ i ] = 0;
	}
#endif
	return dh;
}


#define HTHDR DictHdr* dh = (DictHdr*) data->data; VHTable* ht = &dh->ht;
static void* sgsstd_dict_functable[];

static int sgsstd_dict_destruct( SGS_CTX, sgs_VarObj* data, int dco )
{
	HTHDR;
	vht_free( ht, C, dco );
	sgs_Dealloc( dh );
	return SGS_SUCCESS;
}

static int sgsstd_dict_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	char bfr[ 32 ];
	HTHDR;
	VHTableVar *pair = ht->vars, *pend = ht->vars + vht_size( ht );
	sprintf( bfr, "dict (%d)\n{", vht_size( ht ) );
	sgs_PushString( C, bfr );
	if( depth )
	{
		if( vht_size( ht ) )
		{
			while( pair < pend )
			{
				sgs_PushString( C, "\n" );
				sgs_PushStringBuf( C, pair->str, pair->size );
				sgs_PushString( C, " = " );
				sgs_PushVariable( C, &pair->var );
				if( sgs_DumpVar( C, depth ) )
					return SGS_EINPROC;
				pair++;
			}
			if( sgs_StringMultiConcat( C, ( pend - ht->vars ) * 4 ) || sgs_PadString( C ) )
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
	return sgs_StringMultiConcat( C, 2 + !!vht_size( ht ) );
}

static int sgsstd_dict_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
{
	HTHDR;
	VHTableVar *pair = ht->vars, *pend = ht->vars + vht_size( ht );
	while( pair < pend )
	{
		int ret = sgs_GCMark( C, &pair->var );
		if( ret != SGS_SUCCESS )
			return ret;
		pair++;
	}
	return SGS_SUCCESS;
}

static int sgsstd_dict_getindex_exact( SGS_CTX, sgs_VarObj* data )
{
	VHTableVar* pair = NULL;
	HTHDR;

	sgs_ToString( C, -1 );
	if( sgs_ItemType( C, -1 ) != SVT_STRING )
		return SGS_EINVAL;

	pair = vht_get( ht, sgs_GetStringPtr( C, -1 ), sgs_GetStringSize( C, -1 ) );
	if( !pair )
		return SGS_ENOTFND;

	sgs_PushVariable( C, &pair->var );
	return SGS_SUCCESS;
}

static int sgsstd_dict_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	VHTableVar* pair = NULL;
	HTHDR;

	if( !prop )
		return sgsstd_dict_getindex_exact( C, data );

	if( sgs_ItemType( C, -1 ) == SVT_INT )
	{
		int32_t off = (int32_t) (C->stack_top-1)->data.I;
		if( off < 0 || off > vht_size( ht ) )
			return SGS_EBOUNDS;
		sgs_PushVariable( C, &ht->vars[ off ].var );
		return SGS_SUCCESS;
	}

#ifdef DICT_CACHE_SIZE
	int i, cacheable = sgs_ItemType( C, -1 ) == SVT_STRING;
	string_t* key = (C->stack_top-1)->data.S;
	if( cacheable )
		cacheable = key->isconst;
#endif

	sgs_ToString( C, -1 );
	if( sgs_ItemType( C, -1 ) != SVT_STRING )
		return SGS_EINVAL;

#ifdef DICT_CACHE_SIZE
	key = (C->stack_top-1)->data.S;
	for( i = 0; i < DICT_CACHE_SIZE; ++i )
	{
		if( dh->cachekeys[ i ] == key )
		{
			pair = ht->vars + dh->cachevars[ i ];
			/* for the extremely rare case when some constant is reallocated
			in the space of a previous one and the dict has it cached,
			we have to validate all hits */
			if( pair->size != key->size ||
				0 != strncmp( pair->str, str_cstr( key ), key->size ) )
				pair = NULL;
			else
				cacheable = 0;
			break;
		}
	}

	if( !pair )
	{
		pair = vht_getS( ht, key );
		if( !pair )
			return SGS_ENOTFND;
	}

	if( cacheable )
	{
		for( i = DICT_CACHE_SIZE - 1; i > 0; --i )
		{
			dh->cachekeys[ i ] = dh->cachekeys[ i - 1 ];
			dh->cachevars[ i ] = dh->cachevars[ i - 1 ];
		}
		dh->cachekeys[ 0 ] = key;
		dh->cachevars[ 0 ] = pair - ht->vars;
	}
#else
	/*
		the old method
	*/
	pair = vht_getS( ht, (C->stack_top-1)->data.S );
	if( !pair )
		return SGS_ENOTFND;
#endif

	sgs_PushVariable( C, &pair->var );
	return SGS_SUCCESS;
}

static int sgsstd_dict_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	char* str;
	sgs_SizeVal size;
	sgs_Variable val;
	HTHDR;
	str = sgs_ToStringBuf( C, -2, &size );
	sgs_GetStackItem( C, -1, &val );
	if( !str )
		return SGS_EINVAL;
	vht_set( ht, str, size, &val, C );
	return SGS_SUCCESS;
}

/* iterator */

typedef struct sgsstd_dict_iter_s
{
	sgs_Variable ref;
	int32_t size;
	int32_t off;
}
sgsstd_dict_iter_t;

static int sgsstd_dict_iter_destruct( SGS_CTX, sgs_VarObj* data, int dch )
{
	sgs_ReleaseOwned( C, &((sgsstd_dict_iter_t*) data->data)->ref, dch );
	sgs_Dealloc( data->data );
	return SGS_SUCCESS;
}

static int sgsstd_dict_iter_getnext( SGS_CTX, sgs_VarObj* data, int flags )
{
	sgsstd_dict_iter_t* iter = (sgsstd_dict_iter_t*) data->data;
	VHTable* ht = (VHTable*) iter->ref.data.O->data;
	if( iter->size != ht->ht.load )
		return SGS_EINVAL;

	if( !flags )
	{
		iter->off++;
		return iter->off < iter->size;
	}
	else
	{
		if( flags & SGS_GETNEXT_KEY )
			sgs_PushStringBuf( C, ht->vars[ iter->off ].str, ht->vars[ iter->off ].size );
		if( flags & SGS_GETNEXT_VALUE )
			sgs_PushVariable( C, &ht->vars[ iter->off ].var );
		return SGS_SUCCESS;
	}
}

static int sgsstd_dict_iter_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
{
	sgsstd_dict_iter_t* iter = (sgsstd_dict_iter_t*) data->data;
	sgs_GCMark( C, &iter->ref );
	return SGS_SUCCESS;
}

static void* sgsstd_dict_iter_functable[] =
{
	SOP_DESTRUCT, sgsstd_dict_iter_destruct,
	SOP_GETNEXT, sgsstd_dict_iter_getnext,
	SOP_GCMARK, sgsstd_dict_iter_gcmark,
	SOP_END,
};


static int sgsstd_dict_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	HTHDR;
	if( type == SGS_CONVOP_TOITER )
	{
		sgsstd_dict_iter_t* iter = sgs_Alloc( sgsstd_dict_iter_t );

		iter->ref.type = SVT_OBJECT;
		iter->ref.data.O = data;
		iter->size = ht->ht.load;
		iter->off = -1;
		sgs_Acquire( C, &iter->ref );

		sgs_PushObject( C, iter, sgsstd_dict_iter_functable );
		return SGS_SUCCESS;
	}
	else if( type == SVT_BOOL )
	{
		sgs_PushBool( C, vht_size( ht ) != 0 );
		return SGS_SUCCESS;
	}
	else if( type == SVT_STRING )
	{
		VHTableVar *pair = ht->vars, *pend = ht->vars + vht_size( ht );
		int cnt = 0;
		sgs_PushString( C, "{" );
		while( pair < pend )
		{
			if( cnt )
				sgs_PushString( C, "," );
			sgs_PushStringBuf( C, pair->str, pair->size );
			sgs_PushString( C, "=" );
			sgs_PushVariable( C, &pair->var );
			sgs_ToStringFast( C, -1 );
			cnt++;
			pair++;
		}
		sgs_PushString( C, "}" );
		return sgs_StringMultiConcat( C, cnt * 4 + 1 + !cnt );
	}
	else if( type == SGS_CONVOP_CLONE )
	{
		int i, htsize = vht_size( ht );
		DictHdr* ndh = mkdict( C );
		for( i = 0; i < htsize; ++i )
		{
			vht_set( &ndh->ht, ht->vars[ i ].str, ht->vars[ i ].size, &ht->vars[ i ].var, C );
		}
		sgs_PushObject( C, ndh, sgsstd_dict_functable );
		return SGS_SUCCESS;
	}
	else if( type == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "dict" );
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_dict_serialize( SGS_CTX, sgs_VarObj* data, int unused )
{
	int ret;
	HTHDR;
	VHTableVar *pair = ht->vars, *pend = ht->vars + vht_size( ht );
	UNUSED( unused );
	while( pair < pend )
	{
		sgs_PushStringBuf( C, pair->str, pair->size );
		ret = sgs_Serialize( C );
		if( ret != SGS_SUCCESS )
			return ret;
		sgs_PushVariable( C, &pair->var );
		ret = sgs_Serialize( C );
		if( ret != SGS_SUCCESS )
			return ret;
		pair++;
	}
	return sgs_SerializeObject( C, vht_size( ht ) * 2, "dict" );
}

static void* sgsstd_dict_functable[] =
{
	SOP_DESTRUCT, sgsstd_dict_destruct,
	SOP_CONVERT, sgsstd_dict_convert,
	SOP_GETINDEX, sgsstd_dict_getindex,
	SOP_SETINDEX, sgsstd_dict_setindex,
	SOP_DUMP, sgsstd_dict_dump,
	SOP_SERIALIZE, sgsstd_dict_serialize,
	SOP_GCMARK, sgsstd_dict_gcmark,
	SOP_END,
};

static int sgsstd_dict( SGS_CTX )
{
	DictHdr* dh;
	VHTable* ht;
	int i, objcnt = sgs_StackSize( C );
	
	SGSFN( "dict" );

	if( objcnt % 2 != 0 )
		STDLIB_WARN( "unexpected argument count,"
			" function expects 0 or an even number of arguments" )

	dh = mkdict( C );
	ht = &dh->ht;

	for( i = 0; i < objcnt; i += 2 )
	{
		char* kstr;
		sgs_SizeVal ksize;
		sgs_Variable val;
		if( !sgs_ParseString( C, i, &kstr, &ksize ) )
		{
			vht_free( ht, C, 1 );
			sgs_Dealloc( ht );
			return sgs_Printf( C, SGS_WARNING, 
				"key argument %d is not a string", i );
		}

		sgs_GetStackItem( C, i + 1, &val );
		vht_set( ht, kstr, (int32_t) ksize, &val, C );
	}

	sgs_PushObject( C, ht, sgsstd_dict_functable );
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

static int sgsstd_class_destruct( SGS_CTX, sgs_VarObj* data, int dch )
{
	SGSCLASS_HDR;
	sgs_ReleaseOwned( C, &hdr->data, dch );
	sgs_ReleaseOwned( C, &hdr->inh, dch );
	sgs_Dealloc( hdr );
	return SGS_SUCCESS;
}

static int sgsstd_class_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	int ret;
	sgs_Variable var, idx;
	SGSCLASS_HDR;
	if( prop && strcmp( sgs_ToString( C, -1 ), "_super" ) == 0 )
	{
		sgs_PushVariable( C, &hdr->inh );
		return SGS_SUCCESS;
	}
	sgs_GetStackItem( C, -1, &idx );
	sgs_Acquire( C, &idx );

	if( sgs_GetIndex( C, &var, &hdr->data, &idx ) == SGS_SUCCESS )
		goto success;

	ret = sgs_GetIndex( C, &var, &hdr->inh, &idx );
	if( ret == SGS_SUCCESS )
		goto success;

	sgs_Release( C, &idx );
	return ret;

success:
	sgs_PushVariable( C, &var );
	sgs_Release( C, &var );
	sgs_Release( C, &idx );
	return SGS_SUCCESS;
}

static int sgsstd_class_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	sgs_Variable k, v;
	SGSCLASS_HDR;
	if( strcmp( sgs_ToString( C, -2 ), "_super" ) == 0 )
	{
		sgs_Release( C, &hdr->inh );
		sgs_GetStackItem( C, -1, &hdr->inh );
		sgs_Acquire( C, &hdr->inh );
		return SGS_SUCCESS;
	}
	sgs_GetStackItem( C, -2, &k );
	sgs_GetStackItem( C, -1, &v );
	return sgs_SetIndex( C, &hdr->data, &k, &v );
}

static int sgsstd_class_getmethod( SGS_CTX, sgs_VarObj* data, const char* method )
{
	int ret;
	sgs_Variable var, idx;
	SGSCLASS_HDR;

	sgs_PushString( C, method );
	sgs_GetStackItem( C, -1, &idx );
	sgs_Acquire( C, &idx );
	sgs_Pop( C, 1 );
	
	ret = sgs_GetIndex( C, &var, &hdr->inh, &idx );
	if( ret == SGS_SUCCESS )
	{
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
	}
	sgs_Release( C, &idx );
	return ret == SGS_SUCCESS;
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
	if( sgs_StringMultiConcat( C, 4 ) || sgs_PadString( C ) )
		return SGS_EINPROC;
	sgs_PushString( C, "\n}" );
	return sgs_StringMultiConcat( C, 3 );
}

static int sgsstd_class( SGS_CTX );
static void* sgsstd_class_functable[];

static int sgsstd_class_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	int otype = type;
	static const char* ops[] = { "__tobool", "__toint", "__toreal", "__tostr", "__clone", "__typeof" };

	if( type < 1 || ( type > 4 && type < SGS_CONVOP_CLONE ) || type > SGS_CONVOP_TOTYPE )
		return SGS_ENOTFND;
	if( type < SGS_CONVOP_CLONE )
		type -= 1;
	else
		type += 4 - SGS_CONVOP_CLONE;

	if( sgsstd_class_getmethod( C, data, ops[ type ] ) )
		return sgs_ThisCall( C, 0, 1 );

	if( otype == SVT_STRING || otype == SGS_CONVOP_TOTYPE )
	{
		sgs_PushString( C, "class" );
		return SGS_SUCCESS;
	}

	if( otype == SGS_CONVOP_CLONE )
	{
		SGSCLASS_HDR;
		sgsstd_class_header_t* hdr2;
		sgs_PushVariable( C, &hdr->data );
		sgs_CloneItem( C, -1 );

		hdr2 = sgs_Alloc( sgsstd_class_header_t );
		sgs_GetStackItem( C, -1, &hdr2->data );
		hdr2->inh = hdr->inh;
		sgs_Acquire( C, &hdr2->data );
		sgs_Acquire( C, &hdr2->inh );
		sgs_PushObject( C, hdr2, sgsstd_class_functable );
		return SGS_SUCCESS;
	}

	return SGS_ENOTSUP;
}

static int sgsstd_class_serialize( SGS_CTX, sgs_VarObj* data, int unused )
{
	int ret;
	SGSCLASS_HDR;
	UNUSED( unused );

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

static int sgsstd_class_gcmark( SGS_CTX, sgs_VarObj* data, int type )
{
	SGSCLASS_HDR;
	int ret = sgs_GCMark( C, &hdr->data );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_GCMark( C, &hdr->inh );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

static int sgsstd_class_expr( SGS_CTX, sgs_VarObj* data, int type )
{
	static const char* ops[] = { "__add", "__sub", "__mul", "__div", "__mod", "__compare", "__negate" };
	if( sgsstd_class_getmethod( C, data, ops[type] ) )
		return sgs_FCall( C, type == SGS_EOP_NEGATE ? 0 : 2, 1, type == SGS_EOP_NEGATE ? 1 : 0 );

	return SGS_ENOTFND;
}

static int sgsstd_class_call( SGS_CTX, sgs_VarObj* data, int unused )
{
	if( sgsstd_class_getmethod( C, data, "__call" ) )
	{
		int ret, i;
		sgs_Variable var, fn;
		var.type = SVT_OBJECT;
		var.data.O = data;
		sgs_PushVariable( C, &var );
		for( i = 0; i < C->call_args; ++i )
			sgs_PushItem( C, i );
		sgs_GetStackItem( C, -2 - C->call_args, &fn );
		ret = sgsVM_VarCall( C, &fn, C->call_args, C->call_expect, TRUE );
		return ret;
	}
	return SGS_ENOTFND;
}

static void* sgsstd_class_functable[] =
{
	SOP_DESTRUCT, sgsstd_class_destruct,
	SOP_GETINDEX, sgsstd_class_getindex,
	SOP_SETINDEX, sgsstd_class_setindex,
	SOP_CONVERT, sgsstd_class_convert,
	SOP_SERIALIZE, sgsstd_class_serialize,
	SOP_DUMP, sgsstd_class_dump,
	SOP_GCMARK, sgsstd_class_gcmark,
	SOP_EXPR, sgsstd_class_expr,
	SOP_CALL, sgsstd_class_call,
	SOP_END,
};

static int sgsstd_class( SGS_CTX )
{
	sgsstd_class_header_t* hdr;
	
	SGSFN( "class" );
	
	if( sgs_StackSize( C ) != 2 )
		STDLIB_WARN( "unexpected arguments; "
			"function requires 2 arguments: any (data), any (inherited data)" )

	hdr = sgs_Alloc( sgsstd_class_header_t );
	sgs_GetStackItem( C, 0, &hdr->data );
	sgs_GetStackItem( C, 1, &hdr->inh );
	sgs_Acquire( C, &hdr->data );
	sgs_Acquire( C, &hdr->inh );
	sgs_PushObject( C, hdr, sgsstd_class_functable );
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

static int sgsstd_closure_destruct( SGS_CTX, sgs_VarObj* data, int dch )
{
	SGSCLOSURE_HDR;
	sgs_ReleaseOwned( C, &hdr->func, dch );
	sgs_ReleaseOwned( C, &hdr->data, dch );
	sgs_Dealloc( hdr );
	return SGS_SUCCESS;
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
	if( sgs_StringMultiConcat( C, 4 ) || sgs_PadString( C ) )
		return SGS_EINPROC;
	sgs_PushString( C, "\n}" );
	return sgs_StringMultiConcat( C, 3 );
}

static int sgsstd_closure_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type == SVT_BOOL )
	{
		sgs_PushBool( C, 1 );
		return SGS_SUCCESS;
	}
	if( type != SVT_STRING && type != SGS_CONVOP_TOTYPE )
		return SGS_ENOTSUP;
	UNUSED( data );
	sgs_PushString( C, "closure" );
	return SGS_SUCCESS;
}

static int sgsstd_closure_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
{
	SGSCLOSURE_HDR;
	int ret = sgs_GCMark( C, &hdr->func );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_GCMark( C, &hdr->data );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

static int sgsstd_closure_call( SGS_CTX, sgs_VarObj* data, int unused )
{
	int ismethod = sgs_Method( C );
	SGSCLOSURE_HDR;
	sgs_BreakIf( sgs_InsertVariable( C, -C->call_args - 1, &hdr->data ) );
	return sgsVM_VarCall( C, &hdr->func, C->call_args + 1,
		C->call_expect, ismethod ) * C->call_expect;
}

static void* sgsstd_closure_functable[] =
{
	SOP_DESTRUCT, sgsstd_closure_destruct,
	SOP_CALL, sgsstd_closure_call,
	SOP_CONVERT, sgsstd_closure_convert,
	SOP_GCMARK, sgsstd_closure_gcmark,
	SOP_DUMP, sgsstd_closure_dump,
	SOP_END,
};

static int sgsstd_closure( SGS_CTX )
{
	sgsstd_closure_t* hdr;
	
	SGSFN( "closure" );
	
	if( sgs_StackSize( C ) != 2 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 2 arguments: callable, any" )

	hdr = sgs_Alloc( sgsstd_closure_t );
	sgs_GetStackItem( C, 0, &hdr->func );
	sgs_GetStackItem( C, 1, &hdr->data );
	sgs_Acquire( C, &hdr->func );
	sgs_Acquire( C, &hdr->data );
	sgs_PushObject( C, hdr, sgsstd_closure_functable );
	return 1;
}


/* UTILITIES */

static int sgsstd_isset( SGS_CTX )
{
	int oml, ret;
	char* str;
	sgs_SizeVal size;
	sgs_Variable var;
	
	SGSFN( "isset" );
	
	if( sgs_StackSize( C ) != 2 ||
		!sgs_GetStackItem( C, 0, &var ) ||
		!sgs_ParseString( C, 1, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; function expects 2 arguments: any, string" )

	oml = C->minlev;
	C->minlev = INT32_MAX;
	sgs_Pop( C, 1 );
	ret = sgs_PushProperty( C, str );
	C->minlev = oml;
	sgs_PushBool( C, ret == SGS_SUCCESS );
	return 1;
}

static int sgsstd_unset( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	sgs_Variable var;
	DictHdr* dh;
	
	SGSFN( "unset" );
	
	if( sgs_StackSize( C ) != 2 ||
		!sgs_GetStackItem( C, 0, &var ) ||
		var.type != SVT_OBJECT ||
		var.data.O->iface != sgsstd_dict_functable ||
		!( dh = (DictHdr*) sgs_GetObjectData( C, 0 )->data ) ||
		!sgs_ParseString( C, 1, &str, &size ) )
		STDLIB_WARN( "unexpected arguments; function expects 2 arguments: dict, string" )

#ifdef DICT_CACHE_SIZE
	{
		VHTableVar* tv = vht_get( &dh->ht, str, size );
		if( tv )
		{
			int i;
			int32_t idx = tv - dh->ht.vars;
			for( i = 0; i < DICT_CACHE_SIZE; ++i )
			{
				if( dh->cachevars[ i ] == idx )
					dh->cachekeys[ i ] = NULL;
			}
		}
	}
#endif
	vht_unset( &dh->ht, str, size, C );

	return 0;
}

static int sgsstd_clone( SGS_CTX )
{
	int ret;
	
	SGSFN( "clone" );
	
	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "one argument required" )

	ret = sgs_CloneItem( C, 0 );
	if( ret != SGS_SUCCESS )
		STDLIB_WARN( "failed to clone variable" )
	return 1;
}


static int _foreach_lister( SGS_CTX, int vnk )
{
	if( sgs_StackSize( C ) != 1 ||
		sgs_PushIterator( C, 0 ) != SGS_SUCCESS )
		STDLIB_WARN( "unexpected arguments; "
			" function expects 1 argument of iterable type" );

	sgs_PushArray( C, 0 );
	/* arg1, arg1 iter, output */
	while( sgs_IterAdvance( C, 1 ) > 0 )
	{
		sgs_PushItem( C, 2 );
		sgs_IterPushData( C, 1, !vnk, vnk );
		sgs_PushCFunction( C, sgsstd_arrayI_push );
		sgs_ThisCall( C, 1, 0 );
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
	const char* E = "unexpected arguments; "
			"function expects 2+ arguments: iterable, iterable, ...";
	int i, ssz = sgs_StackSize( C );
	SGSFN( "get_concat" );
	if( ssz < 2 )
		STDLIB_WARN( E );

	sgs_PushArray( C, 0 );
	for( i = 0; i < ssz; ++i )
	{
		if( sgs_PushIterator( C, i ) != SGS_SUCCESS )
			STDLIB_WARN( E );
		while( sgs_IterAdvance( C, -1 ) > 0 )
		{
			/* ..., output, arg2 iter */
			sgs_PushItem( C, -2 );
			sgs_IterPushData( C, -2, FALSE, TRUE );
			sgs_PushCFunction( C, sgsstd_arrayI_push );
			sgs_ThisCall( C, 1, 0 );
		}
		sgs_Pop( C, 1 );
	}
	return 1;
}

static int sgsstd_get_merged( SGS_CTX )
{
	const char* E = "unexpected arguments; "
			"function expects 2+ arguments: iterable, iterable, ...";
	int i, ssz = sgs_StackSize( C );
	SGSFN( "get_merged" );
	if( ssz < 2 )
		STDLIB_WARN( E );

	sgs_PushDict( C, 0 );
	for( i = 0; i < ssz; ++i )
	{
		if( sgs_PushIterator( C, i ) != SGS_SUCCESS )
			STDLIB_WARN( E );
		while( sgs_IterAdvance( C, -1 ) > 0 )
		{
			int ret;
			/* ..., output, arg2 iter */
			ret = sgs_IterPushData( C, -1, TRUE, TRUE );
			if( ret != SGS_SUCCESS ) STDLIB_WARN( 
				"failed to retrieve data from iterator" )
			ret = sgs_StoreIndex( C, -4, -2 );
			if( ret != SGS_SUCCESS ) STDLIB_WARN( "failed to store data" )
			sgs_Pop( C, 1 );
		}
		sgs_Pop( C, 1 );
	}
	return 1;
}



/* I/O */

static int sgsstd_print( SGS_CTX )
{
	int i, ssz;
	SGSBASEFN( "println" );
	ssz = sgs_StackSize( C );
	for( i = 0; i < ssz; ++i )
	{
		sgs_SizeVal size;
		char* buf = sgs_ToStringBuf( C, i, &size );
		sgs_Write( C, buf, size );
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
		sgs_Write( C, buf, size );
		sgs_Write( C, "\n", 1 );
	}
	return 0;
}

static int sgsstd_printvar( SGS_CTX )
{
	sgs_Integer depth = 5;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "printvar" );

	if( ssz < 1 || ssz > 2 ||
		( ssz == 2 && !sgs_ParseInt( C, 1, &depth ) ) )
		STDLIB_WARN( "unexpected arguments; function expects <any>[, int]" );

	if( ssz == 2 )
		sgs_Pop( C, 1 );
	if( sgs_DumpVar( C, depth ) == SGS_SUCCESS )
	{
		sgs_SizeVal bsz;
		char* buf = sgs_ToStringBuf( C, -1, &bsz );
		sgs_Write( C, buf, bsz );
		sgs_Write( C, "\n", 1 );
	}
	else
		STDLIB_WARN( "unknown error while dumping variable" );
	return 0;
}
static int sgsstd_printvars( SGS_CTX )
{
	int i, ssz;
	ssz = sgs_StackSize( C );
	
	SGSFN( "printvars" );
	
	for( i = 0; i < ssz; ++i )
	{
		sgs_PushItem( C, i );
		int res = sgs_DumpVar( C, 5 );
		if( res == SGS_SUCCESS )
		{
			sgs_SizeVal bsz;
			char* buf = sgs_ToStringBuf( C, -1, &bsz );
			sgs_Write( C, buf, bsz );
			sgs_Write( C, "\n", 1 );
		}
		else
		{
			char ebuf[ 64 ];
			sprintf( ebuf, "unknown error while dumping variable #%d", i + 1 );
			STDLIB_WARN( ebuf );
		}
		sgs_Pop( C, 1 );
	}
	return 0;
}

static int sgsstd_read_stdin( SGS_CTX )
{
	MemBuf B;
	char bfr[ 1024 ];
	int all = 0, ssz = sgs_StackSize( C );
	
	SGSFN( "read_stdin" );
	
	if( ssz < 0 || ssz > 1 ||
		( ssz >= 1 && !sgs_ParseBool( C, 0, &all ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 optional argument: bool" )

	B = membuf_create();
	while( fgets( bfr, 1024, stdin ) )
	{
		int len = strlen( bfr );
		membuf_appbuf( &B, C, bfr, len );
		if( len && !all && bfr[ len - 1 ] == '\n' )
			break;
	}
	sgs_PushStringBuf( C, B.ptr, B.size );
	membuf_destroy( &B, C );
	return 1;
}


/* OS */

static int sgsstd_ftime( SGS_CTX )
{
	sgs_PushReal( C, sgs_GetTime() );
	return 1;
}


/* utils */

static int sgsstd_eval( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;
	int rvc = 0, restore = 0, state = C->state;
	
	SGSFN( "eval" );

	if( sgs_StackSize( C ) < 1 || sgs_StackSize( C ) > 2 || !sgs_ParseString( C, 0, &str, &size ) ||
		( sgs_StackSize( C ) == 2 && !sgs_ParseBool( C, 1, &restore ) ) )
		STDLIB_WARN( "unexpected arguments; function expects 1-2 arguments: string[, bool]" )

	sgs_EvalBuffer( C, str, (int) size, &rvc );
	if( restore )
		C->state = state;
	return rvc;
}

static int sgsstd_eval_file( SGS_CTX )
{
	int ret, retcnt = 0;
	char* str;
	int cnt = sgs_StackSize( C );
	
	SGSFN( "eval_file" );

	if( cnt != 1 || !sgs_ParseString( C, 0, &str, NULL ) )
		STDLIB_WARN( "unexpected arguments; function expects 1 argument: string" )

	ret = sgs_EvalFile( C, str, &retcnt );
	if( ret == SGS_ENOTFND )
		STDLIB_WARN( "file not found" )
	return retcnt;
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
#if 0
	else if( strcmp( name, "native" ) == 0 ) ret = sgs_LoadLib_Native( C );
#endif
	else if( strcmp( name, "os" ) == 0 ) ret = sgs_LoadLib_OS( C );
	else if( strcmp( name, "string" ) == 0 ) ret = sgs_LoadLib_String( C );
	else if( strcmp( name, "type" ) == 0 ) ret = sgs_LoadLib_Type( C );

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
	sgs_StringConcat( C );
	if( sgs_PushGlobal( C, sgs_ToString( C, -1 ) ) != SGS_SUCCESS )
		return FALSE;
	/* sgs_Pop( C, 2 ); [.., string, bool] - will return last */
	return TRUE;
}

static void sgsstd__setinc( SGS_CTX, int argid )
{
	sgs_PushString( C, "+" );
	sgs_PushItem( C, argid );
	sgs_StringConcat( C );
	sgs_PushBool( C, TRUE );
	sgs_StoreGlobal( C, sgs_ToString( C, -2 ) );
	sgs_Pop( C, 1 );
}

static int sgsstd_include_library( SGS_CTX )
{
	int ret, sz = sgs_StackSize( C ), over = FALSE;
	char* str;

	SGSBASEFN( "include_library" );

	if( sz < 1 || sz > 2 || !sgs_ParseString( C, 0, &str, NULL ) ||
		( sz == 2 && !sgs_ParseBool( C, 1, &over ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, bool]" )

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
	int cnt = sgs_StackSize( C );

	SGSBASEFN( "include_file" );
	
	if( cnt < 1 || cnt > 2 || !sgs_ParseString( C, 0, &str, NULL )
		|| ( cnt == 2 && !sgs_ParseBool( C, 1, &over ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, bool]" )

	if( !over && sgsstd__chkinc( C, 0 ) )
		return 1;

	ret = sgs_ExecFile( C, str );
	if( ret == SGS_ENOTFND )
		STDLIB_WARN( "file not found" )
	if( ret == SGS_SUCCESS )
		sgsstd__setinc( C, 0 );
	sgs_PushBool( C, ret == SGS_SUCCESS );
	return 1;
}

static int sgsstd_include_shared( SGS_CTX )
{
	char* fnstr;
	int ret, cnt = sgs_StackSize( C ), over = FALSE;
	sgs_CFunc func;
	
	SGSBASEFN( "include_shared" );
	
	if( cnt < 1 || cnt > 2 || !sgs_ParseString( C, 0, &fnstr, NULL )
		|| ( cnt == 2 && !sgs_ParseBool( C, 1, &over ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, bool]" )

	if( !over && sgsstd__chkinc( C, 0 ) )
		return 1;

	ret = sgs_GetProcAddress( fnstr, "sgscript_main", (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE ) STDLIB_WARN( "file not found" )
		else if( ret == SGS_XPC_NOPROC )
			STDLIB_WARN( "procedure not found" )
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

static int sgsstd_include_module( SGS_CTX )
{
	int ret;
	sgs_Variable backup;
	SGSBASEFN( "include_module" );
	if( sgs_StackSize( C ) < 1 || !sgs_ParseString( C, 0, NULL, NULL ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, bool]" )
	
	sgs_GetStackItem( C, 0, &backup );
	sgs_Acquire( C, &backup );
	
	sgs_PushString( C, "sgs" );
	sgs_PushItem( C, 0 );
	sgs_PushString( C, SGS_MODULE_EXT );
	sgs_StringMultiConcat( C, 3 );
	sgs_StoreItem( C, 0 );
	
	ret = sgsstd_include_shared( C );
	if( !ret )
	{
		sgs_PushVariable( C, &backup );
		sgs_StoreItem( C, 0 );
	}
	sgs_Release( C, &backup );
	return ret;
}

static int sgsstd_include( SGS_CTX )
{
	int32_t oml;
	char* fnstr;
	sgs_SizeVal fnsize;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "include" );
	
	if( ssz < 1 || ssz > 2 || !sgs_ParseString( C, 0, &fnstr, &fnsize )
		|| ( ssz >= 2 && !sgs_ParseBool( C, 1, NULL ) ) )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1-2 arguments: string[, bool]" )
	
	oml = C->minlev;
	C->minlev = INT32_MAX;
	
	if( sgsstd_include_library( C ) && sgs_ToBool( C, -1 ) ) goto success;
	sgs_Pop( C, sgs_StackSize( C ) - ssz );
	
	if( sgsstd_include_file( C ) && sgs_ToBool( C, -1 ) ) goto success;
	sgs_Pop( C, sgs_StackSize( C ) - ssz );
	
	if( sgsstd_include_module( C ) && sgs_ToBool( C, -1 ) ) goto success;
	sgs_Pop( C, sgs_StackSize( C ) - ssz );
	
	if( sgsstd_include_shared( C ) && sgs_ToBool( C, -1 ) ) goto success;
	
	C->minlev = oml;
	return sgs_Printf( C, SGS_WARNING, "could not load '%.*s'", fnsize, fnstr );
	
success:
	C->minlev = oml;
	return 1;
}

static int sgsstd_import_cfunc( SGS_CTX )
{
	char* fnstr, *pnstr;
	int ret, argc = sgs_StackSize( C );
	sgs_CFunc func;
	
	SGSFN( "import_cfunc" );
	
	if( argc != 2 || !sgs_ParseString( C, 0, &fnstr, NULL ) ||
		!sgs_ParseString( C, 1, &pnstr, NULL ) )
		STDLIB_WARN( "unexpected arguments; function expects 2 arguments: string, string" )

	ret = sgs_GetProcAddress( fnstr, pnstr, (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE ) STDLIB_WARN( "file not found" )
		else if( ret == SGS_XPC_NOPROC )
			STDLIB_WARN( "procedure not found" )
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
		STDLIB_WARN( "unexpected arguments; function expects none" )

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

static int sgsstd_sys_print( SGS_CTX )
{
	char* errmsg;
	sgs_Integer errcode;
	
	SGSFN( "sys_print" );

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseInt( C, 0, &errcode ) ||
		!sgs_ParseString( C, 1, &errmsg, NULL ) )
		STDLIB_WARN( "unexpected arguments; function expects 2 arguments: int, string" )

	sgs_Printf( C, errcode, errmsg );
	return 0;
}

static int sgsstd_sys_errorstate( SGS_CTX )
{
	int state = 1;
	if( sgs_StackSize( C ) )
		state = sgs_GetBool( C, 0 );
	C->state = ( C->state & ~SGS_HAS_ERRORS ) | ( SGS_HAS_ERRORS * state );
	return 0;
}
static int sgsstd_sys_abort( SGS_CTX )
{
	sgs_StackFrame* sf = C->sf_first;
	while( sf )
	{
		sf->iptr = sf->iend;
		sf++;
	}
	return 0;
}

static int sgsstd_sys_replevel( SGS_CTX )
{
	int lev = C->minlev;
	
	SGSFN( "sys_replevel" );
	
	if( sgs_StackSize( C ) )
	{
		sgs_Integer i;
		if( sgs_StackSize( C ) != 1 ||
			!sgs_ParseInt( C, 0, &i ) )
			STDLIB_WARN( "unexpected arguments; "
				"function expects 0-1 arguments - [int]" )
		C->minlev = (int) i;
		return 0;
	}
	sgs_PushInt( C, lev );
	return 1;
}

static int sgsstd_sys_stat( SGS_CTX )
{
	sgs_Integer type;
	
	SGSFN( "sys_stat" );
	
	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseInt( C, 0, &type ) )
		STDLIB_WARN( "unexpected arguments; function expects int" );

	sgs_PushInt( C, sgs_Stat( C, type ) );
	return 1;
}

static int sgsstd_dumpvar( SGS_CTX )
{
	sgs_Integer depth = 5;
	int ssz = sgs_StackSize( C );
	
	SGSFN( "dumpvar" );
	
	if( ssz < 1 || ssz > 2 ||
		( ssz == 2 && !sgs_ParseInt( C, 1, &depth ) ) )
		STDLIB_WARN( "unexpected arguments; function expects <any>[, int]" );

	if( ssz == 2 )
		sgs_Pop( C, 1 );
	if( sgs_DumpVar( C, depth ) == SGS_SUCCESS )
		return 1;
	else
		STDLIB_WARN( "unknown error while dumping variable" );
	return 0;
}
static int sgsstd_dumpvars( SGS_CTX )
{
	int i, ssz, rc = 0;
	ssz = sgs_StackSize( C );
	
	SGSFN( "dumpvars" );
	
	for( i = 0; i < ssz; ++i )
	{
		sgs_PushItem( C, i );
		int res = sgs_DumpVar( C, 5 );
		if( res == SGS_SUCCESS )
		{
			sgs_PushString( C, "\n" );
			rc++;
		}
		else
		{
			char ebuf[ 64 ];
			sprintf( ebuf, "unknown error while dumping variable #%d", i + 1 );
			STDLIB_WARN( ebuf );
			sgs_Pop( C, 1 );
		}
	}
	if( rc )
	{
		if( sgs_StringMultiConcat( C, rc * 2 ) == SGS_SUCCESS )
			STDLIB_WARN( "failed to concatenate the output" )
		return 1;
	}
	return 0;
}

static int sgsstd_gc_collect( SGS_CTX )
{
	int ret;
	int32_t orvc = sgs_Stat( C, SGS_STAT_VARCOUNT );
	
	SGSFN( "gc_collect" );
	
	ret = sgs_GCExecute( C );
	
	if( ret == SGS_SUCCESS )
		sgs_PushInt( C, orvc - sgs_Stat( C, SGS_STAT_VARCOUNT ) );
	else
		sgs_PushBool( C, FALSE );
	return 1;
}


static int sgsstd_serialize( SGS_CTX )
{
	int ret;
	
	SGSFN( "serialize" );
	
	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument" )

	ret = sgs_Serialize( C );
	if( ret == SGS_SUCCESS )
		return 1;
	else
		STDLIB_WARN( "failed to serialize" )
}

static int sgsstd_unserialize( SGS_CTX )
{
	int ret;
	
	SGSFN( "unserialize" );
	
	if( sgs_StackSize( C ) != 1 )
		STDLIB_WARN( "unexpected arguments; "
			"function expects 1 argument: string" )

	ret = sgs_Unserialize( C );
	if( ret == SGS_SUCCESS )
		return 1;
	else if( ret == SGS_EINPROC )
		STDLIB_WARN( "error in data" )
	else if( ret == SGS_ENOTFND )
		STDLIB_WARN( "could not find something" )
	else
		STDLIB_WARN( "unknown error" )
}


/* register all */
#define FN( name ) { #name, sgsstd_##name }
static sgs_RegFuncConst regfuncs[] =
{
	/* containers */
	FN( array ), FN( dict ), { "class", sgsstd_class }, FN( closure ),
	FN( isset ), FN( unset ), FN( clone ),
	FN( get_keys ), FN( get_values ), FN( get_concat ), FN( get_merged ),
	/* I/O */
	FN( print ), FN( println ), FN( printlns ),
	FN( printvar ), FN( printvars ),
	FN( read_stdin ),
	/* OS */
	FN( ftime ),
	/* utils */
	FN( eval ), FN( eval_file ),
	FN( include_library ), FN( include_file ),
	FN( include_shared ), FN( include_module ), FN( import_cfunc ),
	FN( include ),
	FN( sys_curfile ),
	FN( sys_print ), FN( sys_errorstate ), FN( sys_abort ),
	FN( sys_replevel ), FN( sys_stat ),
	FN( dumpvar ), FN( dumpvars ),
	FN( gc_collect ),
	FN( serialize ), FN( unserialize ),
};

static const sgs_RegIntConst regiconsts[] =
{
	{ "SGS_INFO", SGS_INFO },
	{ "SGS_WARNING", SGS_WARNING },
	{ "SGS_ERROR", SGS_ERROR },
};


int sgsSTD_PostInit( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, regiconsts, ARRAY_SIZE( regiconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, regfuncs, ARRAY_SIZE( regfuncs ) );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

int sgsSTD_MakeArray( SGS_CTX, int cnt )
{
	int i = 0, ssz = sgs_StackSize( C );

	if( ssz < cnt )
		return SGS_EINVAL;
	else
	{
		sgs_Variable *p, *pend;
		void* data = sgs_Malloc( C, SGSARR_ALLOCSIZE( cnt ) );
		SGSARR_HDRBASE;

		hdr->size = cnt;
		hdr->mem = cnt;
		p = SGSARR_PTR( data );
		pend = p + cnt;
		while( p < pend )
		{
			sgs_GetStackItem( C, i++ - cnt, p );
			sgs_Acquire( C, p++ );
		}

		sgs_Pop( C, cnt );
		sgs_PushObject( C, data, sgsstd_array_functable );
		return SGS_SUCCESS;
	}
}

int sgsSTD_MakeDict( SGS_CTX, int cnt )
{
	DictHdr* dh;
	VHTable* ht;
	int i, ssz = sgs_StackSize( C );

	if( cnt > ssz || cnt % 2 != 0 )
		return SGS_EINVAL;

	dh = mkdict( C );
	ht = &dh->ht;

	for( i = 0; i < cnt; i += 2 )
	{
		char* kstr;
		sgs_SizeVal ksize;
		sgs_Variable val;
		if( !sgs_ParseString( C, i - cnt, &kstr, &ksize ) )
		{
			vht_free( ht, C, 1 );
			sgs_Dealloc( ht );
			sgs_Pop( C, sgs_StackSize( C ) - ssz );
			return SGS_EINVAL;
		}

		sgs_GetStackItem( C, i + 1 - cnt, &val );
		vht_set( ht, kstr, (int32_t) ksize, &val, C );
	}

	sgs_Pop( C, cnt );
	sgs_PushObject( C, ht, sgsstd_dict_functable );
	return SGS_SUCCESS;
}


#define GLBP (*(sgs_VarObj**)&C->data)

int sgsSTD_GlobalInit( SGS_CTX )
{
	sgs_Variable var;
	if( sgsSTD_MakeDict( C, 0 ) < 0 ||
		sgs_GetStackItem( C, -1, &var ) < 0 )
		return SGS_EINPROC;
	sgs_Acquire( C, &var );
	if( sgs_Pop( C, 1 ) < 0 )
		return SGS_EINPROC;
	GLBP = var.data.O;
	return SGS_SUCCESS;
}

int sgsSTD_GlobalFree( SGS_CTX )
{
	sgs_Variable var;
	VHTableVar *p, *pend;
	sgs_VarObj* data = GLBP;
	HTHDR;

	p = ht->vars;
	pend = p + vht_size( ht );
	while( p < pend )
	{
		sgs_Release( C, &p->var );
		p++;
	}

	var.type = SVT_OBJECT;
	var.data.O = GLBP;
	sgs_Release( C, &var );

	C->data = NULL;

	return SGS_SUCCESS;
}

int sgsSTD_GlobalGet( SGS_CTX, sgs_Variable* out, sgs_Variable* idx, int apicall )
{
	VHTableVar* pair;
	sgs_VarObj* data = GLBP;
	HTHDR;

	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	sgs_Release( C, out );
	pair = vht_getS( ht, idx->data.S );

	if( pair )
	{
		*out = pair->var;
		sgs_Acquire( C, out );
		return SGS_SUCCESS;
	}
	else if( strcmp( str_cstr( idx->data.S ), "_G" ) == 0 )
	{
		out->type = SVT_OBJECT;
		out->data.O = GLBP;
		sgs_Acquire( C, out );
		return SGS_SUCCESS;
	}
	else
	{
		if( !apicall )
			sgs_Printf( C, SGS_WARNING, "variable '%s' was not found",
				str_cstr( idx->data.S ) );
		out->type = SVT_NULL;
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
		if( !apicall )
			sgs_Printf( C, SGS_WARNING, "cannot change the value "
				"of a hardcoded constant (%s)", var_cstr( idx ) );
		return SGS_EINPROC;
	}

	vht_set( ht, var_cstr( idx ), idx->data.S->size, val, C );
	return SGS_SUCCESS;
}

int sgsSTD_GlobalGC( SGS_CTX )
{
	sgs_VarObj* data = GLBP;

	if( data )
	{
		sgs_Variable tmp;
		VHTableVar *pair, *pend;
		HTHDR;

		tmp.type = SVT_OBJECT;
		tmp.data.O = data;
		if( sgs_GCMark( C, &tmp ) < 0 )
			return SGS_EINPROC;

		pair = ht->vars;
		pend = pair + vht_size( ht );
		while( pair < pend )
		{
			if( sgs_GCMark( C, &pair->var ) < 0 )
				return SGS_EINPROC;
			pair++;
		}
	}
	return SGS_SUCCESS;
}

int sgsSTD_GlobalIter( SGS_CTX, sgs_VHTableVar** outp, sgs_VHTableVar** outpend )
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
	const sgs_RegFuncConst* last = list + size;
	while( list < last )
	{
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
	const sgs_RegIntConst* last = list + size;
	while( list < last )
	{
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
	const sgs_RegRealConst* last = list + size;
	while( list < last )
	{
		sgs_PushReal( C, list->value );
		ret = sgs_StoreGlobal( C, list->name );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}
