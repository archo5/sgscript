

#include <stdio.h>

#include "sgscript.h"
#include "sgs_proc.h"
#include "sgs_ctx.h"

#define STDLIB_WARN( warn ) { sgs_Printf( C, SGS_WARNING, -1, warn ); return 0; }


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

#define SGSARR_UNIT sizeof( sgs_Variable )
#define SGSARR_HDRBASE sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data
#define SGSARR_HDR sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) data->data
#define SGSARR_HDRUPDATE hdr = (sgsstd_array_header_t*) data->data
#define SGSARR_HDRSIZE sizeof(sgsstd_array_header_t)
#define SGSARR_ALLOCSIZE( cnt ) ((cnt)*sizeof(sgs_Variable)+SGSARR_HDRSIZE)
#define SGSARR_PTR( base ) ((sgs_Variable*)(((char*)base)+SGSARR_HDRSIZE))

static void sgsstd_array_reserve( SGS_CTX, sgs_VarObj* data, uint32_t size )
{
	void* nd;
	SGSARR_HDR;
	if( size <= hdr->mem )
		return;

	UNUSED( C );

	nd = sgs_Malloc( SGSARR_ALLOCSIZE( size ) );
	memcpy( nd, data->data, SGSARR_ALLOCSIZE( hdr->mem ) );
	sgs_Free( data->data );
	data->data = nd;
	SGSARR_HDRUPDATE;
	hdr->mem = size;
}

static void sgsstd_array_clear( SGS_CTX, sgs_VarObj* data )
{
	sgs_Variable *var, *vend;
	SGSARR_HDR;
	var = SGSARR_PTR( data->data );
	vend = var + hdr->size;
	while( var < vend )
	{
		sgs_Release( C, var );
		var++;
	}
	hdr->size = 0;
}

static void sgsstd_array_insert( SGS_CTX, sgs_VarObj* data, uint32_t pos )
{
	int i;
	uint32_t cnt = sgs_StackSize( C ) - 1;
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
	for( i = 1; i < sgs_StackSize( C ); ++i )
	{
		sgs_Variable* var = sgs_StackItem( C, i );
		sgs_Acquire( C, var );
		ptr[ pos + i - 1 ] = *var;
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

static int sgsstd_array_destruct( SGS_CTX, sgs_VarObj* data )
{
	sgsstd_array_clear( C, data );
	sgs_Free( data->data );
	return 0;
}

#define SGSARR_IHDR \
	sgs_VarObj* data; \
	sgsstd_array_header_t* hdr; \
	if( !sgs_Method( C ) ){ sgs_Printf( C, SGS_ERROR, -1, "Function isn't called on an array" ); return 0; } \
	data = sgs_GetObjectData( C, 0 ); \
	hdr = (sgsstd_array_header_t*) data->data; \
	UNUSED( hdr );


static int sgsstd_arrayI_clear( SGS_CTX )
{
	SGSARR_IHDR;
	sgsstd_array_clear( C, data );
	return 0;
}
static int sgsstd_arrayI_push( SGS_CTX )
{
	SGSARR_IHDR;
	sgsstd_array_insert( C, data, hdr->size );
	return 0;
}
static int sgsstd_arrayI_pop( SGS_CTX )
{
	SGSARR_IHDR;
	sgs_Variable* ptr = SGSARR_PTR( data->data );
	if( !hdr->size )
	{
		sgs_Printf( C, SGS_ERROR, -1, "Array is empty, cannot pop." );
		return 0;
	}
	sgs_PushVariable( C, ptr + hdr->size - 1 );
	sgsstd_array_erase( C, data, hdr->size - 1, hdr->size - 1 );
	return 1;
}
static int sgsstd_arrayI_shift( SGS_CTX )
{
	SGSARR_IHDR;
	sgs_Variable* ptr = SGSARR_PTR( data->data );
	if( !hdr->size )
	{
		sgs_Printf( C, SGS_ERROR, -1, "Array is empty, cannot shift." );
		return 0;
	}
	sgs_PushVariable( C, ptr );
	sgsstd_array_erase( C, data, 0, 0 );
	return 1;
}
static int sgsstd_arrayI_unshift( SGS_CTX )
{
	SGSARR_IHDR;
	sgsstd_array_insert( C, data, 0 );
	return 0;
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
	else if( 0 == strcmp( name, "push" ) )		func = sgsstd_arrayI_push;
	else if( 0 == strcmp( name, "pop" ) )		func = sgsstd_arrayI_pop;
	else if( 0 == strcmp( name, "shift" ) )		func = sgsstd_arrayI_shift;
	else if( 0 == strcmp( name, "unshift" ) )	func = sgsstd_arrayI_unshift;
	else if( 0 == strcmp( name, "clear" ) )		func = sgsstd_arrayI_clear;
	else return SGS_ENOTFND;

	sgs_PushCFunction( C, func );
	return SGS_SUCCESS;
}

static int sgsstd_array_getindex( SGS_CTX, sgs_VarObj* data )
{
	SGSARR_HDR;
	sgs_Variable* ptr = SGSARR_PTR( data->data );
	sgs_Integer i = sgs_ToInt( C, -1 );
	if( i < 0 || i >= hdr->size )
	{
		sgs_Printf( C, SGS_ERROR, -1, "Array index out of bounds" );
		return SGS_EBOUNDS;
	}
	sgs_PushVariable( C, ptr + i );
	return SGS_SUCCESS;
}

static int sgsstd_array_setindex( SGS_CTX, sgs_VarObj* data )
{
	SGSARR_HDR;
	sgs_Variable* ptr = SGSARR_PTR( data->data );
	sgs_Integer i = sgs_ToInt( C, -2 );
	if( i < 0 || i >= hdr->size )
	{
		sgs_Printf( C, SGS_ERROR, -1, "Array index out of bounds" );
		return SGS_EBOUNDS;
	}
	sgs_Release( C, ptr + i );
	ptr[ i ] = *sgs_StackItem( C, -1 );
	sgs_Acquire( C, ptr + i );
	return SGS_SUCCESS;
}

static int sgsstd_array_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	sgs_PushString( C, "array" );
	return SGS_SUCCESS;
}

static int sgsstd_array_tobool( SGS_CTX, sgs_VarObj* data )
{
	SGSARR_HDR;
	sgs_PushBool( C, !!hdr->size );
	return SGS_SUCCESS;
}

static int sgsstd_array_tostring( SGS_CTX, sgs_VarObj* data )
{
	int cnt;
	SGSARR_HDR;
	sgs_Variable* var = SGSARR_PTR( data->data );
	sgs_Variable* vend = var + hdr->size;
	cnt = vend - var;

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
	return sgs_StringMultiConcat( C, cnt * 2 + 1 + !cnt );
}

int sgsstd_array_dump( SGS_CTX, sgs_VarObj* data )
{
	int i, depth = (int) sgs_ToInt( C, 0 );
	SGSARR_HDR;
	sgs_PushString( C, "array\n[" );
	if( depth )
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
	else
	{
		sgs_PushString( C, "\n..." );
		if( sgs_PadString( C ) )
			return SGS_EINPROC;
	}
	sgs_PushString( C, "\n]" );
	return sgs_StringMultiConcat( C, 3 );
}

#if 0
static void sgsstd_array_resize( SGS_CTX, sgs_VarObj* data, uint32_t size )
{
	SGSARR_HDR;
	if( size > hdr->mem )
		sgsstd_array_reserve( C, data, ( size < hdr->mem * 2 ) ? hdr->mem * 2 : size );
}
#endif

static int sgsstd_array_gcmark( SGS_CTX, sgs_VarObj* data )
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

static int sgsstd_array_iter_destruct( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( C );
	sgs_Free( data->data );
	return SGS_SUCCESS;
}

static int sgsstd_array_iter_nextkey( SGS_CTX, sgs_VarObj* data )
{
	sgsstd_array_iter_t* iter = (sgsstd_array_iter_t*) data->data;
	sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) iter->ref.data.O->data;
	if( iter->size != hdr->size )
		return SGS_EINVAL;

	sgs_PushInt( C, iter->off++ );
	sgs_PushBool( C, iter->off-1 < iter->size );
	return SGS_SUCCESS;
}

void* sgsstd_array_iter_functable[] =
{
	SOP_DESTRUCT, sgsstd_array_iter_destruct,
	SOP_NEXTKEY, sgsstd_array_iter_nextkey,
	SOP_END,
};

static int sgsstd_array_getiter( SGS_CTX, sgs_VarObj* data )
{
	SGSARR_HDR;
	sgsstd_array_iter_t* iter = sgs_Alloc( sgsstd_array_iter_t );

	iter->ref.type = SVT_OBJECT;
	iter->ref.data.O = data;
	iter->size = hdr->size;
	iter->off = 0;

	sgs_PushObject( C, iter, sgsstd_array_iter_functable );
	return SGS_SUCCESS;
}

void* sgsstd_array_functable[] =
{
	SOP_DESTRUCT, sgsstd_array_destruct,
	SOP_GETPROP, sgsstd_array_getprop,
	SOP_GETINDEX, sgsstd_array_getindex,
	SOP_SETINDEX, sgsstd_array_setindex,
	SOP_GETTYPE, sgsstd_array_gettype,
	SOP_TOBOOL, sgsstd_array_tobool,
	SOP_TOSTRING, sgsstd_array_tostring,
	SOP_DUMP, sgsstd_array_dump,
	SOP_GCMARK, sgsstd_array_gcmark,
	SOP_GETITER, sgsstd_array_getiter,
	SOP_END,
};

int sgsstd_array( SGS_CTX )
{
	int i, objcnt = sgs_StackSize( C );
	void* data = sgs_Malloc( SGSARR_ALLOCSIZE( objcnt ) );
	SGSARR_HDRBASE;
	hdr->size = objcnt;
	hdr->mem = objcnt;
	for( i = 0; i < objcnt; ++i )
	{
		sgs_Variable* var = sgs_StackItem( C, i );
		sgs_Acquire( C, var );
		SGSARR_PTR( data )[ i ] = *var;
	}
	sgs_PushObject( C, data, sgsstd_array_functable );
	return 1;
}


/*
	DICT
*/

#define HTHDR VHTable* ht = (VHTable*) data->data

static void _dict_clearvals( SGS_CTX, VHTable* ht )
{
	VHTableVar* p = ht->vars, *pend = ht->vars + vht_size( ht );
	while( p < pend )
	{
		sgs_Release( C, &p->var );
		p++;
	}
}

static int sgsstd_dict_destruct( SGS_CTX, sgs_VarObj* data )
{
	HTHDR;
	_dict_clearvals( C, ht );
	vht_free( ht, C );
	sgs_Free( ht );
	return SGS_SUCCESS;
}

static int sgsstd_dict_tostring( SGS_CTX, sgs_VarObj* data )
{
	HTHDR;
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

int sgsstd_dict_dump( SGS_CTX, sgs_VarObj* data )
{
	int depth = (int) sgs_ToInt( C, 0 );
	HTHDR;
	VHTableVar *pair = ht->vars, *pend = ht->vars + vht_size( ht );
	sgs_PushString( C, "dict\n{" );
	if( depth )
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
	else
	{
		sgs_PushString( C, "\n..." );
		if( sgs_PadString( C ) )
			return SGS_EINPROC;
	}
	sgs_PushString( C, "\n}" );
	return sgs_StringMultiConcat( C, 3 );
}

static int sgsstd_dict_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	sgs_PushString( C, "dict" );
	return SGS_SUCCESS;
}

static int sgsstd_dict_gcmark( SGS_CTX, sgs_VarObj* data )
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

static int sgsstd_dict_getindex( SGS_CTX, sgs_VarObj* data )
{
	HTHDR;
	VHTableVar* pair;
	sgs_ToString( C, -1 );
	if( sgs_StackItem( C, -1 )->type != SVT_STRING )
		return SGS_EINVAL;
	pair = vht_get( ht, sgs_GetStringPtr( C, -1 ), sgs_GetStringSize( C, -1 ) );
	if( !pair )
		return SGS_ENOTFND;
	sgs_PushVariable( C, &pair->var );
	return SGS_SUCCESS;
}

static int sgsstd_dict_setindex( SGS_CTX, sgs_VarObj* data )
{
	HTHDR;
	sgs_Variable* key;
	sgs_ToString( C, -2 );
	key = sgs_StackItem( C, -2 );
	if( key->type != SVT_STRING )
		return SGS_EINVAL;
	vht_set( ht, var_cstr( key ), key->data.S->size, sgs_StackItem( C, -1 ), C );
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

static int sgsstd_dict_iter_destruct( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( C );
	sgs_Free( data->data );
	return SGS_SUCCESS;
}

static int sgsstd_dict_iter_nextkey( SGS_CTX, sgs_VarObj* data )
{
	sgsstd_dict_iter_t* iter = (sgsstd_dict_iter_t*) data->data;
	VHTable* ht = (VHTable*) iter->ref.data.O->data;
	if( iter->size != ht->ht.load )
		return SGS_EINVAL;

	if( iter->off < iter->size )
		sgs_PushStringBuf( C, ht->vars[ iter->off ].str, ht->vars[ iter->off ].size );
	else
		sgs_PushNull( C );
	sgs_PushBool( C, iter->off < iter->size );
	iter->off++;
	return SGS_SUCCESS;
}

void* sgsstd_dict_iter_functable[] =
{
	SOP_DESTRUCT, sgsstd_dict_iter_destruct,
	SOP_NEXTKEY, sgsstd_dict_iter_nextkey,
	SOP_END,
};

static int sgsstd_dict_getiter( SGS_CTX, sgs_VarObj* data )
{
	HTHDR;
	sgsstd_dict_iter_t* iter = sgs_Alloc( sgsstd_dict_iter_t );

	iter->ref.type = SVT_OBJECT;
	iter->ref.data.O = data;
	iter->size = ht->ht.load;
	iter->off = 0;

	sgs_PushObject( C, iter, sgsstd_dict_iter_functable );
	return SGS_SUCCESS;
}

#define sgsstd_dict_getprop sgsstd_dict_getindex
#define sgsstd_dict_setprop sgsstd_dict_setindex

void* sgsstd_dict_functable[] =
{
	SOP_DESTRUCT, sgsstd_dict_destruct,
	SOP_GETPROP, sgsstd_dict_getprop,
	SOP_SETPROP, sgsstd_dict_setprop,
	SOP_GETINDEX, sgsstd_dict_getindex,
	SOP_SETINDEX, sgsstd_dict_setindex,
	SOP_TOSTRING, sgsstd_dict_tostring,
	SOP_DUMP, sgsstd_dict_dump,
	SOP_GETTYPE, sgsstd_dict_gettype,
	SOP_GCMARK, sgsstd_dict_gcmark,
	SOP_GETITER, sgsstd_dict_getiter,
	SOP_END,
};

int sgsstd_dict_internal( SGS_CTX )
{
	int i, objcnt = sgs_StackSize( C );
	VHTable* ht = sgs_Alloc( VHTable );
	vht_init( ht );
	for( i = 0; i < objcnt; i += 2 )
	{
		sgs_Variable* vkey = sgs_StackItem( C, i );
		vht_set( ht, var_cstr( vkey ), vkey->data.S->size, sgs_StackItem( C, i + 1 ), C );
	}
	sgs_PushObject( C, ht, sgsstd_dict_functable );
	return 1;
}

int sgsstd_dict( SGS_CTX )
{
	VHTable* ht;
	int i, objcnt = sgs_StackSize( C );

	if( objcnt % 2 != 0 )
		STDLIB_WARN( "dict() - unexpected argument count, function expects 0 or an even number of arguments" )

	ht = sgs_Alloc( VHTable );
	vht_init( ht );

	for( i = 0; i < objcnt; i += 2 )
	{
		char* kstr;
		sgs_SizeVal ksize;
		if( !sgs_ParseString( C, i, &kstr, &ksize ) )
		{
			_dict_clearvals( C, ht );
			vht_free( ht, C );
			sgs_Free( ht );
			sgs_Printf( C, SGS_WARNING, -1, "dict() - key argument %d is not a string", i );
			return 0;
		}

		vht_set( ht, kstr, (int32_t) ksize, sgs_StackItem( C, i + 1 ), C );
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

int sgsstd_class_destruct( SGS_CTX, sgs_VarObj* data )
{
	SGSCLASS_HDR;
	sgs_Release( C, &hdr->data );
	sgs_Release( C, &hdr->inh );
	sgs_Free( hdr );
	return SGS_SUCCESS;
}

int sgsstd_class_getindex( SGS_CTX, sgs_VarObj* data )
{
	int ret;
	sgs_Variable var, idx;
	SGSCLASS_HDR;
	if( strcmp( sgs_ToString( C, -1 ), "_super" ) == 0 )
	{
		sgs_PushVariable( C, &hdr->inh );
		return SGS_SUCCESS;
	}
	idx = *sgs_StackItem( C, -1 );
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

int sgsstd_class_setindex( SGS_CTX, sgs_VarObj* data )
{
	SGSCLASS_HDR;
	if( strcmp( sgs_ToString( C, -2 ), "_super" ) == 0 )
	{
		sgs_Release( C, &hdr->inh );
		hdr->inh = *sgs_StackItem( C, -1 );
		sgs_Acquire( C, &hdr->inh );
		return SGS_SUCCESS;
	}
	return sgs_SetIndex( C, &hdr->data, sgs_StackItem( C, -2 ), sgs_StackItem( C, -1 ) );
}

#define sgsstd_class_getprop sgsstd_class_getindex
#define sgsstd_class_setprop sgsstd_class_setindex

int sgsstd_class_getmethod( SGS_CTX, sgs_VarObj* data, const char* method )
{
	int ret;
	sgs_Variable var, idx;
	SGSCLASS_HDR;

	sgs_PushString( C, method );
	idx = *sgs_StackItem( C, -1 );
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

int sgsstd_class_tostring( SGS_CTX, sgs_VarObj* data )
{
	if( sgsstd_class_getmethod( C, data, "__tostr" ) )
		return sgs_Call( C, 1, 1 );

	sgs_PushString( C, "class" );
	return SGS_SUCCESS;
}

int sgsstd_class_dump( SGS_CTX, sgs_VarObj* data )
{
	int depth = (int) sgs_ToInt( C, 0 );
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

int sgsstd_class_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	sgs_PushString( C, "class" );
	return SGS_SUCCESS;
}

int sgsstd_class_gcmark( SGS_CTX, sgs_VarObj* data )
{
	SGSCLASS_HDR;
	int ret = sgs_GCMark( C, &hdr->data );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_GCMark( C, &hdr->inh );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

#define class_op_wrapper( op ) \
int sgsstd_class_##op ( SGS_CTX, sgs_VarObj* data ){   \
	if( sgsstd_class_getmethod( C, data, "__" #op ) ){ \
		return sgs_Call( C, 2, 1 ); } return SGS_ENOTFND; }
class_op_wrapper( add )
class_op_wrapper( sub )
class_op_wrapper( mul )
class_op_wrapper( div )
class_op_wrapper( mod )
class_op_wrapper( compare )
#undef class_op_wrapper

int sgsstd_class_negate( SGS_CTX, sgs_VarObj* data )
{
	if( sgsstd_class_getmethod( C, data, "__negate" ) )
		return sgs_Call( C, 1, 1 );

	return SGS_ENOTFND;
}

int sgsstd_class_call( SGS_CTX, sgs_VarObj* data )
{
	if( sgsstd_class_getmethod( C, data, "__call" ) )
	{
		int ret, i;
		sgs_Variable var;
		var.type = SVT_OBJECT;
		var.data.O = data;
		sgs_PushVariable( C, &var );
		for( i = 0; i < C->call_args; ++i )
			sgs_PushVariable( C, sgs_StackItem( C, i ) );
		ret = sgsVM_VarCall( C, sgs_StackItem( C, -2 - C->call_args ), C->call_args, C->call_expect, TRUE );
		return ret;
	}
	return SGS_ENOTFND;
}

void* sgsstd_class_functable[] =
{
	SOP_DESTRUCT, sgsstd_class_destruct,
	SOP_GETPROP, sgsstd_class_getprop,
	SOP_SETPROP, sgsstd_class_setprop,
	SOP_GETINDEX, sgsstd_class_getindex,
	SOP_SETINDEX, sgsstd_class_setindex,
	SOP_TOSTRING, sgsstd_class_tostring,
	SOP_DUMP, sgsstd_class_dump,
	SOP_GETTYPE, sgsstd_class_gettype,
	SOP_GCMARK, sgsstd_class_gcmark,
	SOP_OP_ADD, sgsstd_class_add,
	SOP_OP_SUB, sgsstd_class_sub,
	SOP_OP_MUL, sgsstd_class_mul,
	SOP_OP_DIV, sgsstd_class_div,
	SOP_OP_MOD, sgsstd_class_mod,
	SOP_OP_NEGATE, sgsstd_class_negate,
	SOP_COMPARE, sgsstd_class_compare,
	SOP_CALL, sgsstd_class_call,
	SOP_END,
};

int sgsstd_class( SGS_CTX )
{
	sgsstd_class_header_t* hdr;
	if( sgs_StackSize( C ) != 2 )
		goto argerr;

	hdr = sgs_Alloc( sgsstd_class_header_t );
	hdr->data = *sgs_StackItem( C, 0 );
	hdr->inh = *sgs_StackItem( C, 1 );
	sgs_Acquire( C, &hdr->data );
	sgs_Acquire( C, &hdr->inh );
	sgs_PushObject( C, hdr, sgsstd_class_functable );
	return 1;

argerr:
	sgs_Printf( C, SGS_ERROR, -1, "'class' requires 2 arguments: data, inherited" );
	return 0;
}


/* CLOSURE */

typedef struct sgsstd_closure_s
{
	sgs_Variable func;
	sgs_Variable data;
}
sgsstd_closure_t;


#define SGSCLOSURE_HDR sgsstd_closure_t* hdr = (sgsstd_closure_t*) data->data;

int sgsstd_closure_destruct( SGS_CTX, sgs_VarObj* data )
{
	SGSCLOSURE_HDR;
	sgs_Release( C, &hdr->func );
	sgs_Release( C, &hdr->data );
	sgs_Free( hdr );
	return SGS_SUCCESS;
}

int sgsstd_closure_tostring( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	sgs_PushString( C, "closure" );
	return SGS_SUCCESS;
}

int sgsstd_closure_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	sgs_PushString( C, "class" );
	return SGS_SUCCESS;
}

int sgsstd_closure_gcmark( SGS_CTX, sgs_VarObj* data )
{
	SGSCLOSURE_HDR;
	int ret = sgs_GCMark( C, &hdr->func );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_GCMark( C, &hdr->data );
	if( ret != SGS_SUCCESS ) return ret;
	return SGS_SUCCESS;
}

int sgsstd_closure_call( SGS_CTX, sgs_VarObj* data )
{
	int i;
	SGSCLOSURE_HDR;
	sgs_PushVariable( C, &hdr->data );
	for( i = 0; i < C->call_args; ++i )
		sgs_PushVariable( C, sgs_StackItem( C, i ) );
	return sgsVM_VarCall( C, &hdr->func, C->call_args, C->call_expect, TRUE );
}

void* sgsstd_closure_functable[] =
{
	SOP_DESTRUCT, sgsstd_closure_destruct,
	SOP_TOSTRING, sgsstd_closure_tostring,
	SOP_GETTYPE, sgsstd_closure_gettype,
	SOP_GCMARK, sgsstd_closure_gcmark,
	SOP_CALL, sgsstd_closure_call,
	SOP_END,
};

int sgsstd_closure( SGS_CTX )
{
	sgsstd_closure_t* hdr;
	if( sgs_StackSize( C ) != 2 )
		goto argerr;

	hdr = sgs_Alloc( sgsstd_closure_t );
	hdr->func = *sgs_StackItem( C, 0 );
	hdr->data = *sgs_StackItem( C, 1 );
	sgs_Acquire( C, &hdr->func );
	sgs_Acquire( C, &hdr->data );
	sgs_PushObject( C, hdr, sgsstd_closure_functable );
	return 1;

argerr:
	sgs_Printf( C, SGS_ERROR, -1, "'closure' requires 2 arguments: function, data" );
	return 0;
}


/* UTILITIES */

int sgsstd_isset( SGS_CTX )
{
	sgs_Variable* var;
	VHTable* ht;
	VHTableVar* pair;
	if( sgs_StackSize( C ) != 2 )
		goto argerr;

	var = sgs_StackItem( C, -2 );
	if( var->type != SVT_OBJECT || var->data.O->iface != sgsstd_dict_functable )
		goto argerr;

	sgs_ToString( C, -1 );
	if( sgs_StackItem( C, -1 )->type != SVT_STRING )
		goto argerr;
	ht = (VHTable*) var->data.O->data;

	pair = vht_get( ht, sgs_GetStringPtr( C, -1 ), sgs_GetStringSize( C, -1 ) );
	sgs_PushBool( C, pair != NULL );
	return 1;

argerr:
	sgs_Printf( C, SGS_ERROR, -1, "'isset' requires 2 arguments: object (dict), property name (string)" );
	return 0;
}

int sgsstd_unset( SGS_CTX )
{
	sgs_Variable* var;
	VHTable* ht;
	if( sgs_StackSize( C ) != 2 )
		goto argerr;

	var = sgs_StackItem( C, -2 );
	if( var->type != SVT_OBJECT || var->data.O->iface != sgsstd_dict_functable )
		goto argerr;

	sgs_ToString( C, -1 );
	if( sgs_StackItem( C, -1 )->type != SVT_STRING )
		goto argerr;

	ht = (VHTable*) var->data.O->data;
	vht_unset( ht, sgs_GetStringPtr( C, -1 ), sgs_GetStringSize( C, -1 ), C );

	return 0;

argerr:
	sgs_Printf( C, SGS_ERROR, -1, "'unset' requires 2 arguments: object (dict), property name (string)" );
	return 0;
}



/* I/O */

static int sgsstd_print( SGS_CTX )
{
	int i, ssz;
	ssz = sgs_StackSize( C );
	for( i = 0; i < ssz; ++i )
	{
		printf( "%s", sgs_ToString( C, i ) );
	}
	return 0;
}

static int sgsstd_printvar( SGS_CTX )
{
	sgs_Integer depth = 5;
	int ssz = sgs_StackSize( C );

	if( ssz < 1 || ssz > 2 ||
		( ssz == 2 && !sgs_ParseInt( C, 1, &depth ) ) )
		STDLIB_WARN( "printvar(): unexpected arguments; function expects <any>[, int]" );

	if( ssz == 2 )
		sgs_Pop( C, 1 );
	if( sgs_DumpVar( C, depth ) == SGS_SUCCESS )
	{
		fputs( sgs_ToString( C, -1 ), stdout );
		fputs( "\n", stdout );
	}
	else
		STDLIB_WARN( "printvar(): unknown error while dumping variable" );
	return 0;
}
static int sgsstd_printvars( SGS_CTX )
{
	int i, ssz;
	ssz = sgs_StackSize( C );
	for( i = 0; i < ssz; ++i )
	{
		sgs_PushItem( C, i );
		int res = sgs_DumpVar( C, 5 );
		if( res == SGS_SUCCESS )
		{
			fputs( sgs_ToString( C, -1 ), stdout );
			fputs( "\n", stdout );
		}
		else
		{
			char ebuf[ 64 ];
			sprintf( ebuf, "printvars(): unknown error while dumping variable #%d", i + 1 );
			STDLIB_WARN( ebuf );
		}
		sgs_Pop( C, 1 );
	}
	return 0;
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
	int rvc = 0;

	if( sgs_StackSize( C ) != 1 || !sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "eval() - unexpected arguments; function expects 1 argument: string" )

	sgs_EvalBuffer( C, str, (int) size, &rvc );
	return rvc;
}

static int sgsstd_include_library( SGS_CTX )
{
	int ret = SGS_ENOTFND;
	char* str;
	sgs_SizeVal strsize;

	if( sgs_StackSize( C ) != 1 || !sgs_ParseString( C, 0, &str, &strsize ) )
		STDLIB_WARN( "include_library() - unexpected arguments; function expects 1 argument: string" )

	if( strcmp( str, "math" ) == 0 )
		ret = sgs_LoadLib_Math( C );
#if 0
	else if( strcmp( str, "native" ) == 0 )
		ret = sgs_LoadLib_Native( C );
#endif
	else if( strcmp( str, "string" ) == 0 )
		ret = sgs_LoadLib_String( C );
	else if( strcmp( str, "type" ) == 0 )
		ret = sgs_LoadLib_Type( C );

	if( ret == SGS_ENOTFND )
		STDLIB_WARN( "include_library() - library not found" )
	sgs_PushBool( C, ret == SGS_SUCCESS );
	return 1;
}

static int sgsstd_include_file( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal strsize;

	if( sgs_StackSize( C ) != 1 || !sgs_ParseString( C, 0, &str, &strsize ) )
		STDLIB_WARN( "include_file() - unexpected arguments; function expects 1 argument: string" )

	ret = sgs_ExecFile( C, str );
	if( ret == SGS_ENOTFND )
		STDLIB_WARN( "include_file() - file not found" )
	sgs_PushBool( C, ret == SGS_SUCCESS );
	return 1;
}

static int sgsstd_include_shared( SGS_CTX )
{
	char* fnstr;
	sgs_SizeVal fnsize;
	int ret, argc = sgs_StackSize( C );
	sgs_CFunc func;

	if( argc != 1 || !sgs_ParseString( C, 0, &fnstr, &fnsize ) )
		STDLIB_WARN( "include_shared() - unexpected arguments; function expects 1 argument: string" )

	ret = sgs_GetProcAddress( fnstr, "sgscript_main", (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE ) STDLIB_WARN( "include_shared() - file not found" )
		else if( ret == SGS_XPC_NOPROC ) STDLIB_WARN( "include_shared() - procedure not found" )
		else if( ret == SGS_XPC_NOTSUP ) STDLIB_WARN( "include_shared() - feature is not supported on this platform" )
		else STDLIB_WARN( "include_shared() - unknown error occured" )
	}
	
	return func( C );
}

static int sgsstd_import_cfunc( SGS_CTX )
{
	char* fnstr, *pnstr;
	sgs_SizeVal fnsize, pnsize;
	int ret, argc = sgs_StackSize( C );
	sgs_CFunc func;

	if( argc != 2 || !sgs_ParseString( C, 0, &fnstr, &fnsize ) ||
		!sgs_ParseString( C, 1, &pnstr, &pnsize ) )
		STDLIB_WARN( "import_cfunc() - unexpected arguments; function expects 2 arguments: string, string" )

	ret = sgs_GetProcAddress( fnstr, pnstr, (void**) &func );
	if( ret != 0 )
	{
		if( ret == SGS_XPC_NOFILE ) STDLIB_WARN( "import_cfunc() - file not found" )
		else if( ret == SGS_XPC_NOPROC ) STDLIB_WARN( "import_cfunc() - procedure not found" )
		else if( ret == SGS_XPC_NOTSUP ) STDLIB_WARN( "import_cfunc() - feature is not supported on this platform" )
		else STDLIB_WARN( "import_cfunc() - unknown error occured" )
	}
	
	sgs_PushCFunction( C, func );
	return 1;
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

static int sgsstd_sys_stat( SGS_CTX )
{
	sgs_Integer type;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseInt( C, 0, &type ) )
		STDLIB_WARN( "sys_stat(): unexpected arguments; function expects int" );

	sgs_Stat( C, type );
	return 0;
}

static int sgsstd_dumpvar( SGS_CTX )
{
	sgs_Integer depth = 5;
	int ssz = sgs_StackSize( C );

	if( ssz < 1 || ssz > 2 ||
		( ssz == 2 && !sgs_ParseInt( C, 1, &depth ) ) )
		STDLIB_WARN( "dumpvar(): unexpected arguments; function expects <any>[, int]" );

	if( ssz == 2 )
		sgs_Pop( C, 1 );
	if( sgs_DumpVar( C, depth ) == SGS_SUCCESS )
		return 1;
	else
		STDLIB_WARN( "dumpvar(): unknown error while dumping variable" );
	return 0;
}
static int sgsstd_dumpvars( SGS_CTX )
{
	int i, ssz, rc = 0;
	ssz = sgs_StackSize( C );
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
			sprintf( ebuf, "dumpvars(): unknown error while dumping variable #%d", i + 1 );
			STDLIB_WARN( ebuf );
			sgs_Pop( C, 1 );
		}
	}
	if( rc )
	{
		if( sgs_StringMultiConcat( C, rc * 2 ) == SGS_SUCCESS )
			STDLIB_WARN( "dumpvars(): failed to concatenate the output" );
		return 1;
	}
	return 0;
}

static int sgsstd_gc_collect( SGS_CTX )
{
	int32_t orvc = sgs_Stat( C, SGS_STAT_VARCOUNT );
	int ret = sgs_GCExecute( C );
	if( ret == SGS_SUCCESS )
		sgs_PushInt( C, orvc - sgs_Stat( C, SGS_STAT_VARCOUNT ) );
	else
		sgs_PushBool( C, FALSE );
	return 1;
}


/* register all */
#define FN( name ) { #name, sgsstd_##name }
sgs_RegFuncConst regfuncs[] =
{
	/* containers */
	FN( array ), FN( dict ), { "class", sgsstd_class }, FN( closure ),
	FN( isset ), FN( unset ),
	/* I/O */
	FN( print ), FN( printvar ), FN( printvars ),
	/* OS */
	FN( ftime ),
	/* utils */
	FN( eval ), FN( include_library ), FN( include_file ),
	FN( include_shared ), FN( import_cfunc ),
	FN( sys_errorstate ), FN( sys_abort ), FN( sys_stat ),
	FN( dumpvar ), FN( dumpvars ),
	FN( gc_collect ),
};

#if 0
sgs_RegIntConst regiconsts[] =
{
	{ "__dummy", 0 },
};
#endif

int sgsVM_RegStdLibs( SGS_CTX )
{
	int ret;
#if 0
	ret = sgs_RegIntConsts( C, regiconsts, ARRAY_SIZE( regiconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
#endif
	ret = sgs_RegFuncConsts( C, regfuncs, ARRAY_SIZE( regfuncs ) );
	if( ret != SGS_SUCCESS ) return ret;

	C->array_func = &sgsstd_array;
	C->dict_func = &sgsstd_dict_internal;

	return SGS_SUCCESS;
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
