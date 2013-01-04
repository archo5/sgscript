

#include <math.h>
#include <stdarg.h>

#include "sgs_xpc.h"
#include "sgs_ctx.h"
#include "sgs_proc.h"


static const char* sgs_VarNames[] =
{
	"Null",
	"Bool",
	"Int",
	"Real",
	"String",
	"Function",
	"C function",
	"Object",
};

/*
sgs_Variable* make_var( SGS_CTX, int type );
void destroy_var( SGS_CTX, sgs_Variable* var );
*/
#define VAR_ACQUIRE( pvar ) { if( pvar )(pvar)->refcount++; }
#define VAR_RELEASE( pvar ) { if( pvar ){ (pvar)->refcount--; sgs_BreakIf( (pvar)->refcount < 0 ); if( (pvar)->refcount == 0 ) destroy_var( C, (pvar) ); } }

#define STK_UNITSIZE sizeof( sgs_VarPtr )


static int obj_exec( SGS_CTX, void* sop, sgs_VarObj* data )
{
	void** func = data->iface;
	while( *func != SOP_END )
	{
		if( *func == sop )
			return ( (sgs_ObjCallback) func[ 1 ] )( C, data );
		func += 2;
	}

	return SGS_ENOTFND;
}


static void funct_copy( SGS_CTX, funct* to, funct* from )
{
	sgs_Variable** vbeg = AS_( from->bytecode, sgs_Variable** );
	sgs_Variable** vend = AS_( from->bytecode + from->instr_off, sgs_Variable** );
	sgs_Variable** var = vbeg;

	UNUSED( C );
	while( var < vend )
	{
		VAR_ACQUIRE( *var );
		var++;
	}

	to->bytecode = sgs_Alloc_n( char, from->size );
	to->instr_off = from->instr_off;
	to->size = from->size;

	memcpy( to->bytecode, from->bytecode, from->size );
}

static void funct_destroy( SGS_CTX, funct* fn )
{
	sgs_Variable** vbeg = (sgs_Variable**) fn->bytecode;
	sgs_Variable** vend = (sgs_Variable**) ( fn->bytecode + fn->instr_off );
	sgs_Variable** var = vbeg;

	while( var < vend )
	{
		VAR_RELEASE( *var );
		var++;
	}

	sgs_Free( fn->bytecode );
}


static SGS_INLINE sgs_Variable* var_alloc( SGS_CTX )
{
	if( C->poolsize > 0 )
	{
		sgs_Variable* vout = C->pool;
		C->pool = vout->next;
		C->poolsize--;
		return vout;
	}
	else
	{
#if SGS_DEBUG && SGS_DEBUG_PERF
		printf( "[perf] var alloc\n" );
#endif
		return sgs_Alloc( sgs_Variable );
	}
}

static SGS_INLINE void var_free( SGS_CTX, sgs_Variable* var )
{
	if( C->poolsize < C->maxpool )
	{
		var->next = C->pool;
		C->pool = var;
		C->poolsize++;
	}
	else
	{
#if SGS_DEBUG && SGS_DEBUG_PERF
		printf( "[perf] var free\n" );
#endif
		sgs_Free( var );
	}
}

sgs_Variable* make_var( SGS_CTX, int type )
{
	sgs_Variable* var = var_alloc( C );
	var->type = type;
	var->refcount = 1;
	var->redblue = C->redblue;
	var->destroying = 0;
	var->prev = NULL;
	var->next = C->vars;
	if( var->next )
		var->next->prev = var;
	C->vars = var;
	C->varcount++;
	return var;
}

#define var_cstr( var ) (var)->data.S.ptr

static void var_destroy( SGS_CTX, sgs_Variable* var )
{
#if SGS_DEBUG && SGS_DEBUG_STATE
	sgs_BreakIf( var->type == 0 );
	var->type = 0;
#endif
	switch( var->type )
	{
	case SVT_STRING:
		strbuf_destroy( &var->data.S );
		break;
	case SVT_FUNC:
		funct_destroy( C, &var->data.F );
		break;
	case SVT_OBJECT:
		obj_exec( C, SOP_DESTRUCT, &var->data.O );
		break;
	}
}

static sgs_Variable* var_create_0str( SGS_CTX, int32_t len )
{
	sgs_Variable* var = make_var( C, SVT_STRING );
	var->data.S = strbuf_create();
	strbuf_resize( &var->data.S, len );
	return var;
}

sgs_Variable* var_create_str( SGS_CTX, const char* str, int32_t len )
{
	sgs_Variable* var;
	sgs_BreakIf( !str );

	if( len < 0 )
		len = strlen( str );

	var = var_create_0str( C, len );
	memcpy( var_cstr( var ), str, len );
	return var;
}

void destroy_var( SGS_CTX, sgs_Variable* var )
{
	sgs_BreakIf( var->prev != NULL && var->prev == var->next );

	var->destroying = 1;
	var_destroy( C, var );

	if( var->prev ) var->prev->next = var->next;
	else C->vars = var->next;
	if( var->next ) var->next->prev = var->prev;
	C->varcount--;

	var_free( C, var );
}




/*
	Stack management
*/

#if SGS_DEBUG && SGS_DEBUG_EXTRA
 #define DBG_STACK_CHECK sgs_MemCheckDbg( C->stack_base );
#else
 #define DBG_STACK_CHECK
#endif

static SGS_INLINE sgs_VarPtr stk_gettop( SGS_CTX )
{
#if SGS_DEBUG_EXTRA
    sgs_BreakIf( C->stack_top == C->stack_base );
    DBG_STACK_CHECK
#endif
    return *( C->stack_top - 1 );
}

static SGS_INLINE int stk_absindex( SGS_CTX, int stkid )
{
	if( stkid < 0 ) return C->stack_top + stkid - C->stack_off;
	else return stkid;
}

static SGS_INLINE sgs_VarPtr* stk_getpos( SGS_CTX, int stkid )
{
#if SGS_DEBUG_EXTRA
	DBG_STACK_CHECK
    if( stkid < 0 ) sgs_BreakIf( -stkid > C->stack_size )
    else            sgs_BreakIf( stkid >= C->stack_top - C->stack_off )
#endif
	if( stkid < 0 )	return C->stack_top + stkid;
	else			return C->stack_off + stkid;
}

static SGS_INLINE sgs_VarPtr stk_getvar( SGS_CTX, int stkid ){ return *stk_getpos( C, stkid ); }

static SGS_INLINE void stk_setvar( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr* vpos = stk_getpos( C, stkid );
	VAR_ACQUIRE( var );
	VAR_RELEASE( *vpos );
	*vpos = var;
}
static SGS_INLINE void stk_setvar_leave( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr* vpos = stk_getpos( C, stkid );
	VAR_RELEASE( *vpos );
	*vpos = var;
}

static SGS_INLINE sgs_VarPtr* stk_getlpos( SGS_CTX, int stkid )
{
#if SGS_DEBUG_EXTRA
	DBG_STACK_CHECK
	sgs_BreakIf( stkid >= C->stack_top - C->stack_off );
#endif
	return C->stack_off + stkid;
}
static SGS_INLINE sgs_VarPtr stk_getlvar( SGS_CTX, int stkid ){ return *stk_getlpos( C, stkid ); }
static SGS_INLINE void stk_setlvar( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr* vpos = stk_getlpos( C, stkid );
	VAR_RELEASE( *vpos );
	*vpos = var;
	VAR_ACQUIRE( var );
}

static void stk_makespace( SGS_CTX, int num )
{
	int stkoff, stkend, nsz, stksz = C->stack_top - C->stack_base;
	sgs_VarPtr* nmem;
	if( stksz + num <= C->stack_mem )
		return;
	stkoff = C->stack_off - C->stack_base;
	stkend = C->stack_top - C->stack_base;
	DBG_STACK_CHECK
	nsz = ( stksz + num ) + C->stack_mem * 2; /* MAX( stksz + num, C->stack_mem * 2 ); */
	nmem = sgs_Alloc_n( sgs_VarPtr, nsz );
	if( stksz )
		memcpy( nmem, C->stack_base, stksz * STK_UNITSIZE );
	sgs_Free( C->stack_base );
	C->stack_base = nmem;
	C->stack_off = C->stack_base + stkoff;
	C->stack_top = C->stack_base + stkend;
}

static void stk_makespace1( SGS_CTX )
{
	int stksz = C->stack_top - C->stack_base;
	int ssz1 = stksz + 1;
	if( ssz1 > C->stack_mem )
	{
		sgs_VarPtr* nmem;
		int stkoff, stkend, nsz;

		stkoff = C->stack_off - C->stack_base;
		stkend = C->stack_top - C->stack_base;
		nsz = ssz1 + C->stack_mem * 2; /* MAX( stksz + 1, C->stack_mem * 2 ); */

		DBG_STACK_CHECK

		nmem = sgs_Alloc_n( sgs_VarPtr, nsz );
		if( stksz )
			memcpy( nmem, C->stack_base, stksz * STK_UNITSIZE );
		sgs_Free( C->stack_base );
		C->stack_base = nmem;
		C->stack_off = C->stack_base + stkoff;
		C->stack_top = C->stack_base + stkend;
	}
}

static void stk_push( SGS_CTX, sgs_VarPtr var )
{
	stk_makespace1( C );
	VAR_ACQUIRE( var );
	*C->stack_top++ = var;
}

static void stk_push_leave( SGS_CTX, sgs_VarPtr var )
{
	stk_makespace1( C );
	*C->stack_top++ = var;
}

static void stk_clean( SGS_CTX, sgs_VarPtr* from, sgs_VarPtr* to )
{
	int len = to - from;
	int oh = C->stack_top - to;
	sgs_VarPtr *p = from, *pend = to;
	sgs_BreakIf( C->stack_top - C->stack_base < len );
	sgs_BreakIf( to < from );
	DBG_STACK_CHECK

	C->stack_top -= len;

	while( p < pend )
	{
		VAR_RELEASE( *p );
#if SGS_DEBUG
		*p = NULL;
#endif
		p++;
	}

	if( oh )
		memmove( from, to, oh * STK_UNITSIZE );
}

static void stk_popskip( SGS_CTX, int num, int skip )
{
	sgs_VarPtr *off, *ptr;
	if( num <= 0 ) return;
	DBG_STACK_CHECK
	off = C->stack_top - skip;
	ptr = off - num;
	stk_clean( C, ptr, off );
}

#define stk_pop( C, num ) stk_popskip( C, num, 0 )

static void stk_pop1( SGS_CTX )
{
	sgs_BreakIf( C->stack_top - C->stack_off < 1 );
	DBG_STACK_CHECK

	C->stack_top--;
	VAR_RELEASE( *C->stack_top );
#if SGS_DEBUG
	*C->stack_top = NULL;
#endif
}

static void stk_pop2( SGS_CTX )
{
	sgs_BreakIf( C->stack_top - C->stack_off < 1 );
	DBG_STACK_CHECK

	C->stack_top -= 2;
	VAR_RELEASE( C->stack_top[ 0 ] );
	VAR_RELEASE( C->stack_top[ 1 ] );
#if SGS_DEBUG
	C->stack_top[ 0 ] = NULL;
	C->stack_top[ 1 ] = NULL;
#endif
}


/*
	Conversions
*/

static int init_var_bool( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	UNUSED( C );
	if( !var )
	{
		out->data.B = FALSE;
		return SGS_SUCCESS;
	}

	switch( var->type )
	{
	case SVT_INT: out->data.B = !!var->data.I; break;
	case SVT_REAL: out->data.B = !!var->data.R; break;
	case SVT_STRING: out->data.B = !!*var_cstr( var ); break;
	case SVT_FUNC:
	case SVT_CFUNC: out->data.B = TRUE; break;
	}
	return SGS_SUCCESS;
}

static int init_var_int( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	UNUSED( C );
	if( !var )
	{
		out->data.I = 0;
		return SGS_SUCCESS;
	}

	switch( var->type )
	{
	case SVT_BOOL: out->data.I = (sgs_Integer) var->data.B; break;
	case SVT_REAL: out->data.I = (sgs_Integer) var->data.R; break;
	case SVT_STRING: out->data.I = util_atoi( var->data.S.ptr, var->data.S.size ); break;
	case SVT_FUNC:
	case SVT_CFUNC: return SGS_ENOTSUP;
	}
	return SGS_SUCCESS;
}

static int init_var_real( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	UNUSED( C );
	if( !var )
	{
		out->data.R = 0;
		return SGS_SUCCESS;
	}

	switch( var->type )
	{
	case SVT_BOOL: out->data.R = (sgs_Real) var->data.B; break;
	case SVT_INT: out->data.R = (sgs_Real) var->data.I; break;
	case SVT_STRING: out->data.R = util_atof( var->data.S.ptr, var->data.S.size ); break;
	case SVT_FUNC:
	case SVT_CFUNC: return SGS_ENOTSUP;
	}
	return SGS_SUCCESS;
}

static int init_var_string( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	char buf[ 32 ];
	UNUSED( C );

	out->data.S = strbuf_create();
	if( !var )
	{
		strbuf_appstr( &out->data.S, "null" );
		return SGS_SUCCESS;
	}

	switch( var->type )
	{
	case SVT_BOOL: strbuf_appstr( &out->data.S, var->data.B ? "true" : "false" ); break;
	case SVT_INT: sprintf( buf, "%" PRId64, var->data.I ); strbuf_appstr( &out->data.S, buf ); break;
	case SVT_REAL: sprintf( buf, "%g", var->data.R ); strbuf_appstr( &out->data.S, buf ); break;
	case SVT_FUNC: strbuf_appstr( &out->data.S, "Function" ); break;
	case SVT_CFUNC: strbuf_appstr( &out->data.S, "C Function" ); break;
	}
	return SGS_SUCCESS;
}

static int vm_convert( SGS_CTX, int item, int type )
{
	sgs_Variable *var, *nv;
	int ret = SGS_ENOTSUP;

	var = stk_getvar( C, item );
	if( var && var->type == type )
		return SGS_SUCCESS;

#if SGS_DEBUG && SGS_DEBUG_PERF
	printf( "[perf] var convert, %d to %d\n", var ? var->type : 0, type );
#endif

	if( var && var->type == SVT_OBJECT )
	{
		void* sop = NULL;
		switch( type )
		{
		case SVT_BOOL: sop = SOP_TOBOOL; break;
		case SVT_INT: sop = SOP_TOINT; break;
		case SVT_REAL: sop = SOP_TOREAL; break;
		case SVT_STRING: sop = SOP_TOSTRING; break;
		default:
			return SGS_ENOTFND;
		}
		ret = obj_exec( C, sop, &var->data.O );
		stk_setvar( C, item, stk_getvar( C, -1 ) );
		stk_pop1( C );
		return ret;
	}

	nv = make_var( C, type );
	switch( type )
	{
	case SVT_BOOL: ret = init_var_bool( C, nv, var ); break;
	case SVT_INT: ret = init_var_int( C, nv, var ); break;
	case SVT_REAL: ret = init_var_real( C, nv, var ); break;
	case SVT_STRING: ret = init_var_string( C, nv, var ); break;
	default:
		sgs_Free( nv );
		return SGS_ENOTSUP;
	}

	if( ret == SGS_SUCCESS )
	{
		stk_setvar( C, item, nv );
		VAR_RELEASE( nv );
	}

	return ret;
}

static int var_getbool( SGS_CTX, sgs_VarPtr var )
{
	if( !var )
		return FALSE;
	switch( var->type )
	{
	case SVT_BOOL: return var->data.B;
	case SVT_INT: return !!var->data.I;
	case SVT_REAL: return !!var->data.R;
	case SVT_STRING: return !!var->data.S.size;
	case SVT_FUNC: return TRUE;
	case SVT_CFUNC: return TRUE;
	case SVT_OBJECT:
		{
			int out;
			stk_push( C, var );
			if( vm_convert( C, -1, SVT_BOOL ) != SGS_SUCCESS )
			{
				stk_pop1( C );
				return 0;
			}
			out = var->data.B;
			stk_pop1( C );
			return out;
		}
	}
	return FALSE;
}

static int var_getint( SGS_CTX, sgs_VarPtr var )
{
	if( !var )
		return 0;
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Integer) var->data.B;
	case SVT_INT: return var->data.I;
	case SVT_REAL: return (sgs_Integer) var->data.R;
	case SVT_STRING: return util_atoi( var->data.S.ptr, var->data.S.size );
	case SVT_FUNC: return 0;
	case SVT_CFUNC: return 0;
	case SVT_OBJECT:
		{
			sgs_Integer out;
			stk_push( C, var );
			if( vm_convert( C, -1, SVT_INT ) != SGS_SUCCESS )
			{
				stk_pop1( C );
				return 0;
			}
			out = var->data.I;
			stk_pop1( C );
			return out;
		}
	}
	return 0;
}

static sgs_Real var_getreal( SGS_CTX, sgs_Variable* var )
{
	if( !var )
		return 0;
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Real) var->data.B;
	case SVT_INT: return (sgs_Real) var->data.I;
	case SVT_REAL: return var->data.R;
	case SVT_STRING: return util_atof( var->data.S.ptr, var->data.S.size );
	case SVT_FUNC: return 0;
	case SVT_CFUNC: return 0;
	case SVT_OBJECT:
		{
			sgs_Real out;
			stk_push( C, var );
			if( vm_convert( C, -1, SVT_REAL ) != SGS_SUCCESS )
			{
				stk_pop1( C );
				return 0;
			}
			out = var->data.R;
			stk_pop1( C );
			return out;
		}
	}
	return 0;
}


/*
	VM mutation
*/

static int vm_gettype( SGS_CTX )
{
	sgs_Variable* A;
	if( sgs_StackSize( C ) <= 0 )
		return 0;

	A = stk_getvar( C, -1 );
	if( !A )
	{
		stk_pop1( C );
		sgs_PushString( C, "null" );
		return 1;
	}

	if( A->type == SVT_OBJECT )
	{
		int ret = obj_exec( C, SOP_GETTYPE, &A->data.O );
		if( ret != SGS_SUCCESS )
		{
			char bfr[ 32 ];
			sprintf( bfr, "0x%p", A->data.O.iface );
			sgs_PushString( C, bfr );
		}
	}
	else
	{
		const char* ty = "ERROR";
		switch( A->type )
		{
		case SVT_BOOL:	ty = "bool"; break;
		case SVT_INT:	ty = "int"; break;
		case SVT_REAL:	ty = "real"; break;
		case SVT_STRING:	ty = "string"; break;
		case SVT_CFUNC:	ty = "cfunc"; break;
		case SVT_FUNC:	ty = "func"; break;
		}
		sgs_PushString( C, ty );
	}

	stk_popskip( C, 1, 1 );
	return 1;
}

/* TODO: understand whether this needs extending or dropping */
static void vm_assign( SGS_CTX, int16_t to, sgs_Variable* from )
{
	stk_setvar( C, to, from );
}

static int vm_copy( SGS_CTX, int16_t to, sgs_Variable* B )
{
	sgs_Variable *A;
	A = stk_getvar( C, to );
	if( !A || !B )
		return 0;
	var_destroy( C, B );
	B->type = A->type;
	switch( A->type )
	{
	case SVT_BOOL: B->data.B = A->data.B; break;
	case SVT_INT: B->data.I = A->data.I; break;
	case SVT_REAL: B->data.R = A->data.R; break;
	case SVT_STRING: B->data.S = strbuf_create(); strbuf_appbuf( &B->data.S, A->data.S.ptr, A->data.S.size ); break;
	case SVT_CFUNC: B->data.C = A->data.C; break;
	case SVT_FUNC: funct_copy( C, &B->data.F, &A->data.F ); break;
	case SVT_OBJECT: return obj_exec( C, SOP_COPY, &A->data.O );
	}
	stk_pop2( C );
	return 1;
}

static int vm_gcmark( SGS_CTX, sgs_Variable* var )
{
	if( var == NULL )
		return SGS_SUCCESS;
	if( var->redblue != C->redblue )
	{
		var->redblue = C->redblue;
		if( var->type == SVT_FUNC )
		{
			sgs_Variable** vbeg = (sgs_Variable**) var->data.F.bytecode;
			sgs_Variable** vend = (sgs_Variable**) ( var->data.F.bytecode + var->data.F.instr_off );
			while( vbeg < vend )
			{
				int ret = vm_gcmark( C, *vbeg++ );
				if( ret != SGS_SUCCESS )
					return ret;
			}
		}
		else if( var->type == SVT_OBJECT )
		{
			int ret = obj_exec( C, SOP_GCMARK, &var->data.O );
			if( ret != SGS_SUCCESS )
				return ret;
		}
	}
	return SGS_SUCCESS;
}


/*
	Object property / array accessor handling
*/

static int vm_getprop_builtin( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx )
{
	const char* prop = var_cstr( idx );

	switch( obj->type )
	{
	case SVT_STRING:
		if( 0 == strcmp( prop, "length" ) )
		{
			sgs_PushInt( C, obj->data.S.size );
			return SGS_SUCCESS;
		}
		break;
	}

	return SGS_ENOTFND;
}

static int vm_getprop( SGS_CTX, int16_t out, sgs_Variable* obj, sgs_Variable* idx, int isindex )
{
	int ret;

	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	if( obj && obj->type == SVT_OBJECT )
	{
		stk_push( C, idx );
		ret = obj_exec( C, isindex ? SOP_GETINDEX : SOP_GETPROP, &obj->data.O );
		stk_popskip( C, 1, ret == SGS_SUCCESS );
	}
	else
		ret = vm_getprop_builtin( C, obj, idx );

	if( ret != SGS_SUCCESS )
	{
		if( ret == SGS_ENOTFND )
		{
			const char* err = isindex ? "Cannot index variable" : "Property not found";
			sgs_Printf( C, SGS_ERROR, -1, "%s: %s", err, var_cstr( idx ) );
		}
		stk_setvar( C, out, NULL );
	}
	else
	{
		stk_setvar( C, out, stk_getvar( C, -1 ) );
		stk_pop( C, 1 );
	}
	return ret;
}

static int vm_setprop( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* src, int isindex )
{
	int ret;

	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	if( obj && obj->type == SVT_OBJECT )
	{
		stk_push( C, idx );
		stk_push( C, src );
		ret = obj_exec( C, isindex ? SOP_SETINDEX : SOP_SETPROP, &obj->data.O );
		stk_pop( C, 2 );
	}
	else
		ret = SGS_ENOTFND;

	if( ret != SGS_SUCCESS )
	{
		if( ret == SGS_ENOTFND )
		{
			const char* err = isindex ? "Cannot index variable" : "Property not found";
			sgs_Printf( C, SGS_ERROR, -1, "%s: %s", err, var_cstr( idx ) );
		}
	}
	return ret;
}


/*
	Global variable dictionary
*/

static int vm_getvar( SGS_CTX, int16_t out, sgs_Variable* idx )
{
	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;
	stk_setvar( C, out, (sgs_VarPtr) ht_get( &C->data, idx->data.S.ptr, idx->data.S.size ) );
	return SGS_SUCCESS;
}

static int vm_setvar( SGS_CTX, sgs_Variable* idx, sgs_Variable* val )
{
	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	{
		void* olddata = ht_get( &C->data, idx->data.S.ptr, idx->data.S.size );
		VAR_RELEASE( (sgs_Variable*) olddata );
	}
	ht_set( &C->data, idx->data.S.ptr, idx->data.S.size, val );
	VAR_ACQUIRE( val );
	return SGS_SUCCESS;
}


/*
	OPs
*/
#define _cti_num( ty ) ( ty == SVT_BOOL || ty == SVT_INT || ty == SVT_REAL )
static int calc_typeid( sgs_Variable* A, sgs_Variable* B )
{
	int ty1 = A ? A->type : SVT_INT;
	int ty2 = B ? B->type : SVT_INT;
	int ii1 = _cti_num( ty1 );
	int ii2 = _cti_num( ty2 );
	if( ii1 && ii2 )
		return MAX( MAX( ty1, ty2 ), SVT_INT );
	else if( ii1 )
		return ty1;
	else if( ii2 )
		return ty2;
	else
		return SVT_REAL;
}
#undef _cti_num


static void vm_op_concat( SGS_CTX, int16_t out, sgs_Variable *A, sgs_Variable *B )
{
	sgs_Variable *N;
	stk_push( C, A );
	stk_push( C, B );
	vm_convert( C, -2, SVT_STRING );
	vm_convert( C, -1, SVT_STRING );
	A = stk_getvar( C, -2 );
	B = stk_getvar( C, -1 );
	N = var_create_0str( C, A->data.S.size + B->data.S.size );
	memcpy( N->data.S.ptr, A->data.S.ptr, A->data.S.size );
	memcpy( N->data.S.ptr + A->data.S.size, B->data.S.ptr, B->data.S.size );
	stk_pop2( C );
	stk_setvar_leave( C, out, N );
}

static int vm_op_concat_ex( SGS_CTX, int args )
{
	int i;
	int32_t totsz = 0, curoff = 0;
	sgs_Variable *var, *N;
	if( args < 2 )
		return 1;
	if( sgs_StackSize( C ) < args )
		return 0;
	for( i = 1; i <= args; ++i )
	{
		vm_convert( C, -i, SVT_STRING );
		totsz += stk_getvar( C, -i )->data.S.size;
	}
	N = var_create_0str( C, totsz );
	for( i = 1; i <= args; ++i )
	{
		var = stk_getvar( C, -i );
		memcpy( N->data.S.ptr + curoff, var->data.S.ptr, var->data.S.size );
		curoff += var->data.S.size;
	}
	stk_setvar( C, -args, N );
	VAR_RELEASE( N );
	stk_pop( C, args - 1 );
	return 1;
}

static void vm_op_booland( SGS_CTX, int16_t out, sgs_Variable *A, sgs_Variable *B )
{
	sgs_SetBool( C, out, var_getbool( C, A ) && var_getbool( C, B ) );
}

static void vm_op_boolor( SGS_CTX, int16_t out, sgs_Variable *A, sgs_Variable *B )
{
	sgs_SetBool( C, out, var_getbool( C, A ) || var_getbool( C, B ) );
}

static int vm_op_negate( SGS_CTX, int16_t out, sgs_Variable *A )
{
	int i = A->type;

	if( i == SVT_REAL )
	{
		sgs_SetReal( C, out, -var_getreal( C, A ) );
	}
	else if( i == SVT_INT || i == SVT_BOOL )
	{
		sgs_SetInt( C, out, -var_getint( C, A ) );
	}
	else if( i == SVT_OBJECT )
	{
		sgs_Printf( C, SGS_WARNING, -1, "Object negation is NOT IMPLEMENTED" );
		return 0;
	}
	else
	{
		sgs_Printf( C, SGS_WARNING, -1, "Negating object of type %s is not supported.", sgs_VarNames[ i ] );
		return 0;
	}

	return 1;
}

static void vm_op_boolinv( SGS_CTX, int16_t out, sgs_Variable *A )
{
	sgs_SetBool( C, out, !var_getbool( C, A ) );
}

static void vm_op_invert( SGS_CTX, int16_t out, sgs_Variable *A )
{
	sgs_SetInt( C, out, ~var_getint( C, A ) );
}


#define VAR_MOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, int16_t out, sgs_Variable *A ) { \
	if( !A || ( A->type != SVT_INT && A->type != SVT_REAL ) ) \
		{ sgs_Printf( C, SGS_ERROR, -1, "Cannot " #pfx "rement null/bool/string/func/cfunc variables!" ); return; } \
	switch( A->type ){ \
		case SVT_INT: sgs_SetInt( C, out, A->data.I op ); break; \
		case SVT_REAL: sgs_SetReal( C, out, A->data.R op ); break; \
	} }

VAR_MOP( inc, +1 )
VAR_MOP( dec, -1 )


#define VAR_AOP_BASE( pfx, op, act ) \
static void vm_op_##pfx( SGS_CTX, int16_t out, sgs_Variable* a, sgs_Variable* b ) { \
	int i = calc_typeid( a, b ); \
	switch( i ){ \
		case SVT_INT: { sgs_Integer A = var_getint( C, a ), B = var_getint( C, b ); sgs_SetInt( C, out, A op B ); break; } \
		case SVT_REAL: { sgs_Real A = var_getreal( C, a ), B = var_getreal( C, b ); sgs_SetReal( C, out, act ); break; } \
	}  }

#define VAR_AOP( pfx, op ) VAR_AOP_BASE( pfx, op, A op B )

VAR_AOP( add, + )
VAR_AOP( sub, - )
VAR_AOP( mul, * )
VAR_AOP( div, / )
VAR_AOP_BASE( mod, %, fmod( A, B ) )


#define VAR_IOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, int16_t out, sgs_Variable* a, sgs_Variable* b ) { \
	sgs_Integer A = var_getint( C, a ); \
	sgs_Integer B = var_getint( C, b ); \
	sgs_SetInt( C, out, A op B ); \
	}

VAR_IOP( and, & )
VAR_IOP( or, | )
VAR_IOP( xor, ^ )
VAR_IOP( lsh, << )
VAR_IOP( rsh, >> )


#define VAR_LOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, int16_t out, sgs_Variable* a, sgs_Variable* b ) { \
	int i = calc_typeid( a, b ); \
	switch( i ){ \
		case SVT_INT: { sgs_Integer A = var_getint( C, a ), B = var_getint( C, b ); sgs_SetBool( C, out, A op B ); break; } \
		case SVT_REAL: { sgs_Real A = var_getreal( C, a ), B = var_getreal( C, b ); sgs_SetBool( C, out, A op B ); break; } \
	} }

VAR_LOP( eq, == )
VAR_LOP( neq, != )
VAR_LOP( lt, < )
VAR_LOP( gt, > )
VAR_LOP( lte, <= )
VAR_LOP( gte, >= )


static void vm_make_array( SGS_CTX, int args, int16_t outpos )
{
	int expect = 1, rvc, stkoff = C->stack_off - C->stack_base;

	sgs_BreakIf( sgs_StackSize( C ) < args );
	C->stack_off = C->stack_top - args;

	rvc = (*C->array_func)( C );

	stk_clean( C, C->stack_off, C->stack_top - rvc );
	C->stack_off = C->stack_base + stkoff;

	if( rvc > expect )
		stk_pop( C, rvc - expect );
	else
	{
		while( expect-- > rvc )
			stk_push( C, NULL );
	}

	stk_setvar( C, outpos, stk_getvar( C, -1 ) );
	stk_pop1( C );
}


static int vm_exec( SGS_CTX, const void* code, int32_t codesize, const void* data, int32_t datasize );

/*
	Tries to execute the given variable as a function
*/
static int vm_call_do( SGS_CTX, sgs_Variable* func )
{
	if( !func )
		return 0;
	else if( func->type == SVT_CFUNC )
		return (*func->data.C)( C );
	else if( func->type == SVT_FUNC )
		return vm_exec( C,
						((char*)func->data.F.bytecode) + func->data.F.instr_off,
						func->data.F.size - func->data.F.instr_off,
						func->data.F.bytecode, func->data.F.instr_off );
	else
	{
		sgs_Printf( C, SGS_ERROR, -1, "Variable of type %s cannot be called", sgs_VarNames[ func->type ] );
		return 0;
	}
}

/*
	Call the virtual machine.
	Args must equal the number of arguments pushed before the function
*/
static int vm_call( SGS_CTX, int args, int expect, sgs_Variable* fvar )
{
	int stkoff = C->stack_off - C->stack_base;
	int rvc;

	sgs_BreakIf( sgs_StackSize( C ) < args );
	C->stack_off = C->stack_top - args;

	rvc = vm_call_do( C, fvar );

	stk_clean( C, C->stack_off, C->stack_top - rvc );
	C->stack_off = C->stack_base + stkoff;

	if( rvc > expect )
		stk_pop( C, rvc - expect );
	else
	{
		while( expect-- > rvc )
			stk_push( C, NULL );
	}

	return 1;
}


#if SGS_DEBUG && SGS_DEBUG_VALIDATE
static SGS_INLINE sgs_VarPtr const_getvar( sgs_VarPtr* consts, uint32_t count, int16_t off )
{
	sgs_BreakIf( (int)off >= (int)count );
	return consts[ off ];
}
#endif

/*
	Main VM execution loop
*/
const char* opnames[] =
{
	"nop", "push_const", "push_stack", "push_null", "duplicate",
	"copy", "pop", "pop_n", "pop_stack", "return", "jump", "jump_if_false", "call",
	"getvar", "setvar", "getprop", "setprop", "getindex", "setindex",
	"concat", "bool_and", "bool_or", "negate", "bool_inv", "invert", "inc", "dec", "add", "sub", "mul", "div", "mod",
	"and", "or", "xor", "lsh", "rsh", "seq", "sneq", "eq", "neq", "lt", "gt", "lte", "gte",
};
static int vm_exec( SGS_CTX, const void* code, int32_t codesize, const void* data, int32_t datasize )
{
	char* ptr = (char*) code;
	char* pend = ptr + codesize;
	sgs_Variable** cptr = (sgs_Variable**) data;
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
	sgs_Variable** cpend = (sgs_Variable**) ( ((char*)data) + datasize );
	uint32_t constcount = cpend - cptr;
	int stkoff = C->stack_top - C->stack_off;
#  define RESVAR( v ) ( ( (v) & 0x8000 ) ? const_getvar( cptr, constcount, (v) & 0x7fff ) : stk_getlvar( C, (v) ) )
#else
#  define RESVAR( v ) ( ( (v) & 0x8000 ) ? cptr[ (v) & 0x7fff ] : stk_getlvar( C, (v) ) )
#endif

	/* preallocated helpers */
	int32_t ret = 0;
	UNUSED( datasize );

	while( ptr < pend )
	{
		uint8_t instr = *ptr++;
#if SGS_DEBUG
 #if SGS_DEBUG_FLOW
		printf( "*** [at 0x%04X] %s ***\n", ptr - 1 - (char*)code, opnames[ instr ] );
 #endif
 #if SGS_DEBUG_STATE
		sgsVM_StackDump( C );
 #endif
#endif
		switch( instr )
		{
		case SI_NOP: break;

		case SI_PUSH:
		{
			int16_t item = AS_INT16( ptr );
			ptr += 2;
			stk_push( C, RESVAR( item ) );
			break;
		}
		case SI_PUSHN:
		{
			uint8_t count = AS_UINT8( ptr++ );
			stk_makespace( C, count );
			while( count-- )
				*C->stack_top++ = NULL;
			break;
		}

		case SI_POPN:
		{
			stk_pop( C, AS_UINT8( ptr++ ) );
			break;
		}
		case SI_POPR:
		{
			uint16_t to = AS_UINT16( ptr );
			ptr += 2;
			stk_setlvar( C, to, stk_gettop( C ) );
			stk_pop1( C );
			break;
		}

		case SI_RETN:
		{
			ret = AS_UINT8( ptr++ );
			sgs_BreakIf( ( C->stack_top - C->stack_off ) - stkoff < ret );
			ptr = pend;
			break;
		}

		case SI_JUMP:
		{
			int16_t off = AS_INT16( ptr );
			ptr += 2;
			ptr += off;
			sgs_BreakIf( ptr > pend || ptr < (char*)code );
			break;
		}

		case SI_JMPF:
		{
			int16_t arg, off;
			arg = AS_INT16( ptr );
			ptr += 2;
			off = AS_INT16( ptr );
			ptr += 2;
			sgs_BreakIf( ptr + off > pend || ptr + off < (char*)code );
			if( !var_getbool( C, RESVAR( arg ) ) )
				ptr += off;
			break;
		}

		case SI_CALL:
		{
			uint8_t args = AS_UINT8( ptr++ );
			uint8_t expect = AS_UINT8( ptr++ );
			int16_t src = AS_INT16( ptr );
			ptr += 2;
			vm_call( C, args, expect, RESVAR( src ) );
			break;
		}

#define ARGPOS_2 int16_t a1, a2; a1 = AS_INT16( ptr ); ptr += 2; a2 = AS_INT16( ptr ); ptr += 2;
#define ARGPOS_3 int16_t a1, a2, a3; a1 = AS_INT16( ptr ); ptr += 2; a2 = AS_INT16( ptr ); ptr += 2; a3 = AS_INT16( ptr ); ptr += 2;
#define ARGS_2 sgs_VarPtr p1, p2; ARGPOS_2; p1 = RESVAR( a1 ); UNUSED( p1 ); p2 = RESVAR( a2 );
#define ARGS_3 sgs_VarPtr p1, p2, p3; ARGPOS_3; p1 = RESVAR( a1 ); UNUSED( p1 ); p2 = RESVAR( a2 ); p3 = RESVAR( a3 );
		case SI_GETVAR: { ARGS_2; vm_getvar( C, a1, p2 ); break; }
		case SI_SETVAR: { ARGS_2; vm_setvar( C, p1, p2 ); break; }
		case SI_GETPROP: { ARGS_3; vm_getprop( C, a1, p2, p3, FALSE ); break; }
		case SI_SETPROP: { ARGS_3; vm_setprop( C, p1, p2, p3, FALSE ); break; }
		case SI_GETINDEX: { ARGS_3; vm_getprop( C, a1, p2, p3, TRUE ); break; }
		case SI_SETINDEX: { ARGS_3; vm_setprop( C, p1, p2, p3, TRUE ); break; }

		case SI_SET: { ARGS_2; vm_assign( C, a1, p2 ); break; }
		case SI_COPY: { ARGS_2; vm_copy( C, a1, p2 ); break; }
		case SI_CONCAT: { ARGS_3; vm_op_concat( C, a1, p2, p3 ); break; }
		case SI_BOOL_AND: { ARGS_3; vm_op_booland( C, a1, p2, p3 ); break; }
		case SI_BOOL_OR: { ARGS_3; vm_op_boolor( C, a1, p2, p3 ); break; }
		case SI_NEGATE: { ARGS_2; vm_op_negate( C, a1, p2 ); break; }
		case SI_BOOL_INV: { ARGS_2; vm_op_boolinv( C, a1, p2 ); break; }
		case SI_INVERT: { ARGS_2; vm_op_invert( C, a1, p2 ); break; }

		case SI_INC: { ARGS_2; vm_op_inc( C, a1, p2 ); break; }
		case SI_DEC: { ARGS_2; vm_op_dec( C, a1, p2 ); break; }
		case SI_ADD: { ARGS_3; vm_op_add( C, a1, p2, p3 ); break; }
		case SI_SUB: { ARGS_3; vm_op_sub( C, a1, p2, p3 ); break; }
		case SI_MUL: { ARGS_3; vm_op_mul( C, a1, p2, p3 ); break; }
		case SI_DIV: { ARGS_3; vm_op_div( C, a1, p2, p3 ); break; }
		case SI_MOD: { ARGS_3; vm_op_mod( C, a1, p2, p3 ); break; }

		case SI_AND: { ARGS_3; vm_op_and( C, a1, p2, p3 ); break; }
		case SI_OR: { ARGS_3; vm_op_or( C, a1, p2, p3 ); break; }
		case SI_XOR: { ARGS_3; vm_op_xor( C, a1, p2, p3 ); break; }
		case SI_LSH: { ARGS_3; vm_op_lsh( C, a1, p2, p3 ); break; }
		case SI_RSH: { ARGS_3; vm_op_rsh( C, a1, p2, p3 ); break; }

#define STRICTLY_EQUAL( val ) if( p2->type != p3->type ) { sgs_SetBool( C, a1, val ); break; }
		case SI_SEQ: { ARGS_3; STRICTLY_EQUAL( FALSE ); vm_op_eq( C, a1, p2, p3 ); break; }
		case SI_EQ: { ARGS_3; vm_op_eq( C, a1, p2, p3 ); break; }
		case SI_SNEQ: { ARGS_3; STRICTLY_EQUAL( TRUE ); vm_op_neq( C, a1, p2, p3 ); break; }
		case SI_NEQ: { ARGS_3; vm_op_neq( C, a1, p2, p3 ); break; }
		case SI_LT: { ARGS_3; vm_op_lt( C, a1, p2, p3 ); break; }
		case SI_GT: { ARGS_3; vm_op_gt( C, a1, p2, p3 ); break; }
		case SI_LTE: { ARGS_3; vm_op_lte( C, a1, p2, p3 ); break; }
		case SI_GTE: { ARGS_3; vm_op_gte( C, a1, p2, p3 ); break; }

		case SI_ARRAY: { uint8_t argc = AS_UINT8( ptr++ ); int16_t a1 = AS_UINT16( ptr ); ptr += 2; vm_make_array( C, argc, a1 ); break; }
#undef STRICTLY_EQUAL
#undef ARGS_2
#undef ARGS_3
#undef ARGPOS_2
#undef ARGPOS_3
#undef RESVAR

		default:
			sgs_Printf( C, SGS_ERROR, -1, "Illegal instruction executed: %d", (int) *(ptr - 1) );
			break;
		}
	}

#if SGS_DEBUG && SGS_DEBUG_STATE
    sgs_MemCheckDbg( C->stack.ptr );
	sgsVM_StackDump( C );
#endif
	return ret;
}


/* INTERNAL INERFACE */

static int funct_size( funct* f )
{
	int sz = f->size;
	sgs_Variable* beg = (sgs_Variable*) f->bytecode;
	sgs_Variable* end = (sgs_Variable*) ( f->bytecode + f->instr_off );
	while( beg < end )
		sz += sgsVM_VarSize( beg++ );
	return sz;
}

int sgsVM_VarSize( sgs_Variable* var )
{
	if( !var )
		return 0;

	int out = sizeof( sgs_Variable );
	switch( var->type )
	{
	case SVT_FUNC: out += funct_size( &var->data.F ); break;
	/* case SVT_OBJECT: break; */
	case SVT_STRING: out += var->data.S.size; break;
	}
	return out;
}

void sgsVM_VarDump( sgs_VarPtr var )
{
	if( !var )
	{
		printf( "Null (rc:0)" );
		return;
	}
	printf( "%s (rc:%d, size:%d)", sgs_VarNames[ var->type ], var->refcount, sgsVM_VarSize( var ) );
	switch( var->type )
	{
	case SVT_BOOL: printf( " = %s", var->data.B ? "True" : "False" ); break;
	case SVT_INT: printf( " = %" PRId64, var->data.I ); break;
	case SVT_REAL: printf( " = %f", var->data.R ); break;
	case SVT_STRING: printf( " = \"" ); print_safe( var->data.S.ptr, 16 ); printf( var->data.S.size > 16 ? "...\"" : "\"" ); break;
	case SVT_OBJECT: printf( "TODO [object impl]" ); break;
	}
}

void sgsVM_StackDump( SGS_CTX )
{
	int i, stksz = C->stack_top - C->stack_base;
	printf( "STACK (size=%d, bytes=%d/%d)--\n", stksz, (int)( stksz * STK_UNITSIZE ), (int)( C->stack_mem * STK_UNITSIZE ) );
	for( i = 0; i < stksz; ++i )
	{
		sgs_VarPtr var = C->stack_base[ i ];
		printf( "  " ); sgsVM_VarDump( var ); printf( "\n" );
	}
	printf( "--\n" );
}

int sgsVM_ExecFn( SGS_CTX, const void* code, int32_t codesize, const void* data, int32_t datasize )
{
	int ret = vm_exec( C, code, codesize, data, datasize );
	stk_pop( C, C->stack_top - C->stack_base );
	return ret;
}


/* INTERFACE */

/*
sgs_Variable* sgsVM_VarCreate( SGS_CTX, int type )
{
	return make_var( C, type );
}
sgs_Variable* sgsVM_VarCreateString( SGS_CTX, const char* str, int32_t len )
{
	return var_create_str( C, str, len );
}
void sgsVM_VarDestroy( SGS_CTX, sgs_Variable* var )
{
	destroy_var( C, var );
}
*/


int sgs_SetNull( SGS_CTX, int item )
{
	stk_setvar_leave( C, item, NULL );
	return SGS_SUCCESS;
}

int sgs_SetBool( SGS_CTX, int item, int value )
{
	sgs_Variable* var = make_var( C, SVT_BOOL );
	var->data.B = value ? 1 : 0;
	stk_setvar_leave( C, item, var );
	return SGS_SUCCESS;
}

int sgs_SetInt( SGS_CTX, int item, sgs_Integer value )
{
	sgs_Variable* var = make_var( C, SVT_INT );
	var->data.I = value;
	stk_setvar_leave( C, item, var );
	return SGS_SUCCESS;
}

int sgs_SetReal( SGS_CTX, int item, sgs_Real value )
{
	sgs_Variable* var = make_var( C, SVT_REAL );
	var->data.R = value;
	stk_setvar_leave( C, item, var );
	return SGS_SUCCESS;
}


int sgs_PushNull( SGS_CTX )
{
	stk_push_leave( C, NULL );
	return SGS_SUCCESS;
}

int sgs_PushBool( SGS_CTX, int value )
{
	sgs_Variable* var = make_var( C, SVT_BOOL );
	var->data.B = value ? 1 : 0;
	stk_push_leave( C, var );
	return SGS_SUCCESS;
}

int sgs_PushInt( SGS_CTX, sgs_Integer value )
{
	sgs_Variable* var = make_var( C, SVT_INT );
	var->data.I = value;
	stk_push_leave( C, var );
	return SGS_SUCCESS;
}

int sgs_PushReal( SGS_CTX, sgs_Real value )
{
	sgs_Variable* var = make_var( C, SVT_REAL );
	var->data.R = value;
	stk_push_leave( C, var );
	return SGS_SUCCESS;
}

int sgs_PushStringBuf( SGS_CTX, const char* str, int32_t size )
{
	sgs_Variable* var = var_create_str( C, str, size );
	stk_push_leave( C, var );
	return SGS_SUCCESS;
}

int sgs_PushString( SGS_CTX, const char* str )
{
	sgs_Variable* var = var_create_str( C, str, -1 );
	stk_push_leave( C, var );
	return SGS_SUCCESS;
}

int sgs_PushCFunction( SGS_CTX, sgs_CFunc func )
{
	sgs_Variable* var = make_var( C, SVT_CFUNC );
	var->data.C = func;
	stk_push_leave( C, var );
	return SGS_SUCCESS;
}

int sgs_PushObject( SGS_CTX, void* data, void** iface )
{
	sgs_Variable* var = make_var( C, SVT_OBJECT );
	var->data.O.data = data;
	var->data.O.iface = iface;
	stk_push_leave( C, var );
	return SGS_SUCCESS;
}

int sgs_PushVariable( SGS_CTX, sgs_Variable* var )
{
	stk_push( C, var );
	return SGS_SUCCESS;
}


int sgs_PushProperty( SGS_CTX, const char* name )
{
	int ret = sgs_PushString( C, name );
	if( ret )
		return ret;
	ret = vm_getprop( C, stk_absindex( C, -1 ), stk_getvar( C, -2 ), stk_getvar( C, -1 ), FALSE );
	stk_popskip( C, 1, 1 );
	return ret;
}

int sgs_StringConcat( SGS_CTX )
{
	if( sgs_StackSize( C ) < 2 )
		return SGS_ESTKUF;
	vm_op_concat( C, -1, stk_getvar( C, -2 ), stk_getvar( C, -1 ) );
	stk_popskip( C, 1, 1 );
	return SGS_SUCCESS;
}

int sgs_StringMultiConcat( SGS_CTX, int args )
{
	return vm_op_concat_ex( C, args ) ? SGS_SUCCESS : SGS_ESTKUF;
}


int sgs_Pop( SGS_CTX, int count )
{
	if( C->stack_top - C->stack_base < count )
		return SGS_ESTKUF;
	stk_pop( C, count );
	return SGS_SUCCESS;
}

int sgs_Call( SGS_CTX, int args, int expect )
{
	return vm_call( C, args, expect, stk_getvar( C, -1 - args ) );
}

int sgs_TypeOf( SGS_CTX )
{
	return vm_gettype( C ) ? SGS_SUCCESS : SGS_ESTKUF;
}


int sgs_GetGlobal( SGS_CTX, const char* name )
{
	sgs_PushString( C, name );
	vm_getvar( C, -1, stk_getvar( C, -1 ) );
	return SGS_SUCCESS;
}

int sgs_SetGlobal( SGS_CTX, const char* name )
{
	sgs_PushString( C, name );
	vm_setvar( C, stk_getvar( C, -1 ), stk_getvar( C, -2 ) );
	sgs_Pop( C, 2 );
	return SGS_SUCCESS;
}


int sgs_GetBool( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getvar( C, item );
	return var_getbool( C, var );
}

sgs_Integer sgs_GetInt( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getvar( C, item );
	return var_getint( C, var );
}

sgs_Real sgs_GetReal( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getvar( C, item );
	return var_getreal( C, var );
}


int sgs_ToBool( SGS_CTX, int item )
{
	if( vm_convert( C, item, SVT_BOOL ) != SGS_SUCCESS )
		return 0;
	return stk_getvar( C, item )->data.B;
}

sgs_Integer sgs_ToInt( SGS_CTX, int item )
{
	if( vm_convert( C, item, SVT_INT ) != SGS_SUCCESS )
		return 0;
	return stk_getvar( C, item )->data.I;
}

sgs_Real sgs_ToReal( SGS_CTX, int item )
{
	if( vm_convert( C, item, SVT_REAL ) != SGS_SUCCESS )
		return 0;
	return stk_getvar( C, item )->data.R;
}

const char* sgs_ToString( SGS_CTX, int item )
{
	if( vm_convert( C, item, SVT_STRING ) != SGS_SUCCESS )
		return "";
	return stk_getvar( C, item )->data.S.ptr;
}


int sgs_StackSize( SGS_CTX )
{
	return ((sgs_Variable**)C->stack_top) - ((sgs_Variable**)C->stack_off);
}

sgs_Variable* sgs_StackItem( SGS_CTX, int item )
{
	return stk_getvar( C, item );
}

int sgs_ItemType( SGS_CTX, int item )
{
	return stk_getvar( C, item )->type;
}

int sgs_Acquire( SGS_CTX, sgs_Variable* var )
{
	UNUSED( C );
	VAR_ACQUIRE( var );
	return var->refcount;
}

int sgs_Release( SGS_CTX, sgs_Variable* var )
{
	int ret = var->refcount - 1;
	if( var->destroying )
		return -1;
	VAR_RELEASE( var );
	return ret;
}

int sgs_GCExecute( SGS_CTX )
{
	C->redblue = !C->redblue;

	/* -- mark -- */
	/* gclist / currently executed "main" function */
	{
		sgs_VarPtr* vbeg = C->gclist;
		sgs_VarPtr* vend = vbeg + C->gclist_size;
		while( vbeg < vend )
		{
			int ret = vm_gcmark( C, *vbeg++ );
			if( ret != SGS_SUCCESS )
				return ret;
		}
	}

	/* stack */
	{
		sgs_VarPtr* vbeg = (sgs_VarPtr*) C->stack_base;
		sgs_VarPtr* vend = (sgs_VarPtr*) C->stack_top;
		while( vbeg < vend )
		{
			int ret = vm_gcmark( C, *vbeg++ );
			if( ret != SGS_SUCCESS )
				return ret;
		}
	}

	/* globals */
	{
		HTPair* pbeg = C->data.pairs;
		HTPair* pend = pbeg + C->data.size;
		while( pbeg < pend )
		{
			if( pbeg->str )
			{
				int ret = vm_gcmark( C, (sgs_Variable*) pbeg->ptr );
				if( ret != SGS_SUCCESS )
					return ret;
			}
			pbeg++;
		}
	}

	/* -- sweep -- */
	/* mark as being destroyed */
	{
		sgs_Variable* p = C->vars;
		while( p )
		{
			if( p->redblue != C->redblue )
				p->destroying = TRUE;
			p = p->next;
		}
	}

	/* destroy variables */
	{
		sgs_Variable* p = C->vars;
		while( p )
		{
			sgs_Variable* pn = p->next;
			if( p->redblue != C->redblue )
				destroy_var( C, p );
			p = pn;
		}
	}

	return SGS_SUCCESS;
}

int sgs_GCMark( SGS_CTX, sgs_Variable* var )
{
	return vm_gcmark( C, var );
}

static const char* ca_curend( const char* str, const char* endchrs )
{
	while( *str && !isoneof( *str, endchrs ) )
		str++;
	return str;
}

static int ca_compare( const char* str, const char* end, const char* type )
{
	int len;
	if( end - str == 1 && *str == '*' )
		return 1;

	len = strlen( type );
	while( str < end )
	{
		const char* pend = ca_curend( str, "|," );
		if( *str != '!' )
		{
			/*
				accept basic promotion:
				null/bool/int/real -> interchangeable/string
			*/
			if( strncmp( "null", str, pend - str ) == 0 ||
				strncmp( "bool", str, pend - str ) == 0 ||
				strncmp( "int", str, pend - str ) == 0 ||
				strncmp( "real", str, pend - str ) == 0 )
			{
				const char* ptr = "!real|!int|!bool|!null";
				if( !ca_compare( ptr, ptr + strlen( ptr ), type ) )
					return 0;
			}
			else if( strncmp( "string", str, pend - str ) == 0 )
			{
				const char* ptr = "!string|!real|!int|!bool|!null";
				if( !ca_compare( ptr, ptr + strlen( ptr ), type ) )
					return 0;
			}
			return 1;
		}
		/* after this point, string assumed to have "!" in front */
		else
		{
			if( pend - str - 1 == len && strncmp( str + 1, type, len ) == 0 )
				return 1;
			str++;
		}
		str = pend + ( *pend == '|' );
	}

	return 0;
}

int sgs_CheckArgs( SGS_CTX, const char* str )
{
	const char* orig = str;
	int curarg = 0, ret;
	const char* end = ca_curend( str, "," );
	while( *str )
	{
		if( curarg >= sgs_StackSize( C ) )
			goto fail;

		sgs_PushVariable( C, stk_getvar( C, curarg ) );
		if( !vm_gettype( C ) )
			goto fail;

		ret = ca_compare( str, end, var_cstr( stk_getvar( C, -1 ) ) );
		stk_pop1( C );
		if( !ret ) goto fail;

		str = end + !!*end;
		end = ca_curend( str, "," );
		curarg++;
	}
	if( curarg < sgs_StackSize( C ) )
		goto fail;
	return 1;

fail:
	sgs_Printf( C, SGS_ERROR, -1, "Invalid arguments passed, expected \"%s\"", orig );
	return 0;
}


const char* sgs_GetStringPtr( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getvar( C, item );
	sgs_BreakIf( !var || var->type != SVT_STRING );
	return var_cstr( var );
}

int32_t sgs_GetStringSize( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getvar( C, item );
	sgs_BreakIf( !var || var->type != SVT_STRING );
	return var->data.S.size;
}

sgs_VarObj* sgs_GetObjectData( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getvar( C, item );
	sgs_BreakIf( !var || var->type != SVT_OBJECT );
	return &var->data.O;
}


