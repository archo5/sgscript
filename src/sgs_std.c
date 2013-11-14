

#include <stdio.h>
#include <errno.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL

#include "sgs_int.h"

#define STDLIB_WARN( warn ) return sgs_Printf( C, SGS_WARNING, warn );
#define STDLIB_ERR( err ) return sgs_Printf( C, SGS_ERROR, err );


/* Containers */


/*
	ARRAY
*/

SGS_DECLARE sgs_ObjCallback sgsstd_array_functable[ 15 ];

#define SGSARR_UNIT sizeof( sgs_Variable )
#define SGSARR_HDR sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data
#define SGSARR_HDR_OI sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data->data
#define SGSARR_HDRSIZE sizeof(sgsstd_array_header_t)
#define SGSARR_ALLOCSIZE( cnt ) ((cnt)*SGSARR_UNIT)
#define SGSARR_PTR( base ) (((sgsstd_array_header_t*)base)->data)

static void sgsstd_array_reserve( SGS_CTX, sgsstd_array_header_t* hdr, sgs_SizeVal size )
{
	if( size <= hdr->mem )
		return;

	hdr->data = (sgs_Variable*) sgs_Realloc( C, hdr->data, SGSARR_ALLOCSIZE( size ) );
	hdr->mem = size;
}

static void sgsstd_array_clear( SGS_CTX, sgsstd_array_header_t* hdr, int unused )
{
	sgs_Variable *var, *vend;
	UNUSED( unused );
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
		memmove( ptr + pos + cnt, ptr + pos, ( hdr->size - pos ) * SGSARR_UNIT );
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
		memmove( ptr + from, ptr + to1, ( hdr->size - to1 ) * SGSARR_UNIT );
	hdr->size -= cnt;
}

#define SGSARR_IHDR( name ) \
	sgsstd_array_header_t* hdr; \
	int method_call = sgs_Method( C ); \
	SGSFN( "array." #name ); \
	if( !sgs_IsObject( C, 0, sgsstd_array_functable ) ) \
		return sgs_ArgErrorExt( C, 0, method_call, "array", "" ); \
	hdr = (sgsstd_array_header_t*) sgs_GetObjectData( C, 0 ); \
	UNUSED( hdr );
/* after this, the counting starts from 1 because of sgs_Method */


static int sgsstd_arrayI_push( SGS_CTX )
{
	SGSARR_IHDR( push );
	sgsstd_array_insert( C, hdr, hdr->size, 1 );
	sgs_SetStackSize( C, 1 );
	return 1;
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
	sgsstd_array_insert( C, hdr, 0, 1 );
	sgs_SetStackSize( C, 1 );
	return 1;
}

static int sgsstd_arrayI_insert( SGS_CTX )
{
	sgs_Int at;
	SGSARR_IHDR( insert );
	
	if( !sgs_LoadArgs( C, "@>i?v", &at ) )
		return 0;

	if( at < 0 )
		at += hdr->size + 1;
	if( at < 0 || at > hdr->size )
		STDLIB_WARN( "index out of bounds" )

	sgsstd_array_insert( C, hdr, (sgs_SizeVal) at, 2 );
	sgs_SetStackSize( C, 1 );
	return 1;
}
static int sgsstd_arrayI_erase( SGS_CTX )
{
	int cnt = sgs_StackSize( C );
	sgs_Int at, at2;
	SGSARR_IHDR( erase );
	
	if( !sgs_LoadArgs( C, "@>i|i", &at, &at2 ) )
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
	sgs_SetStackSize( C, 1 );
	return 1;
}
static int sgsstd_arrayI_part( SGS_CTX )
{
	sgs_SizeVal from, max = 0x7fffffff, to, i;
	SGSARR_IHDR( part );
	
	if( !sgs_LoadArgs( C, "@>l|l", &from, &max ) )
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
	sgsstd_array_clear( C, hdr, 1 );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}

static int sgsstd_arrayI_reverse( SGS_CTX )
{
	SGSARR_IHDR( reverse );
	
	if( !sgs_LoadArgs( C, "@>." ) )
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
	
	sgs_SetStackSize( C, 1 );
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
		( SGSARR_PTR( hdr ) + (hdr->size++) )->type = VTC_NULL;
	}
}
static int sgsstd_arrayI_resize( SGS_CTX )
{
	sgs_SizeVal sz;
	SGSARR_IHDR( resize );
	
	if( !sgs_LoadArgs( C, "@>l", &sz ) )
		return 0;
	
	if( sz < 0 )
		STDLIB_WARN( "argument 1 (size) must be bigger than or equal to 0" )

	sgsstd_array_reserve( C, hdr, sz );
	sgsstd_array_adjust( C, hdr, sz );
	sgs_Pop( C, sgs_StackSize( C ) - 1 );
	return 1;
}
static int sgsstd_arrayI_reserve( SGS_CTX )
{
	sgs_SizeVal sz;
	SGSARR_IHDR( reserve );
	
	if( !sgs_LoadArgs( C, "@>l", &sz ) )
		return 0;
	
	if( sz < 0 )
		STDLIB_WARN( "argument 1 (size) must be bigger than or equal to 0" )

	sgsstd_array_reserve( C, hdr, sz );
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
	int rev = 0;
	SGSARR_IHDR( sort );
	
	if( !sgs_LoadArgs( C, "@>|b", &rev ) )
		return 0;
	
	quicksort( SGSARR_PTR( hdr ), hdr->size, sizeof( sgs_Variable ),
		rev ? sgsarrcomp_basic_rev : sgsarrcomp_basic, C );
	sgs_SetStackSize( C, 1 );
	return 1;
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
	u.sortfunc.type = VTC_NULL;
	
	if( !sgs_LoadArgs( C, "@>?p<v|b", &u.sortfunc, &rev ) )
		return 0;
	
	quicksort( SGSARR_PTR( hdr ), hdr->size,
		sizeof( sgs_Variable ), rev ? sgsarrcomp_custom_rev : sgsarrcomp_custom, &u );
	sgs_SetStackSize( C, 1 );
	return 1;
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
	int rev = 0;
	SGSARR_IHDR( sort_mapped );
	
	if( !sgs_LoadArgs( C, "@>a|b", &asize, &rev ) )
		return 0;

	if( asize != hdr->size )
		STDLIB_WARN( "array sizes must match" )

	{
		sgsarr_smi* smis = sgs_Alloc_n( sgsarr_smi, asize );
		for( i = 0; i < asize; ++i )
		{
			if( sgs_PushNumIndex( C, 1, i ) )
			{
				sgs_Dealloc( smis );
				STDLIB_WARN( "error in mapping array" )
			}
			smis[ i ].value = sgs_GetReal( C, -1 );
			smis[ i ].pos = i;
			sgs_Pop( C, 1 );
		}
		quicksort( smis, asize, sizeof( sgsarr_smi ),
			rev ? sgsarrcomp_smi_rev : sgsarrcomp_smi, NULL );

		{
			sgs_Variable *p1, *p2;
			p1 = SGSARR_PTR( hdr );
			p2 = sgs_Alloc_n( sgs_Variable, hdr->mem );
			memcpy( p2, p1, SGSARR_ALLOCSIZE( hdr->mem ) );
			for( i = 0; i < asize; ++i )
				p1[ i ] = p2[ smis[ i ].pos ];
			sgs_Dealloc( p2 );
		}

		sgs_Dealloc( smis );

		sgs_SetStackSize( C, 1 );
		return 1;
	}
}

static int sgsstd_arrayI_find( SGS_CTX )
{
	sgs_Variable comp;
	int strict = FALSE;
	sgs_SizeVal off = 0;
	SGSARR_IHDR( find );
	
	if( !sgs_LoadArgs( C, "@>v|bl", &comp, &strict, &off ) )
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
	
	if( !sgs_LoadArgs( C, "@>v|bbl", &comp, &strict, &all, &off ) )
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
	if( !sgs_LoadArgs( C, "@>|b", &strconv ) )
		return 0;
	
	sgs_SetStackSize( C, 1 );
	sgs_PushArray( C, 0 );
	nadata = sgs_GetObjectData( C, -1 );
	while( off < hdr->size )
	{
		sgs_PushVariable( C, SGSARR_PTR( hdr ) + off );
		if( !_in_array( C, nadata, strconv ) )
		{
			sgsstd_array_insert( C, (sgsstd_array_header_t*) nadata, asz, 2 );
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
	if( !sgs_LoadArgs( C, "@>i", &num ) )
		return 0;
	
	if( num < 0 )
		STDLIB_WARN( "argument 1 (count) cannot be negative" )
	
	sgs_SetStackSize( C, 1 );
	sgs_PushArray( C, 0 );
	nadata = (sgsstd_array_header_t*) sgs_GetObjectData( C, -1 );
	while( num-- )
	{
		sgs_PushVariable( C, SGSARR_PTR( hdr ) + ( rand() % hdr->size ) );
		sgsstd_array_insert( C, nadata, asz, 2 );
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
	if( !sgs_LoadArgs( C, "@>." ) )
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

static int sgsstd_array_getprop( SGS_CTX, void* data )
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
	else if( 0 == strcmp( name, "first" ) )
	{
		SGSARR_HDR;
		if( hdr->size )
			sgs_PushVariable( C, SGSARR_PTR( hdr ) );
		else
		{
			sgs_PushNull( C );
			sgs_Printf( C, SGS_WARNING, "array is empty, cannot get first item" );
		}
		return SGS_SUCCESS;
	}
	else if( 0 == strcmp( name, "last" ) )
	{
		SGSARR_HDR;
		if( hdr->size )
			sgs_PushVariable( C, SGSARR_PTR( hdr ) + hdr->size - 1 );
		else
		{
			sgs_PushNull( C );
			sgs_Printf( C, SGS_WARNING, "array is empty, cannot get last item" );
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

int sgsstd_array_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	if( prop )
		return sgsstd_array_getprop( C, data->data );
	else
	{
		SGSARR_HDR_OI;
		sgs_Variable* ptr = SGSARR_PTR( hdr );
		sgs_Int i = sgs_ToInt( C, -1 );
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
		SGSARR_HDR_OI;
		sgs_Variable* ptr = SGSARR_PTR( hdr );
		sgs_Int i = sgs_ToInt( C, -2 );
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
	int i, ssz = sgs_StackSize( C );
	SGSARR_HDR_OI;
	sprintf( bfr, "array (%d)\n[", hdr->size );
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
	return sgs_StringMultiConcat( C, sgs_StackSize( C ) - ssz );
}

static int sgsstd_array_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
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

static int sgsstd_array_iter_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	UNUSED( unused );
	sgs_Release( C, &((sgsstd_array_iter_t*) data->data)->ref );
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

static sgs_ObjCallback sgsstd_array_iter_functable[ 5 ] =
{
	SOP_DESTRUCT, sgsstd_array_iter_destruct,
	SOP_GETNEXT, sgsstd_array_iter_getnext,
	SOP_END,
};

static int sgsstd_array_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	SGSARR_HDR_OI;
	if( type == SGS_CONVOP_TOITER )
	{
		sgsstd_array_iter_t* iter = sgs_Alloc( sgsstd_array_iter_t );

		iter->ref.type = VTC_ARRAY;
		iter->ref.data.O = data;
		iter->size = hdr->size;
		iter->off = -1;

		sgs_Acquire( C, &iter->ref );
		sgs_PushObject( C, iter, sgsstd_array_iter_functable );
		(C->stack_top-1)->type = SGS_VTC_ARRAY_ITER;
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
		sgsstd_array_header_t* hdr2 = (sgsstd_array_header_t*)
			sgs_PushObjectIPA( C, sizeof( sgsstd_array_header_t ), sgsstd_array_functable );
		memcpy( hdr2, hdr, sizeof( sgsstd_array_header_t ) );
		hdr2->data = sgs_Alloc_n( sgs_Variable, hdr->mem );
		memcpy( hdr2->data, hdr->data, SGSARR_ALLOCSIZE( hdr->mem ) );
		{
			sgs_Variable* ptr = SGSARR_PTR( hdr );
			sgs_Variable* pend = ptr + hdr->size;
			while( ptr < pend )
				sgs_Acquire( C, ptr++ );
		}
		(C->stack_top-1)->type = VTC_ARRAY;
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
	SGSARR_HDR_OI;
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
	SGSARR_HDR_OI;
	sgsstd_array_clear( C, hdr, dch );
	sgs_Dealloc( hdr->data );
	return 0;
}

static sgs_ObjCallback sgsstd_array_functable[ 15 ] =
{
	SOP_DESTRUCT, sgsstd_array_destruct,
	SOP_GETINDEX, sgsstd_array_getindex,
	SOP_SETINDEX, sgsstd_array_setindex,
	SOP_DUMP, sgsstd_array_dump,
	SOP_GCMARK, sgsstd_array_gcmark,
	SOP_CONVERT, sgsstd_array_convert,
	SOP_SERIALIZE, sgsstd_array_serialize,
	SOP_END,
};

static int sgsstd_array( SGS_CTX )
{
	int i = 0, objcnt = sgs_StackSize( C );
	void* data = sgs_Malloc( C, SGSARR_ALLOCSIZE( objcnt ) );
	sgs_Variable *p, *pend;
	sgsstd_array_header_t* hdr = (sgsstd_array_header_t*)
		sgs_PushObjectIPA( C, sizeof( sgsstd_array_header_t ), sgsstd_array_functable );
	hdr->size = objcnt;
	hdr->mem = objcnt;
	p = hdr->data = (sgs_Variable*) data;
	pend = p + objcnt;
	while( p < pend )
	{
		sgs_GetStackItem( C, i++, p );
		sgs_Acquire( C, p++ );
	}
	(C->stack_top-1)->type = VTC_ARRAY;
	return 1;
}


/*
	DICT
*/

#ifdef SGS_DICT_CACHE_SIZE
#define DICT_CACHE_SIZE SGS_DICT_CACHE_SIZE
#endif

#ifdef __cplusplus
extern
#else
static
#endif
sgs_ObjCallback sgsstd_dict_functable[ 15 ];

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
	DictHdr* dh = (DictHdr*) sgs_PushObjectIPA( C, sizeof( DictHdr ), sgsstd_dict_functable );
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

static int sgsstd_dict_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	HTHDR;
	UNUSED( unused );
	vht_free( ht, C );
	return SGS_SUCCESS;
}

static int sgsstd_dict_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	int ssz;
	char bfr[ 32 ];
	VHTableVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + vht_size( ht );
	ssz = sgs_StackSize( C );
	sprintf( bfr, "dict (%d)\n{", vht_size( ht ) );
	sgs_PushString( C, bfr );
	if( depth )
	{
		if( vht_size( ht ) )
		{
			while( pair < pend )
			{
				sgs_Variable tmp; tmp.type = VTC_STRING;
				tmp.data.S = pair->str;
				sgs_PushString( C, "\n" );
				sgs_PushVariable( C, &tmp );
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
	return sgs_StringMultiConcat( C, sgs_StackSize( C ) - ssz );
}

static int sgsstd_dict_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
{
	VHTableVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + vht_size( ht );
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

	pair = vht_getS( ht, (C->stack_top-1)->data.S );
	if( !pair )
		return SGS_ENOTFND;

	sgs_PushVariable( C, &pair->var );
	return SGS_SUCCESS;
}

int sgsstd_dict_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	VHTableVar* pair = NULL;
	HTHDR;

	if( !prop )
		return sgsstd_dict_getindex_exact( C, data );

	if( (C->stack_top-1)->type == VTC_INT )
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

	if( !sgs_ToString( C, -1 ) )
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
	const char* str;
	sgs_Variable val;
	HTHDR;
	str = sgs_ToString( C, -2 );
	sgs_GetStackItem( C, -1, &val );
	if( !str )
		return SGS_EINVAL;
	vht_setS( ht, (C->stack_top-2)->data.S, &val, C );
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

static int sgsstd_dict_iter_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	UNUSED( unused );
	sgs_Release( C, &((sgsstd_dict_iter_t*) data->data)->ref );
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
		{
			sgs_Variable tmp; tmp.type = VTC_STRING;
			tmp.data.S = ht->vars[ iter->off ].str;
			sgs_PushVariable( C, &tmp );
		}
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

static sgs_ObjCallback sgsstd_dict_iter_functable[ 7 ] =
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

		iter->ref.type = VTC_DICT;
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
			sgs_Variable tmp; tmp.type = VTC_STRING;
			tmp.data.S = pair->str;
			if( cnt )
				sgs_PushString( C, "," );
			sgs_PushVariable( C, &tmp );
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
			vht_setS( &ndh->ht, ht->vars[ i ].str, &ht->vars[ i ].var, C );
		}
		(C->stack_top-1)->type = VTC_DICT;
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
	VHTableVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + vht_size( ht );
	UNUSED( unused );
	while( pair < pend )
	{
		sgs_Variable tmp; tmp.type = VTC_STRING;
		tmp.data.S = pair->str;
		sgs_PushVariable( C, &tmp );
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

static sgs_ObjCallback sgsstd_dict_functable[ 15 ] =
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
		STDLIB_WARN( "function expects 0 or an even number of arguments" )

	dh = mkdict( C );
	ht = &dh->ht;

	for( i = 0; i < objcnt; i += 2 )
	{
		sgs_Variable val;
		if( !sgs_ParseString( C, i, NULL, NULL ) )
			return sgs_FuncArgError( C, i, SVT_STRING, 0 );

		sgs_GetStackItem( C, i + 1, &val );
		vht_setS( ht, (C->stack_off+i)->data.S, &val, C );
	}

	(C->stack_top-1)->type = VTC_DICT;
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

static int sgsstd_class_getmethod( SGS_CTX, sgs_VarObj* data, const char* method )
{
	int ret;
	sgs_Variable idx;
	SGSCLASS_HDR;

	sgs_PushString( C, method );
	sgs_GetStackItem( C, -1, &idx );
	
	ret = sgs_PushIndexP( C, &hdr->inh, &idx );
	sgs_PopSkip( C, 1, ret == SGS_SUCCESS ? 1 : 0 );
	return ret == SGS_SUCCESS;
}

static int sgsstd_class_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	SGSCLASS_HDR;
	UNUSED( unused );
	if( sgsstd_class_getmethod( C, data, "__destruct" ) )
	{
		sgs_Variable tmp;
		tmp.type = VTC_OBJECT;
		tmp.data.O = data;
		data->refcount++;
		sgs_PushVariable( C, &tmp );
		sgs_PushItem( C, -2 );
		if( sgs_ThisCall( C, 0, 0 ) )
			sgs_Pop( C, 3 );
		else
			sgs_Pop( C, 1 );
		data->refcount--;
	}
	sgs_Release( C, &hdr->data );
	sgs_Release( C, &hdr->inh );
	return SGS_SUCCESS;
}

static int sgsstd_class_getindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	int ret;
	sgs_Variable idx;
	SGSCLASS_HDR;
	if( prop && strcmp( sgs_ToString( C, -1 ), "_super" ) == 0 )
	{
		sgs_PushVariable( C, &hdr->inh );
		return SGS_SUCCESS;
	}
	sgs_GetStackItem( C, -1, &idx );

	if( sgs_PushIndexP( C, &hdr->data, &idx ) == SGS_SUCCESS )
		goto success;

	ret = sgs_PushIndexP( C, &hdr->inh, &idx );
	if( ret == SGS_SUCCESS )
		goto success;

	return ret;

success:
	return SGS_SUCCESS;
}

static int sgsstd_class_setindex( SGS_CTX, sgs_VarObj* data, int prop )
{
	sgs_Variable k;
	SGSCLASS_HDR;
	if( strcmp( sgs_ToString( C, -2 ), "_super" ) == 0 )
	{
		sgs_Release( C, &hdr->inh );
		sgs_GetStackItem( C, -1, &hdr->inh );
		sgs_Acquire( C, &hdr->inh );
		return SGS_SUCCESS;
	}
	sgs_GetStackItem( C, -2, &k );
	return sgs_StoreIndexP( C, &hdr->data, &k );
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

SGS_DECLARE sgs_ObjCallback sgsstd_class_functable[ 19 ];

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
		sgsstd_class_header_t* hdr2;
		SGSCLASS_HDR;
		sgs_PushVariable( C, &hdr->data );
		sgs_CloneItem( C, -1 );

		hdr2 = (sgsstd_class_header_t*) sgs_PushObjectIPA( C,
			sizeof( sgsstd_class_header_t ), sgsstd_class_functable );
		sgs_GetStackItem( C, -1, &hdr2->data );
		hdr2->inh = hdr->inh;
		sgs_Acquire( C, &hdr2->data );
		sgs_Acquire( C, &hdr2->inh );
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
	int ret;
	SGSCLASS_HDR;
	ret = sgs_GCMark( C, &hdr->data );
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
		var.type = VTC_OBJECT;
		var.data.O = data;
		sgs_PushVariable( C, &var );
		for( i = 0; i < C->call_args; ++i )
			sgs_PushItem( C, i );
		sgs_GetStackItem( C, -2 - C->call_args, &fn );
		ret = sgsVM_VarCall( C, &fn, C->call_args, 0, C->call_expect, TRUE );
		return ret;
	}
	return SGS_ENOTFND;
}

static sgs_ObjCallback sgsstd_class_functable[ 19 ] =
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
	
	if( !sgs_LoadArgs( C, "?v?v." ) )
		return 0;
	
	hdr = (sgsstd_class_header_t*) sgs_PushObjectIPA( C,
		sizeof( sgsstd_class_header_t ), sgsstd_class_functable );
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

static int sgsstd_closure_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	SGSCLOSURE_HDR;
	UNUSED( unused );
	sgs_Release( C, &hdr->func );
	sgs_Release( C, &hdr->data );
	return SGS_SUCCESS;
}

static int sgsstd_closure_getprop( SGS_CTX, sgs_VarObj* data, int isprop )
{
	char* str;
	if( sgs_ParseString( C, 0, &str, NULL ) )
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
	if( sgs_StringMultiConcat( C, 4 ) || sgs_PadString( C ) )
		return SGS_EINPROC;
	sgs_PushString( C, "\n}" );
	return sgs_StringMultiConcat( C, 3 );
}

static int sgsstd_closure_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type != SVT_STRING && type != SGS_CONVOP_TOTYPE )
		return SGS_ENOTSUP;
	UNUSED( data );
	sgs_PushString( C, "closure" );
	return SGS_SUCCESS;
}

static int sgsstd_closure_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
{
	int ret;
	SGSCLOSURE_HDR;
	ret = sgs_GCMark( C, &hdr->func );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_GCMark( C, &hdr->data );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

static int sgsstd_closure_call( SGS_CTX, sgs_VarObj* data, int unused )
{
	int ismethod = sgs_Method( C );
	SGSCLOSURE_HDR;
	sgs_InsertVariable( C, -C->call_args - 1, &hdr->data );
	return sgsVM_VarCall( C, &hdr->func, C->call_args + 1,
		0, C->call_expect, ismethod ) * C->call_expect;
}

static sgs_ObjCallback sgsstd_closure_functable[] =
{
	SOP_DESTRUCT, sgsstd_closure_destruct,
	SOP_GETINDEX, sgsstd_closure_getprop,
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
	
	if( !sgs_LoadArgs( C, "?p?v." ) )
		return 0;
	
	hdr = (sgsstd_closure_t*) sgs_PushObjectIPA( C,
		sizeof( sgsstd_closure_t ), sgsstd_closure_functable );
	sgs_GetStackItem( C, 0, &hdr->func );
	sgs_GetStackItem( C, 1, &hdr->data );
	sgs_Acquire( C, &hdr->func );
	sgs_Acquire( C, &hdr->data );
	(C->stack_top-1)->type |= VTF_CALL;
	return 1;
}



/*
	real closure memory layout:
	- sgs_Variable: function
	- int32: closure count
	- sgs_Closure* x ^^: closures
*/

static int sgsstd_realclsr_destruct( SGS_CTX, sgs_VarObj* data, int unused )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, cc = *(int32_t*)(cl+sizeof(sgs_Variable));
	sgs_Closure** cls = (sgs_Closure**)(cl+sizeof(sgs_Variable)+sizeof(int32_t));
	UNUSED( unused );
	
	sgs_Release( C, (sgs_Variable*) cl );
	
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

static int sgsstd_realclsr_getprop( SGS_CTX, sgs_VarObj* data, int isprop )
{
	char* str;
	if( sgs_ParseString( C, 0, &str, NULL ) )
	{
		if( !strcmp( str, "thiscall" ) )
		{
			sgs_PushCFunction( C, sgs_thiscall_method );
			return SGS_SUCCESS;
		}
	}
	return SGS_ENOTFND;
}

static int sgsstd_realclsr_convert( SGS_CTX, sgs_VarObj* data, int type )
{
	if( type != SVT_STRING && type != SGS_CONVOP_TOTYPE )
		return SGS_ENOTSUP;
	UNUSED( data );
	sgs_PushString( C, "sys.closure" );
	return SGS_SUCCESS;
}

static int sgsstd_realclsr_call( SGS_CTX, sgs_VarObj* data, int unused )
{
	int ismethod = sgs_Method( C ), expected = C->call_expect;
	uint8_t* cl = (uint8_t*) data->data;
	int32_t cc = *(int32_t*)(cl+sizeof(sgs_Variable));
	sgs_Closure** cls = (sgs_Closure**)(cl+sizeof(sgs_Variable)+sizeof(int32_t));
	
	sgsVM_PushClosures( C, cls, cc );
	return sgsVM_VarCall( C, (sgs_Variable*) cl, C->call_args,
		cc, C->call_expect, ismethod ) * expected;
}

static int sgsstd_realclsr_gcmark( SGS_CTX, sgs_VarObj* data, int unused )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, cc = *(int32_t*)(cl+sizeof(sgs_Variable));
	sgs_Closure** cls = (sgs_Closure**)(cl+sizeof(sgs_Variable)+sizeof(int32_t));
	
	sgs_GCMark( C, (sgs_Variable*) cl );
	
	for( i = 0; i < cc; ++i )
	{
		sgs_GCMark( C, &cls[ i ]->var );
	}
	
	return SGS_SUCCESS;
}

static int sgsstd_realclsr_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, ssz, cc = *(int32_t*)(cl+sizeof(sgs_Variable));
	sgs_Closure** cls = (sgs_Closure**)(cl+sizeof(sgs_Variable)+sizeof(int32_t));

	sgs_PushString( C, "sys.closure\n{" );

	ssz = sgs_StackSize( C );
	sgs_PushString( C, "\nfunc: " );
	sgs_PushVariable( C, (sgs_Variable*) cl ); /* function */
	if( sgs_DumpVar( C, depth ) )
	{
		sgs_Pop( C, 1 );
		sgs_PushString( C, "<error>" );
	}
	for( i = 0; i < cc; ++i )
	{
		char intro[ 64 ];
		sprintf( intro, "\n#%d (rc=%d): ", i, cls[ i ]->refcount );
		sgs_PushString( C, intro );
		sgs_PushVariable( C, &cls[ i ]->var );
		if( sgs_DumpVar( C, depth ) )
		{
			sgs_Pop( C, 1 );
			sgs_PushString( C, "<error>" );
		}
	}
	if( sgs_StringMultiConcat( C, sgs_StackSize( C ) - ssz ) || sgs_PadString( C ) )
		return SGS_EINPROC;

	sgs_PushString( C, "\n}" );
	return sgs_StringMultiConcat( C, 3 );
}

static sgs_ObjCallback sgsstd_realclsr_functable[] =
{
	SOP_DESTRUCT, sgsstd_realclsr_destruct,
	SOP_CALL, sgsstd_realclsr_call,
	SOP_GETINDEX, sgsstd_realclsr_getprop,
	SOP_CONVERT, sgsstd_realclsr_convert,
	SOP_GCMARK, sgsstd_realclsr_gcmark,
	SOP_DUMP, sgsstd_realclsr_dump,
	SOP_END,
};

int sgsSTD_MakeClosure( SGS_CTX, sgs_Variable* func, int32_t clc )
{
	int i, clsz = sizeof(sgs_Closure*) * clc;
	int memsz = clsz + sizeof(sgs_Variable) + sizeof(int32_t);
	uint8_t* cl = (uint8_t*) sgs_PushObjectIPA( C, memsz, sgsstd_realclsr_functable );
	
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
			sgs_StoreIndex( C, -4, -2 );
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
		sgs_StoreIndex( C, 0, -2 );
		sgs_Pop( C, 1 );
	}
	
	sgs_SetStackSize( C, 1 );
	return 1;
}


static int sgsstd_isset( SGS_CTX )
{
	int oml, ret;
	char* str;
	sgs_SizeVal size;
	sgs_Variable var;
	
	SGSFN( "isset" );
	
	if( !sgs_LoadArgs( C, "vm.", &var, &str, &size ) )
		return 0;

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
	DictHdr* dh;
	
	SGSFN( "unset" );
	
	if( !sgs_LoadArgs( C, "?tm.", &str, &size ) )
		return 0;
	
	dh = (DictHdr*) sgs_GetObjectData( C, 0 );

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
	vht_unsetS( &dh->ht, (C->stack_off+1)->data.S, C );

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
	int i, ssz = sgs_StackSize( C );
	SGSFN( "get_concat" );
	if( ssz < 2 )
	{
		return sgs_Printf( C, SGS_WARNING,
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
	int i, ssz = sgs_StackSize( C );
	SGSFN( "get_merged" );
	if( ssz < 2 )
	{
		return sgs_Printf( C, SGS_WARNING,
			"function expects at least 2 arguments, got %d",
			sgs_StackSize( C ) );
	}

	sgs_PushDict( C, 0 );
	for( i = 0; i < ssz; ++i )
	{
		if( sgs_PushIterator( C, i ) != SGS_SUCCESS )
			return sgs_ArgErrorExt( C, i, 0, "iterable", "" );
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


static int sgsstd_tobool( SGS_CTX )
{
	SGSFN( "tobool" );
	sgs_PushBool( C, sgs_GetBool( C, 0 ) );
	return 1;
}

static int sgsstd_toint( SGS_CTX )
{
	SGSFN( "toint" );
	sgs_PushInt( C, sgs_GetInt( C, 0 ) );
	return 1;
}

static int sgsstd_toreal( SGS_CTX )
{
	SGSFN( "toreal" );
	sgs_PushReal( C, sgs_GetReal( C, 0 ) );
	return 1;
}

static int sgsstd_tostring( SGS_CTX )
{
	char* str;
	sgs_SizeVal sz;
	SGSFN( "tostring" );
	str = sgs_ToStringBuf( C, 0, &sz );
	if( str )
		sgs_PushStringBuf( C, str, sz );
	else
		sgs_PushStringBuf( C, "", 0 );
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

static int sgsstd_typeflags( SGS_CTX )
{
	SGSFN( "typeflags" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;
	sgs_PushInt( C, sgs_ItemTypeExt( C, 0 ) );
	return 1;
}

static int sgsstd_is_numeric( SGS_CTX )
{
	int res, ty = sgs_ItemTypeExt( C, 0 );
	
	SGSFN( "is_numeric" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;

	if( ty == VTC_NULL || ( ty & (VTF_CALL|SVT_OBJECT) ) )
		res = FALSE;
	else
		res = ty != VTC_STRING || sgs_IsNumericString(
			sgs_GetStringPtr( C, 0 ), sgs_GetStringSize( C, 0 ) );

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_is_callable( SGS_CTX )
{
	SGSFN( "is_callable" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;
	
	sgs_PushBool( C, sgs_IsCallable( C, -1 ) );
	return 1;
}

static int sgsstd_loadtypeflags( SGS_CTX )
{
	static const sgs_RegIntConst tycs[] =
	{
		{ "VTF_NUM", VTF_NUM },
		{ "VTF_CALL", VTF_CALL },
		{ "VTF_REF", VTF_REF },
		{ "VTF_ARRAY", VTF_ARRAY },
		{ "VTF_ARRAY_ITER", VTF_ARRAY_ITER },
		{ "VTF_DICT", VTF_DICT },
		
		{ "VTC_NULL", VTC_NULL },
		{ "VTC_BOOL", VTC_BOOL },
		{ "VTC_INT", VTC_INT },
		{ "VTC_REAL", VTC_REAL },
		{ "VTC_STRING", VTC_STRING },
		{ "VTC_FUNC", VTC_FUNC },
		{ "VTC_CFUNC", VTC_CFUNC },
		{ "VTC_OBJECT", VTC_OBJECT },
		{ "VTC_ARRAY", VTC_ARRAY },
		{ "VTC_ARRAY_ITER", VTC_ARRAY_ITER },
		{ "VTC_DICT", VTC_DICT },
	};
	
	SGSFN( "loadtypeflags" );
	if( !sgs_LoadArgs( C, "." ) )
		return 0;
	
	sgs_RegIntConsts( C, tycs, sizeof(tycs)/sizeof(tycs[0]) );
	return 0;
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
			sgs_Write( C, buf, bsz );
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
		sgs_Write( C, buf, bsz );
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
		int len = strlen( bfr );
		membuf_appbuf( &B, C, bfr, len );
		if( len && !all && bfr[ len - 1 ] == '\n' )
		{
			B.size--;
			break;
		}
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

static int sgsstd_rand( SGS_CTX )
{
	sgs_PushInt( C, rand() );
	return 1;
}

static int sgsstd_randf( SGS_CTX )
{
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
	sgs_PrintFunc pfn;
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
	else if( P->handler.type != VTC_NULL )
	{
		sgs_PushInt( C, type );
		sgs_PushString( C, message );
		sgs_PushVariable( C, &P->handler );
		if( sgs_Call( C, 2, 1 ) )
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
	
	P.pfn = C->print_fn;
	P.pctx = C->print_ctx;
	P.handler.type = VTC_NULL;
	P.depth = 0;
	if( b )
		sgs_GetStackItem( C, 1, &P.handler );
	
	C->print_fn = sgsstd_pcall_print;
	C->print_ctx = &P;
	
	sgs_PushItem( C, 0 );
	sgs_Call( C, 0, 0 );
	
	C->print_fn = P.pfn;
	C->print_ctx = P.pctx;
	
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
		sgs_Printf( C, SGS_ERROR, !str ? "assertion failed" :
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

	sgs_EvalBuffer( C, str, (int) size, &rvc );
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
	sgs_SizeVal size = 0, outsize = 0;
	sgs_Variable var;
	
	sgs_PrintFunc oldpfn;
	void* oldpctx;
	
	SGSFN( "compile_sgs" );
	
	if( !sgs_LoadArgs( C, "m", &buf, &size ) )
		return 0;
	
	sgs_PushArray( C, 0 );
	sgs_GetStackItem( C, -1, &var );
	sgs_Acquire( C, &var );
	sgs_Pop( C, 1 );
	
	oldpfn = C->print_fn;
	oldpctx = C->print_ctx;
	sgs_SetPrintFunc( C, _sgsstd_compile_pfn, &var );
	SGSFN( NULL );
	
	ret = sgs_Compile( C, buf, size, &outbuf, &outsize );
	
	SGSFN( "compile_sgs" );
	C->print_fn = oldpfn;
	C->print_ctx = oldpctx;
	
	if( ret < 0 )
		sgs_PushNull( C );
	else
	{
		sgs_PushStringBuf( C, outbuf, outsize );
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
		return sgs_Printf( C, SGS_WARNING, "file '%s' was not found", str );
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

	ret = sgs_GetProcAddress( fnstr, SGS_LIB_ENTRY_POINT, (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE )
			return sgs_Printf( C, SGS_WARNING, "file '%s' was not found", fnstr );
		else if( ret == SGS_XPC_NOPROC )
			return sgs_Printf( C, SGS_WARNING, "procedure '" SGS_LIB_ENTRY_POINT "' was not found" );
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
			sgs_PushStringBuf( C, file, fend - file );
		return 1;
	}
	
	return 0;
}

static int _find_includable_file( SGS_CTX, MemBuf* tmp, char* ps,
	sgs_SizeVal pssize, char* fn, sgs_SizeVal fnsize, char* dn, sgs_SizeVal dnsize )
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
			pssize = strlen( ps );
		}
		
		if( _push_curdir( C ) )
		{
			dnstr = sgs_GetStringPtr( C, -1 );
			dnsize = sgs_GetStringSize( C, -1 );
		}
		ret = _find_includable_file( C, &mb, ps, pssize, fnstr, fnsize, dnstr, dnsize );
		if( ret == 0 || mb.size == 0 )
		{
			membuf_destroy( &mb, C );
			return sgs_Printf( C, SGS_WARNING, "could not find '%.*s' "
				"with include path '%.*s'", fnsize, fnstr, pssize, ps );
		}
		
		ret = sgs_GetProcAddress( mb.ptr, SGS_LIB_ENTRY_POINT, (void**) &func );
		if( ret == SGS_SUCCESS )
		{
			ret = func( C );
			if( ret == SGS_SUCCESS )
			{
				membuf_destroy( &mb, C );
				goto success;
			}
		}
		
		sgs_PushString( C, mb.ptr );
		sgs_PushString( C, " - include" );
		sgs_StringMultiConcat( C, 2 );
		SGSFN( sgs_GetStringPtr( C, -1 ) );
		ret = sgs_ExecFile( C, mb.ptr );
		SGSFN( "include" );
		membuf_destroy( &mb, C );
		if( ret == SGS_SUCCESS )
			goto success;
	}
	
	sgs_Printf( C, SGS_WARNING, "could not load '%.*s'", fnsize, fnstr );
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

	ret = sgs_GetProcAddress( fnstr, pnstr, (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE )
			return sgs_Printf( C, SGS_WARNING, "file '%s' was not found", fnstr );
		else if( ret == SGS_XPC_NOPROC )
			return sgs_Printf( C, SGS_WARNING, "procedure '%s' was not found", pnstr );
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

	sgs_Printf( C, (int) errcode, errmsg );
	return 0;
}

static int sgsstd__printwrapper( SGS_CTX, const char* fn, int code )
{
	char* msg;
	SGSFN( fn );
	if( sgs_LoadArgs( C, "s", &msg ) )
	{
		SGSFN( NULL );
		sgs_Printf( C, code, msg );
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
	
	sgs_Printf( C, SGS_ERROR, "this errno value is unsupported" );
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
		if( sgs_StringMultiConcat( C, rc ) != SGS_SUCCESS )
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
	int32_t orvc = sgs_Stat( C, SGS_STAT_OBJCOUNT );
	
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
	int ty = sgs_ItemTypeExt( C, argid );
	if( ty != VTC_ARRAY && ty != VTC_DICT )
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
		if( sgs_ItemTypeExt( C, 1 ) == VTC_ARRAY )
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
	FN( array ), FN( dict ), { "class", sgsstd_class }, FN( closure ),
	FN( array_filter ), FN( array_process ),
	FN( dict_filter ), FN( dict_process ),
	FN( isset ), FN( unset ), FN( clone ),
	FN( get_keys ), FN( get_values ), FN( get_concat ), FN( get_merged ),
	/* types */
	FN( tobool ), FN( toint ), FN( toreal ), FN( tostring ), FN( typeof ),
	FN( typeid ), FN( typeflags ), FN( is_numeric ), FN( is_callable ),
	FN( loadtypeflags ),
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
	
	ret = sgs_RegisterType( C, "array", sgsstd_array_functable );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "array_iterator", sgsstd_array_iter_functable );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "dict", sgsstd_dict_functable );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "dict_iterator", sgsstd_dict_iter_functable );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "class", sgsstd_class_functable );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_RegisterType( C, "closure", sgsstd_closure_functable );
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
		sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) sgs_PushObjectIPA( C,
			sizeof( sgsstd_array_header_t ), sgsstd_array_functable );

		hdr->size = cnt;
		hdr->mem = cnt;
		p = hdr->data = (sgs_Variable*) data;
		pend = p + cnt;
		while( p < pend )
		{
			sgs_GetStackItem( C, i++ - cnt - 1, p );
			sgs_Acquire( C, p++ );
		}

		sgs_PopSkip( C, cnt, 1 );
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
		sgs_Variable val;
		if( !sgs_ParseString( C, i - cnt - 1, NULL, NULL ) )
		{
			sgs_Pop( C, sgs_StackSize( C ) - ssz );
			return SGS_EINVAL;
		}

		sgs_GetStackItem( C, i - cnt, &val );
		vht_setS( ht, (C->stack_top+i-cnt-1)->data.S, &val, C );
	}

	sgs_PopSkip( C, cnt, 1 );
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

	var.type = VTC_DICT;
	var.data.O = GLBP;
	sgs_Release( C, &var );

	C->data = NULL;

	return SGS_SUCCESS;
}

extern void sgsVM_ReleaseStack( SGS_CTX, sgs_Variable* var );
int sgsSTD_GlobalGet( SGS_CTX, sgs_Variable* out, sgs_Variable* idx, int apicall )
{
	VHTableVar* pair;
	sgs_VarObj* data = GLBP;
	HTHDR;

	if( idx->type != VTC_STRING )
	{
		sgsVM_ReleaseStack( C, out );
		return SGS_ENOTSUP;
	}

	if( strcmp( str_cstr( idx->data.S ), "_G" ) == 0 )
	{
		sgsVM_ReleaseStack( C, out );
		out->type = VTC_DICT;
		out->data.O = GLBP;
		return SGS_SUCCESS;
	}
	else if( ( pair = vht_getS( ht, idx->data.S ) ) != NULL )
	{
		sgsVM_ReleaseStack( C, out );
		*out = pair->var;
		return SGS_SUCCESS;
	}
	else
	{
		sgsVM_ReleaseStack( C, out );
		if( !apicall )
			sgs_Printf( C, SGS_WARNING, "variable '%s' was not found",
				str_cstr( idx->data.S ) );
		out->type = VTC_NULL;
		return SGS_ENOTFND;
	}
}

int sgsSTD_GlobalSet( SGS_CTX, sgs_Variable* idx, sgs_Variable* val, int apicall )
{
	sgs_VarObj* data = GLBP;
	HTHDR;

	if( idx->type != VTC_STRING )
		return SGS_ENOTSUP;

	if( strcmp( var_cstr( idx ), "_G" ) == 0 )
	{
		sgs_Variable tmp;
		
		if( val->type != VTC_DICT )
		{
			if( !apicall )
				sgs_Printf( C, SGS_ERROR, "_G only accepts 'dict' values" );
			return SGS_ENOTSUP;
		}
		
		sgs_Acquire( C, val );
		GLBP = val->data.O;
		
		tmp.type = VTC_DICT;
		tmp.data.O = data;
		sgs_Release( C, &tmp );
		return SGS_SUCCESS;
	}

	vht_setS( ht, idx->data.S, val, C );
	return SGS_SUCCESS;
}

int sgsSTD_GlobalGC( SGS_CTX )
{
	sgs_VarObj* data = GLBP;

	if( data )
	{
		sgs_Variable tmp;

		tmp.type = VTC_DICT;
		tmp.data.O = data;
		if( sgs_GCMark( C, &tmp ) < 0 )
			return SGS_EINPROC;
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
	if( searchpath )
	{
		pathrep = sgs_PushGlobal( C, "SGS_PATH" ) == SGS_SUCCESS ? 1 : 2;
		sgs_PushString( C, searchpath );
		sgs_StoreGlobal( C, "SGS_PATH" );
	}
	
	sz = sgs_StackSize( C );
	sgs_PushString( C, name );
	sgs_PushCFunction( C, sgsstd_include );
	ret = sgs_Call( C, 1, 1 );
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

SGSRESULT sgs_ObjectAction( SGS_CTX, int item, int act, int arg )
{
	int i, j;
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
	
	case SGS_ACT_DICT_UNSET:
		if( sgs_ItemTypeExt( C, item ) != VTC_DICT ||
			sgs_ItemType( C, arg ) != SVT_STRING )
			return SGS_EINVAL;
		else
		{
			sgs_Variable str;
			sgs_GetStackItem( C, arg, &str );
			vht_unsetS(
				&((DictHdr*)sgs_GetObjectData( C, item ))->ht,
				str.data.S, C );
			return SGS_SUCCESS;
		}
		
	}
	return SGS_ENOTFND;
}

