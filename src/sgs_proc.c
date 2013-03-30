

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


#define RTTMASK( x ) (1<<(x))
#define REFTYPE_CONSTANT (RTTMASK(SVT_STRING)|RTTMASK(SVT_FUNC)|RTTMASK(SVT_OBJECT))
#define IS_REFTYPE( type ) ((1<<(type))& REFTYPE_CONSTANT )


#if 1

#define VAR_ACQUIRE( pvar ) { if( IS_REFTYPE( (pvar)->type ) ) var_acquire( C, pvar ); }
#define VAR_RELEASE( pvar ) { if( IS_REFTYPE( (pvar)->type ) ) var_release( C, pvar ); }

#else

#define VAR_ACQUIRE( pvar ) var_acquire( C, pvar )
#define VAR_RELEASE( pvar ) var_release( C, pvar )

#endif

#define STK_UNITSIZE sizeof( sgs_Variable )


static int obj_exec( SGS_CTX, const void* sop, object_t* data, int args )
{
	void** func = data->iface;
	int ret = SGS_ENOTFND, stkoff = C->stack_off - C->stack_base;
	C->stack_off = C->stack_top - args;

	while( *func != SOP_END )
	{
		if( *func == sop )
		{
			ret = ( (sgs_ObjCallback) func[ 1 ] )( C, data );
			break;
		}
		func += 2;
	}

	C->stack_off = C->stack_base + stkoff;
	return ret;
}

void var_free_object( SGS_CTX, object_t* O )
{
	if( O->prev ) O->prev->next = O->next;
	if( O->next ) O->next->prev = O->prev;
	if( C->objs == O )
		C->objs = O->next;
	sgs_Free( O );
	C->objcount--;
}

void var_destroy_object( SGS_CTX, object_t* O )
{
	if( O->prev ) O->prev->next = O->next;
	if( O->next ) O->next->prev = O->prev;
	if( C->objs == O )
		C->objs = O->next;
	O->destroying = TRUE;
	obj_exec( C, SOP_DESTRUCT, O, 0 );
	sgs_Free( O );
	C->objcount--;
}

static void var_release( SGS_CTX, sgs_VarPtr p );
void var_destroy_func( SGS_CTX, func_t* F )
{
	sgs_VarPtr var = (sgs_VarPtr) func_consts( F ), vend = (sgs_VarPtr) func_bytecode( F );
	while( var < vend )
	{
		VAR_RELEASE( var );
		var++;
	}
	sgs_Free( F->lineinfo );
	strbuf_destroy( &F->funcname );
	sgs_Free( F );
}

static void var_acquire( SGS_CTX, sgs_VarPtr p )
{
	UNUSED( C );
	switch( p->type )
	{
	case SVT_STRING: p->data.S->refcount++; break;
	case SVT_FUNC: p->data.F->refcount++; break;
	case SVT_OBJECT: p->data.O->refcount++; break;
	}
}

static void var_release( SGS_CTX, sgs_VarPtr p )
{
	switch( p->type )
	{
	case SVT_STRING:
		p->data.S->refcount--;
		if( p->data.S->refcount <= 0 )
			sgs_Free( p->data.S );

		p->type = SVT_NULL;
		break;
	case SVT_FUNC:
		p->data.F->refcount--;
		if( p->data.F->refcount <= 0 )
			var_destroy_func( C, p->data.F );

		p->type = SVT_NULL;
		break;
	case SVT_OBJECT:
		p->data.O->refcount--;
		if( p->data.O->refcount <= 0 )
			var_destroy_object( C, p->data.O );

		p->type = SVT_NULL;
		break;
	}
}


static void var_create_0str( SGS_CTX, sgs_VarPtr out, int32_t len )
{
	UNUSED( C );
	out->type = SVT_STRING;
	out->data.S = sgs_Alloc_a( string_t, len + 1 );
	out->data.S->refcount = 1;
	out->data.S->size = len;
	out->data.S->mem = len;
	var_cstr( out )[ len ] = 0;
}

void var_create_str( SGS_CTX, sgs_Variable* out, const char* str, int32_t len )
{
	sgs_BreakIf( !str );

	if( len < 0 )
		len = strlen( str );

	var_create_0str( C, out, len );
	memcpy( str_cstr( out->data.S ), str, len );
}

static void var_create_obj( SGS_CTX, sgs_Variable* out, void* data, void** iface )
{
	object_t* obj = sgs_Alloc( object_t );
	obj->data = data;
	obj->destroying = 0;
	obj->iface = iface;
	obj->redblue = C->redblue;
	obj->next = C->objs;
	obj->prev = NULL;
	obj->refcount = 1;
	if( obj->next ) /* ! */
		obj->next->prev = obj;
	C->objcount++;
	C->objs = obj;

	out->type = SVT_OBJECT;
	out->data.O = obj;
}


/*
	Call stack
*/

static void vm_frame_push( SGS_CTX, sgs_Variable* func, uint16_t* T, instr_t* code, int icnt )
{
	sgs_StackFrame* F = sgs_Alloc( sgs_StackFrame );
	F->func = func;
	F->code = code;
	F->iptr = code;
	F->iend = code + icnt;
	if( func && func->type == SVT_FUNC )
	{
		func_t* fn = func->data.F;
		F->iptr = F->code = func_bytecode( fn );
		F->iend = F->iptr + ( ( fn->size - fn->instr_off ) / sizeof( instr_t ) );
	}
	F->lntable = T;
	F->next = NULL;
	F->prev = C->sf_last;
	if( C->sf_last )
		C->sf_last->next = F;
	else
		C->sf_first = F;
	C->sf_last = F;
}

static void vm_frame_pop( SGS_CTX )
{
	sgs_StackFrame* F = C->sf_last;
	if( F->prev )
		F->prev->next = NULL;
	C->sf_last = F->prev;
	if( C->sf_first == F )
		C->sf_first = NULL;
	sgs_Free( F );
}


/*
	Variable hash table
*/


void vht_init( VHTable* vht )
{
	ht_init( &vht->ht, 4 );
	vht->vars = NULL;
	vht->mem = 0;
}

void vht_free( VHTable* vht, SGS_CTX )
{
	int32_t i;
	for( i = 0; i < vht_size( vht ); ++i )
	{
		sgs_VarPtr p = &vht->vars[ i ].var;
		VAR_RELEASE( p );
	}
	ht_free( &vht->ht );
	if( vht->vars )
		sgs_Free( vht->vars );
}

VHTableVar* vht_get( VHTable* vht, const char* key, int32_t size )
{
	void* idx = ht_get( &vht->ht, key, size );
	uint32_t ii = (uint32_t) idx;
	return ii > 0 ? vht->vars + ( ii - 1 ) : NULL;
}

void vht_set( VHTable* vht, const char* key, int32_t size, sgs_Variable* var, SGS_CTX )
{
	VHTableVar* tv = vht_get( vht, key, size );
	VAR_ACQUIRE( var );
	if( tv )
	{
		VAR_RELEASE( &tv->var );
		tv->var = *var;
	}
	else
	{
		if( vht->mem == vht_size( vht ) )
		{
			int32_t nmem = vht->mem * 2 + 4;
			VHTableVar* narr = sgs_Alloc_n( VHTableVar, nmem );
			memcpy( narr, vht->vars, sizeof( VHTableVar ) * vht_size( vht ) );
			if( vht->vars )
				sgs_Free( vht->vars );
			vht->vars = narr;
			vht->mem = nmem;
		}

		{
			uint32_t ni = vht_size( vht );
			HTPair* p = ht_set( &vht->ht, key, size, (void*)( ni + 1 ) );
			VHTableVar htv = { *var, p->str, p->size };
			vht->vars[ ni ] = htv;
		}
	}
}

int vht_unset( VHTable* vht, const char* key, int32_t size, SGS_CTX )
{
	HTPair* tvp = ht_find( &vht->ht, key, size );
	if( tvp )
	{
		VHTableVar* tv = vht->vars + ( ((uint32_t)tvp->ptr) - 1 );
		VAR_RELEASE( &tv->var );
		if( tv - vht->vars != vht_size( vht ) )
		{
			VHTableVar* lhtv = vht->vars + ( vht_size( vht ) - 1 );
			tv->var = lhtv->var;
			tv->str = lhtv->str;
			tv->size = lhtv->size;
			ht_find( &vht->ht, tv->str, tv->size )->ptr = (void*)( tv - vht->vars + 1 );
		}
		ht_unset_pair( &vht->ht, tvp );
		return 1;
	}
	return 0;
}



/*
	Stack management
*/

#define USING_STACK

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
    return C->stack_top - 1;
}

static SGS_INLINE int stk_absindex( SGS_CTX, int stkid )
{
	if( stkid < 0 ) return C->stack_top + stkid - C->stack_off;
	else return stkid;
}

static SGS_INLINE sgs_VarPtr stk_getpos( SGS_CTX, int stkid )
{
#if SGS_DEBUG_EXTRA
	DBG_STACK_CHECK
    if( stkid < 0 ) sgs_BreakIf( -stkid > C->stack_top - C->stack_off )
    else            sgs_BreakIf( stkid >= C->stack_top - C->stack_off )
#endif
	if( stkid < 0 )	return C->stack_top + stkid;
	else			return C->stack_off + stkid;
}

static SGS_INLINE void stk_setvar( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getpos( C, stkid );
	VAR_ACQUIRE( var );
	VAR_RELEASE( vpos );
	*vpos = *var;
}
static SGS_INLINE void stk_setvar_leave( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
}
static SGS_INLINE void stk_setvar_null( SGS_CTX, int stkid )
{
	sgs_VarPtr vpos = stk_getpos( C, stkid );
	VAR_RELEASE( vpos );
	vpos->type = SVT_NULL;
}

#define stk_getlpos( C, stkid ) (C->stack_off + stkid)
static SGS_INLINE void stk_setlvar( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
	VAR_ACQUIRE( var );
}
static SGS_INLINE void stk_setlvar_leave( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
}
static SGS_INLINE void stk_setlvar_null( SGS_CTX, int stkid )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	VAR_RELEASE( vpos );
	vpos->type = SVT_NULL;
}

static void stk_makespace( SGS_CTX, int num )
{
	int stkoff, stkend, nsz, stksz = C->stack_top - C->stack_base;
	sgs_VarPtr nmem;
	if( stksz + num <= C->stack_mem )
		return;
	stkoff = C->stack_off - C->stack_base;
	stkend = C->stack_top - C->stack_base;
	DBG_STACK_CHECK
	nsz = ( stksz + num ) + C->stack_mem * 2; /* MAX( stksz + num, C->stack_mem * 2 ); */
	nmem = sgs_Alloc_n( sgs_Variable, nsz );
	if( stksz )
		memcpy( nmem, C->stack_base, stksz * STK_UNITSIZE );
	sgs_Free( C->stack_base );
	C->stack_base = nmem;
	C->stack_mem = nsz;
	C->stack_off = C->stack_base + stkoff;
	C->stack_top = C->stack_base + stkend;
}

static void stk_push( SGS_CTX, sgs_VarPtr var )
{
	stk_makespace( C, 1 );
	VAR_ACQUIRE( var );
	*C->stack_top++ = *var;
}

static void stk_push_leave( SGS_CTX, sgs_VarPtr var )
{
	stk_makespace( C, 1 );
	*C->stack_top++ = *var;
}

static void stk_push_null( SGS_CTX )
{
	stk_makespace( C, 1 );
	(C->stack_top++)->type = SVT_NULL;
}
static void stk_push_nulls( SGS_CTX, int cnt )
{
	sgs_VarPtr tgt;
	stk_makespace( C, cnt );
	tgt = C->stack_top + cnt;
	while( C->stack_top < tgt )
		(C->stack_top++)->type = SVT_NULL;
}

static void stk_clean( SGS_CTX, sgs_VarPtr from, sgs_VarPtr to )
{
	int len = to - from;
	int oh = C->stack_top - to;
	sgs_VarPtr p = from, pend = to;
	sgs_BreakIf( C->stack_top - C->stack_base < len );
	sgs_BreakIf( to < from );
	DBG_STACK_CHECK

	C->stack_top -= len;

	while( p < pend )
	{
		VAR_RELEASE( p );
		p++;
	}

	if( oh )
		memmove( from, to, oh * STK_UNITSIZE );
}

static void stk_popskip( SGS_CTX, int num, int skip )
{
	sgs_VarPtr off, ptr;
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
	VAR_RELEASE( C->stack_top );
}
static void stk_pop1nr( SGS_CTX )
{
	sgs_BreakIf( C->stack_top - C->stack_off < 1 );
	DBG_STACK_CHECK
	C->stack_top--;
}

static void stk_pop2( SGS_CTX )
{
	sgs_BreakIf( C->stack_top - C->stack_off < 1 );
	DBG_STACK_CHECK

	C->stack_top -= 2;
	VAR_RELEASE( C->stack_top );
	VAR_RELEASE( C->stack_top + 1 );
}


/*
	Conversions
*/


static int var_getbool( SGS_CTX, const sgs_VarPtr var )
{
	switch( var->type )
	{
	case SVT_NULL: return FALSE;
	case SVT_BOOL: return var->data.B;
	case SVT_INT: return var->data.I != 0;
	case SVT_REAL: return var->data.R != 0;
	case SVT_STRING: return !!var->data.S->size;
	case SVT_FUNC: return TRUE;
	case SVT_CFUNC: return TRUE;
	case SVT_OBJECT:
		if( obj_exec( C, SOP_TOBOOL, var->data.O, 0 ) == SGS_SUCCESS )
		{
			int out = stk_getpos( C, -1 )->data.B;
			stk_pop1( C );
			return out;
		}
	default: return FALSE;
	}
}

static sgs_Integer var_getint( SGS_CTX, sgs_VarPtr var )
{
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Integer) var->data.B;
	case SVT_INT: return var->data.I;
	case SVT_REAL: return (sgs_Integer) var->data.R;
	case SVT_STRING: return util_atoi( str_cstr( var->data.S ), var->data.S->size );
	case SVT_OBJECT:
		if( obj_exec( C, SOP_TOINT, var->data.O, 0 ) == SGS_SUCCESS )
		{
			sgs_Integer out = stk_getpos( C, -1 )->data.I;
			stk_pop1( C );
			return out;
		}
	}
	return 0;
}

static sgs_Real var_getreal( SGS_CTX, sgs_Variable* var )
{
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Real) var->data.B;
	case SVT_INT: return (sgs_Real) var->data.I;
	case SVT_REAL: return var->data.R;
	case SVT_STRING: return util_atof( str_cstr( var->data.S ), var->data.S->size );
	case SVT_OBJECT:
		if( obj_exec( C, SOP_TOREAL, var->data.O, 0 ) == SGS_SUCCESS )
		{
			sgs_Real out = stk_getpos( C, -1 )->data.R;
			stk_pop1( C );
			return out;
		}
	}
	return 0;
}

static SGS_INLINE sgs_Integer var_getint_simple( sgs_VarPtr var )
{
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Integer) var->data.B;
	case SVT_INT: return var->data.I;
	case SVT_REAL: return (sgs_Integer) var->data.R;
	case SVT_STRING: return util_atoi( str_cstr( var->data.S ), var->data.S->size );
	}
	return 0;
}

static SGS_INLINE sgs_Real var_getreal_simple( sgs_Variable* var )
{
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Real) var->data.B;
	case SVT_INT: return (sgs_Real) var->data.I;
	case SVT_REAL: return var->data.R;
	case SVT_STRING: return util_atof( str_cstr( var->data.S ), var->data.S->size );
	}
	return 0;
}

#define var_setnull( C, v ) \
{ sgs_VarPtr var = (v); VAR_RELEASE( var ); var->type = SVT_NULL; }
#define var_setbool( C, v, value ) \
{ sgs_VarPtr var = (v); if( var->type == SVT_BOOL ) var->data.B = value; \
	else { VAR_RELEASE( var ); var->type = SVT_BOOL; var->data.B = value; } }
#define var_setint( C, v, value ) \
{ sgs_VarPtr var = (v); if( var->type == SVT_INT ) var->data.I = value; \
	else { VAR_RELEASE( var ); var->type = SVT_INT; var->data.I = value; } }
#define var_setreal( C, v, value ) \
{ sgs_VarPtr var = (v); if( var->type == SVT_REAL ) var->data.R = value; \
	else { VAR_RELEASE( var ); var->type = SVT_REAL; var->data.R = value; } }


static int init_var_string( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	char buf[ 32 ];

	out->data.S = NULL;

	switch( var->type )
	{
	case SVT_NULL: var_create_str( C, out, "null", 4 ); break;
	case SVT_BOOL: if( var->data.B ) var_create_str( C, out, "true", 4 ); else var_create_str( C, out, "false", 5 ); break;
	case SVT_INT: sprintf( buf, "%" PRId64, var->data.I ); var_create_str( C, out, buf, -1 ); break;
	case SVT_REAL: sprintf( buf, "%g", var->data.R ); var_create_str( C, out, buf, -1 ); break;
	case SVT_FUNC: var_create_str( C, out, "Function", -1 ); break;
	case SVT_CFUNC: var_create_str( C, out, "C Function", -1 ); break;
	}
	return SGS_SUCCESS;
}

static int vm_convert( SGS_CTX, sgs_VarPtr var, int type )
{
	sgs_Variable cvar;
	int ret = SGS_ENOTSUP;

#if SGS_DEBUG && SGS_DEBUG_PERF
	printf( "[perf] var convert, %d to %d\n", var->type, type );
#endif

	if( var->type == type )
		return SGS_SUCCESS;
	else if( type == SVT_NULL )
	{
		VAR_RELEASE( var );
		var->type = SVT_NULL;
		return SGS_SUCCESS;
	}

	if( var->type == SVT_OBJECT )
	{
		void* sop = NULL;
		switch( type )
		{
		case SVT_BOOL: sop = SOP_TOBOOL; break;
		case SVT_INT: sop = SOP_TOINT; break;
		case SVT_REAL: sop = SOP_TOREAL; break;
		case SVT_STRING: sop = SOP_TOSTRING; break;
		default:
			ret = SGS_ENOTSUP;
			goto ending;
		}

		stk_push( C, var );
		ret = obj_exec( C, sop, var->data.O, 1 );
		if( ret == SGS_SUCCESS )
		{
			cvar = *stk_getpos( C, -1 );
			stk_pop1nr( C );
			stk_pop1( C );
		}
		else
			stk_pop1( C );
		goto ending;
	}

	cvar.type = type;
	switch( type )
	{
	case SVT_BOOL: cvar.data.B = var_getbool( C, var ); ret = SGS_SUCCESS; break;
	case SVT_INT: cvar.data.I = var_getint( C, var ); ret = SGS_SUCCESS; break;
	case SVT_REAL: cvar.data.R = var_getreal( C, var ); ret = SGS_SUCCESS; break;
	case SVT_STRING: ret = init_var_string( C, &cvar, var ); break;
	default:
		goto ending;
	}

ending:
	VAR_RELEASE( var );

	if( ret == SGS_SUCCESS )
		*var = cvar;
	else
		var->type = SVT_NULL;

	return ret;
}

int vm_convert_stack( SGS_CTX, int item, int type )
{
	int ret;
	sgs_Variable var;
	var = *stk_getpos( C, item );
	ret = vm_convert( C, &var, type );
	*stk_getpos( C, item ) = var;
	return ret;
}


/*
	VM mutation
*/

static int vm_gettype( SGS_CTX )
{
	sgs_Variable* A;
	if( sgs_StackSize( C ) <= 0 )
		return 0;

	A = stk_getpos( C, -1 );
	if( A->type == SVT_OBJECT )
	{
		int ret = obj_exec( C, SOP_GETTYPE, A->data.O, 0 );
		if( ret != SGS_SUCCESS )
		{
			char bfr[ 32 ];
			sprintf( bfr, "0x%p", A->data.O->iface );
			sgs_PushString( C, bfr );
		}
	}
	else
	{
		const char* ty = "ERROR";
		switch( A->type )
		{
		case SVT_NULL:   ty = "null"; break;
		case SVT_BOOL:   ty = "bool"; break;
		case SVT_INT:    ty = "int"; break;
		case SVT_REAL:   ty = "real"; break;
		case SVT_STRING: ty = "string"; break;
		case SVT_CFUNC:  ty = "cfunc"; break;
		case SVT_FUNC:   ty = "func"; break;
		}
		sgs_PushString( C, ty );
	}

	stk_popskip( C, 1, 1 );
	return 1;
}

static int vm_gcmark( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SVT_OBJECT || var->data.O->destroying || var->data.O->redblue == C->redblue )
		return SGS_SUCCESS;
	var->data.O->redblue = C->redblue;
	return obj_exec( C, SOP_GCMARK, var->data.O, 0 );
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
			sgs_PushInt( C, obj->data.S->size );
			return SGS_SUCCESS;
		}
		break;
	}

	return SGS_ENOTFND;
}

static int vm_getprop( SGS_CTX, int16_t out, sgs_Variable* obj, sgs_Variable* idx, int isindex )
{
	int ret;

	if( !isindex && idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	if( obj->type == SVT_OBJECT )
	{
		stk_push( C, idx );
		ret = obj_exec( C, isindex ? SOP_GETINDEX : SOP_GETPROP, obj->data.O, 1 );
		stk_popskip( C, 1, ret == SGS_SUCCESS );
	}
	else
		ret = vm_getprop_builtin( C, obj, idx );

	if( ret != SGS_SUCCESS )
	{
		stk_setvar_null( C, out );
	}
	else
	{
		stk_setvar( C, out, stk_getpos( C, -1 ) );
		stk_pop( C, 1 );
	}
	return ret;
}

static int vm_setprop( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* src, int isindex )
{
	int ret;

	if( !isindex && idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	if( obj->type == SVT_OBJECT )
	{
		stk_makespace( C, 2 );
		C->stack_top[ 0 ] = *idx;
		C->stack_top[ 1 ] = *src;
		C->stack_top += 2;
		VAR_ACQUIRE( idx );
		VAR_ACQUIRE( src );

		ret = obj_exec( C, isindex ? SOP_SETINDEX : SOP_SETPROP, obj->data.O, 2 );

		C->stack_top -= 2;
		VAR_RELEASE( C->stack_top );
		VAR_RELEASE( C->stack_top + 1 );
	}
	else
		ret = SGS_ENOTFND;

	return ret;
}

static void vm_properr( SGS_CTX, int ret, sgs_Variable* idx, int isindex )
{
	if( ret == SGS_ENOTFND )
	{
		const char* err = isindex ? "Cannot find value by index" : "Property not found";
		stk_push( C, idx );
		sgs_ToString( C, -1 );
		sgs_Printf( C, SGS_ERROR, -1, "%s: \"%s\"", err, var_cstr( sgs_StackItem( C, -1 ) ) );
		stk_pop1( C );
	}
}


/*
	Global variable dictionary
*/

static int vm_getvar( SGS_CTX, sgs_Variable* out, sgs_Variable* idx )
{
	sgs_VarPtr data;
	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;
	data = (sgs_VarPtr) ht_get( &C->data, str_cstr( idx->data.S ), idx->data.S->size );
	VAR_RELEASE( out );
	if( data )
	{
		VAR_ACQUIRE( data );
		*out = *data;
	}
	return data ? SGS_SUCCESS : SGS_ENOTFND;
}

static int vm_setvar( SGS_CTX, sgs_Variable* idx, sgs_Variable* val )
{
	sgs_VarPtr data;
	if( idx->type != SVT_STRING )
		return SGS_ENOTSUP;

	{
		void* olddata = ht_get( &C->data, str_cstr( idx->data.S ), idx->data.S->size );
		if( olddata )
		{
			VAR_RELEASE( (sgs_Variable*) olddata );
			sgs_Free( olddata );
		}
	}
	data = sgs_Alloc( sgs_Variable );
	*data = *val;
	VAR_ACQUIRE( data );
	ht_set( &C->data, str_cstr( idx->data.S ), idx->data.S->size, data );
	return SGS_SUCCESS;
}


/*
	OPs
*/

static void vm_clone( SGS_CTX, int16_t out, sgs_Variable* var )
{
	switch( var->type )
	{
	case SVT_STRING:
		{
			sgs_Variable ns;
			var_create_str( C, &ns, var_cstr( var ), var->data.S->size );
			stk_setlvar_leave( C, out, &ns );
		}
		break;
	case SVT_FUNC:
		sgs_Printf( C, SGS_INFO, -1, "Functions are strictly immutable and cannot be cloned." );
		stk_setlvar_null( C, out );
		break;
	case SVT_OBJECT:
		if( obj_exec( C, SOP_CLONE, var->data.O, 0 ) != SGS_SUCCESS )
		{
			sgs_Printf( C, SGS_ERROR, -1, "This object does not support cloning or failed to do it." );
			stk_setlvar_null( C, out );
		}
		else
		{
			sgs_Variable ns = *stk_getpos( C, -1 );
			stk_setlvar( C, out, &ns );
			stk_pop1( C );
		}
		break;
	default:
		stk_setlvar( C, out, var );
		break;
	}
}

static void vm_op_concat( SGS_CTX, int16_t out, sgs_Variable *A, sgs_Variable *B )
{
	sgs_Variable vA, vB, N;
	vA = *A;
	vB = *B;
	VAR_ACQUIRE( &vA );
	VAR_ACQUIRE( &vB );
	vm_convert( C, &vA, SVT_STRING );
	vm_convert( C, &vB, SVT_STRING );
	var_create_0str( C, &N, vA.data.S->size + vB.data.S->size );
	memcpy( var_cstr( &N ), var_cstr( &vA ), vA.data.S->size );
	memcpy( var_cstr( &N ) + vA.data.S->size, var_cstr( &vB ), vB.data.S->size );
	stk_setvar_leave( C, out, &N );
	VAR_RELEASE( &vA );
	VAR_RELEASE( &vB );
}

static int vm_op_concat_ex( SGS_CTX, int args )
{
	int i;
	int32_t totsz = 0, curoff = 0;
	sgs_Variable *var, N;
	if( args < 2 )
		return 1;
	if( sgs_StackSize( C ) < args )
		return 0;
	for( i = 1; i <= args; ++i )
	{
		if( vm_convert_stack( C, -i, SVT_STRING ) != SGS_SUCCESS )
			return 0;
		var = stk_getpos( C, -i );
		totsz += var->data.S->size;
	}
	var_create_0str( C, &N, totsz );
	for( i = args; i >= 1; --i )
	{
		var = stk_getpos( C, -i );
		memcpy( var_cstr( &N ) + curoff, var_cstr( var ), var->data.S->size );
		curoff += var->data.S->size;
	}
	stk_setvar_leave( C, -args, &N );
	stk_pop( C, args - 1 );
	return 1;
}

static int vm_op_negate( SGS_CTX, sgs_VarPtr out, sgs_Variable *A )
{
	int i = A->type;

	switch( i )
	{
	case SVT_NULL: var_setnull( C, out ); break;
	case SVT_BOOL: var_setint( C, out, -A->data.B ); break;
	case SVT_INT: var_setint( C, out, -A->data.I ); break;
	case SVT_REAL: var_setreal( C, out, -A->data.R ); break;
	case SVT_OBJECT:
		{
			int ofs = out - C->stack_off;
			stk_push( C, A );
			if( obj_exec( C, SOP_OP_NEGATE, A->data.O, 1 ) == SGS_SUCCESS )
			{
				out = C->stack_off + ofs;
				VAR_RELEASE( out );
				*out = *stk_getpos( C, -1 );
				stk_pop1nr( C );
				stk_pop1( C );
				break;
			}
			stk_pop1( C );
			sgs_Printf( C, SGS_ERROR, -1, "Given object does not support negation." );
			var_setnull( C, C->stack_off + ofs );
		}
		return 0;
	default:
		sgs_Printf( C, SGS_ERROR, -1, "Negating variable of type %s is not supported.", sgs_VarNames[ i ] );
		var_setnull( C, out );
		return 0;
	}

	return 1;
}

static void vm_op_boolinv( SGS_CTX, int16_t out, sgs_Variable *A )
{
	int val = !var_getbool( C, A );
	var_setbool( C, C->stack_off + out, val );
}

static void vm_op_invert( SGS_CTX, int16_t out, sgs_Variable *A )
{
	sgs_Integer val = ~var_getint( C, A );
	var_setint( C, C->stack_off + out, val );
}


#define VAR_MOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, sgs_VarPtr out, sgs_Variable *A ) { \
	switch( A->type ){ \
		case SVT_INT: var_setint( C, out, A->data.I op ); break; \
		case SVT_REAL: var_setreal( C, out, A->data.R op ); break; \
		default: sgs_Printf( C, SGS_ERROR, -1, "Cannot " #pfx "rement null/bool/string/func/cfunc/object variables!" ); return; \
	} }

VAR_MOP( inc, +1 )
VAR_MOP( dec, -1 )


#define ARITH_OP_ADD	0
#define ARITH_OP_SUB	1
#define ARITH_OP_MUL	2
#define ARITH_OP_DIV	3
#define ARITH_OP_MOD	4
static const void* aop_sops[] = { SOP_OP_ADD, SOP_OP_SUB, SOP_OP_MUL, SOP_OP_DIV, SOP_OP_MOD };
static const uint8_t aop_types[ 35 ] = /* 5x5: NBIRS */
{
	SVT_NULL, SVT_INT,  SVT_INT,  SVT_REAL, SVT_REAL,
	SVT_INT,  SVT_INT,  SVT_INT,  SVT_REAL, SVT_REAL,
	SVT_INT,  SVT_INT,  SVT_INT,  SVT_REAL, SVT_REAL,
	SVT_REAL, SVT_REAL, SVT_REAL, SVT_REAL, SVT_REAL,
	SVT_REAL, SVT_REAL, SVT_REAL, SVT_REAL, SVT_REAL,
};
static void vm_arith_op( SGS_CTX, sgs_VarPtr out, sgs_VarPtr a, sgs_VarPtr b, uint8_t op )
{
	if( a->type == SVT_REAL && b->type == SVT_REAL )
	{
		sgs_Real A = a->data.R, B = b->data.R, R;
		switch( op ){
			/*
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			*/
			case ARITH_OP_DIV: if( B == 0 ) goto div0err; R = A / B; break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = fmod( A, B ); break;
			default: R = 0; break;
		}
		var_setreal( C, out, R );
		return;
	}
	if( a->type == SVT_INT && b->type == SVT_INT )
	{
		sgs_Integer A = a->data.I, B = b->data.I, R;
		switch( op ){
			/*
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			*/
			case ARITH_OP_DIV: if( B == 0 ) goto div0err; R = A / B; break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = A % B; break;
			default: R = 0; break;
		}
		var_setint( C, out, R );
	}

	if( a->type == SVT_OBJECT || b->type == SVT_OBJECT )
	{
		const void* sop = aop_sops[ op ];
		int ofs = out - C->stack_off;

		USING_STACK
		stk_makespace( C, 2 );
		*C->stack_top++ = *a;
		*C->stack_top++ = *b;
		VAR_ACQUIRE( a );
		VAR_ACQUIRE( b );

		if( ( a->type == SVT_OBJECT && obj_exec( C, sop, a->data.O, 2 ) == SGS_SUCCESS ) ||
			( b->type == SVT_OBJECT && obj_exec( C, sop, b->data.O, 2 ) == SGS_SUCCESS ) )
		{
			USING_STACK
			VAR_RELEASE( C->stack_off + ofs );
			C->stack_off[ ofs ] = *--C->stack_top;
			stk_pop2( C );
			return;
		}

		stk_pop2( C );
		goto fail;
	}

	if( a->type == SVT_FUNC || a->type == SVT_CFUNC ||
		b->type == SVT_FUNC || b->type == SVT_CFUNC )
		goto fail;

	if( a->type >= SVT_REAL || b->type >= SVT_REAL )
	{
		sgs_Real A = var_getreal_simple( a ), B = var_getreal_simple( b ), R;
		switch( op ){
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			case ARITH_OP_DIV: if( B == 0 ) goto div0err; R = A / B; break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = fmod( A, B ); break;
			default: R = 0; break;
		}
		var_setreal( C, out, R );
	}
	else
	{
		sgs_Integer A = var_getint_simple( a ), B = var_getint_simple( b ), R;
		switch( op ){
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			case ARITH_OP_DIV: if( B == 0 ) goto div0err; R = A / B; break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = A % B; break;
			default: R = 0; break;
		}
		var_setint( C, out, R );
	}
	return;

div0err:
	VAR_RELEASE( out );
	out->type = SVT_NULL;
	sgs_Printf( C, SGS_ERROR, -1, "Division by 0" );
	return;
fail:
	VAR_RELEASE( out );
	out->type = SVT_NULL;
	sgs_Printf( C, SGS_ERROR, -1, "Operation is not supported on the given set of arguments" );
}


#define VAR_IOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, int16_t out, sgs_Variable* a, sgs_Variable* b ) { \
	sgs_Integer A = var_getint( C, a ); \
	sgs_Integer B = var_getint( C, b ); \
	var_setint( C, C->stack_off + out, A op B ); \
	}

VAR_IOP( and, & )
VAR_IOP( or, | )
VAR_IOP( xor, ^ )
VAR_IOP( lsh, << )
VAR_IOP( rsh, >> )


/* returns 0 if equal, >0 if A is bigger, <0 if B is bigger */
static sgs_Real vm_compare( SGS_CTX, sgs_VarPtr a, sgs_VarPtr b )
{
	const uint8_t ta = a->type, tb = b->type;
	if( ta == SVT_INT && tb == SVT_INT ) return a->data.I - b->data.I;
	if( ta == SVT_REAL && tb == SVT_REAL ) return a->data.R - b->data.R;

	if( ta == SVT_OBJECT || tb == SVT_OBJECT )
	{
		USING_STACK
		stk_makespace( C, 2 );
		*C->stack_top++ = *a;
		*C->stack_top++ = *b;
		VAR_ACQUIRE( a );
		VAR_ACQUIRE( b );

		if( ( ta == SVT_OBJECT && obj_exec( C, SOP_COMPARE, a->data.O, 2 ) == SGS_SUCCESS ) ||
			( tb == SVT_OBJECT && obj_exec( C, SOP_COMPARE, b->data.O, 2 ) == SGS_SUCCESS ) )
		{
			USING_STACK
			sgs_Real out = var_getreal( C, --C->stack_top );
			VAR_RELEASE( C->stack_top );
			stk_pop2( C );
			return out;
		}

		stk_pop2( C );
		/* fallback: check for equality */
		if( ta == tb )
			return a->data.O - b->data.O;
		else
			return ta - tb;
	}
	if( ta == SVT_BOOL || tb == SVT_BOOL )
		return var_getbool( C, a ) - var_getbool( C, b );
	if( ta == SVT_FUNC || tb == SVT_FUNC || ta == SVT_CFUNC || tb == SVT_CFUNC )
	{
		if( ta != tb )
			return ta - tb;
		if( ta == SVT_FUNC )
			return a->data.F - b->data.F;
		else
			return a->data.C == b->data.C ? 0 : a->data.C < b->data.C ? -1 : 1;
	}
	if( ta == SVT_STRING || tb == SVT_STRING )
	{
		int out;
		sgs_Variable A = *a, B = *b;
		stk_push( C, &A );
		stk_push( C, &B );
		if( vm_convert_stack( C, -2, SVT_STRING ) != SGS_SUCCESS ) return -1;
		if( vm_convert_stack( C, -1, SVT_STRING ) != SGS_SUCCESS ) return 1;
		a = stk_getpos( C, -2 );
		b = stk_getpos( C, -1 );
		out = strncmp( var_cstr( a ), var_cstr( b ), MIN( a->data.S->size, b->data.S->size ) );
		if( out == 0 && a->data.S->size != b->data.S->size )
			out = a->data.S->size - b->data.S->size;
		else if( out != 0 )
			out = out > 0 ? 1 : -1;
		stk_pop2( C );
		return out;
	}
	/* int/real */
	return var_getreal( C, a ) - var_getreal( C, b );
}


static void vm_forprep( SGS_CTX, int outiter, sgs_VarPtr obj )
{
	int ret;

	if( obj->type != SVT_OBJECT )
	{
		sgs_Printf( C, SGS_ERROR, -1, "Variable of type '%s' doesn't have an iterator", sgs_VarNames[ obj->type ] );
		stk_setvar_null( C, outiter );
		return;
	}

	ret = obj_exec( C, SOP_GETITER, obj->data.O, 0 );
	if( ret != SGS_SUCCESS )
	{
		sgs_Printf( C, SGS_ERROR, -1, "Object [%p] doesn't have an iterator", obj->data.O );
		stk_setvar_null( C, outiter );
		return;
	}
	stk_setvar( C, outiter, stk_getpos( C, -1 ) );
	stk_pop1( C );
}

static void vm_fornext( SGS_CTX, int outkey, int outstate, sgs_VarPtr iter )
{
	if( iter->type != SVT_OBJECT || obj_exec( C, SOP_NEXTKEY, iter->data.O, 0 ) != SGS_SUCCESS )
	{
		sgs_Printf( C, SGS_ERROR, -1, "Failed to retrieve data from iterator" );
		stk_setvar_null( C, outstate );
		return;
	}

	stk_setvar( C, outkey, stk_getpos( C, -2 ) );
	stk_setvar( C, outstate, stk_getpos( C, -1 ) );
	stk_pop2( C );
}


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
		stk_push_nulls( C, expect - rvc );

	stk_setvar( C, outpos, stk_getpos( C, -1 ) );
	stk_pop1( C );
}

static void vm_make_dict( SGS_CTX, int args, int16_t outpos )
{
	int expect = 1, rvc, stkoff = C->stack_off - C->stack_base;

	sgs_BreakIf( sgs_StackSize( C ) < args );
	C->stack_off = C->stack_top - args;

	rvc = (*C->dict_func)( C );

	stk_clean( C, C->stack_off, C->stack_top - rvc );
	C->stack_off = C->stack_base + stkoff;

	if( rvc > expect )
		stk_pop( C, rvc - expect );
	else
		stk_push_nulls( C, expect - rvc );

	stk_setvar( C, outpos, stk_getpos( C, -1 ) );
	stk_pop1( C );
}


static int vm_exec( SGS_CTX, sgs_Variable* consts, int32_t constcount );


/*
	Call the virtual machine.
	Args must equal the number of arguments pushed before the function
*/
static int vm_call( SGS_CTX, int args, int gotthis, int expect, sgs_Variable* func )
{
	sgs_Variable V = *func;
	int stkoff = C->stack_off - C->stack_base;
	int rvc = 0, ret = 1;

	sgs_BreakIf( sgs_StackSize( C ) < args + gotthis );
	vm_frame_push( C, &V, NULL, NULL, 0 );
	C->stack_off = C->stack_top - args;

	if( func->type == SVT_CFUNC )
	{
		int stkoff2 = C->stack_off - C->stack_base;
		int hadthis = C->call_this;
		C->call_this = gotthis;
		rvc = (*func->data.C)( C );
		C->call_this = hadthis;
		C->stack_off = C->stack_base + stkoff2;
	}
	else if( func->type == SVT_FUNC )
	{
		func_t* F = func->data.F;
		int stkargs = args + ( F->gotthis && gotthis );
		int expargs = F->numargs + F->gotthis;
		/* fix argument stack */
		if( stkargs > expargs )
			stk_pop( C, stkargs - expargs );
		else
			stk_push_nulls( C, expargs - stkargs );
		/* if <this> was expected but wasn't properly passed, assume that it's already in the argument list */
		/* if <this> wasn't expected but was passed, just don't use it */
		if( F->gotthis && gotthis )
			C->stack_off--;
		{
			int constcnt = F->instr_off / sizeof( sgs_Variable* );
			rvc = vm_exec( C, func_consts( F ), constcnt );
		}
		if( F->gotthis && gotthis )
			C->stack_off++;
	}
	else if( func->type == SVT_OBJECT )
	{
		int cargs = C->call_args, cexp = C->call_expect;
		C->call_args = args;
		C->call_expect = expect;
		rvc = obj_exec( C, SOP_CALL, func->data.O, args );
		if( rvc < 0 )
		{
			sgs_Printf( C, SGS_ERROR, -1, "The object could not be called" );
			rvc = 0;
			ret = 0;
		}
		C->call_args = cargs;
		C->call_expect = cexp;
	}
	else
	{
		sgs_Printf( C, SGS_ERROR, -1, "Variable of type '%s' cannot be called", sgs_VarNames[ func->type ] );
		ret = 0;
	}

	/* subtract gotthis from offset if pushed extra variable */
	stk_clean( C, C->stack_off - gotthis, C->stack_top - rvc );
	C->stack_off = C->stack_base + stkoff;
	vm_frame_pop( C );

	if( rvc > expect )
		stk_pop( C, rvc - expect );
	else
		stk_push_nulls( C, expect - rvc );

	return ret;
}


#if SGS_DEBUG && SGS_DEBUG_VALIDATE
static SGS_INLINE sgs_Variable* const_getvar( sgs_Variable* consts, int32_t count, int16_t off )
{
	sgs_BreakIf( (int)off >= (int)count );
	return consts + off;
}
#endif

/*
	Main VM execution loop
*/
const char* opnames[] =
{
	"nop",  "push", "push_nulls", "pop_n", "pop_reg",  "return", "jump", "jump_if_true", "jump_if_false", "call",
	"for_prep", "for_next", "getvar", "setvar", "getprop", "setprop", "getindex", "setindex",  "set", "copy",
	"concat", "negate", "bool_inv", "invert",  "inc", "dec", "add", "sub", "mul", "div", "mod",
	"and", "or", "xor", "lsh", "rsh",  "seq", "sneq", "eq", "neq", "lt", "gte", "gt", "lte",  "array", "dict"
};
static int vm_exec( SGS_CTX, sgs_Variable* consts, int32_t constcount )
{
	sgs_StackFrame* SF = C->sf_last;
	int32_t ret = 0;
	sgs_Variable* cptr = consts;
	const instr_t* pend = SF->iend, *pp = SF->code;

#if SGS_DEBUG && SGS_DEBUG_VALIDATE
	int stkoff = C->stack_top - C->stack_off;
#  define RESVAR( v ) ( CONSTVAR(v) ? const_getvar( cptr, constcount, CONSTDEC(v) ) : stk_getlpos( C, (v) ) )
#else
#  define RESVAR( v ) ( CONSTVAR(v) ? ( cptr + CONSTDEC(v) ) : stk_getlpos( C, (v) ) )
#endif
	UNUSED( constcount );

	while( pp < pend )
	{
		const instr_t I = *pp;
		SF->iptr = pp;
#define instr INSTR_GET_OP(I)
#define argA INSTR_GET_A(I)
#define argB INSTR_GET_B(I)
#define argC INSTR_GET_C(I)
#define argE INSTR_GET_E(I)
		const sgs_VarPtr p1 = C->stack_off + argA;

#if SGS_DEBUG
#  if SGS_DEBUG_INSTR
		printf( "*** [at 0x%04X] %s ***\n", ptr - 1 - (char*)code, opnames[ instr ] );
#  endif
#  if SGS_DEBUG_STATE
		sgsVM_StackDump( C );
#  endif
#endif

		switch( instr )
		{
		case SI_NOP: break;

		case SI_PUSH: stk_push( C, RESVAR( argB ) ); break;
		case SI_PUSHN:
		{
			int count = argA;
			stk_makespace( C, count );
			while( count-- )
				(C->stack_top++)->type = SVT_NULL;
			break;
		}

		case SI_POPN: stk_pop( C, argA ); break;
		case SI_POPR: stk_setlvar( C, argA, stk_gettop( C ) ); stk_pop1( C ); break;

		case SI_RETN:
		{
			ret = argA;
			sgs_BreakIf( ( C->stack_top - C->stack_off ) - stkoff < ret );
			pp = pend;
			break;
		}

		case SI_JUMP:
		{
			int16_t off = argE;
			pp += off;
			sgs_BreakIf( pp > pend || pp < SF->code );
			break;
		}

		case SI_JMPT:
		{
			int16_t off = argE;
			sgs_BreakIf( pp + off > pend || pp + off < SF->code );
			if( var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}
		case SI_JMPF:
		{
			int16_t off = argE;
			sgs_BreakIf( pp + off > pend || pp + off < SF->code );
			if( !var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}

		case SI_CALL:
		{
			int expect = argA, args = argB, src = argC;
			int gotthis = ( argB & 0x100 ) != 0;
			args &= 0xff;
			vm_call( C, args, gotthis, expect, RESVAR( src ) );
			break;
		}

		case SI_FORPREP: vm_forprep( C, argA, RESVAR( argB ) ); break;
		case SI_FORNEXT: vm_fornext( C, argA, argB, RESVAR( argC ) ); break;

#define a1 argA
#define ARGS_2 const sgs_VarPtr p2 = RESVAR( argB );
#define ARGS_3 const sgs_VarPtr p2 = RESVAR( argB ), p3 = RESVAR( argC );
		case SI_GETVAR: { ARGS_2; vm_getvar( C, p1, p2 ); break; }
		case SI_SETVAR: { ARGS_3; vm_setvar( C, p2, p3 ); break; }
		case SI_GETPROP: { ARGS_3; vm_properr( C, vm_getprop( C, a1, p2, p3, FALSE ), p3, FALSE ); break; }
		case SI_SETPROP: { ARGS_3; vm_properr( C, vm_setprop( C, p1, p2, p3, FALSE ), p2, FALSE ); break; }
		case SI_GETINDEX: { ARGS_3; vm_properr( C, vm_getprop( C, a1, p2, p3, TRUE ), p3, TRUE ); break; }
		case SI_SETINDEX: { ARGS_3; vm_properr( C, vm_setprop( C, p1, p2, p3, TRUE ), p2, TRUE ); break; }

		case SI_SET: { ARGS_2; VAR_ACQUIRE( p2 ); VAR_RELEASE( p1 ); *p1 = *p2; break; }
		case SI_CLONE: { ARGS_2; vm_clone( C, a1, p2 ); break; }
		case SI_CONCAT: { ARGS_3; vm_op_concat( C, a1, p2, p3 ); break; }
		case SI_NEGATE: { ARGS_2; vm_op_negate( C, p1, p2 ); break; }
		case SI_BOOL_INV: { ARGS_2; vm_op_boolinv( C, a1, p2 ); break; }
		case SI_INVERT: { ARGS_2; vm_op_invert( C, a1, p2 ); break; }

		case SI_INC: { ARGS_2; vm_op_inc( C, p1, p2 ); break; }
		case SI_DEC: { ARGS_2; vm_op_dec( C, p1, p2 ); break; }
		case SI_ADD: { ARGS_3;
			if( p2->type == SVT_REAL && p3->type == SVT_REAL ){ var_setreal( C, p1, p2->data.R + p3->data.R ); break; }
			if( p2->type == SVT_INT && p3->type == SVT_INT ){ var_setint( C, p1, p2->data.I + p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_ADD ); break; }
		case SI_SUB: { ARGS_3;
			if( p2->type == SVT_REAL && p3->type == SVT_REAL ){ var_setreal( C, p1, p2->data.R - p3->data.R ); break; }
			if( p2->type == SVT_INT && p3->type == SVT_INT ){ var_setint( C, p1, p2->data.I - p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_SUB ); break; }
		case SI_MUL: { ARGS_3;
			if( p2->type == SVT_REAL && p3->type == SVT_REAL ){ var_setreal( C, p1, p2->data.R * p3->data.R ); break; }
			if( p2->type == SVT_INT && p3->type == SVT_INT ){ var_setint( C, p1, p2->data.I * p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_MUL ); break; }
		case SI_DIV: { ARGS_3; vm_arith_op( C, p1, p2, p3, ARITH_OP_DIV ); break; }
		case SI_MOD: { ARGS_3; vm_arith_op( C, p1, p2, p3, ARITH_OP_MOD ); break; }

		case SI_AND: { ARGS_3; vm_op_and( C, a1, p2, p3 ); break; }
		case SI_OR: { ARGS_3; vm_op_or( C, a1, p2, p3 ); break; }
		case SI_XOR: { ARGS_3; vm_op_xor( C, a1, p2, p3 ); break; }
		case SI_LSH: { ARGS_3; vm_op_lsh( C, a1, p2, p3 ); break; }
		case SI_RSH: { ARGS_3; vm_op_rsh( C, a1, p2, p3 ); break; }

#define STRICTLY_EQUAL( val ) if( p2->type != p3->type || ( p2->type == SVT_OBJECT && \
								p2->data.O->iface != p3->data.O->iface ) ) { var_setbool( C, p1, val ); break; }
#define VCOMPARE( op ) int cr = vm_compare( C, p2, p3 ) op 0; var_setbool( C, C->stack_off + a1, cr );
		case SI_SEQ: { ARGS_3; STRICTLY_EQUAL( FALSE ); VCOMPARE( == ); break; }
		case SI_SNEQ: { ARGS_3; STRICTLY_EQUAL( TRUE ); VCOMPARE( != ); break; }
		case SI_EQ: { ARGS_3; VCOMPARE( == ); break; }
		case SI_NEQ: { ARGS_3; VCOMPARE( != ); break; }
		case SI_LT: { ARGS_3; VCOMPARE( < ); break; }
		case SI_GTE: { ARGS_3; VCOMPARE( >= ); break; }
		case SI_GT: { ARGS_3; VCOMPARE( > ); break; }
		case SI_LTE: { ARGS_3; VCOMPARE( <= ); break; }

		case SI_ARRAY: { vm_make_array( C, argB, argA ); break; }
		case SI_DICT: { vm_make_dict( C, argB, argA ); break; }
#undef VCOMPARE
#undef STRICTLY_EQUAL
#undef ARGS_2
#undef ARGS_3
#undef a1
#undef RESVAR

		default:
			sgs_Printf( C, SGS_ERROR, -1, "Illegal instruction executed: 0x%08X", I );
			break;
		}

		if( ( C->state & SGS_MUST_STOP ) == SGS_MUST_STOP )
			break;

		pp++;
	}

#if SGS_DEBUG && SGS_DEBUG_STATE
    sgs_MemCheckDbg( C->stack_base );
	sgsVM_StackDump( C );
#endif
	return ret;
}


/* INTERNAL INERFACE */

static int funct_size( func_t* f )
{
	int sz = f->size + f->funcname.mem;
	sgs_Variable* beg = (sgs_Variable*) func_consts( f );
	sgs_Variable* end = (sgs_Variable*) func_bytecode( f );
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
	case SVT_FUNC: out += funct_size( var->data.F ); break;
	/* case SVT_OBJECT: break; */
	case SVT_STRING: out += var->data.S->mem + sizeof( string_t ); break;
	}
	return out;
}

void sgsVM_VarDump( sgs_VarPtr var )
{
	printf( "%s (size:%d)", sgs_VarNames[ var->type ], sgsVM_VarSize( var ) );
	switch( var->type )
	{
	case SVT_NULL: break;
	case SVT_BOOL: printf( " = %s", var->data.B ? "True" : "False" ); break;
	case SVT_INT: printf( " = %" PRId64, var->data.I ); break;
	case SVT_REAL: printf( " = %f", var->data.R ); break;
	case SVT_STRING: printf( " [rc:%d] = \"", var->data.S->refcount ); print_safe( var_cstr( var ), 16 ); printf( var->data.S->size > 16 ? "...\"" : "\"" ); break;
	case SVT_FUNC: printf( " [rc:%d]", var->data.F->refcount ); break;
	case SVT_CFUNC: printf( " = %p", var->data.C ); break;
	case SVT_OBJECT: printf( "TODO [object impl]" ); break;
	}
}

void sgsVM_StackDump( SGS_CTX )
{
	int i, stksz = C->stack_top - C->stack_base;
	printf( "STACK (size=%d, bytes=%d/%d)--\n", stksz, (int)( stksz * STK_UNITSIZE ), (int)( C->stack_mem * STK_UNITSIZE ) );
	for( i = 0; i < stksz; ++i )
	{
		sgs_VarPtr var = C->stack_base + i;
		printf( "  " ); sgsVM_VarDump( var ); printf( "\n" );
	}
	printf( "--\n" );
}

int sgsVM_ExecFn( SGS_CTX, void* code, int32_t codesize, void* data, int32_t datasize, int clean, uint16_t* T )
{
	int stkoff = C->stack_off - C->stack_base, rvc;
	vm_frame_push( C, NULL, T, (instr_t*) code, codesize / sizeof( instr_t ) );
	rvc = vm_exec( C, (sgs_Variable*) data, datasize / sizeof( sgs_Variable* ) );
	C->stack_off = C->stack_base + stkoff;
	if( clean )
		stk_pop( C, C->stack_top - C->stack_off );
	else
	{
		/* keep only returned values */
		stk_clean( C, C->stack_off, C->stack_top - rvc );
	}
	vm_frame_pop( C );
	return rvc;
}

int sgsVM_VarCall( SGS_CTX, sgs_Variable* var, int args, int expect, int gotthis )
{
	return vm_call( C, args, gotthis, expect, var );
}


int sgs_PushNull( SGS_CTX )
{
	stk_push_null( C );
	return SGS_SUCCESS;
}

int sgs_PushBool( SGS_CTX, int value )
{
	sgs_Variable var;
	var.type = SVT_BOOL;
	var.data.B = value ? 1 : 0;
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

int sgs_PushInt( SGS_CTX, sgs_Integer value )
{
	sgs_Variable var;
	var.type = SVT_INT;
	var.data.I = value;
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

int sgs_PushReal( SGS_CTX, sgs_Real value )
{
	sgs_Variable var;
	var.type = SVT_REAL;
	var.data.R = value;
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

int sgs_PushStringBuf( SGS_CTX, const char* str, int32_t size )
{
	sgs_Variable var;
	if( str )
		var_create_str( C, &var, str, size );
	else
		var_create_0str( C, &var, size );
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

int sgs_PushString( SGS_CTX, const char* str )
{
	sgs_Variable var;
	var_create_str( C, &var, str, -1 );
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

int sgs_PushCFunction( SGS_CTX, sgs_CFunc func )
{
	sgs_Variable var;
	var.type = SVT_CFUNC;
	var.data.C = func;
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

int sgs_PushObject( SGS_CTX, void* data, void** iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, data, iface );
	stk_push_leave( C, &var );
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
	if( ret != SGS_SUCCESS )
		return ret;
	ret = vm_getprop( C, stk_absindex( C, -1 ), stk_getpos( C, -2 ), stk_getpos( C, -1 ), FALSE );
	stk_popskip( C, 1, 1 );
	return ret;
}

int sgs_StringConcat( SGS_CTX )
{
	if( sgs_StackSize( C ) < 2 )
		return SGS_ESTKUF;
	vm_op_concat( C, stk_absindex( C, -1 ), stk_getpos( C, -2 ), stk_getpos( C, -1 ) );
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
int sgs_PopSkip( SGS_CTX, int count, int skip )
{
	if( C->stack_top - C->stack_base < count + skip )
		return SGS_ESTKUF;
	stk_popskip( C, count, skip );
	return SGS_SUCCESS;
}

int sgs_Call( SGS_CTX, int args, int expect )
{
	return vm_call( C, args, FALSE, expect, stk_getpos( C, -1 - args ) ) ? SGS_SUCCESS : SGS_EINPROC;
}

int sgs_Method( SGS_CTX )
{
	if( C->call_this )
	{
		C->stack_off--;
		C->call_this = FALSE;
		return TRUE;
	}
	else
		return FALSE;
}

int sgs_TypeOf( SGS_CTX )
{
	return vm_gettype( C ) ? SGS_SUCCESS : SGS_ESTKUF;
}


int sgs_GetGlobal( SGS_CTX, const char* name )
{
	sgs_VarPtr pos;
	sgs_PushString( C, name );
	pos = stk_getpos( C, -1 );
	return vm_getvar( C, pos, pos );
}

int sgs_SetGlobal( SGS_CTX, const char* name )
{
	sgs_PushString( C, name );
	vm_setvar( C, stk_getpos( C, -1 ), stk_getpos( C, -2 ) );
	sgs_Pop( C, 2 );
	return SGS_SUCCESS;
}


int sgs_GetIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Variable* idx )
{
	int ret;
	sgs_Variable Sobj = *obj, Sidx = *idx;
	VAR_ACQUIRE( &Sobj );
	VAR_ACQUIRE( &Sidx );

	stk_push_null( C );
	ret = vm_getprop( C, stk_absindex( C, -1 ), &Sobj, &Sidx, TRUE );
	*out = *stk_getpos( C, -1 );
	if( ret == SGS_SUCCESS )
	{
		VAR_ACQUIRE( out );
	}
	stk_pop1( C );

	VAR_RELEASE( &Sobj );
	VAR_RELEASE( &Sidx );
	return ret;
}

int sgs_SetIndex( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* val )
{
	return vm_setprop( C, obj, idx, val, TRUE );
}

int sgs_GetNumIndex( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Integer idx )
{
	sgs_Variable tmp;
	tmp.type = SVT_INT;
	tmp.data.I = idx;

	return sgs_GetIndex( C, out, obj, &tmp );
}

int sgs_SetNumIndex( SGS_CTX, sgs_Variable* obj, sgs_Integer idx, sgs_Variable* val )
{
	sgs_Variable tmp;
	tmp.type = SVT_INT;
	tmp.data.I = idx;

	return sgs_SetIndex( C, obj, &tmp, val );
}


int sgs_GetBool( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getpos( C, item );
	return var_getbool( C, var );
}

sgs_Integer sgs_GetInt( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getpos( C, item );
	return var_getint( C, var );
}

sgs_Real sgs_GetReal( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getpos( C, item );
	return var_getreal( C, var );
}


int sgs_ToBool( SGS_CTX, int item )
{
	if( vm_convert_stack( C, item, SVT_BOOL ) != SGS_SUCCESS )
		return 0;
	return stk_getpos( C, item )->data.B;
}

sgs_Integer sgs_ToInt( SGS_CTX, int item )
{
	if( vm_convert_stack( C, item, SVT_INT ) != SGS_SUCCESS )
		return 0;
	return stk_getpos( C, item )->data.I;
}

sgs_Real sgs_ToReal( SGS_CTX, int item )
{
	if( vm_convert_stack( C, item, SVT_REAL ) != SGS_SUCCESS )
		return 0;
	return stk_getpos( C, item )->data.R;
}

char* sgs_ToStringBuf( SGS_CTX, int item, sgs_Integer* outsize )
{
	sgs_Variable* var;
	if( vm_convert_stack( C, item, SVT_STRING ) != SGS_SUCCESS )
		return NULL;
	var = stk_getpos( C, item );
	if( outsize )
		*outsize = var->data.S->size;
	return var_cstr( var );
}


int sgs_StackSize( SGS_CTX )
{
	return C->stack_top - C->stack_off;
}

sgs_Variable* sgs_StackItem( SGS_CTX, int item )
{
	return stk_getpos( C, item );
}

int sgs_ItemType( SGS_CTX, int item )
{
	return stk_getpos( C, item )->type;
}

void sgs_Acquire( SGS_CTX, sgs_Variable* var )
{
	UNUSED( C );
	VAR_ACQUIRE( var );
}

void sgs_Release( SGS_CTX, sgs_Variable* var )
{
	if( var->type == SVT_OBJECT && var->data.O->destroying )
		return;
	VAR_RELEASE( var );
}

int sgs_GCExecute( SGS_CTX )
{
	C->redblue = !C->redblue;

	/* -- mark -- */
	/* gclist / currently executed "main" function */
	{
		sgs_VarPtr vbeg = C->gclist;
		sgs_VarPtr vend = vbeg + C->gclist_size;
		while( vbeg < vend )
		{
			int ret = vm_gcmark( C, vbeg );
			if( ret != SGS_SUCCESS )
				return ret;
			vbeg++;
		}
	}

	/* stack */
	{
		sgs_VarPtr vbeg = C->stack_base;
		sgs_VarPtr vend = C->stack_top;
		while( vbeg < vend )
		{
			int ret = vm_gcmark( C, vbeg++ );
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
		object_t* p = C->objs;
		while( p )
		{
			if( p->redblue != C->redblue )
			{
				p->destroying = TRUE;
				p->refcount++;
			}
			p = p->next;
		}
	}

	/* destruct objects */
	{
		object_t* p = C->objs;
		while( p )
		{
			object_t* pn = p->next;
			if( p->redblue != C->redblue )
			{
				int ret = obj_exec( C, SOP_DESTRUCT, p, 0 );
				if( ret != SGS_SUCCESS )
					return ret;
			}
			p = pn;
		}
	}

	/* free variables */
	{
		object_t* p = C->objs;
		while( p )
		{
			object_t* pn = p->next;
			if( p->redblue != C->redblue )
				var_free_object( C, p );
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

int sgs_CheckArgs( SGS_CTX, const char* name, const char* str )
{
	const char* orig = str;
	int curarg = 0, ret;
	const char* end = ca_curend( str, "," );
	while( *str )
	{
		if( curarg >= sgs_StackSize( C ) )
			goto fail;

		sgs_PushVariable( C, stk_getpos( C, curarg ) );
		if( !vm_gettype( C ) )
			goto fail;

		ret = ca_compare( str, end, var_cstr( stk_getpos( C, -1 ) ) );
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
	sgs_Printf( C, SGS_ERROR, -1, "Invalid arguments passed to %s, expected \"%s\"", name, orig );
	return 0;
}


const char* sgs_GetStringPtr( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getpos( C, item );
	sgs_BreakIf( var->type != SVT_STRING );
	return var_cstr( var );
}

int32_t sgs_GetStringSize( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getpos( C, item );
	sgs_BreakIf( var->type != SVT_STRING );
	return var->data.S->size;
}

sgs_VarObj* sgs_GetObjectData( SGS_CTX, int item )
{
	sgs_Variable* var = stk_getpos( C, item );
	sgs_BreakIf( var->type != SVT_OBJECT );
	return var->data.O;
}


