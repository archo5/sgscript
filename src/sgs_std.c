

#include <stdio.h>
#include <errno.h>

#include "sgs_int.h"

#ifndef STDLIB_WARN
#  define STDLIB_WARN( warn ) return sgs_Msg( C, SGS_WARNING, warn );
#endif
#ifndef STDLIB_ERR
#  define STDLIB_ERR( err ) return sgs_Msg( C, SGS_ERROR, err );
#endif


static int sgsstd_expectnum( SGS_CTX, sgs_StkIdx n )
{
	sgs_StkIdx ssz = sgs_StackSize( C );
	if( n != ssz )
		return sgs_Msg( C, SGS_WARNING, "function expects exactly %d arguments"
			", %d given", n, ssz );
	return 1;
}


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
	sgs_ReleaseArray( C, SGSARR_PTR( hdr ), hdr->size );
	hdr->size = 0;
}

/* off = offset in stack to start inserting from */
static void sgsstd_array_insert( SGS_CTX, sgsstd_array_header_t* hdr, sgs_SizeVal pos, sgs_StkIdx off )
{
	int i;
	sgs_StkIdx cnt = sgs_StackSize( C ) - off;
	sgs_SizeVal nsz = hdr->size + cnt;
	sgs_Variable* ptr = SGSARR_PTR( hdr );
	
	if( !cnt ) return;
	
	if( nsz > hdr->mem )
	{
		sgsstd_array_reserve( C, hdr, SGS_MAX( nsz, hdr->mem * 2 ) );
		ptr = SGSARR_PTR( hdr );
	}
	if( pos < hdr->size )
		memmove( ptr + pos + cnt, ptr + pos, SGSARR_ALLOCSIZE( hdr->size - pos ) );
	for( i = off; i < sgs_StackSize( C ); ++i )
	{
		sgs_Variable* var = ptr + pos + i - off;
		sgs_GetStackItem( C, i, var );
	}
	hdr->size = nsz;
}

static void sgsstd_array_insert_p( SGS_CTX, sgsstd_array_header_t* hdr, sgs_SizeVal pos, sgs_Variable* var )
{
	sgs_SizeVal nsz = hdr->size + 1;
	sgs_Variable* ptr = SGSARR_PTR( hdr );
	
	if( nsz > hdr->mem )
	{
		sgsstd_array_reserve( C, hdr, SGS_MAX( nsz, hdr->mem * 2 ) );
		ptr = SGSARR_PTR( hdr );
	}
	if( pos < hdr->size )
		memmove( ptr + pos + 1, ptr + pos, SGSARR_ALLOCSIZE( hdr->size - pos ) );
	
	ptr[ pos ] = *var;
	sgs_Acquire( C, var );
	
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
	SGS_UNUSED( hdr );


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
		from = SGS_MAX( from, 0 );
		to = SGS_MIN( to, hdr->size );
		
		if( from < to )
		{
			sgs_Variable *psrc, *pdst;
			sgs_SizeVal cnt = to - from;
			sgsstd_array_header_t* nhdr = (sgsstd_array_header_t*) sgs_GetObjectData( C, -1 );
			sgsstd_array_reserve( C, nhdr, to - from );
			nhdr->size = cnt;
			psrc = SGSARR_PTR( hdr ) + from;
			pdst = SGSARR_PTR( nhdr );
			for( i = 0; i < cnt; ++i )
			{
				pdst[ i ] = psrc[ i ];
				sgs_Acquire( C, pdst + i );
			}
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
		( SGSARR_PTR( hdr ) + (hdr->size++) )->type = SGS_VT_NULL;
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
	sgs_quicksort( SGSARR_PTR( hdr ), (size_t) hdr->size, sizeof( sgs_Variable ),
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
	u.sortfunc.type = SGS_VT_NULL;
	
	if( !sgs_LoadArgs( C, "?p<v|b", &u.sortfunc, &rev ) )
		return 0;
	
	/* WP: array limit */
	sgs_quicksort( SGSARR_PTR( hdr ), (size_t) hdr->size,
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
			if( SGS_FAILED( sgs_PushNumIndex( C, 0, i ) ) )
			{
				sgs_Dealloc( smis );
				STDLIB_WARN( "error in mapping array" )
			}
			smis[ i ].value = sgs_GetReal( C, -1 );
			smis[ i ].pos = i;
			sgs_Pop( C, 1 );
		}
		sgs_quicksort( smis, (size_t) asize, sizeof( sgsarr_smi ),
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
	int strict = SGS_FALSE;
	sgs_SizeVal off = 0;
	
	SGSARR_IHDR( find );
	if( !sgs_LoadArgs( C, "v|bl", &comp, &strict, &off ) )
		return 0;
	
	while( off < hdr->size )
	{
		sgs_Variable* p = SGSARR_PTR( hdr ) + off;
		if( ( !strict || sgs_EqualTypes( p, &comp ) )
			&& sgs_Compare( C, p, &comp ) == 0 )
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
	int strict = SGS_FALSE, all = SGS_FALSE, rmvd = 0;
	sgs_SizeVal off = 0;
	
	SGSARR_IHDR( remove );
	if( !sgs_LoadArgs( C, "v|bbl", &comp, &strict, &all, &off ) )
		return 0;
	
	while( off < hdr->size )
	{
		sgs_Variable* p = SGSARR_PTR( hdr ) + off;
		if( ( !strict || sgs_EqualTypes( p, &comp ) )
			&& sgs_Compare( C, p, &comp ) == 0 )
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

static int _in_array( SGS_CTX, void* data, sgs_Variable* var, int strconv )
{
	sgs_SizeVal off = 0;
	SGSARR_HDR;
	
	if( !strconv )
	{
		while( off < hdr->size )
		{
			sgs_Variable* cur = SGSARR_PTR( hdr ) + off;
			if( sgs_EqualTypes( var, cur ) && sgs_Compare( C, var, cur ) == 0 )
				return SGS_TRUE;
			off++;
		}
		return SGS_FALSE;
	}
	else
	{
		int found = 0;
		sgs_Variable A, B;
		
		A = *var;
		sgs_Acquire( C, &A );
		sgs_ToStringP( C, &A );
		while( off < hdr->size )
		{
			B = SGSARR_PTR( hdr )[ off ];
			sgs_Acquire( C, &B );
			sgs_ToStringP( C, &B );
			
			found = sgs_EqualTypes( &A, &B ) && sgs_Compare( C, &A, &B ) == 0;
			sgs_Release( C, &B );
			if( found )
				break;
			off++;
		}
		sgs_Release( C, &A );
		return found;
	}
}

static int sgsstd_arrayI_unique( SGS_CTX )
{
	int strconv = SGS_FALSE;
	sgs_SizeVal off = 0, asz = 0;
	void* nadata;
	
	SGSARR_IHDR( unique );
	if( !sgs_LoadArgs( C, "|b", &strconv ) )
		return 0;
	
	sgs_PushArray( C, 0 );
	nadata = sgs_GetObjectData( C, -1 );
	while( off < hdr->size )
	{
		sgs_Variable* var = SGSARR_PTR( hdr ) + off;
		if( !_in_array( C, nadata, var, strconv ) )
		{
			sgsstd_array_insert_p( C, (sgsstd_array_header_t*) nadata, asz, var );
			asz++;
		}
		off++;
	}
	
	return 1;
}

static int sgsstd_arrayI_random( SGS_CTX )
{
	sgs_SizeVal num, asz = 0;
	sgsstd_array_header_t* nadata;
	
	SGSARR_IHDR( random );
	if( !sgs_LoadArgs( C, "l", &num ) )
		return 0;
	
	if( num < 0 )
		STDLIB_WARN( "argument 1 (count) cannot be negative" )
	
	sgs_PushArray( C, 0 );
	nadata = (sgsstd_array_header_t*) sgs_GetObjectData( C, -1 );
	sgsstd_array_reserve( C, nadata, num );
	while( num-- )
	{
		sgsstd_array_insert_p( C, nadata, asz, SGSARR_PTR( hdr ) + ( rand() % hdr->size ) );
		asz++;
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
	
	SGS_RETURN_THIS( C );
}


static int sgsstd_array( SGS_CTX );
static sgs_RegFuncConst array_iface_fconsts[] =
{
	{ "push", sgsstd_arrayI_push },
	{ "pop", sgsstd_arrayI_pop },
	{ "shift", sgsstd_arrayI_shift },
	{ "unshift", sgsstd_arrayI_unshift },
	{ "insert", sgsstd_arrayI_insert },
	{ "erase", sgsstd_arrayI_erase },
	{ "part", sgsstd_arrayI_part },
	{ "clear", sgsstd_arrayI_clear },
	{ "reverse", sgsstd_arrayI_reverse },
	{ "resize", sgsstd_arrayI_resize },
	{ "reserve", sgsstd_arrayI_reserve },
	{ "sort", sgsstd_arrayI_sort },
	{ "sort_custom", sgsstd_arrayI_sort_custom },
	{ "sort_mapped", sgsstd_arrayI_sort_mapped },
	{ "find", sgsstd_arrayI_find },
	{ "remove", sgsstd_arrayI_remove },
	{ "unique", sgsstd_arrayI_unique },
	{ "random", sgsstd_arrayI_random },
	{ "shuffle", sgsstd_arrayI_shuffle },
	{ "__call", sgsstd_array },
};

static int sgsstd_array_iface_gen( SGS_CTX )
{
	sgs_PushDict( C, 0 );
	sgs_StoreFuncConsts( C, sgs_StackSize( C ) - 1, array_iface_fconsts, SGS_ARRAY_SIZE(array_iface_fconsts) );
	sgs_ObjSetMetaMethodEnable( sgs_GetObjectStruct( C, -1 ), 1 );
	return 1;
}

static int sgsstd_array_getprop( SGS_CTX, void* data, sgs_Variable* key )
{
	char* name;
	SGSARR_HDR;
	if( sgs_ParseStringP( C, key, &name, NULL ) )
	{
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
	return sgs_GCMarkArray( C, SGSARR_PTR( hdr ), hdr->size );
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

SGS_APIFUNC sgs_ObjInterface sgsstd_array_iter_iface[1] =
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
		
		sgs_InitObjectPtr( &iter->ref, data ); /* acquires ref */
		iter->size = hdr->size;
		iter->off = -1;
		
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_BOOL )
	{
		sgs_PushBool( C, !!hdr->size );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
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
		
		sgs_PushInterface( C, sgsstd_array_iface_gen );
		sgs_ObjSetMetaObj( C, sgs_GetObjectStruct( C, -2 ), sgs_GetObjectStruct( C, -1 ) );
		sgs_Pop( C, 1 );
		
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

SGS_APIFUNC sgs_ObjInterface sgsstd_array_iface[1] =
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
		sgs_GetStackItem( C, i++, p++ );
	
	sgs_PushInterface( C, sgsstd_array_iface_gen );
	sgs_ObjSetMetaObj( C, sgs_GetObjectStruct( C, -2 ), sgs_GetObjectStruct( C, -1 ) );
	sgs_Pop( C, 1 );
	
	return 1;
}


/*
	VHT containers
*/

typedef
struct _DictHdr
{
	sgs_VHTable ht;
}
DictHdr;

#define HTHDR DictHdr* dh = (DictHdr*) data->data; sgs_VHTable* ht = &dh->ht;

static int sgsstd_vht_serialize( SGS_CTX, sgs_VarObj* data, const char* initfn )
{
	int ret;
	sgs_VHTVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + sgs_vht_size( ht );
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
	return sgs_SerializeObject( C, sgs_vht_size( ht ) * 2, initfn );
}

static int sgsstd_vht_dump( SGS_CTX, sgs_VarObj* data, int depth, const char* name )
{
	int ssz;
	char bfr[ 32 ];
	sgs_VHTVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + sgs_vht_size( ht );
	ssz = sgs_StackSize( C );
	sprintf( bfr, "%s (%"PRId32")\n{", name, sgs_vht_size( ht ) );
	sgs_PushString( C, bfr );
	if( depth )
	{
		if( sgs_vht_size( ht ) )
		{
			while( pair < pend )
			{
				sgs_PushString( C, "\n" );
				sgs_PushVariable( C, &pair->key );
				if( pair->key.type == SGS_VT_STRING )
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
			if( sgs_StringConcat( C, (sgs_StkIdx) ( pend - ht->vars ) * 4 ) || sgs_PadString( C ) )
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
	sgs_ObjInterface* iface = SGSIFACE_DICT;
	SGS_IF_DLL( ;,
	if( !iface )
		iface = sgsstd_dict_iface;
	)
	DictHdr* dh = (DictHdr*) ( out
		? sgs_InitObjectIPA( C, out, sizeof( DictHdr ), iface )
		: sgs_PushObjectIPA( C, sizeof( DictHdr ), iface ) );
	sgs_vht_init( &dh->ht, C, 4, 4 );
	return dh;
}


static int sgsstd_dict_destruct( SGS_CTX, sgs_VarObj* data )
{
	HTHDR;
	sgs_vht_free( ht, C );
	return SGS_SUCCESS;
}

static int sgsstd_dict_gcmark( SGS_CTX, sgs_VarObj* data )
{
	sgs_VHTVar *pair, *pend;
	HTHDR;
	pair = ht->vars;
	pend = ht->vars + sgs_vht_size( ht );
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

/* ref'd in sgs_proc.c */ int sgsstd_dict_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	sgs_VHTVar* pair = NULL;
	HTHDR;
	
	if( prop && key->type == SGS_VT_INT )
	{
		int32_t off = (int32_t) key->data.I;
		if( off < 0 || off > sgs_vht_size( ht ) )
			return SGS_EBOUNDS;
		sgs_PushVariable( C, &ht->vars[ off ].val );
		return SGS_SUCCESS;
	}
	
	if( sgs_ParseStringP( C, key, NULL, NULL ) )
	{
		pair = sgs_vht_get( ht, key );
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
		sgs_vht_set( ht, C, key, val );
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
	sgs_VHTable* ht = (sgs_VHTable*) iter->ref.data.O->data;
	if( iter->size != sgs_vht_size( ht ) )
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

SGS_APIFUNC sgs_ObjInterface sgsstd_dict_iter_iface[1] =
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
		
		sgs_InitObjectPtr( &iter->ref, data ); /* acquires ref */
		iter->size = sgs_vht_size( ht );
		iter->off = -1;
		
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_BOOL )
	{
		sgs_PushBool( C, sgs_vht_size( ht ) != 0 );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
	{
		sgs_VHTVar *pair = ht->vars, *pend = ht->vars + sgs_vht_size( ht );
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
		int i, htsize = sgs_vht_size( ht );
		DictHdr* ndh = mkdict( C, NULL );
		for( i = 0; i < htsize; ++i )
		{
			sgs_vht_set( &ndh->ht, C, &ht->vars[ i ].key, &ht->vars[ i ].val );
		}
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_dict_serialize( SGS_CTX, sgs_VarObj* data )
{
	return sgsstd_vht_serialize( C, data, "dict" );
}

SGS_APIFUNC sgs_ObjInterface sgsstd_dict_iface[1] =
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
	sgs_VHTable* ht;
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
			return sgs_FuncArgError( C, i, SGS_VT_STRING, 0 );
		
		if( !sgs_PeekStackItem( C, i + 1, &val ) )
			STDLIB_ERR( "Internal error, stack state changed while building dict" )
		sgs_vht_set( ht, C, (C->stack_off+i), &val );
	}
	
	return 1;
}


/* MAP */

static DictHdr* mkmap( SGS_CTX, sgs_Variable* out )
{
	DictHdr* dh = (DictHdr*) ( out
		? sgs_InitObjectIPA( C, out, sizeof( DictHdr ), SGSIFACE_MAP )
		: sgs_PushObjectIPA( C, sizeof( DictHdr ), SGSIFACE_MAP ) );
	sgs_vht_init( &dh->ht, C, 4, 4 );
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
		
		sgs_InitObjectPtr( &iter->ref, data ); /* acquires ref */
		iter->size = sgs_vht_size( ht );
		iter->off = -1;
		
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_BOOL )
	{
		sgs_PushBool( C, sgs_vht_size( ht ) != 0 );
		return SGS_SUCCESS;
	}
	else if( type == SGS_VT_STRING )
	{
		sgs_VHTVar *pair = ht->vars, *pend = ht->vars + sgs_vht_size( ht );
		int cnt = 0;
		sgs_PushString( C, "[map]{" );
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
		int i, htsize = sgs_vht_size( ht );
		DictHdr* ndh = mkmap( C, NULL );
		for( i = 0; i < htsize; ++i )
		{
			sgs_vht_set( &ndh->ht, C, &ht->vars[ i ].key, &ht->vars[ i ].val );
		}
		return SGS_SUCCESS;
	}
	return SGS_ENOTSUP;
}

static int sgsstd_map_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int prop )
{
	sgs_VHTVar* pair = NULL;
	HTHDR;
	
	pair = sgs_vht_get( ht, key );
	if( !pair )
		return SGS_ENOTFND;
	
	sgs_PushVariable( C, &pair->val );
	return SGS_SUCCESS;
}

static int sgsstd_map_setindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, sgs_Variable* val, int prop )
{
	HTHDR;
	sgs_vht_set( ht, C, key, val );
	return SGS_SUCCESS;
}

SGS_APIFUNC sgs_ObjInterface sgsstd_map_iface[1] =
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
	sgs_VHTable* ht;
	int i, objcnt = sgs_StackSize( C );
	
	SGSFN( "map" );
	
	if( objcnt % 2 != 0 )
		STDLIB_WARN( "function expects 0 or an even number of arguments" )
	
	dh = mkmap( C, NULL );
	ht = &dh->ht;
	
	for( i = 0; i < objcnt; i += 2 )
	{
		sgs_vht_set( ht, C, C->stack_off+i, C->stack_off+i+1 );
	}
	
	return 1;
}


static int sgsstd_class( SGS_CTX )
{
	sgs_VarObj *obj1, *obj2;
	SGSFN( "class" );
	if( !sgs_LoadArgs( C, "!x!x", sgs_ArgCheck_Object, &obj1, sgs_ArgCheck_Object, &obj2 ) )
		return 0;
	sgs_ObjSetMetaObj( C, obj1, obj2 );
	sgs_ObjSetMetaMethodEnable( obj1, SGS_TRUE );
	sgs_SetStackSize( C, 1 );
	return 1;
}


/*
	closure memory layout:
	- sgs_Variable: function
	- int32: closure count
	- sgs_Closure* x ^^: closures
*/

static int sgsstd_closure_destruct( SGS_CTX, sgs_VarObj* data )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, cc = *(int32_t*) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));
	
	sgs_Release( C, (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( cl, sizeof(void*) ) );
	
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

static int sgsstd_closure_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* key, int isprop )
{
	char* str;
	if( sgs_ParseStringP( C, key, &str, NULL ) )
	{
		if( !strcmp( str, "call" ) )
		{
			sgs_PushCFunction( C, sgs_specfn_call );
			return SGS_SUCCESS;
		}
		if( !strcmp( str, "apply" ) )
		{
			sgs_PushCFunction( C, sgs_specfn_apply );
			return SGS_SUCCESS;
		}
	}
	return SGS_ENOTFND;
}

static int sgsstd_closure_call( SGS_CTX, sgs_VarObj* data )
{
	int ismethod = sgs_Method( C ), expected = C->sf_last->expected;
	uint8_t* cl = (uint8_t*) data->data;
	int32_t cc = *(int32_t*) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));
	
	sgsVM_PushClosures( C, cls, cc );
	return sgsVM_VarCall( C, (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( cl, sizeof(void*) ), C->sf_last->argcount,
		cc, C->sf_last->expected, ismethod ) * expected;
}

static int sgsstd_closure_gcmark( SGS_CTX, sgs_VarObj* data )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, cc = *(int32_t*) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));
	
	sgs_GCMark( C, (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( cl, sizeof(void*) ) );
	
	for( i = 0; i < cc; ++i )
	{
		sgs_GCMark( C, &cls[ i ]->var );
	}
	
	return SGS_SUCCESS;
}

static int sgsstd_closure_dump( SGS_CTX, sgs_VarObj* data, int depth )
{
	uint8_t* cl = (uint8_t*) data->data;
	int32_t i, ssz, cc = *(int32_t*) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable),sizeof(void*));
	sgs_Closure** cls = (sgs_Closure**) (void*) SGS_ASSUME_ALIGNED(cl+sizeof(sgs_Variable)+sizeof(int32_t),sizeof(void*));
	
	sgs_PushString( C, "closure\n{" );
	
	ssz = sgs_StackSize( C );
	sgs_PushString( C, "\nfunc: " );
	sgs_PushVariable( C, (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( cl, sizeof(void*) ) ); /* function */
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

static sgs_ObjInterface sgsstd_closure_iface[1] =
{{
	"closure",
	sgsstd_closure_destruct, sgsstd_closure_gcmark,
	sgsstd_closure_getindex, NULL,
	NULL, NULL, sgsstd_closure_dump, NULL,
	sgsstd_closure_call, NULL
}};

int sgsSTD_MakeClosure( SGS_CTX, sgs_Variable* func, uint32_t clc )
{
	/* WP: range not affected by conversion */
	uint32_t i, clsz = (uint32_t) sizeof(sgs_Closure*) * clc;
	uint32_t memsz = clsz + (uint32_t) ( sizeof(sgs_Variable) + sizeof(int32_t) );
	uint8_t* cl = (uint8_t*) sgs_PushObjectIPA( C, memsz, sgsstd_closure_iface );
	
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
				if( sgs_Call( C, 2, 1 ) != SGS_SUCCESS )
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
			if( sgs_Call( C, 2, 1 ) != SGS_SUCCESS )
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
		if( sgs_IterPushData( C, -1, 1, 1 ) != SGS_SUCCESS )
			STDLIB_WARN( "failed to read iterator (was dict changed in callback?)" )
		if( cset )
		{
			sgs_PushItem( C, -1 );
			sgs_PushItem( C, -3 );
			sgs_PushItem( C, 1 );
			if( sgs_Call( C, 2, 1 ) != SGS_SUCCESS )
				STDLIB_WARN( "failed to call the filter function" )
		}
		use = sgs_GetBool( C, -1 );
		if( cset )
			sgs_Pop( C, 1 );
		if( use )
		{
			/* src-dict, ... dest-dict, iterator, key, value */
			sgs_StoreIndexII( C, -4, -2, SGS_FALSE );
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
		if( sgs_IterPushData( C, -1, 1, 1 ) != SGS_SUCCESS )
			STDLIB_WARN( "failed to read iterator (was dict changed in callback?)" )
		sgs_PushItem( C, -2 );
		sgs_PushItem( C, 1 );
		if( sgs_Call( C, 2, 1 ) != SGS_SUCCESS )
			STDLIB_WARN( "failed to call the processing function" )
		/* src-dict, callable, ... iterator, key, proc.val. */
		sgs_StoreIndexII( C, 0, -2, SGS_FALSE );
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
	int ret;
	int32_t oldapilev;
	SGSFN( "isset" );
	if( !sgs_LoadArgs( C, "?v?v." ) )
		return 0;
	
	oldapilev = sgs_Cntl( C, SGS_CNTL_APILEV, SGS_ERROR );
	ret = sgs_PushIndexII( C, 0, 1, SGS_FALSE ) == SGS_SUCCESS;
	sgs_Cntl( C, SGS_CNTL_APILEV, oldapilev );
	sgs_PushBool( C, ret );
	return 1;
}

static int sgsstd_unset( SGS_CTX )
{
	DictHdr* dh;
	
	SGSFN( "unset" );
	
	if( !sgs_IsObject( C, 0, sgsstd_dict_iface ) &&
		!sgs_IsObject( C, 0, sgsstd_map_iface ) )
		return sgs_ArgErrorExt( C, 0, 0, "dict / map", "" );
	
	dh = (DictHdr*) sgs_GetObjectData( C, 0 );
	
	if( sgs_IsObject( C, 0, sgsstd_dict_iface ) )
	{
		if( !sgs_LoadArgs( C, ">?m." ) )
			return 0;
	}
	else
	{
		if( !sgs_LoadArgs( C, ">?v." ) )
			return 0;
	}
	sgs_vht_unset( &dh->ht, C, (C->stack_off+1) );
	
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
			if( SGS_FAILED( sgs_IterPushData( C, -1, SGS_FALSE, SGS_TRUE ) ) )
				STDLIB_ERR( "failed to retrieve data from iterator" )
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
			ret = sgs_IterPushData( C, -1, SGS_TRUE, SGS_TRUE );
			if( ret != SGS_SUCCESS ) STDLIB_ERR( "failed to retrieve data from iterator" )
			ret = sgs_StoreIndexII( C, -4, -2, SGS_FALSE );
			if( ret != SGS_SUCCESS ) STDLIB_ERR( "failed to store data" )
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

static int sgsstd_get_iterator( SGS_CTX )
{
	SGSFN( "get_iterator" );
	if( !sgs_LoadArgs( C, "?!v" ) )
		return 0;
	if( SGS_FAILED( sgs_PushIterator( C, 0 ) ) )
		STDLIB_WARN( "failed to retrieve iterator" );
	return 1;
}

static int sgsstd_iter_advance( SGS_CTX )
{
	int ret;
	SGSFN( "iter_advance" );
	if( !sgs_LoadArgs( C, "?!v" ) )
		return 0;
	ret = sgs_IterAdvance( C, 0 );
	if( SGS_FAILED( ret ) )
		STDLIB_WARN( "failed to advance iterator" )
	sgs_PushBool( C, ret != 0 );
	return 1;
}

static int sgsstd_iter_getdata( SGS_CTX )
{
	int ret;
	sgs_Bool pushkey = 0, pushval = 1;
	SGSFN( "iter_getdata" );
	if( !sgs_LoadArgs( C, "?!v|bb", &pushkey, &pushval ) )
		return 0;
	if( pushkey + pushval == 0 )
		STDLIB_WARN( "no data requested from iterator" );
	ret = sgs_IterPushData( C, 0, pushkey, pushval );
	if( SGS_FAILED( ret ) )
		STDLIB_WARN( "failed to retrieve data from iterator" );
	return pushkey + pushval;
}


static int sgsstd_tobool( SGS_CTX )
{
	SGSFN( "tobool" );
	sgs_SetStackSize( C, 1 );
	sgs_ToBool( C, 0 );
	return 1;
}

static int sgsstd_toint( SGS_CTX )
{
	SGSFN( "toint" );
	sgs_SetStackSize( C, 1 );
	sgs_ToInt( C, 0 );
	return 1;
}

static int sgsstd_toreal( SGS_CTX )
{
	SGSFN( "toreal" );
	sgs_SetStackSize( C, 1 );
	sgs_ToReal( C, 0 );
	return 1;
}

static int sgsstd_tostring( SGS_CTX )
{
	SGSFN( "tostring" );
	sgs_SetStackSize( C, 1 );
	sgs_ToString( C, 0 );
	return 1;
}

static int sgsstd_toptr( SGS_CTX )
{
	SGSFN( "toptr" );
	sgs_SetStackSize( C, 1 );
	sgs_ToPtr( C, 0 );
	return 1;
}


static int sgsstd_parseint( SGS_CTX )
{
	sgs_Int i;
	SGSFN( "parseint" );
	sgs_SetStackSize( C, 1 );
	if( sgs_ItemType( C, 0 ) == SGS_VT_INT )
		return 1;
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
	sgs_SetStackSize( C, 1 );
	if( sgs_ItemType( C, 0 ) == SGS_VT_REAL )
		return 1;
	if( sgs_ParseReal( C, 0, &r ) )
		sgs_PushReal( C, r );
	else
		sgs_PushNull( C );
	return 1;
}


static int sgsstd_typeof( SGS_CTX )
{
	SGSFN( "typeof" );
	if( !sgsstd_expectnum( C, 1 ) )
		return 0;
	sgs_TypeOf( C, 0 );
	return 1;
}

static int sgsstd_typeid( SGS_CTX )
{
	SGSFN( "typeid" );
	if( !sgsstd_expectnum( C, 1 ) )
		return 0;
	sgs_PushInt( C, sgs_ItemType( C, 0 ) );
	return 1;
}

static int sgsstd_typeptr( SGS_CTX )
{
	SGSFN( "typeptr" );
	if( !sgs_LoadArgs( C, ">." ) )
		return 0;
	if( sgs_ItemType( C, 0 ) == SGS_VT_OBJECT )
		sgs_PushPtr( C, sgs_GetObjectIface( C, 0 ) );
	else
		sgs_PushPtr( C, NULL );
	return 1;
}

static int sgsstd_typeptr_by_name( SGS_CTX )
{
	char* typenm;
	SGSFN( "typeptr_by_name" );
	if( !sgs_LoadArgs( C, "s", &typenm ) )
		return 0;
	sgs_PushPtr( C, sgs_FindType( C, typenm ) );
	return 1;
}

static int sgsstd_is_numeric( SGS_CTX )
{
	int res;
	uint32_t ty;
	
	SGSFN( "is_numeric" );
	if( !sgsstd_expectnum( C, 1 ) )
		return 0;
	
	ty = sgs_ItemType( C, 0 );
	if( ty == SGS_VT_NULL || ty == SGS_VT_FUNC ||
		ty == SGS_VT_CFUNC || ty == SGS_VT_OBJECT )
		res = SGS_FALSE;
	else
		res = ty != SGS_VT_STRING || sgs_IsNumericString(
			sgs_GetStringPtr( C, 0 ), sgs_GetStringSize( C, 0 ) );
	
	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_is_callable( SGS_CTX )
{
	SGSFN( "is_callable" );
	if( !sgsstd_expectnum( C, 1 ) )
		return 0;
	
	sgs_PushBool( C, sgs_IsCallable( C, 0 ) );
	return 1;
}

static int sgsstd_is_array( SGS_CTX )
{
	SGSFN( "is_array" );
	sgs_PushBool( C, sgs_IsObject( C, 0, sgsstd_array_iface ) );
	return 1;
}

static int sgsstd_is_dict( SGS_CTX )
{
	SGSFN( "is_dict" );
	sgs_PushBool( C, sgs_IsObject( C, 0, sgsstd_dict_iface ) );
	return 1;
}

static int sgsstd_is_map( SGS_CTX )
{
	SGSFN( "is_map" );
	sgs_PushBool( C, sgs_IsObject( C, 0, sgsstd_map_iface ) );
	return 1;
}

static int sgsstd_is_iterable( SGS_CTX )
{
	SGSFN( "is_iterable" );
	sgs_PushBool( C, sgs_PushIterator( C, 0 ) == SGS_SUCCESS );
	return 1;
}


/* I/O */

static int sgsstd_print( SGS_CTX )
{
	sgs_StkIdx i, ssz;
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
	sgs_StkIdx i, ssz;
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

static int sgsstd_errprint( SGS_CTX )
{
	sgs_StkIdx i, ssz;
	SGSBASEFN( "errprint" );
	ssz = sgs_StackSize( C );
	for( i = 0; i < ssz; ++i )
	{
		sgs_SizeVal size;
		char* buf = sgs_ToStringBuf( C, i, &size );
		/* WP: string limit */
		sgs_ErrWrite( C, buf, (size_t) size );
	}
	return 0;
}

static int sgsstd_errprintln( SGS_CTX )
{
	SGSFN( "errprintln" );
	sgsstd_errprint( C );
	sgs_ErrWrite( C, "\n", 1 );
	return 0;
}

static int sgsstd_errprintlns( SGS_CTX )
{
	sgs_StkIdx i, ssz;
	ssz = sgs_StackSize( C );
	
	SGSFN( "errprintlns" );
	
	for( i = 0; i < ssz; ++i )
	{
		sgs_SizeVal size;
		char* buf = sgs_ToStringBuf( C, i, &size );
		/* WP: string limit */
		sgs_ErrWrite( C, buf, (size_t) size );
		sgs_ErrWrite( C, "\n", 1 );
	}
	return 0;
}

static int sgsstd_printvar( SGS_CTX )
{
	int res;
	sgs_StkIdx i, ssz;
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
	sgs_MemBuf B;
	char bfr[ 1024 ];
	int all = 0;
	
	SGSFN( "read_stdin" );
	
	if( !sgs_LoadArgs( C, "|b", &all ) )
		return 0;
	
	B = sgs_membuf_create();
	while( fgets( bfr, 1024, stdin ) )
	{
		size_t len = strlen( bfr );
		sgs_membuf_appbuf( &B, C, bfr, len );
		if( len && !all && bfr[ len - 1 ] == '\n' )
		{
			B.size--;
			break;
		}
	}
	if( B.size > 0x7fffffff )
	{
		sgs_membuf_destroy( &B, C );
		STDLIB_WARN( "read more bytes than allowed to store" );
	}
	/* WP: error condition */
	sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	sgs_membuf_destroy( &B, C );
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

static int sgsstd_hash_fnv( SGS_CTX )
{
	uint8_t* buf;
	sgs_SizeVal i, bufsize;
	uint32_t hv = 2166136261u;
	sgs_Bool as_hexstr = 0;
	SGSFN( "hash_fnv" );
	if( !sgs_LoadArgs( C, "m|b", &buf, &bufsize, &as_hexstr ) )
		return 0;
	for( i = 0; i < bufsize; ++i )
	{
		hv ^= buf[ i ];
		hv *= 16777619u;
	}
	if( as_hexstr )
	{
		char hexstr[ 9 ] = {0};
		sprintf( hexstr, "%08x", hv );
		sgs_PushStringBuf( C, hexstr, 8 );
	}
	else
		sgs_PushInt( C, hv );
	return 1;
}

static int sgsstd_hash_crc32( SGS_CTX )
{
	uint8_t* buf;
	sgs_SizeVal bufsize;
	uint32_t hv;
	sgs_Bool as_hexstr = 0;
	SGSFN( "hash_crc32" );
	if( !sgs_LoadArgs( C, "m|b", &buf, &bufsize, &as_hexstr ) )
		return 0;
	hv = sgs_crc32( buf, (size_t) bufsize, 0 );
	if( as_hexstr )
	{
		char hexstr[ 9 ] = {0};
		sprintf( hexstr, "%08x", hv );
		sgs_PushStringBuf( C, hexstr, 8 );
	}
	else
		sgs_PushInt( C, hv );
	return 1;
}


/* internal utils */

static int sgsstd_va_get_args( SGS_CTX )
{
	uint8_t i, xac, pcnt;
	sgs_StackFrame* sf;
	SGSFN( "va_get_args" );
	if( !C->sf_last || !C->sf_last->prev )
		STDLIB_WARN( "not called from function" )
	sf = C->sf_last->prev;
	/* WP: argument count limit */
	
	/* accepted arguments */
	pcnt = SGS_MIN( sf->argcount, sf->inexp );
	for( i = 0; i < pcnt; ++i )
		sgs_PushVariable( C, C->stack_base + sf->argend - pcnt + i );
	/* extra arguments */
	if( sf->argcount > sf->inexp )
	{
		sgs_Variable* tpv;
		xac = (uint8_t)( sf->argcount - sf->inexp );
		tpv = C->stack_base + sf->argbeg + xac - 1;
		for( i = 0; i < xac; ++i )
			sgs_PushVariable( C, tpv - i );
	}
	sgs_PushArray( C, sf->argcount );
	return 1;
}

static int sgsstd_va_get_arg( SGS_CTX )
{
	sgs_Int argnum;
	uint8_t i, xac, pcnt;
	sgs_StackFrame* sf;
	SGSFN( "va_get_arg" );
	if( !C->sf_last || !C->sf_last->prev )
		STDLIB_WARN( "not called from function" )
	if( !sgs_LoadArgs( C, "i", &argnum ) )
		return 0;
	sf = C->sf_last->prev;
	if( argnum < 0 || argnum >= sf->argcount )
		STDLIB_WARN( "argument ID out of bounds" )
	
	/* WP: argument count limit */
	i = (uint8_t) argnum;
	
	/* accepted arguments */
	pcnt = SGS_MIN( sf->argcount, sf->inexp );
	if( i < pcnt )
	{
		sgs_PushVariable( C, C->stack_base + sf->argend - pcnt + i );
	}
	else
	/* extra arguments */
	if( sf->argcount > sf->inexp )
	{
		sgs_Variable* tpv;
		i = (uint8_t)( i - pcnt );
		xac = (uint8_t)( sf->argcount - sf->inexp );
		tpv = C->stack_base + sf->argbeg + xac - 1;
		sgs_PushVariable( C, tpv - i );
	}
	else
		sgs_PushNull( C );
	
	return 1;
}

static int sgsstd_va_arg_count( SGS_CTX )
{
	SGSFN( "va_arg_count" );
	if( !C->sf_last || !C->sf_last->prev )
		STDLIB_WARN( "not called from function" )
	sgs_PushInt( C, C->sf_last->prev->argcount );
	return 1;
}


static int sgsstd_metaobj_set( SGS_CTX )
{
	sgs_VarObj *obj1, *obj2;
	SGSFN( "metaobj_set" );
	if( !sgs_LoadArgs( C, "!xx", sgs_ArgCheck_Object, &obj1, sgs_ArgCheck_Object, &obj2 ) )
		return 0;
	sgs_ObjSetMetaObj( C, obj1, obj2 );
	sgs_SetStackSize( C, 1 );
	return 1;
}

static int sgsstd_metaobj_get( SGS_CTX )
{
	sgs_VarObj* obj;
	SGSFN( "metaobj_get" );
	if( !sgs_LoadArgs( C, "!x", sgs_ArgCheck_Object, &obj ) )
		return 0;
	obj = sgs_ObjGetMetaObj( obj );
	if( obj )
	{
		sgs_PushObjectPtr( C, obj );
		return 1;
	}
	return 0;
}

static int sgsstd_metamethods_enable( SGS_CTX )
{
	sgs_VarObj* obj;
	sgs_Bool enable;
	SGSFN( "metamethods_enable" );
	if( !sgs_LoadArgs( C, "!xb", sgs_ArgCheck_Object, &obj, &enable ) )
		return 0;
	sgs_ObjSetMetaMethodEnable( obj, enable );
	sgs_SetStackSize( C, 1 );
	return 1;
}

static int sgsstd_metamethods_test( SGS_CTX )
{
	sgs_VarObj* obj;
	SGSFN( "metamethods_test" );
	if( !sgs_LoadArgs( C, "!x", sgs_ArgCheck_Object, &obj ) )
		return 0;
	sgs_PushBool( C, sgs_ObjGetMetaMethodEnable( obj ) );
	return 1;
}

static int sgsstd_mm_getindex_router( SGS_CTX )
{
	int ret;
	sgs_Variable func, movar;
	SGSFN( "mm_getindex_router" );
	
	if( sgs_StackSize( C ) < 1 ) goto fail;
	if( !sgs_Method( C ) || sgs_ItemType( C, 0 ) != SGS_VT_OBJECT ) goto fail;
	if( !( movar.data.O = sgs_ObjGetMetaObj( sgs_GetObjectStruct( C, 0 ) ) ) ) goto fail;
	movar.type = SGS_VT_OBJECT;
	
	sgs_PushString( C, "__get_" );
	sgs_PushItem( C, 1 );
	if( SGS_FAILED( sgs_StringConcat( C, 2 ) ) ) goto fail;
	if( SGS_FAILED( sgs_GetIndexPIP( C, &movar, -1, &func, SGS_FALSE ) ) ) goto fail;
	
	sgs_SetStackSize( C, 1 );
	ret = sgs_ThisCallP( C, &func, 0, 1 );
	sgs_Release( C, &func );
	if( SGS_FAILED( ret ) ) goto fail;
	return 1;
	
fail:
	return 0;
}

static int sgsstd_mm_setindex_router( SGS_CTX )
{
	int ret;
	sgs_Variable func, movar;
	SGSFN( "mm_setindex_router" );
	
	if( sgs_StackSize( C ) < 2 ) goto fail;
	if( !sgs_Method( C ) || sgs_ItemType( C, 0 ) != SGS_VT_OBJECT ) goto fail;
	if( !( movar.data.O = sgs_ObjGetMetaObj( sgs_GetObjectStruct( C, 0 ) ) ) ) goto fail;
	movar.type = SGS_VT_OBJECT;
	
	sgs_PushString( C, "__set_" );
	sgs_PushItem( C, 1 );
	if( SGS_FAILED( sgs_StringConcat( C, 2 ) ) ) goto fail;
	if( SGS_FAILED( sgs_GetIndexPIP( C, &movar, -1, &func, SGS_FALSE ) ) ) goto fail;
	
	sgs_SetStackSize( C, 3 );
	sgs_StoreItem( C, 1 );
	ret = sgs_ThisCallP( C, &func, 1, 1 );
	sgs_Release( C, &func );
	if( SGS_FAILED( ret ) ) goto fail;
	return 0;
	
fail:
	return 0;
}


static int sgsstd_yield( SGS_CTX )
{
	SGSFN( "yield" );
	if( sgs_PauseState( C ) == SGS_FALSE )
		STDLIB_WARN( "cannot yield with C functions in stack" );
	return 0;
}

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
	else if( P->handler.type != SGS_VT_NULL )
	{
		sgs_PushInt( C, type );
		sgs_PushString( C, message );
		ret = sgs_CallP( C, &P->handler, 2, 1 );
		if( SGS_FAILED( ret ) )
		{
			ret = 0;
			P->pfn( P->pctx, C, SGS_ERROR, "Error detected while attempting to call error handler" );
		}
		else
		{
			if( ret == SGS_SUCABRT )
				sgs_Abort( C );
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
	P.handler.type = SGS_VT_NULL;
	P.depth = 0;
	if( b )
		sgs_GetStackItem( C, 1, &P.handler );
	
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
	SGS_UNUSED( ret );
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
		if( outsize <= 0x7fffffff )
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
	
#if !SGS_NO_STDLIB
	if( strcmp( name, "fmt" ) == 0 ) ret = sgs_LoadLib_Fmt( C );
	else if( strcmp( name, "io" ) == 0 ) ret = sgs_LoadLib_IO( C );
	else if( strcmp( name, "math" ) == 0 ) ret = sgs_LoadLib_Math( C );
	else if( strcmp( name, "os" ) == 0 ) ret = sgs_LoadLib_OS( C );
	else if( strcmp( name, "re" ) == 0 ) ret = sgs_LoadLib_RE( C );
	else if( strcmp( name, "string" ) == 0 ) ret = sgs_LoadLib_String( C );
#endif
	
	if( ret == SGS_SUCCESS )
	{
		sgs_PushBool( C, SGS_TRUE );
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
		return SGS_FALSE;
	/* sgs_Pop( C, 2 ); [.., string, bool] - will return last */
	return SGS_TRUE;
}

static void sgsstd__setinc( SGS_CTX, int argid )
{
	sgs_PushEnv( C );
	sgs_PushString( C, "+" );
	sgs_PushItem( C, argid );
	sgs_StringConcat( C, 2 );
	sgs_PushBool( C, SGS_TRUE );
	sgs_SetIndexIII( C, -3, -2, -1, 0 );
	sgs_Pop( C, 3 );
}

static int sgsstd_include_library( SGS_CTX )
{
	int ret, over = SGS_FALSE;
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
	int ret, over = SGS_FALSE;
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
	int ret, over = SGS_FALSE;
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
	sgs_PushBool( C, SGS_TRUE );
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
			if( sizeof(ptrdiff_t) > 4 && len > 0x7fffffff )
				return 0;
			else
				/* WP: error condition */
				sgs_PushStringBuf( C, file, (sgs_SizeVal) len );
		}
		return 1;
	}
	
	return 0;
}

static int _push_procdir( SGS_CTX )
{
	char* mfn = sgsXPC_GetModuleFileName();
	if( mfn )
	{
		char* mfnend = mfn + strlen( mfn );
		while( mfnend > mfn && *mfnend != '/' && *mfnend != '\\' )
			mfnend--;
		if( ((size_t)( mfnend - mfn )) > 0x7fffffff )
			return 0;
		/* WP: added error condition */
		sgs_PushStringBuf( C, mfn, (sgs_SizeVal)( mfnend - mfn ) );
		free( mfn );
		return 1;
	}
	else
		return 0;
}

static int _find_includable_file( SGS_CTX, sgs_MemBuf* tmp, char* ps,
	size_t pssize, char* fn, size_t fnsize, char* dn, size_t dnsize, char* pd, size_t pdsize )
{
	SGS_SHCTX_USE;
	if( ( fnsize > 2 && *fn == '.' && ( fn[1] == '/' || fn[1] == '\\' ) ) ||
#ifdef _WIN32
		( fnsize > 2 && fn[1] == ':' ) )
#else
		( fnsize > 1 && *fn == '/' ) )
#endif
	{
		sgs_membuf_setstrbuf( tmp, C, fn, fnsize );
		
		sgs_ScriptFSData fsd = {0};
		fsd.filename = tmp->ptr;
		if( SGS_SUCCEEDED( S->sfs_fn( S->sfs_ctx, C, SGS_SFS_FILE_EXISTS, &fsd ) ) )
			return 1;
	}
	else
	{
		char* pse = ps + pssize;
		char* psc = ps;
		while( ps <= pse )
		{
			if( ps == pse || *ps == ';' )
			{
				sgs_membuf_resize( tmp, C, 0 );
				while( psc < ps )
				{
					if( *psc == '?' )
						sgs_membuf_appbuf( tmp, C, fn, fnsize );
					else if( *psc == '|' )
					{
						if( dn )
							sgs_membuf_appbuf( tmp, C, dn, dnsize );
						else
						{
							psc = ps;
							goto notthispath;
						}
					}
					else if( *psc == '@' )
					{
						if( pd )
							sgs_membuf_appbuf( tmp, C, pd, pdsize );
						else
						{
							psc = ps;
							goto notthispath;
						}
					}
					else
						sgs_membuf_appchr( tmp, C, *psc );
					psc++;
				}
				sgs_membuf_appchr( tmp, C, 0 );
				
				{
					sgs_ScriptFSData fsd = {0};
					fsd.filename = tmp->ptr;
					if( SGS_SUCCEEDED( S->sfs_fn( S->sfs_ctx, C, SGS_SFS_FILE_EXISTS, &fsd ) ) )
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
	char* fnstr, *dnstr = NULL, *pdstr = NULL;
	sgs_SizeVal fnsize, dnsize = 0, pdsize = 0;
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
		sgs_MemBuf mb = sgs_membuf_create();
		
		ret = sgs_PushGlobal( C, "SGS_PATH" );
		if( ret != SGS_SUCCESS ||
			( ps = sgs_ToStringBuf( C, -1, &pssize ) ) == NULL )
		{
			ps = SGS_INCLUDE_PATH;
			pssize = (sgs_SizeVal) strlen( ps );
		}
		
		if( _push_curdir( C ) )
		{
			dnstr = sgs_GetStringPtr( C, -1 );
			dnsize = sgs_GetStringSize( C, -1 );
		}
		if( _push_procdir( C ) )
		{
			pdstr = sgs_GetStringPtr( C, -1 );
			pdsize = sgs_GetStringSize( C, -1 );
		}
		/* WP: string limit */
		ret = _find_includable_file( C, &mb, ps, (size_t) pssize, fnstr, (size_t) fnsize, dnstr, (size_t) dnsize, pdstr, (size_t) pdsize );
		if( ret == 0 || mb.size == 0 )
		{
			sgs_membuf_destroy( &mb, C );
			return sgs_Msg( C, SGS_WARNING, "could not find '%.*s' "
				"with include path '%.*s'", fnsize, fnstr, pssize, ps );
		}
		
		sgs_PushString( C, mb.ptr );
		sgs_PushString( C, " - include" );
		sgs_StringConcat( C, 2 );
		SGSFN( sgs_GetStringPtr( C, -1 ) );
		ret = sgs_ExecFile( C, mb.ptr );
		SGSFN( "include" );
		if( ret == SGS_SUCCESS )
		{
			sgs_membuf_destroy( &mb, C );
			goto success;
		}
		
		ret = sgsXPC_GetProcAddress( mb.ptr, SGS_LIB_ENTRY_POINT, (void**) &func );
		if( SGS_SUCCEEDED( ret ) )
		{
			ret = func( C );
			if( SGS_SUCCEEDED( ret ) )
			{
				sgs_membuf_destroy( &mb, C );
				goto success;
			}
		}
		else
		{
			sgs_membuf_destroy( &mb, C );
			return sgs_Msg( C, SGS_ERROR, "failed to load native module '%.*s'", fnsize, fnstr );
		}
		
		sgs_membuf_destroy( &mb, C );
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

static int sgsstd_sys_curprocfile( SGS_CTX )
{
	char* path;
	SGSFN( "sys_curprocfile" );
	
	if( sgs_StackSize( C ) )
		STDLIB_WARN( "function expects 0 arguments" )
	
	path = sgsXPC_GetModuleFileName();
	sgs_Errno( C, path != NULL );
	if( path )
	{
		sgs_PushString( C, path );
		free( path );
		return 1;
	}
	else
		return 0;
}

static int sgsstd_sys_curprocdir( SGS_CTX )
{
	SGSFN( "sys_curprocdir" );
	
	if( sgs_StackSize( C ) )
		STDLIB_WARN( "function expects 0 arguments" )
	
	return _push_procdir( C );
}

static int sgsstd_multiply_path_ext_lists( SGS_CTX )
{
	char *pp, *ss, *prefixes, *osfx, *suffixes = "?;?" SGS_MODULE_EXT ";?.sgc;?.sgs", *joinstr = "/";
	size_t joinstrlen;
	SGSFN( "multiply_path_ext_lists" );
	
	if( !sgs_LoadArgs( C, "s|ss", &prefixes, &joinstr, &suffixes ) )
		return 0;
	
	joinstrlen = strlen( joinstr );
	
	sgs_PushArray( C, 0 );
	osfx = suffixes;
	pp = prefixes;
	for(;;)
	{
		if( *pp == ';' || *pp == '\0' )
		{
			ss = suffixes = osfx;
			for(;;)
			{
				if( *ss == ';' || *ss == '\0' )
				{
					char* tmp;
					/* WP: assume no wrap-arounds on string iteration */
					size_t pplen = (size_t) ( pp - prefixes );
					size_t sslen = (size_t) ( ss - suffixes );
					if( pplen + sslen + joinstrlen > 0x7fffffff )
						STDLIB_WARN( "generated path size is bigger than allowed to store" )
					/* WP: error condition */
					tmp = sgs_PushStringAlloc( C, (sgs_SizeVal)( pplen + sslen + joinstrlen ) );
					memcpy( tmp, prefixes, pplen );
					memcpy( tmp + pplen, joinstr, joinstrlen );
					memcpy( tmp + pplen + joinstrlen, suffixes, sslen );
					sgs_FinalizeStringAlloc( C, -1 );
					sgs_ObjectAction( C, -2, SGS_ACT_ARRAY_PUSH, 1 );
					if( !*ss )
						break;
					suffixes = ++ss;
				}
				else
					ss += !!*ss;
			}
			if( !*pp )
				break;
			prefixes = ++pp;
		}
		else
			pp += !!*pp;
	}
	
	return 1;
}

static int sgsstd_sys_backtrace( SGS_CTX )
{
	sgs_Bool as_errinfo = 0;
	SGSFN( "sys_backtrace" );
	
	if( !sgs_LoadArgs( C, "|b", &as_errinfo ) )
		return 0;
	
	if( as_errinfo )
		sgs_PushErrorInfo( C, SGS_ERRORINFO_STACK, 0, NULL );
	else
	{
		sgs_StkIdx sz = sgs_StackSize( C );
		sgs_StackFrame* p = sgs_GetFramePtr( C, SGS_FALSE );
		while( p != NULL )
		{
			const char* file, *name;
			int ln;
			if( !p->next && !p->code )
				break;
			sgs_StackFrameInfo( C, p, &name, &file, &ln );
			
			sgs_PushString( C, "func" );
			sgs_PushString( C, name );
			sgs_PushString( C, "line" );
			if( ln )
				sgs_PushInt( C, ln );
			else
				sgs_PushNull( C );
			sgs_PushString( C, "file" );
			sgs_PushString( C, file );
			
			sgs_PushDict( C, 6 );
			
			p = p->next;
		}
		sgs_PushArray( C, sgs_StackSize( C ) - sz );
	}
	return 1;
}

static int sgsstd_sys_msg( SGS_CTX )
{
	char* errmsg;
	sgs_Int errcode;
	
	SGSFN( "sys_msg" );
	
	if( !sgs_LoadArgs( C, "is", &errcode, &errmsg ) )
		return 0;
	
	SGSFN( NULL );
	
	sgs_Msg( C, (int) errcode, "%s", errmsg );
	return 0;
}

static int sgsstd__msgwrapper( SGS_CTX, const char* fn, int code )
{
	char* msg;
	SGSFN( fn );
	if( sgs_LoadArgs( C, "s", &msg ) )
	{
		SGSFN( NULL );
		sgs_Msg( C, code, "%s", msg );
	}
	return 0;
}

static int sgsstd_INFO( SGS_CTX ){ return sgsstd__msgwrapper( C, "INFO", SGS_INFO ); }
static int sgsstd_WARNING( SGS_CTX ){ return sgsstd__msgwrapper( C, "WARNING", SGS_WARNING ); }
static int sgsstd_ERROR( SGS_CTX ){ return sgsstd__msgwrapper( C, "ERROR", SGS_ERROR ); }

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
		sgs_PushBool( C, SGS_FALSE );
	return 1;
}


static int sgsstd_serialize_core( SGS_CTX, int which )
{
	int ret;
	
	if( !sgs_LoadArgs( C, "?v." ) )
		return 0;
	
	ret = which ? sgs_SerializeV2( C ) : sgs_SerializeV1( C );
	if( ret == SGS_SUCCESS )
		return 1;
	else
		STDLIB_ERR( "failed to serialize" )
}

static int sgsstd_serialize( SGS_CTX ){ SGSFN( "serialize" ); return sgsstd_serialize_core( C, 0 ); }
static int sgsstd_serialize2( SGS_CTX ){ SGSFN( "serialize2" ); return sgsstd_serialize_core( C, 1 ); }


static int check_arrayordict_fn( SGS_CTX, int argid, va_list args, int flags )
{
	uint32_t ty = sgs_ItemType( C, argid );
	if( ty != SGS_VT_OBJECT || (
		!sgs_IsObject( C, argid, sgsstd_array_iface ) &&
		!sgs_IsObject( C, argid, sgsstd_dict_iface ) ) )
		return sgs_ArgErrorExt( C, argid, 0, "array or dict", "" );
	return 1;
}

static int sgsstd_unserialize_core( SGS_CTX, int which )
{
	int ret;
	sgs_StkIdx ssz = sgs_StackSize( C ), dictpos;
	sgs_Variable env;
	
	if( !sgs_LoadArgs( C, "?s|x", check_arrayordict_fn ) )
		return 0;
	
	if( ssz >= 2 )
	{
		sgs_GetEnv( C, &env );
		if( sgs_IsObject( C, 1, sgsstd_array_iface ) )
		{
			dictpos = sgs_StackSize( C );
			sgs_PushDict( C, 0 );
			sgs_PushIterator( C, 1 );
			while( sgs_IterAdvance( C, -1 ) > 0 )
			{
				if( !sgs_IterPushData( C, -1, 0, 1 ) )
				{
					sgs_ToString( C, -1 );
					if( SGS_FAILED( sgs_PushIndexPI( C, &env, -1, 0 ) ) )
						sgs_PushNull( C );
					sgs_StoreIndexII( C, dictpos, -2, 0 );
					sgs_Pop( C, 1 ); /* pop name */
				}
			}
			sgs_Pop( C, 1 ); /* pop iterator */
		}
		else
			dictpos = 1;
		
		if( SGS_FAILED( sgs_PushItem( C, dictpos ) ) ||
			SGS_FAILED( sgs_StoreEnv( C ) ) )
		{
			sgs_Release( C, &env );
			STDLIB_ERR( "failed to change the environment" )
		}
		
		sgs_PushItem( C, 0 );
	}
	
	ret = which ? sgs_UnserializeV2( C ) : sgs_UnserializeV1( C );
	
	if( ssz >= 2 )
	{
		int ret2 = sgs_SetEnv( C, &env );
		sgs_Release( C, &env );
		if( SGS_FAILED( ret2 ) )
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

static int sgsstd_unserialize( SGS_CTX ){ SGSFN( "unserialize" ); return sgsstd_unserialize_core( C, 0 ); }
static int sgsstd_unserialize2( SGS_CTX ){ SGSFN( "unserialize2" ); return sgsstd_unserialize_core( C, 1 ); }


/* register all */
#ifndef STDLIB_FN
#  define STDLIB_FN( x ) { #x, sgsstd_##x }
#endif
static sgs_RegFuncConst regfuncs[] =
{
	/* containers */
	/* STDLIB_FN( array ), -- object */ STDLIB_FN( dict ), STDLIB_FN( map ), { "class", sgsstd_class },
	STDLIB_FN( array_filter ), STDLIB_FN( array_process ),
	STDLIB_FN( dict_filter ), STDLIB_FN( dict_process ),
	STDLIB_FN( dict_size ), STDLIB_FN( map_size ), STDLIB_FN( isset ), STDLIB_FN( unset ), STDLIB_FN( clone ),
	STDLIB_FN( get_keys ), STDLIB_FN( get_values ), STDLIB_FN( get_concat ),
	STDLIB_FN( get_merged ), STDLIB_FN( get_merged_map ),
	STDLIB_FN( get_iterator ), STDLIB_FN( iter_advance ), STDLIB_FN( iter_getdata ),
	/* types */
	STDLIB_FN( tobool ), STDLIB_FN( toint ), STDLIB_FN( toreal ), STDLIB_FN( tostring ), STDLIB_FN( toptr ),
	STDLIB_FN( parseint ), STDLIB_FN( parsereal ),
	STDLIB_FN( typeof ), STDLIB_FN( typeid ), STDLIB_FN( typeptr ), STDLIB_FN( typeptr_by_name ),
	STDLIB_FN( is_numeric ), STDLIB_FN( is_callable ),
	STDLIB_FN( is_array ), STDLIB_FN( is_dict ), STDLIB_FN( is_map ), STDLIB_FN( is_iterable ),
	/* I/O */
	STDLIB_FN( print ), STDLIB_FN( println ), STDLIB_FN( printlns ),
	STDLIB_FN( errprint ), STDLIB_FN( errprintln ), STDLIB_FN( errprintlns ),
	STDLIB_FN( printvar ), STDLIB_FN( printvar_ext ),
	STDLIB_FN( read_stdin ),
	/* OS */
	STDLIB_FN( ftime ),
	/* utils */
	STDLIB_FN( rand ), STDLIB_FN( randf ), STDLIB_FN( srand ), STDLIB_FN( hash_fnv ), STDLIB_FN( hash_crc32 ),
	/* internal utils */
	STDLIB_FN( va_get_args ), STDLIB_FN( va_get_arg ), STDLIB_FN( va_arg_count ),
	{ "sys_call", sgs_specfn_call }, { "sys_apply", sgs_specfn_apply },
	STDLIB_FN( metaobj_set ), STDLIB_FN( metaobj_get ), STDLIB_FN( metamethods_enable ), STDLIB_FN( metamethods_test ),
	STDLIB_FN( mm_getindex_router ), STDLIB_FN( mm_setindex_router ),
	STDLIB_FN( yield ),
	STDLIB_FN( pcall ), STDLIB_FN( assert ),
	STDLIB_FN( eval ), STDLIB_FN( eval_file ), STDLIB_FN( compile_sgs ),
	STDLIB_FN( include_library ), STDLIB_FN( include_file ),
	STDLIB_FN( include_shared ), STDLIB_FN( import_cfunc ),
	STDLIB_FN( include ),
	STDLIB_FN( sys_curfile ), STDLIB_FN( sys_curfiledir ), STDLIB_FN( sys_curprocfile ), STDLIB_FN( sys_curprocdir ),
	STDLIB_FN( multiply_path_ext_lists ),
	STDLIB_FN( sys_backtrace ), STDLIB_FN( sys_msg ), STDLIB_FN( INFO ), STDLIB_FN( WARNING ), STDLIB_FN( ERROR ),
	STDLIB_FN( sys_abort ), STDLIB_FN( app_abort ), STDLIB_FN( app_exit ),
	STDLIB_FN( sys_replevel ), STDLIB_FN( sys_stat ),
	STDLIB_FN( errno ), STDLIB_FN( errno_string ), STDLIB_FN( errno_value ),
	STDLIB_FN( dumpvar ), STDLIB_FN( dumpvar_ext ),
	STDLIB_FN( gc_collect ),
	STDLIB_FN( serialize ), STDLIB_FN( unserialize ),
	STDLIB_FN( serialize2 ), STDLIB_FN( unserialize2 ),
};

static const sgs_RegIntConst regiconsts[] =
{
	{ "SGS_INFO", SGS_INFO },
	{ "SGS_WARNING", SGS_WARNING },
	{ "SGS_ERROR", SGS_ERROR },
	
	{ "VT_NULL", SGS_VT_NULL },
	{ "VT_BOOL", SGS_VT_BOOL },
	{ "VT_INT", SGS_VT_INT },
	{ "VT_REAL", SGS_VT_REAL },
	{ "VT_STRING", SGS_VT_STRING },
	{ "VT_FUNC", SGS_VT_FUNC },
	{ "VT_CFUNC", SGS_VT_CFUNC },
	{ "VT_OBJECT", SGS_VT_OBJECT },
	{ "VT_PTR", SGS_VT_PTR },
	
	{ "RAND_MAX", RAND_MAX },
};


int sgsSTD_PostInit( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, regiconsts, SGS_ARRAY_SIZE( regiconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, regfuncs, SGS_ARRAY_SIZE( regfuncs ) );
	if( ret != SGS_SUCCESS ) return ret;
	
	ret = sgs_PushInterface( C, sgsstd_array_iface_gen );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_StoreGlobal( C, "array" );
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
	
	ret = sgs_RegisterType( C, "closure", sgsstd_closure_iface );
	if( ret != SGS_SUCCESS ) return ret;
	
	return SGS_SUCCESS;
}

int sgsSTD_MakeArray( SGS_CTX, sgs_Variable* out, sgs_SizeVal cnt )
{
	sgs_StkIdx i = 0, ssz = sgs_StackSize( C );
	
	if( ssz < cnt )
		return SGS_EINVAL;
	else
	{
		sgs_Variable *p, *pend;
		void* data = sgs_Malloc( C, SGSARR_ALLOCSIZE( cnt ) );
		sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) sgs_InitObjectIPA( C,
			out, sizeof( sgsstd_array_header_t ), SGSIFACE_ARRAY );
		
		hdr->size = cnt;
		hdr->mem = cnt;
		p = hdr->data = (sgs_Variable*) data;
		pend = p + cnt;
		while( p < pend )
			sgs_GetStackItem( C, i++ - cnt, p++ );
		
		sgs_Pop( C, cnt );
		
		sgs_PushInterface( C, sgsstd_array_iface_gen );
		sgs_ObjSetMetaObj( C, sgs_GetObjectStructP( out ), sgs_GetObjectStruct( C, -1 ) );
		sgs_Pop( C, 1 );
		
		return SGS_SUCCESS;
	}
}

int sgsSTD_MakeDict( SGS_CTX, sgs_Variable* out, sgs_SizeVal cnt )
{
	DictHdr* dh;
	sgs_VHTable* ht;
	sgs_StkIdx i, ssz = sgs_StackSize( C );
	
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
		
		sgs_vht_set( ht, C, (C->stack_top+i-cnt), (C->stack_top+i+1-cnt) );
	}
	
	sgs_Pop( C, cnt );
	return SGS_SUCCESS;
}

int sgsSTD_MakeMap( SGS_CTX, sgs_Variable* out, sgs_SizeVal cnt )
{
	DictHdr* dh;
	sgs_VHTable* ht;
	sgs_StkIdx i, ssz = sgs_StackSize( C );
	
	if( cnt > ssz || cnt % 2 != 0 )
		return SGS_EINVAL;
	
	dh = mkmap( C, out );
	ht = &dh->ht;
	
	for( i = 0; i < cnt; i += 2 )
	{
		sgs_vht_set( ht, C, (C->stack_top+i-cnt), (C->stack_top+i+1-cnt) );
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
	sgs_VHTVar *p, *pend;
	sgs_VarObj* data = GLBP;
	HTHDR;
	
	p = ht->vars;
	pend = p + sgs_vht_size( ht );
	while( p < pend )
	{
		sgs_Release( C, &p->val );
		p++;
	}
	
	sgs_ObjRelease( C, GLBP );
	
	GLBP = NULL;
	
	return SGS_SUCCESS;
}

int sgsSTD_GlobalGet( SGS_CTX, sgs_Variable* out, sgs_Variable* idx )
{
	sgs_VHTVar* pair;
	sgs_VarObj* data = GLBP;
	HTHDR;
	
	/* `out` is expected to point at an initialized variable, which could be same as `idx` */
	
	if( idx->type != SGS_VT_STRING )
		return SGS_ENOTSUP;
	
	if( strcmp( sgs_str_cstr( idx->data.S ), "_G" ) == 0 )
	{
		sgs_Release( C, out );
		sgs_InitObjectPtr( out, data );
		return SGS_SUCCESS;
	}
	
	if( data->mm_enable )
	{
		int ret;
		sgs_Variable obj, tmp;
		obj.type = SGS_VT_OBJECT;
		obj.data.O = data;
		ret = sgs_GetIndexPPP( C, &obj, idx, &tmp, 0 );
		sgs_Release( C, out );
		if( SGS_SUCCEEDED( ret ) )
		{
			*out = tmp;
		}
		return ret;
	}
	
	if( ( pair = sgs_vht_get( ht, idx ) ) != NULL )
	{
		sgs_Release( C, out );
		*out = pair->val;
		sgs_Acquire( C, out );
		return SGS_SUCCESS;
	}
	
	sgs_Msg( C, SGS_WARNING, "variable '%s' was not found", sgs_str_cstr( idx->data.S ) );
	sgs_Release( C, out );
	return SGS_ENOTFND;
}

int sgsSTD_GlobalSet( SGS_CTX, sgs_Variable* idx, sgs_Variable* val )
{
	sgs_VarObj* data = GLBP;
	HTHDR;
	
	if( idx->type != SGS_VT_STRING )
		return SGS_ENOTSUP;
	
	if( strcmp( sgs_var_cstr( idx ), "_G" ) == 0 )
	{
		int ret = sgs_SetEnv( C, val );
		if( SGS_FAILED( ret ) )
			sgs_Msg( C, SGS_ERROR, "_G only accepts 'dict' values" );
		return ret;
	}
	
	if( data->mm_enable )
	{
		sgs_Variable obj;
		obj.type = SGS_VT_OBJECT;
		obj.data.O = data;
		return sgs_SetIndexPPP( C, &obj, idx, val, 0 );
	}
	
	sgs_vht_set( ht, C, idx, val );
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

int sgsSTD_GlobalIter( SGS_CTX, sgs_VHTVar** outp, sgs_VHTVar** outpend )
{
	sgs_VarObj* data = GLBP;
	
	if( data )
	{
		HTHDR;
		*outp = ht->vars;
		*outpend = ht->vars + sgs_vht_size( ht );
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


SGSRESULT sgs_StoreFuncConsts( SGS_CTX, sgs_StkIdx item, const sgs_RegFuncConst* list, int size )
{
	int ret;
	sgs_Variable tgt, key, val;
	if( !sgs_PeekStackItem( C, item, &tgt ) )
		return SGS_EBOUNDS;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_InitCFunction( &val, list->value );
		sgs_InitString( C, &key, list->name );
		ret = sgs_SetIndexPPP( C, &tgt, &key, &val, 1 );
		sgs_Release( C, &key );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIntConsts( SGS_CTX, sgs_StkIdx item, const sgs_RegIntConst* list, int size )
{
	int ret;
	sgs_Variable tgt, key, val;
	if( !sgs_PeekStackItem( C, item, &tgt ) )
		return SGS_EBOUNDS;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_InitInt( &val, list->value );
		sgs_InitString( C, &key, list->name );
		ret = sgs_SetIndexPPP( C, &tgt, &key, &val, 1 );
		sgs_Release( C, &key );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreRealConsts( SGS_CTX, sgs_StkIdx item, const sgs_RegRealConst* list, int size )
{
	int ret;
	sgs_Variable tgt, key, val;
	if( !sgs_PeekStackItem( C, item, &tgt ) )
		return SGS_EBOUNDS;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_InitReal( &val, list->value );
		sgs_InitString( C, &key, list->name );
		ret = sgs_SetIndexPPP( C, &tgt, &key, &val, 1 );
		sgs_Release( C, &key );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}


SGSRESULT sgs_StoreFuncConstsP( SGS_CTX, sgs_Variable* var, const sgs_RegFuncConst* list, int size )
{
	int ret;
	sgs_Variable key, val;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_InitCFunction( &val, list->value );
		sgs_InitString( C, &key, list->name );
		ret = sgs_SetIndexPPP( C, var, &key, &val, 1 );
		sgs_Release( C, &key );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIntConstsP( SGS_CTX, sgs_Variable* var, const sgs_RegIntConst* list, int size )
{
	int ret;
	sgs_Variable key, val;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_InitInt( &val, list->value );
		sgs_InitString( C, &key, list->name );
		ret = sgs_SetIndexPPP( C, var, &key, &val, 1 );
		sgs_Release( C, &key );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreRealConstsP( SGS_CTX, sgs_Variable* var, const sgs_RegRealConst* list, int size )
{
	int ret;
	sgs_Variable key, val;
	while( size-- )
	{
		if( !list->name )
			break;
		sgs_InitReal( &val, list->value );
		sgs_InitString( C, &key, list->name );
		ret = sgs_SetIndexPPP( C, var, &key, &val, 1 );
		sgs_Release( C, &key );
		if( ret != SGS_SUCCESS ) return ret;
		list++;
	}
	return SGS_SUCCESS;
}


SGSBOOL sgs_IncludeExt( SGS_CTX, const char* name, const char* searchpath )
{
	int ret = 0, sz, sz0;
	int pathrep = 0;
	sgs_Variable incfn;
	
	sz0 = sgs_StackSize( C );
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
		sgs_PushEnv( C );
		sgs_PushString( C, "SGS_PATH" );
		sgs_ObjectAction( C, -2, SGS_ACT_DICT_UNSET, -1 );
	}
	
	sgs_SetStackSize( C, sz0 );
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
			if( !sgs_PeekStackItem( C, arg, &comp ) )
				return SGS_EBOUNDS;
			
			while( off < hdr->size )
			{
				sgs_Variable* p = SGSARR_PTR( hdr ) + off;
				if( sgs_EqualTypes( p, &comp )
					&& sgs_Compare( C, p, &comp ) == 0 )
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
			if( !sgs_PeekStackItem( C, arg, &comp ) )
				return SGS_EBOUNDS;
			
			while( off < hdr->size )
			{
				sgs_Variable* p = SGSARR_PTR( hdr ) + off;
				if( sgs_EqualTypes( p, &comp )
					&& sgs_Compare( C, p, &comp ) == 0 )
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
		if( !sgs_IsObject( C, item, sgsstd_dict_iface ) )
			return SGS_EINVAL;
		else
		{
			sgs_Variable key;
			if( !sgs_PeekStackItem( C, arg, &key ) )
				return SGS_EINVAL;
			sgs_vht_unset(
				&((DictHdr*)sgs_GetObjectData( C, item ))->ht, C,
				&key );
			return SGS_SUCCESS;
		}
		
	case SGS_ACT_MAP_UNSET:
		if( !sgs_IsObject( C, item, sgsstd_map_iface ) )
			return SGS_EINVAL;
		else
		{
			sgs_Variable key;
			if( !sgs_PeekStackItem( C, arg, &key ) )
				return SGS_EINVAL;
			sgs_vht_unset(
				&((DictHdr*)sgs_GetObjectData( C, item ))->ht, C,
				&key );
			return SGS_SUCCESS;
		}
		
	}
	return SGS_ENOTFND;
}

