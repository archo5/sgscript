

#include <math.h>
#include <stdarg.h>
#include <ctype.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL
#define SGS_INTERNAL_STRINGTABLES

#include "sgs_xpc.h"
#include "sgs_int.h"



static int fastlog2( int x )
{
	int targetlevel = 0;
	while( x >>= 1 )
		++targetlevel;
	return targetlevel;
}


#define TYPENAME( type ) sgs_VarNames[ fastlog2( ( type & 0xff ) << 1 ) ]

#define IS_REFTYPE( type ) ( type & VTF_REF )


#define VAR_ACQUIRE( pvar ) { if( IS_REFTYPE( (pvar)->type ) ) var_acquire( C, pvar ); }
#define VAR_RELEASE( pvar ) { if( IS_REFTYPE( (pvar)->type ) ) var_release( C, pvar, 1 ); }
#define STKVAR_RELEASE( pvar ) { if( IS_REFTYPE( (pvar)->type ) ) var_release( C, pvar, 0 ); }


#define STK_UNITSIZE sizeof( sgs_Variable )


static int obj_exec( SGS_CTX, const sgs_ObjCallback sop, object_t* data, int arg, int args )
{
	sgs_ObjCallback* func = data->iface;
	int ret = SGS_ENOTFND, stkoff;
	
	while( *func != SOP_END )
	{
		if( *func == sop )
		{
			stkoff = C->stack_off - C->stack_base;
			C->stack_off = C->stack_top - args;
			ret = ( (sgs_ObjCallback) func[ 1 ] )( C, data, arg );
			C->stack_off = C->stack_base + stkoff;
			break;
		}
		func += 2;
	}
	
	return ret;
}

static int obj_exec_specific( SGS_CTX, sgs_ObjCallback fn, object_t* data, int arg, int args )
{
	if( !fn )
		return SGS_ENOTFND;
	else
	{
		int ret, stkoff = C->stack_off - C->stack_base;
		C->stack_off = C->stack_top - args;
		ret = fn( C, data, arg );
		C->stack_off = C->stack_base + stkoff;
		return ret;
	}
}

static void var_free_object( SGS_CTX, object_t* O )
{
	if( O->prev ) O->prev->next = O->next;
	if( O->next ) O->next->prev = O->prev;
	if( C->objs == O )
		C->objs = O->next;
	sgs_Dealloc( O );
	C->objcount--;
}

void sgsVM_VarDestroyObject( SGS_CTX, object_t* O )
{
	obj_exec( C, SOP_DESTRUCT, O, TRUE, 0 );
	var_free_object( C, O );
}

static void var_destroy_string( SGS_CTX, string_t* S )
{
#if SGS_STRINGTABLE_MAXLEN >= 0
	sgs_Variable tmp;
	tmp.type = VTC_STRING;
	tmp.data.S = S;
	VHTVar* p = S->size <= SGS_STRINGTABLE_MAXLEN ? vht_get( &C->stringtable, &tmp ) : NULL;
	if( p && p->key.data.S == S )
	{
		S->refcount = 2; /* the 'less code' way to avoid double free */
		vht_unset( &C->stringtable, C, &tmp );
		printf( "unset string %s\n", str_cstr(S) );
	}
#endif
	sgs_Dealloc( S );
}

static void var_release( SGS_CTX, sgs_VarPtr p, int notonstack );
static void var_destroy_func( SGS_CTX, func_t* F )
{
	sgs_VarPtr var = (sgs_VarPtr) func_consts( F ), vend = (sgs_VarPtr) func_bytecode( F );
	while( var < vend )
	{
		VAR_RELEASE( var );
		var++;
	}
	sgs_Dealloc( F->lineinfo );
	membuf_destroy( &F->funcname, C );
	membuf_destroy( &F->filename, C );
	sgs_Dealloc( F );
}

static void var_acquire( SGS_CTX, sgs_VarPtr p )
{
	UNUSED( C );
	if( p->type & VTF_REF )
		(*p->data.pRC)++;
}

static int is_on_stack( SGS_CTX, sgs_VarPtr p, uint32_t type )
{
	sgs_VarPtr from = C->stack_base, end = C->stack_top;
	while( from < end )
	{
		if( from->type == type && from->data.pRC == p->data.pRC )
			return TRUE;
		from++;
	}
	return FALSE;
}

static void var_release( SGS_CTX, sgs_VarPtr p, int notonstack )
{
	uint32_t type = p->type;
	(*p->data.pRC) -= notonstack;
	p->type = VTC_NULL;
	
	if( (*p->data.pRC) <= 0 && !is_on_stack( C, p, type ) )
	{
		switch( BASETYPE( type ) )
		{
		case SVT_STRING: var_destroy_string( C, p->data.S ); break;
		case SVT_FUNC: var_destroy_func( C, p->data.F ); break;
		case SVT_OBJECT: var_destroy_object( C, p->data.O ); break;
		}
	}
}


void sgsVM_ReleaseStack( SGS_CTX, sgs_Variable* var )
{
	STKVAR_RELEASE( var )
}


static void var_create_0str( SGS_CTX, sgs_VarPtr out, int32_t len )
{
	out->type = VTC_STRING;
	out->data.S = sgs_Alloc_a( string_t, len + 1 );
	out->data.S->refcount = 0;
	out->data.S->size = len;
	out->data.S->hash = 0;
	out->data.S->isconst = 0;
	var_cstr( out )[ len ] = 0;
}

void sgsVM_VarCreateString( SGS_CTX, sgs_Variable* out, const char* str, int32_t len )
{
	sgs_Hash hash;
	sgs_BreakIf( !str );

	if( len < 0 )
		len = strlen( str );

	hash = sgs_HashFunc( str, len );
	if( len <= SGS_STRINGTABLE_MAXLEN )
	{
		VHTVar* var = vht_get_str( &C->stringtable, str, len, hash );
		if( var )
		{
			*out = var->key;
			out->data.S->refcount++;
			return;
		}
	}

	var_create_0str( C, out, len );
	memcpy( str_cstr( out->data.S ), str, len );
	out->data.S->refcount = 1;
	out->data.S->hash = hash;
	out->data.S->isconst = 1;

	if( len <= SGS_STRINGTABLE_MAXLEN )
	{
		vht_set( &C->stringtable, C, out, NULL );
		out->data.S->refcount--;
	}
}

static void var_create_str( SGS_CTX, sgs_Variable* out, const char* str, int32_t len )
{
	sgs_BreakIf( !str );

	if( len < 0 )
		len = strlen( str );

	var_create_0str( C, out, len );
	memcpy( str_cstr( out->data.S ), str, len );
}

static void var_create_obj( SGS_CTX, sgs_Variable* out, void* data, sgs_ObjCallback* iface, int xbytes )
{
	object_t* obj = sgs_Alloc_a( object_t, xbytes );
	obj->data = data;
	if( xbytes )
		obj->data = ((char*)obj) + sizeof( object_t );
	obj->iface = iface;
	obj->redblue = C->redblue;
	obj->next = C->objs;
	obj->prev = NULL;
	obj->refcount = 0;
	if( obj->next ) /* ! */
		obj->next->prev = obj;
	C->objcount++;
	C->objs = obj;
	
	obj->getindex = NULL;
	obj->getnext = NULL;
	{
		sgs_ObjCallback* i = iface;
		while( *i )
		{
			if( i[0] == SOP_GETINDEX )
				obj->getindex = (sgs_ObjCallback) i[1];
			else if( i[0] == SOP_GETNEXT )
				obj->getnext = (sgs_ObjCallback) i[1];
			i += 2;
		}
	}

	out->type = VTC_OBJECT;
	out->data.O = obj;
}


/*
	Call stack
*/

static int vm_frame_push( SGS_CTX, sgs_Variable* func, uint16_t* T, instr_t* code, int icnt )
{
	sgs_StackFrame* F;

	if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
	{
		sgs_Printf( C, SGS_ERROR, "Max. call stack size reached" );
		return 0;
	}

	F = C->sf_last ? C->sf_last->cached : C->sf_cached;
	if( !F )
	{
		F = sgs_Alloc( sgs_StackFrame );
		F->cached = NULL;
		if( C->sf_last )
			C->sf_last->cached = F;
		else
			C->sf_cached = F;
	}
	C->sf_count++;
	F->func = func;
	F->code = code;
	F->iptr = code;
	F->lptr = code;
	F->iend = code + icnt;
	if( func && func->type == VTC_FUNC )
	{
		func_t* fn = func->data.F;
		F->lptr = F->iptr = F->code = func_bytecode( fn );
		F->iend = F->iptr + ( ( fn->size - fn->instr_off ) / sizeof( instr_t ) );
	}
	F->lntable = T;
	F->nfname = NULL;
	F->filename = C->filename;
	F->next = NULL;
	F->prev = C->sf_last;
	if( C->sf_last )
		C->sf_last->next = F;
	else
		C->sf_first = F;
	C->sf_last = F;

#ifdef SGS_JIT
	sgsJIT_CB_FI( C );
#endif
	if( C->hook_fn )
		C->hook_fn( C->hook_ctx, C, SGS_HOOK_ENTER );

	return 1;
}

static void vm_frame_pop( SGS_CTX )
{
	sgs_StackFrame* F = C->sf_last;

#ifdef SGS_JIT
	sgsJIT_CB_FO( C );
#endif
	if( C->hook_fn )
		C->hook_fn( C->hook_ctx, C, SGS_HOOK_EXIT );

	C->sf_count--;
	if( F->prev )
		F->prev->next = NULL;
	C->sf_last = F->prev;
	if( C->sf_first == F )
		C->sf_first = NULL;
}


/*
	Stack management
*/

#define USING_STACK

#define DBG_STACK_CHECK /* TODO fix */

static SGS_INLINE sgs_VarPtr stk_gettop( SGS_CTX )
{
#if SGS_DEBUG && SGS_DEBUG_VALIDATE && SGS_DEBUG_EXTRA
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
#if SGS_DEBUG && SGS_DEBUG_VALIDATE && SGS_DEBUG_EXTRA
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
	STKVAR_RELEASE( vpos );
	*vpos = *var;
}
#define stk_setvar_leave stk_setvar
static SGS_INLINE void stk_setvar_null( SGS_CTX, int stkid )
{
	sgs_VarPtr vpos = stk_getpos( C, stkid );
	STKVAR_RELEASE( vpos );
	vpos->type = VTC_NULL;
}

#define stk_getlpos( C, stkid ) (C->stack_off + stkid)
static SGS_INLINE void stk_setlvar( SGS_CTX, int stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	STKVAR_RELEASE( vpos );
	*vpos = *var;
}
#define stk_setlvar_leave stk_setlvar
static SGS_INLINE void stk_setlvar_null( SGS_CTX, int stkid )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	STKVAR_RELEASE( vpos );
	vpos->type = VTC_NULL;
}

static void stk_makespace( SGS_CTX, int num )
{
	int stkoff, stkend, nsz, stksz = C->stack_top - C->stack_base;
	if( stksz + num <= C->stack_mem )
		return;
	stkoff = C->stack_off - C->stack_base;
	stkend = C->stack_top - C->stack_base;
	DBG_STACK_CHECK
	nsz = ( stksz + num ) + C->stack_mem * 2; /* MAX( stksz + num, C->stack_mem * 2 ); */
	C->stack_base = (sgs_VarPtr) sgs_Realloc( C, C->stack_base, sizeof( sgs_Variable ) * nsz );
	C->stack_mem = nsz;
	C->stack_off = C->stack_base + stkoff;
	C->stack_top = C->stack_base + stkend;
}

static void stk_push( SGS_CTX, sgs_VarPtr var )
{
	stk_makespace( C, 1 );
	*C->stack_top++ = *var;
}

#define stk_push_leave stk_push

static void stk_push_null( SGS_CTX )
{
	stk_makespace( C, 1 );
	(C->stack_top++)->type = VTC_NULL;
}
static void stk_push_nulls( SGS_CTX, int cnt )
{
	sgs_VarPtr tgt;
	stk_makespace( C, cnt );
	tgt = C->stack_top + cnt;
	while( C->stack_top < tgt )
		(C->stack_top++)->type = VTC_NULL;
}

static sgs_Variable* stk_insert_pos( SGS_CTX, int off )
{
	sgs_Variable *op, *p;
	stk_makespace( C, 1 );
	op = C->stack_off + off, p = C->stack_top;
	while( p != op )
	{
		*p = *(p-1);
		p--;
	}
	C->stack_top++;
	return op;
}

static void stk_insert_null( SGS_CTX, int off )
{
	stk_insert_pos( C, off )->type = VTC_NULL;
}

static void stk_clean( SGS_CTX, sgs_VarPtr from, sgs_VarPtr to )
{
	int len = to - from;
	int oh = C->stack_top - to;
	sgs_VarPtr p = from, pend = to;
	sgs_BreakIf( C->stack_top - C->stack_base < len );
	sgs_BreakIf( to < from );
	DBG_STACK_CHECK

	while( p < pend )
	{
		STKVAR_RELEASE( p );
		p++;
	}

	C->stack_top -= len;

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
	STKVAR_RELEASE( C->stack_top );
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
	STKVAR_RELEASE( C->stack_top );
	STKVAR_RELEASE( C->stack_top + 1 );
}

static void stk_swapLF( SGS_CTX, int pos1, int pos2 )
{
	sgs_Variable* p1p = stk_getlpos( C, pos1 ), *p2p = stk_getpos( C, pos2 );
	sgs_Variable tmp = *p1p;
	*p1p = *p2p;
	*p2p = tmp;
}


/*
	The Closure Stack
*/

static void closure_deref( SGS_CTX, sgs_Closure* cl )
{
	if( --cl->refcount < 1 )
	{
		VAR_RELEASE( &cl->var );
		sgs_Dealloc( cl );
	}
}

#define CLSTK_UNITSIZE sizeof( sgs_Closure* )

static void clstk_makespace( SGS_CTX, int num )
{
	int stkoff, stkend, nsz, stksz = C->clstk_top - C->clstk_base;
	if( stksz + num <= C->clstk_mem )
		return;
	stkoff = C->clstk_off - C->clstk_base;
	stkend = C->clstk_top - C->clstk_base;
	
	nsz = ( stksz + num ) + C->clstk_mem * 2; /* MAX( stksz + num, C->clstk_mem * 2 ); */
	C->clstk_base = (sgs_Closure**) sgs_Realloc( C, C->clstk_base, CLSTK_UNITSIZE * nsz );
	C->clstk_mem = nsz;
	C->clstk_off = C->clstk_base + stkoff;
	C->clstk_top = C->clstk_base + stkend;
}

static void clstk_push( SGS_CTX, sgs_Closure* var )
{
	clstk_makespace( C, 1 );
	var->refcount++;
	*C->clstk_top++ = var;
}

static void clstk_push_nulls( SGS_CTX, int num )
{
	clstk_makespace( C, num );
	while( num )
	{
		sgs_Closure* cc = sgs_Alloc( sgs_Closure );
		cc->refcount = 1;
		cc->var.type = VTC_NULL;
		*C->clstk_top++ = cc;
		num--;
	}
}

static void clstk_clean( SGS_CTX, sgs_Closure** from, sgs_Closure** to )
{
	int len = to - from;
	int oh = C->clstk_top - to;
	sgs_Closure** p = from, **pend = to;
	sgs_BreakIf( C->clstk_top - C->clstk_base < len );
	sgs_BreakIf( to < from );

	while( p < pend )
	{
		closure_deref( C, *p );
		p++;
	}

	C->clstk_top -= len;

	if( oh )
		memmove( from, to, oh * CLSTK_UNITSIZE );
}

static void clstk_pop( SGS_CTX, int num )
{
	clstk_clean( C, C->clstk_top - num, C->clstk_top );
}

static sgs_Closure* clstk_get( SGS_CTX, int num )
{
	sgs_BreakIf( num < 0 || C->clstk_off + num >= C->clstk_top );
	return C->clstk_off[ num ];
}

void sgsVM_PushClosures( SGS_CTX, sgs_Closure** cls, int num )
{
	clstk_makespace( C, num );
	while( num-- )
	{
		(*cls)->refcount++;
		*C->clstk_top++ = *cls++;
	}
}


/*
	Conversions
*/


static int var_getbool( SGS_CTX, const sgs_VarPtr var )
{
	switch( BASETYPE( var->type ) )
	{
	case SVT_NULL: return FALSE;
	case SVT_BOOL: return var->data.B;
	case SVT_INT: return var->data.I != 0;
	case SVT_REAL: return var->data.R != 0;
	case SVT_STRING: return !!var->data.S->size;
	case SVT_FUNC: return TRUE;
	case SVT_CFUNC: return TRUE;
	case SVT_OBJECT:
		{
			int out = 1;
			int origsize = sgs_StackSize( C );
			stk_push( C, var );
			if( obj_exec( C, SOP_CONVERT, var->data.O, SVT_BOOL, 0 ) == SGS_SUCCESS )
				out = stk_getpos( C, -1 )->data.B;
			stk_pop( C, sgs_StackSize( C ) - origsize );
			return out;
		}
	default: return FALSE;
	}
}

static sgs_Int var_getint( SGS_CTX, sgs_VarPtr var )
{
	switch( BASETYPE( var->type ) )
	{
	case SVT_BOOL: return (sgs_Int) var->data.B;
	case SVT_INT: return var->data.I;
	case SVT_REAL: return (sgs_Int) var->data.R;
	case SVT_STRING: return util_atoi( str_cstr( var->data.S ), var->data.S->size );
	case SVT_OBJECT:
		{
			int origsize = sgs_StackSize( C );
			if( obj_exec( C, SOP_CONVERT, var->data.O, SVT_INT, 0 ) == SGS_SUCCESS )
			{
				sgs_Int out = stk_getpos( C, -1 )->data.I;
				stk_pop( C, sgs_StackSize( C ) - origsize );
				return out;
			}
			stk_pop( C, sgs_StackSize( C ) - origsize );
		}
	}
	return 0;
}

static sgs_Real var_getreal( SGS_CTX, sgs_Variable* var )
{
	switch( BASETYPE( var->type ) )
	{
	case SVT_BOOL: return (sgs_Real) var->data.B;
	case SVT_INT: return (sgs_Real) var->data.I;
	case SVT_REAL: return var->data.R;
	case SVT_STRING: return util_atof( str_cstr( var->data.S ), var->data.S->size );
	case SVT_OBJECT:
		{
			int origsize = sgs_StackSize( C );
			if( obj_exec( C, SOP_CONVERT, var->data.O, VTC_REAL, 0 ) == SGS_SUCCESS )
			{
				sgs_Real out = stk_getpos( C, -1 )->data.R;
				stk_pop( C, sgs_StackSize( C ) - origsize );
				return out;
			}
			stk_pop( C, sgs_StackSize( C ) - origsize );
		}
	}
	return 0;
}

static SGS_INLINE sgs_Int var_getint_simple( sgs_VarPtr var )
{
	switch( var->type )
	{
	case VTC_BOOL: return (sgs_Int) var->data.B;
	case VTC_INT: return var->data.I;
	case VTC_REAL: return (sgs_Int) var->data.R;
	case VTC_STRING: return util_atoi( str_cstr( var->data.S ), var->data.S->size );
	}
	return 0;
}

static SGS_INLINE sgs_Real var_getreal_simple( sgs_Variable* var )
{
	switch( var->type )
	{
	case VTC_BOOL: return (sgs_Real) var->data.B;
	case VTC_INT: return (sgs_Real) var->data.I;
	case VTC_REAL: return var->data.R;
	case VTC_STRING: return util_atof( str_cstr( var->data.S ), var->data.S->size );
	}
	return 0;
}

#define var_setnull( C, v ) \
{ sgs_VarPtr var = (v); STKVAR_RELEASE( var ); var->type = VTC_NULL; }
#define var_setbool( C, v, value ) \
{ sgs_VarPtr var = (v); if( var->type == VTC_BOOL ) var->data.B = value; \
	else { STKVAR_RELEASE( var ); var->type = VTC_BOOL; var->data.B = value; } }
#define var_setint( C, v, value ) \
{ sgs_VarPtr var = (v); if( var->type == VTC_INT ) var->data.I = value; \
	else { STKVAR_RELEASE( var ); var->type = VTC_INT; var->data.I = value; } }
#define var_setreal( C, v, value ) \
{ sgs_VarPtr var = (v); if( var->type == VTC_REAL ) var->data.R = value; \
	else { STKVAR_RELEASE( var ); var->type = VTC_REAL; var->data.R = value; } }


static int init_var_string( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	char buf[ 32 ];

	out->data.S = NULL;

	switch( var->type )
	{
	case VTC_NULL: var_create_str( C, out, "null", 4 ); break;
	case VTC_BOOL: if( var->data.B ) var_create_str( C, out, "true", 4 ); else var_create_str( C, out, "false", 5 ); break;
	case VTC_INT: sprintf( buf, "%" PRId64, var->data.I ); var_create_str( C, out, buf, -1 ); break;
	case VTC_REAL: sprintf( buf, "%g", var->data.R ); var_create_str( C, out, buf, -1 ); break;
	case VTC_FUNC: var_create_str( C, out, "Function", -1 ); break;
	case VTC_CFUNC: var_create_str( C, out, "C Function", -1 ); break;
	}
	return SGS_SUCCESS;
}

static int vm_convert( SGS_CTX, sgs_VarPtr var, int type, int stack )
{
	sgs_Variable cvar;
	int ret = SGS_ENOTSUP;
	
	sgs_BreakIf( type & 0xf0 ); /* does not convert to really complex types */

	if( var->type == type )
		return SGS_SUCCESS;
	else if( type == VTC_NULL )
	{
		if( stack ){ STKVAR_RELEASE( var ); }
		else { VAR_RELEASE( var ); }
		var->type = VTC_NULL;
		return SGS_SUCCESS;
	}

	if( var->type & SVT_OBJECT )
	{
		int origsize = sgs_StackSize( C );
		switch( type )
		{
		case VTC_BOOL:
		case VTC_INT:
		case VTC_REAL:
		case VTC_STRING: break;
		default:
			ret = SGS_ENOTSUP;
			goto ending;
		}

		stk_push( C, var );
		ret = obj_exec( C, SOP_CONVERT, var->data.O, BASETYPE( type ), 1 );
		if( ret == SGS_SUCCESS )
		{
			cvar = *stk_getpos( C, -1 );
			stk_pop1nr( C );
		}
		else if( type == VTC_STRING )
		{
			var_create_str( C, &cvar, "object", 6 );
			ret = SGS_SUCCESS;
		}
		else if( type == VTC_BOOL )
		{
			cvar.type = VTC_BOOL;
			cvar.data.B = 1;
		}
		stk_pop( C, sgs_StackSize( C ) - origsize );
		goto ending;
	}

	cvar.type = type;
	switch( type )
	{
	case VTC_BOOL: cvar.data.B = var_getbool( C, var ); ret = SGS_SUCCESS; break;
	case VTC_INT: cvar.data.I = var_getint( C, var ); ret = SGS_SUCCESS; break;
	case VTC_REAL: cvar.data.R = var_getreal( C, var ); ret = SGS_SUCCESS; break;
	case VTC_STRING: ret = init_var_string( C, &cvar, var ); break;
	default:
		goto ending;
	}

ending:
	if( stack ){ STKVAR_RELEASE( var ); }
	else { VAR_RELEASE( var ); }
	
	if( ret == SGS_SUCCESS )
		*var = cvar;
	else
		var->type = VTC_NULL;

	return ret;
}

static int vm_convert_stack( SGS_CTX, int item, int type )
{
	int ret;
	sgs_Variable var;
	item = stk_absindex( C, item );
	if( item < 0 || item >= C->stack_top - C->stack_off )
		return SGS_EINVAL;
	var = *stk_getpos( C, item );
	ret = vm_convert( C, &var, type, 1 );
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
		return SGS_EINPROC;

	A = stk_getpos( C, -1 );
	if( A->type & SVT_OBJECT )
	{
		int ret = obj_exec( C, SOP_CONVERT, A->data.O, SGS_CONVOP_TOTYPE, 0 );
		if( ret != SGS_SUCCESS )
		{
			char bfr[ 32 ];
			sprintf( bfr, "object (%p)", (void*) A->data.O->iface );
			sgs_PushString( C, bfr );
		}
	}
	else
	{
		const char* ty = "ERROR";
		switch( A->type )
		{
		case VTC_NULL:   ty = "null"; break;
		case VTC_BOOL:   ty = "bool"; break;
		case VTC_INT:    ty = "int"; break;
		case VTC_REAL:   ty = "real"; break;
		case VTC_STRING: ty = "string"; break;
		case VTC_CFUNC:  ty = "cfunc"; break;
		case VTC_FUNC:   ty = "func"; break;
		}
		sgs_PushString( C, ty );
	}

	stk_popskip( C, 1, 1 );
	return SGS_SUCCESS;
}

static int vm_gcmark( SGS_CTX, sgs_Variable* var )
{
	int ret, ssz = STACKFRAMESIZE;
	if( !( var->type & SVT_OBJECT ) || var->data.O->redblue == C->redblue )
		return SGS_SUCCESS;
	var->data.O->redblue = C->redblue;
	ret = obj_exec( C, SOP_GCMARK, var->data.O, 0, 0 );
	if( ret == SGS_ENOTFND )
		ret = SGS_SUCCESS; /* assume no owned objects */
	stk_pop( C, STACKFRAMESIZE - ssz );
	return ret;
}

/*
	Object property / array accessor handling
*/

int sgs_thiscall_method( SGS_CTX )
{
	int ret;
	if( !sgs_Method( C ) || !( sgs_ItemTypeExt( C, 0 ) & VTF_CALL ) )
	{
		sgs_Printf( C, SGS_WARNING, "thiscall() was not called on a function type" );
		return 0;
	}
	if( sgs_StackSize( C ) < 2 )
	{
		sgs_Printf( C, SGS_WARNING, "thiscall() expects at least one argument (this)" );
		return 0;
	}

	sgs_PushItem( C, 0 );
	ret = sgs_ThisCall( C, sgs_StackSize( C ) - 3, 1 );
	if( ret != SGS_SUCCESS )
	{
		sgs_Printf( C, SGS_WARNING, "thiscall() failed with error %d", ret );
		return 0;
	}
	return 1;
}


static int vm_getidx_builtin( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx )
{
	int res;
	sgs_Int pos, size;
	if( obj->type == VTC_STRING )
	{
		size = obj->data.S->size;
		sgs_PushVariable( C, idx );
		res = sgs_ParseInt( C, -1, &pos );
		sgs_Pop( C, 1 );
		if( !res )
		{
			sgs_Printf( C, SGS_WARNING, "Expected integer as string index" );
			return SGS_EINVAL;
		}
		if( pos >= size || pos < -size )
		{
			sgs_Printf( C, SGS_WARNING, "String index out of bounds" );
			return SGS_EBOUNDS;
		}
		pos = ( pos + size ) % size;
		sgs_PushStringBuf( C, var_cstr( obj ) + pos, 1 );
		return SGS_SUCCESS;
	}

	sgs_Printf( C, SGS_WARNING, "Cannot index variable of type '%s'",
		TYPENAME( obj->type ) );
	return SGS_ENOTFND;
}

static int vm_getprop_builtin( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx )
{
	const char* prop = var_cstr( idx );

	switch( obj->type )
	{
	case VTC_STRING:
		if( 0 == strcmp( prop, "length" ) )
		{
			sgs_PushInt( C, obj->data.S->size );
			return SGS_SUCCESS;
		}
		break;
	case VTC_FUNC:
	case VTC_CFUNC:
		if( 0 == strcmp( prop, "thiscall" ) )
		{
			sgs_PushCFunction( C, sgs_thiscall_method );
			return SGS_SUCCESS;
		}
		break;
	}

	sgs_Printf( C, SGS_WARNING, "Property '%s' not found on "
		"object of type '%s'", prop, TYPENAME( obj->type ) );
	return SGS_ENOTFND;
}


extern int sgsstd_array_getindex( SGS_CTX, sgs_VarObj* data, int prop );
extern int sgsstd_dict_getindex( SGS_CTX, sgs_VarObj* data, int prop );



static int vm_runerr_getprop( SGS_CTX, int type, int origsize, int16_t out, sgs_Variable* idx, int isindex )
{
	stk_setvar_null( C, out );
	
	if( type == SGS_ENOTFND )
	{
		char* p;
		const char* err = isindex ? "Cannot find value by index" : "Property not found";
		stk_push( C, idx );
		p = sgs_ToString( C, -1 );
		sgs_Printf( C, SGS_WARNING, "%s: \"%s\"", err, p );
	}
	else if( type == SGS_EBOUNDS )
	{
		sgs_Printf( C, SGS_WARNING, "Index out of bounds" );
	}
	else if( type == SGS_EINVAL )
	{
		sgs_Printf( C, SGS_WARNING, "Invalid value type used for %s",
			isindex ? "index read" : "property read" );
	}
	else
	{
		sgs_Printf( C, SGS_WARNING, "Unknown error on %s",
			isindex ? "index read" : "property read" );
	}
	
	stk_pop( C, STACKFRAMESIZE - origsize );
	return type;
}
#define VM_GETPROP_ERR( type ) vm_runerr_getprop( C, type, origsize, out, idx, isindex )

static int vm_runerr_setprop( SGS_CTX, int type, int origsize, sgs_Variable* idx, int isindex )
{
	if( type == SGS_ENOTFND )
	{
		char* p;
		const char* err = isindex ? "Cannot find value by index" : "Property not found";
		stk_push( C, idx );
		p = sgs_ToString( C, -1 );
		sgs_Printf( C, SGS_WARNING, "%s: \"%s\"", err, p );
	}
	else if( type == SGS_EBOUNDS )
	{
		sgs_Printf( C, SGS_WARNING, "Index out of bounds" );
	}
	else if( type == SGS_EINVAL )
	{
		sgs_Printf( C, SGS_WARNING, "Invalid value type used for %s",
			isindex ? "index write" : "property write" );
	}
	else
	{
		sgs_Printf( C, SGS_WARNING, "Unknown error on %s",
			isindex ? "index write" : "property write" );
	}
	
	stk_pop( C, STACKFRAMESIZE - origsize );
	return type;
}
#define VM_SETPROP_ERR( type ) vm_runerr_setprop( C, type, origsize, idx, isindex )



static int vm_getprop( SGS_CTX, int16_t out, sgs_Variable* obj, sgs_Variable* idx, int isindex )
{
	int ret, origsize = STACKFRAMESIZE;

	if( !isindex && idx->type != VTC_STRING && idx->type != VTC_INT )
	{
		return VM_GETPROP_ERR( SGS_EINVAL );
	}
	else if( obj->type == VTC_DICT )
	{
		VHTable* ht = (VHTable*) obj->data.O->data;
		if( idx->type == VTC_INT && !isindex )
		{
			int32_t off = (int32_t) idx->data.I;
			if( off < 0 || off >= vht_size( ht ) )
				return VM_GETPROP_ERR( SGS_EBOUNDS );
			else
			{
				stk_setlvar( C, out, &ht->vars[ off ].val );
				return SGS_SUCCESS;
			}
		}
		else if( idx->type == VTC_STRING )
		{
			VHTVar* var = vht_get( ht, idx );
			if( !var )
				return VM_GETPROP_ERR( SGS_ENOTFND );
			else
			{
				stk_setlvar( C, out, &var->val );
				return SGS_SUCCESS;
			}
		}
		else
		{
			stk_push( C, idx );
			if( !sgs_ToString( C, -1 ) )
				return VM_GETPROP_ERR( SGS_EINVAL );
			else
			{
				VHTVar* var = vht_get( ht, stk_getpos( C, -1 ) );
				if( !var )
					return VM_GETPROP_ERR( SGS_ENOTFND );
				else
				{
					stk_setlvar( C, out, &var->val );
					stk_pop1( C );
					return SGS_SUCCESS;
				}
			}
		}
	}
	else if( obj->type == VTC_ARRAY )
	{
		stk_push( C, idx );
		ret = sgsstd_array_getindex( C, obj->data.O, !isindex );
	}
	else if( obj->type == VTC_MAP )
	{
		VHTable* ht = (VHTable*) obj->data.O->data;
		VHTVar* var = vht_get( ht, idx );
		if( !var )
			return VM_GETPROP_ERR( SGS_ENOTFND );
		else
		{
			stk_setlvar( C, out, &var->val );
			return SGS_SUCCESS;
		}
	}
	else if( obj->type & SVT_OBJECT )
	{
		sgs_VarObj* o = obj->data.O;
		stk_push( C, idx );
		ret = obj_exec_specific( C, o->getindex, o, !isindex, 1 );
	}
	else
	{
		ret = isindex ? vm_getidx_builtin( C, obj, idx ) : vm_getprop_builtin( C, obj, idx );
		if( ret != SGS_SUCCESS )
		{
			/* lots of custom errors printed */
			stk_setlvar_null( C, out );
			stk_pop( C, STACKFRAMESIZE - origsize );
			return ret;
		}
	}

	if( ret != SGS_SUCCESS )
	{
		return VM_GETPROP_ERR( ret );
	}
	else
	{
		stk_setvar( C, out, stk_getpos( C, -1 ) );
	}
	stk_pop( C, STACKFRAMESIZE - origsize );
	return ret;
}

static int vm_setprop( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* src, int isindex )
{
	int ret, origsize = STACKFRAMESIZE;

	if( !isindex && idx->type != VTC_INT && idx->type != VTC_STRING )
	{
		ret = SGS_EINVAL;
	}
	else if( obj->type & SVT_OBJECT )
	{
		sgs_VarObj* o = obj->data.O;
		stk_makespace( C, 2 );
		C->stack_top[ 0 ] = *idx;
		C->stack_top[ 1 ] = *src;
		C->stack_top += 2;

		ret = obj_exec( C, SOP_SETINDEX, o, !isindex, 2 );
	}
	else
		ret = SGS_ENOTFND;
	
	if( ret != SGS_SUCCESS )
		return VM_SETPROP_ERR( ret );
	
	stk_pop( C, STACKFRAMESIZE - origsize );
	return ret;
}


/*
	OPs
*/

static int vm_clone( SGS_CTX, int16_t out, sgs_Variable* var )
{
	switch( var->type )
	{
	/*
		strings are supposed to be immutable
		(even though C functions can accidentally
		or otherwise modify them with relative ease)
	*/
	case VTC_OBJECT:
	case VTC_ARRAY:
	case VTC_DICT:
	case VTC_MAP:
		{
			int sz = STACKFRAMESIZE;
			int ret = obj_exec( C, SOP_CONVERT, var->data.O, SGS_CONVOP_CLONE, 0 );
			if( ret != SGS_SUCCESS )
			{
				stk_setlvar_null( C, out );
				stk_pop( C, STACKFRAMESIZE - sz );
				return ret;
			}
			else
			{
				sgs_Variable ns = *stk_getpos( C, -1 );
				stk_setlvar( C, out, &ns );
				stk_pop( C, STACKFRAMESIZE - sz );
			}
		}
		break;
	default:
		/* even though functions are immutable, they're also impossible to modify,
			thus there is little need for showing an error when trying to convert one,
			especially if it's a part of some object to be cloned */
		stk_setlvar( C, out, var );
		break;
	}
	return SGS_SUCCESS;
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
		if( vm_convert_stack( C, -i, VTC_STRING ) != SGS_SUCCESS )
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

static void vm_op_concat( SGS_CTX, int16_t out, sgs_Variable *A, sgs_Variable *B )
{
	int ssz = STACKFRAMESIZE;
	stk_push( C, A );
	stk_push( C, B );
	if( vm_op_concat_ex( C, 2 ) )
		stk_setlvar( C, out, stk_getpos( C, -1 ) );
	stk_pop( C, STACKFRAMESIZE - ssz );
}

static int vm_op_negate( SGS_CTX, sgs_VarPtr out, sgs_Variable *A )
{
	int i = BASETYPE( A->type );

	switch( i )
	{
	case SVT_NULL: var_setnull( C, out ); break;
	case SVT_BOOL: var_setint( C, out, -A->data.B ); break;
	case SVT_INT: var_setint( C, out, -A->data.I ); break;
	case SVT_REAL: var_setreal( C, out, -A->data.R ); break;
	case SVT_OBJECT:
		{
			int ofs = out - C->stack_off, ssz = STACKFRAMESIZE;
			stk_push( C, A );
			if( obj_exec( C, SOP_EXPR, A->data.O, SGS_EOP_NEGATE, 1 ) == SGS_SUCCESS )
			{
				out = C->stack_off + ofs;
				STKVAR_RELEASE( out );
				*out = *stk_getpos( C, -1 );
				stk_pop1nr( C );
				stk_pop( C, STACKFRAMESIZE - ssz );
				break;
			}
			stk_pop( C, STACKFRAMESIZE - ssz );
			sgs_Printf( C, SGS_ERROR, "Given object does not support negation." );
			var_setnull( C, C->stack_off + ofs );
		}
		return 0;
	default:
		sgs_Printf( C, SGS_ERROR, "Negating variable of type %s is not supported.", TYPENAME( i ) );
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
	sgs_Int val = ~var_getint( C, A );
	var_setint( C, C->stack_off + out, val );
}


#define VAR_MOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, sgs_VarPtr out, sgs_Variable *A ) { \
	switch( A->type ){ \
		case VTC_INT: var_setint( C, out, A->data.I op ); break; \
		case VTC_REAL: var_setreal( C, out, A->data.R op ); break; \
		default: sgs_Printf( C, SGS_ERROR, "Cannot " #pfx "rement non-numeric variables!" ); return; \
	} }

VAR_MOP( inc, +1 )
VAR_MOP( dec, -1 )


#define ARITH_OP_ADD	SGS_EOP_ADD
#define ARITH_OP_SUB	SGS_EOP_SUB
#define ARITH_OP_MUL	SGS_EOP_MUL
#define ARITH_OP_DIV	SGS_EOP_DIV
#define ARITH_OP_MOD	SGS_EOP_MOD
static void vm_arith_op( SGS_CTX, sgs_VarPtr out, sgs_VarPtr a, sgs_VarPtr b, uint8_t op )
{
	if( ( a->type | b->type ) == VTC_REAL )
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
	if( ( a->type | b->type ) == VTC_INT )
	{
		sgs_Int A = a->data.I, B = b->data.I;
		switch( op ){
			/*
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			*/
			case ARITH_OP_DIV: if( B == 0 ) goto div0err;
				var_setreal( C, out, ((sgs_Real) A) / ((sgs_Real) B) ); break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; var_setint( C, out, A % B ); break;
			default: var_setint( C, out, 0 ); break;
		}
	}

	if( ( a->type | b->type ) & VTC_OBJECT )
	{
		int origsize = sgs_StackSize( C );
		int ofs = out - C->stack_off;
		
		USING_STACK
		stk_makespace( C, 2 );
		*C->stack_top++ = *a;
		*C->stack_top++ = *b;

		if( ( BASETYPE( a->type ) == SVT_OBJECT && obj_exec( C, SOP_EXPR, a->data.O, op, 2 ) == SGS_SUCCESS ) ||
			( BASETYPE( b->type ) == SVT_OBJECT && obj_exec( C, SOP_EXPR, b->data.O, op, 2 ) == SGS_SUCCESS ) )
		{
			USING_STACK
			STKVAR_RELEASE( C->stack_off + ofs );
			C->stack_off[ ofs ] = *--C->stack_top;
			stk_pop( C, sgs_StackSize( C ) - origsize );
			return;
		}

		stk_pop( C, sgs_StackSize( C ) - origsize );
		goto fail;
	}

	if( (a->type | b->type) & VTF_CALL )
		goto fail;

	if( (a->type | b->type) & (SVT_REAL | SVT_STRING) )
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
		sgs_Int A = var_getint_simple( a ), B = var_getint_simple( b ), R;
		switch( op ){
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			case ARITH_OP_DIV: if( B == 0 ) goto div0err;
				var_setreal( C, out, ((sgs_Real) A) / ((sgs_Real) B) ); return;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = A % B; break;
			default: R = 0; break;
		}
		var_setint( C, out, R );
	}
	return;

div0err:
	STKVAR_RELEASE( out );
	out->type = VTC_NULL;
	sgs_Printf( C, SGS_ERROR, "Division by 0" );
	return;
fail:
	STKVAR_RELEASE( out );
	out->type = VTC_NULL;
	sgs_Printf( C, SGS_ERROR, "Specified arithmetic operation is not supported on the given set of arguments" );
}


#define VAR_IOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, int16_t out, sgs_Variable* a, sgs_Variable* b ) { \
	sgs_Int A = var_getint( C, a ); \
	sgs_Int B = var_getint( C, b ); \
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
	const uint32_t ta = a->type, tb = b->type;
	if( (ta|tb) == VTC_INT ) return (sgs_Real)( a->data.I - b->data.I );
	if( (ta|tb) == VTC_REAL ) return a->data.R - b->data.R;

	if( (ta|tb) & SVT_OBJECT )
	{
		int origsize = sgs_StackSize( C );
		USING_STACK
		stk_makespace( C, 2 );
		*C->stack_top++ = *a;
		*C->stack_top++ = *b;

		if( ( (ta & SVT_OBJECT) && obj_exec( C, SOP_EXPR, a->data.O, SGS_EOP_COMPARE, 2 ) == SGS_SUCCESS ) ||
			( (tb & SVT_OBJECT) && obj_exec( C, SOP_EXPR, b->data.O, SGS_EOP_COMPARE, 2 ) == SGS_SUCCESS ) )
		{
			USING_STACK
			sgs_Real out = var_getreal( C, --C->stack_top );
			STKVAR_RELEASE( C->stack_top );
			stk_pop( C, sgs_StackSize( C ) - origsize );
			return out;
		}

		stk_pop( C, sgs_StackSize( C ) - origsize );
		/* fallback: check for equality */
		if( ta == tb )
			return a->data.O - b->data.O;
		else
			return ta - tb;
	}
	if( (ta|tb) & SVT_BOOL )
		return var_getbool( C, a ) - var_getbool( C, b );
	if( (ta|tb) & VTF_CALL )
	{
		if( ta != tb )
			return ta - tb;
		if( ta == VTC_FUNC )
			return a->data.F - b->data.F;
		else
		{
			return a->data.C == b->data.C ? 0 :
				((size_t)a->data.C) < ((size_t)b->data.C) ? -1 : 1;
		}
	}
	if( (ta|tb) & SVT_STRING )
	{
		int out;
		sgs_Variable A = *a, B = *b;
		stk_push( C, &A );
		stk_push( C, &B );
		if( vm_convert_stack( C, -2, VTC_STRING ) != SGS_SUCCESS ) return -1;
		if( vm_convert_stack( C, -1, VTC_STRING ) != SGS_SUCCESS ) return 1;
		a = stk_getpos( C, -2 );
		b = stk_getpos( C, -1 );
		out = memcmp( var_cstr( a ), var_cstr( b ), MIN( a->data.S->size, b->data.S->size ) );
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


static int vm_forprep( SGS_CTX, int outiter, sgs_VarPtr obj )
{
	int ret, ssz = STACKFRAMESIZE;

	if( !( obj->type & SVT_OBJECT ) )
	{
		sgs_Printf( C, SGS_ERROR, "Variable of type '%s' "
			"doesn't have an iterator", TYPENAME( obj->type ) );
		stk_setvar_null( C, outiter );
		return SGS_ENOTSUP;
	}

	ret = obj_exec( C, SOP_CONVERT, obj->data.O, SGS_CONVOP_TOITER, 0 );
	if( ret != SGS_SUCCESS )
	{
		stk_pop( C, STACKFRAMESIZE - ssz );
		sgs_Printf( C, SGS_ERROR, "Object [%p] doesn't have an iterator", obj->data.O );
		stk_setvar_null( C, outiter );
		return SGS_ENOTFND;
	}
	stk_setvar( C, outiter, stk_getpos( C, -1 ) );
	stk_pop( C, STACKFRAMESIZE - ssz );
	return SGS_SUCCESS;
}

static int vm_fornext( SGS_CTX, int outkey, int outval, sgs_VarPtr iter )
{
	int ssz = STACKFRAMESIZE;
	int flags = 0, expargs = 0, out;
	if( outkey >= 0 ){ flags |= SGS_GETNEXT_KEY; expargs++; }
	if( outval >= 0 ){ flags |= SGS_GETNEXT_VALUE; expargs++; }
	
	if( iter->type == SGS_VTC_ARRAY_ITER )
	{
		sgsstd_array_iter_t* it = (sgsstd_array_iter_t*) iter->data.O->data;
		sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) it->ref.data.O->data;
		if( it->size != hdr->size )
			return SGS_EINVAL;
		else if( !flags )
		{
			it->off++;
			return it->off < it->size;
		}
		else
		{
			if( outkey >= 0 )
				var_setint( C, stk_getlpos( C, outkey ), it->off );
			if( outval >= 0 )
				stk_setlvar( C, outval, hdr->data + it->off );
			return SGS_SUCCESS;
		}
	}
	if( !( iter->type & SVT_OBJECT ) ||
		( out = obj_exec_specific( C, iter->data.O->getnext, iter->data.O, flags, 0 ) ) < 0 )
	{
		sgs_Printf( C, SGS_ERROR, "Failed to retrieve data from iterator" );
		out = SGS_EINPROC;
	}
	else
	if( flags )
	{
		if( outkey >= 0 ) stk_swapLF( C, outkey, -2 + (outval<0) );
		if( outval >= 0 ) stk_swapLF( C, outval, -1 );
	}

	stk_pop( C, STACKFRAMESIZE - ssz );
	return out;
}


static void vm_make_array( SGS_CTX, int args, int16_t outpos )
{
	int ret;
	sgs_BreakIf( sgs_StackSize( C ) < args );
	ret = sgsSTD_MakeArray( C, args );
	sgs_BreakIf( ret != SGS_SUCCESS );
	UNUSED( ret );
	
	stk_getpos( C, -1 )->type |= VTF_ARRAY;
	stk_setvar( C, outpos, stk_getpos( C, -1 ) );
	stk_pop1( C );
}

static void vm_make_dict( SGS_CTX, int args, int16_t outpos )
{
	int ret;
	sgs_BreakIf( sgs_StackSize( C ) < args );
	ret = sgsSTD_MakeDict( C, args );
	sgs_BreakIf( ret != SGS_SUCCESS );
	UNUSED( ret );
	
	stk_getpos( C, -1 )->type |= VTF_DICT;
	stk_setvar( C, outpos, stk_getpos( C, -1 ) );
	stk_pop1( C );
}

static void vm_make_closure( SGS_CTX, int args, sgs_Variable* func, int16_t outpos )
{
	int ret;
	sgs_BreakIf( C->clstk_top - C->clstk_off < args );
	ret = sgsSTD_MakeClosure( C, func, args );
	sgs_BreakIf( ret != SGS_SUCCESS );
	UNUSED( ret );
	
	stk_getpos( C, -1 )->type |= VTF_CALL;
	stk_setvar( C, outpos, stk_getpos( C, -1 ) );
	stk_pop1( C );
}


static int vm_exec( SGS_CTX, sgs_Variable* consts, int32_t constcount );


/*
	Call the virtual machine.
	Args must equal the number of arguments pushed before the function
*/
static int vm_call( SGS_CTX, int args, int clsr, int gotthis, int expect, sgs_Variable* func )
{
	sgs_Variable V = *func;
	int stkoff = C->stack_off - C->stack_base, clsoff = C->clstk_off - C->clstk_base;
	int rvc = 0, ret = 1, allowed;

	sgs_BreakIf( sgs_StackSize( C ) < args + gotthis );
	allowed = vm_frame_push( C, &V, NULL, NULL, 0 );
	C->stack_off = C->stack_top - args;
	C->clstk_off = C->clstk_top - clsr;

	if( allowed )
	{
		if( func->type == VTC_CFUNC )
		{
			int stkoff2 = C->stack_off - C->stack_base;
			int hadthis = C->call_this;
			C->call_this = gotthis;
			rvc = (*func->data.C)( C );
			if( rvc > STACKFRAMESIZE )
			{
				sgs_Printf( C, SGS_ERROR, "Function returned more variables than there was on the stack" );
				rvc = 0;
				ret = 0;
			}
			if( rvc < 0 )
			{
				sgs_Printf( C, SGS_ERROR, "The function could not be called" );
				rvc = 0;
				ret = 0;
			}
			C->call_this = hadthis;
			C->stack_off = C->stack_base + stkoff2;
		}
		else if( func->type == VTC_FUNC )
		{
			func_t* F = func->data.F;
			int stkargs = args + ( F->gotthis && gotthis );
			int expargs = F->numargs + F->gotthis;
			/* fix argument stack */
			if( stkargs > expargs )
				stk_pop( C, stkargs - expargs );
			else
				stk_push_nulls( C, expargs - stkargs );
			/* if <this> was expected but wasn't properly passed, insert a NULL in its place */
			/* if <this> wasn't expected but was passed, ignore it */
			if( F->gotthis && !gotthis )
			{
				stk_insert_null( C, 0 );
				C->stack_off++;
				gotthis = TRUE;
			}
			
			stk_push_nulls( C, F->numtmp );

			if( F->gotthis && gotthis ) C->stack_off--;
			{
				int constcnt = F->instr_off / sizeof( sgs_Variable* );
				rvc = vm_exec( C, func_consts( F ), constcnt );
			}
			if( F->gotthis && gotthis ) C->stack_off++;
		}
		else if( ( func->type & VTC_OBJECT ) == VTC_OBJECT )
		{
			int cargs = C->call_args, cexp = C->call_expect;
			int hadthis = C->call_this;
			C->call_args = args;
			C->call_expect = expect;
			C->call_this = gotthis;
			rvc = obj_exec( C, SOP_CALL, func->data.O, 0, args );
			C->stack_off -= gotthis;
			if( rvc > STACKFRAMESIZE )
			{
				sgs_Printf( C, SGS_ERROR, "Object returned more variables than there was on the stack" );
				rvc = 0;
				ret = 0;
			}
			if( rvc < 0 )
			{
				sgs_Printf( C, SGS_ERROR, "The object could not be called" );
				rvc = 0;
				ret = 0;
			}
			C->stack_off += gotthis;
			C->call_args = cargs;
			C->call_expect = cexp;
			C->call_this = hadthis;
		}
		else
		{
			sgs_Printf( C, SGS_ERROR, "Variable of type '%s' "
				"cannot be called", TYPENAME( func->type ) );
			ret = 0;
		}
	}

	/* subtract gotthis from offset if pushed extra variable */
	stk_clean( C, C->stack_off - gotthis, C->stack_top - rvc );
	C->stack_off = C->stack_base + stkoff;
	
	clstk_clean( C, C->clstk_off, C->clstk_top );
	C->clstk_off = C->clstk_base + clsoff;

	if( allowed )
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
static int vm_exec( SGS_CTX, sgs_Variable* consts, int32_t constcount )
{
	sgs_StackFrame* SF = C->sf_last;
	int32_t ret = 0;
	sgs_Variable* cptr = consts;
	const instr_t* pend = SF->iend;

#if SGS_DEBUG && SGS_DEBUG_VALIDATE
	int stkoff = C->stack_top - C->stack_off;
#  define RESVAR( v ) ( CONSTVAR(v) ? const_getvar( cptr, constcount, CONSTDEC(v) ) : stk_getlpos( C, (v) ) )
#else
#  define RESVAR( v ) ( CONSTVAR(v) ? ( cptr + CONSTDEC(v) ) : stk_getlpos( C, (v) ) )
#endif
	UNUSED( constcount );

#if SGS_DEBUG && SGS_DEBUG_INSTR
	{
		const char *name, *file;
		sgs_StackFrameInfo( C, SF, &name, &file, NULL );
		printf( ">>>\n\t'%s' in %s\n>>>\n", name, file );
	}
#endif

	while( SF->iptr < pend )
	{
		const instr_t I = *SF->iptr;
#define pp SF->iptr
#define instr INSTR_GET_OP(I)
#define argA INSTR_GET_A(I)
#define argB INSTR_GET_B(I)
#define argC INSTR_GET_C(I)
#define argE INSTR_GET_E(I)
		const sgs_VarPtr p1 = C->stack_off + argA;

#if SGS_DEBUG
#  if SGS_DEBUG_INSTR
		printf( "*** [at 0x%04X] %s ***\n", pp - SF->code, sgs_OpNames[ instr ] );
#  endif
#  if SGS_DEBUG_STATE
		sgsVM_StackDump( C );
#  endif
#endif
		UNUSED( sgs_ErrNames );
		UNUSED( sgs_OpNames );
		UNUSED( sgs_IfaceNames );

		if( C->hook_fn )
			C->hook_fn( C->hook_ctx, C, SGS_HOOK_STEP );

		switch( instr )
		{
		case SI_NOP: break;

		case SI_PUSH:
		{
			sgs_Variable var = *RESVAR( argB );
			stk_push( C, &var );
			break;
		}
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
			sgs_BreakIf( pp+1 > pend || pp+1 < SF->code );
			break;
		}

		case SI_JMPT:
		{
			int16_t off = argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}
		case SI_JMPF:
		{
			int16_t off = argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( !var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}

		case SI_CALL:
		{
			int expect = argA, args = argB, src = argC;
			int gotthis = ( argB & 0x100 ) != 0;
			args &= 0xff;
			vm_call( C, args, 0, gotthis, expect, RESVAR( src ) );
			break;
		}

		case SI_FORPREP: vm_forprep( C, argA, RESVAR( argB ) ); break;
		case SI_FORLOAD: vm_fornext( C, argB < 0x100 ? argB : -1, argC < 0x100 ? argC : -1, RESVAR( argA ) ); break;
		case SI_FORJUMP:
		{
			int16_t off = argE;
			sgs_BreakIf( pp+1 > pend || pp+1 < SF->code );
			if( vm_fornext( C, -1, -1, RESVAR( argC ) ) < 1 )
				pp += off;
			break;
		}

#define a1 argA
#define ARGS_2 const sgs_VarPtr p2 = RESVAR( argB );
#define ARGS_3 const sgs_VarPtr p2 = RESVAR( argB ), p3 = RESVAR( argC );
		case SI_LOADCONST: { stk_setlvar( C, argC, cptr + argE ); break; }
		case SI_GETVAR: { ARGS_2; sgsSTD_GlobalGet( C, p1, p2, FALSE ); break; }
		case SI_SETVAR: { ARGS_3; sgsSTD_GlobalSet( C, p2, p3, FALSE ); break; }
		case SI_GETPROP: { ARGS_3; vm_getprop( C, a1, p2, p3, FALSE ); break; }
		case SI_SETPROP: { ARGS_3; vm_setprop( C, p1, p2, p3, FALSE ); break; }
		case SI_GETINDEX: { ARGS_3; vm_getprop( C, a1, p2, p3, TRUE ); break; }
		case SI_SETINDEX: { ARGS_3; vm_setprop( C, p1, p2, p3, TRUE ); break; }
		
		case SI_GENCLSR: clstk_push_nulls( C, argA ); break;
		case SI_PUSHCLSR: clstk_push( C, clstk_get( C, argA ) ); break;
		case SI_MAKECLSR: vm_make_closure( C, argC, RESVAR( argB ), argA ); clstk_pop( C, argC ); break;
		case SI_GETCLSR: stk_setlvar( C, argA, &clstk_get( C, argB )->var ); break;
		case SI_SETCLSR: { sgs_VarPtr p3 = RESVAR( argC ), cv = &clstk_get( C, argB )->var;
			VAR_RELEASE( cv ); *cv = *p3; VAR_ACQUIRE( RESVAR( argC ) ); } break;

		case SI_SET: { ARGS_2; STKVAR_RELEASE( p1 ); *p1 = *p2; break; }
		case SI_CLONE: { ARGS_2; vm_clone( C, a1, p2 ); break; }
		case SI_CONCAT: { ARGS_3; vm_op_concat( C, a1, p2, p3 ); break; }
		case SI_NEGATE: { ARGS_2; vm_op_negate( C, p1, p2 ); break; }
		case SI_BOOL_INV: { ARGS_2; vm_op_boolinv( C, a1, p2 ); break; }
		case SI_INVERT: { ARGS_2; vm_op_invert( C, a1, p2 ); break; }

		case SI_INC: { ARGS_2; vm_op_inc( C, p1, p2 ); break; }
		case SI_DEC: { ARGS_2; vm_op_dec( C, p1, p2 ); break; }
		case SI_ADD: { ARGS_3;
			if( p2->type == VTC_REAL && p3->type == VTC_REAL ){ var_setreal( C, p1, p2->data.R + p3->data.R ); break; }
			if( p2->type == VTC_INT && p3->type == VTC_INT ){ var_setint( C, p1, p2->data.I + p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_ADD ); break; }
		case SI_SUB: { ARGS_3;
			if( p2->type == VTC_REAL && p3->type == VTC_REAL ){ var_setreal( C, p1, p2->data.R - p3->data.R ); break; }
			if( p2->type == VTC_INT && p3->type == VTC_INT ){ var_setint( C, p1, p2->data.I - p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_SUB ); break; }
		case SI_MUL: { ARGS_3;
			if( p2->type == VTC_REAL && p3->type == VTC_REAL ){ var_setreal( C, p1, p2->data.R * p3->data.R ); break; }
			if( p2->type == VTC_INT && p3->type == VTC_INT ){ var_setint( C, p1, p2->data.I * p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_MUL ); break; }
		case SI_DIV: { ARGS_3; vm_arith_op( C, p1, p2, p3, ARITH_OP_DIV ); break; }
		case SI_MOD: { ARGS_3; vm_arith_op( C, p1, p2, p3, ARITH_OP_MOD ); break; }

		case SI_AND: { ARGS_3; vm_op_and( C, a1, p2, p3 ); break; }
		case SI_OR: { ARGS_3; vm_op_or( C, a1, p2, p3 ); break; }
		case SI_XOR: { ARGS_3; vm_op_xor( C, a1, p2, p3 ); break; }
		case SI_LSH: { ARGS_3; vm_op_lsh( C, a1, p2, p3 ); break; }
		case SI_RSH: { ARGS_3; vm_op_rsh( C, a1, p2, p3 ); break; }

#define STRICTLY_EQUAL( val ) if( p2->type != p3->type || ( p2->type == VTC_OBJECT && \
								p2->data.O->iface != p3->data.O->iface ) ) { var_setbool( C, p1, val ); break; }
#define VCOMPARE( op ) { int cr = vm_compare( C, p2, p3 ) op 0; var_setbool( C, C->stack_off + a1, cr ); }
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
#undef pp

		default:
			sgs_Printf( C, SGS_ERROR, "Illegal instruction executed: 0x%08X", I );
			break;
		}
		
#ifdef SGS_JIT
		sgsJIT_CB_NI( C );
#endif
		
		SF->lptr = ++SF->iptr;
	}

#if SGS_DEBUG && SGS_DEBUG_STATE
	/* TODO restore memcheck */
	sgsVM_StackDump( C );
#endif
	return ret;
}


/* INTERNAL INERFACE */

static int funct_size( func_t* f )
{
	int sz = f->size + f->funcname.mem + f->filename.mem;
	sgs_Variable* beg = (sgs_Variable*) func_consts( f );
	sgs_Variable* end = (sgs_Variable*) func_bytecode( f );
	while( beg < end )
		sz += sgsVM_VarSize( beg++ );
	return sz;
}

int sgsVM_VarSize( sgs_Variable* var )
{
	int out;
	if( !var )
		return 0;

	out = sizeof( sgs_Variable );
	switch( var->type )
	{
	case VTC_FUNC: out += funct_size( var->data.F ); break;
	/* case VTC_OBJECT: break; */
	case VTC_STRING: out += var->data.S->size + sizeof( string_t ); break;
	}
	return out;
}

void sgsVM_VarDump( sgs_VarPtr var )
{
	printf( "%s (size:%d)", TYPENAME( var->type ), sgsVM_VarSize( var ) );
	switch( BASETYPE( var->type ) )
	{
	case SVT_NULL: break;
	case SVT_BOOL: printf( " = %s", var->data.B ? "True" : "False" ); break;
	case SVT_INT: printf( " = %" PRId64, var->data.I ); break;
	case SVT_REAL: printf( " = %f", var->data.R ); break;
	case SVT_STRING: printf( " [rc:%d] = \"", var->data.S->refcount );
		print_safe( stdout, var_cstr( var ), MIN( var->data.S->size, 16 ) );
		printf( var->data.S->size > 16 ? "...\"" : "\"" ); break;
	case SVT_FUNC: printf( " [rc:%d]", var->data.F->refcount ); break;
	case SVT_CFUNC: printf( " = %p", (void*)(size_t) var->data.C ); break;
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
		if( var == C->stack_off )
			printf( "-- offset --\n" );
		printf( "  " ); sgsVM_VarDump( var ); printf( "\n" );
	}
	printf( "--\n" );
}

int sgsVM_ExecFn( SGS_CTX, int numtmp, void* code, int32_t codesize, void* data, int32_t datasize, int clean, uint16_t* T )
{
	int stkoff = C->stack_off - C->stack_base, rvc = 0, allowed;
	allowed = vm_frame_push( C, NULL, T, (instr_t*) code, codesize / sizeof( instr_t ) );
	stk_push_nulls( C, numtmp );
	if( allowed )
		rvc = vm_exec( C, (sgs_Variable*) data, datasize / sizeof( sgs_Variable* ) );
	C->stack_off = C->stack_base + stkoff;
	if( clean )
		stk_pop( C, C->stack_top - C->stack_off );
	else
	{
		/* keep only returned values */
		stk_clean( C, C->stack_off, C->stack_top - rvc );
	}
	if( allowed )
		vm_frame_pop( C );
	return rvc;
}

int sgsVM_VarCall( SGS_CTX, sgs_Variable* var, int args, int clsr, int expect, int gotthis )
{
	return vm_call( C, args, clsr, gotthis, expect, var );
}



/* ---- The core interface ---- */

/*

	STACK & SUB-ITEMS

*/

void sgs_PushNull( SGS_CTX )
{
	stk_push_null( C );
}

void sgs_PushBool( SGS_CTX, sgs_Bool value )
{
	sgs_Variable var;
	var.type = VTC_BOOL;
	var.data.B = value ? 1 : 0;
	stk_push_leave( C, &var );
}

void sgs_PushInt( SGS_CTX, sgs_Int value )
{
	sgs_Variable var;
	var.type = VTC_INT;
	var.data.I = value;
	stk_push_leave( C, &var );
}

void sgs_PushReal( SGS_CTX, sgs_Real value )
{
	sgs_Variable var;
	var.type = VTC_REAL;
	var.data.R = value;
	stk_push_leave( C, &var );
}

void sgs_PushStringBuf( SGS_CTX, const char* str, sgs_SizeVal size )
{
	sgs_Variable var;
	if( str )
		var_create_str( C, &var, str, size );
	else
		var_create_0str( C, &var, size );
	stk_push_leave( C, &var );
}

void sgs_PushString( SGS_CTX, const char* str )
{
	sgs_Variable var;
	if( !str )
		sgs_BreakIf( "sgs_PushString: str = NULL" );
	var_create_str( C, &var, str, -1 );
	stk_push_leave( C, &var );
}

void sgs_PushCFunction( SGS_CTX, sgs_CFunc func )
{
	sgs_Variable var;
	var.type = VTC_CFUNC;
	var.data.C = func;
	stk_push_leave( C, &var );
}

void sgs_PushObject( SGS_CTX, void* data, sgs_ObjCallback* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, data, iface, 0 );
	stk_push_leave( C, &var );
}

void* sgs_PushObjectIPA( SGS_CTX, sgs_SizeVal added, sgs_ObjCallback* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, NULL, iface, added );
	stk_push_leave( C, &var );
	return var.data.O->data;
}

void sgs_PushVariable( SGS_CTX, sgs_Variable* var )
{
	stk_push( C, var );
}


SGSRESULT sgs_InsertVariable( SGS_CTX, int pos, sgs_Variable* var )
{
	sgs_Variable* vp;
	if( pos > sgs_StackSize( C ) || pos < -sgs_StackSize( C ) - 1 )
		return SGS_EBOUNDS;
	if( pos < 0 )
		pos = sgs_StackSize( C ) + pos + 1;
	vp = stk_insert_pos( C, pos );
	*vp = *var;
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushArray( SGS_CTX, sgs_SizeVal numitems )
{
	int ret = sgsSTD_MakeArray( C, numitems );
	if( ret == SGS_SUCCESS )
		(C->stack_top-1)->type |= VTF_ARRAY;
	return ret;
}

SGSRESULT sgs_PushDict( SGS_CTX, sgs_SizeVal numitems )
{
	int ret = sgsSTD_MakeDict( C, numitems );
	if( ret == SGS_SUCCESS )
		(C->stack_top-1)->type |= VTF_DICT;
	return ret;
}

SGSRESULT sgs_PushMap( SGS_CTX, sgs_SizeVal numitems )
{
	int ret = sgsSTD_MakeMap( C, numitems );
	if( ret == SGS_SUCCESS )
		(C->stack_top-1)->type |= VTF_MAP;
	return ret;
}


SGSRESULT sgs_PushItem( SGS_CTX, int item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_EBOUNDS;

	{
		sgs_Variable copy = *stk_getpos( C, item );
		stk_push( C, &copy );
		return SGS_SUCCESS;
	}
}

SGSRESULT sgs_StoreItem( SGS_CTX, int item )
{
	int g, ret;
	C->stack_top--;
	ret = sgs_IsValidIndex( C, item );
	g = stk_absindex( C, item );
	C->stack_top++;
	if( !ret )
		return SGS_EBOUNDS;

	stk_setlvar( C, g, stk_getpos( C, -1 ) );
	stk_pop1( C );
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushProperty( SGS_CTX, const char* name )
{
	int ret;
	if( STACKFRAMESIZE < 1 )
		return SGS_EINPROC;
	sgs_PushString( C, name );
	ret = vm_getprop( C, stk_absindex( C, -1 ), stk_getpos( C, -2 ), stk_getpos( C, -1 ), FALSE );
	stk_popskip( C, 1, 1 );
	return ret;
}

SGSRESULT sgs_StoreProperty( SGS_CTX, int obj, const char* name )
{
	int ret;
	if( !sgs_IsValidIndex( C, obj ) )
		return SGS_EBOUNDS;
	obj = stk_absindex( C, obj );
	sgs_PushString( C, name );
	ret = vm_setprop( C, stk_getpos( C, obj ), stk_getpos( C, -1 ), stk_getpos( C, -2 ), FALSE );
	sgs_Pop( C, ret == SGS_SUCCESS ? 2 : 1 );
	return ret;
}

SGS_APIFUNC SGSRESULT sgs_PushNumIndex( SGS_CTX, int obj, sgs_Int idx )
{
	sgs_Variable var, ivar;
	ivar.type = VTC_INT;
	ivar.data.I = idx;
	if( !sgs_GetStackItem( C, obj, &var ) )
		return SGS_EBOUNDS;
	return sgs_PushIndexP( C, &var, &ivar );
}

SGS_APIFUNC SGSRESULT sgs_StoreNumIndex( SGS_CTX, int obj, sgs_Int idx )
{
	sgs_Variable var, ivar;
	ivar.type = VTC_INT;
	ivar.data.I = idx;
	if( !sgs_GetStackItem( C, obj, &var ) )
		return SGS_EBOUNDS;
	return sgs_StoreIndexP( C, &var, &ivar );
}

SGSRESULT sgs_PushIndexExt( SGS_CTX, int obj, int idx, int prop )
{
	int32_t oel = C->minlev;
	int ret;
	sgs_Variable vo, vi;
	if( !sgs_GetStackItem( C, obj, &vo ) ||
		!sgs_GetStackItem( C, idx, &vi ) )
		return SGS_EBOUNDS;
	
	C->minlev = INT32_MAX;

	stk_push_null( C );
	ret = vm_getprop( C, stk_absindex( C, -1 ), &vo, &vi, !prop );
	if( ret != SGS_SUCCESS )
		stk_pop1( C );
	
	C->minlev = oel;

	return ret;
}

SGSRESULT sgs_PushIndexP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx )
{
	int32_t oel = C->minlev;
	int ret;
	sgs_Variable Sobj = *obj, Sidx = *idx;
	
	C->minlev = INT32_MAX;

	stk_push_null( C );
	ret = vm_getprop( C, stk_absindex( C, -1 ), &Sobj, &Sidx, TRUE );
	if( ret != SGS_SUCCESS )
		stk_pop1( C );
	
	C->minlev = oel;

	return ret;
}

SGSRESULT sgs_StoreIndexExt( SGS_CTX, int obj, int idx, int prop )
{
	int ret;
	sgs_Variable vo, vi, val;
	if( !sgs_GetStackItem( C, obj, &vo ) ||
		!sgs_GetStackItem( C, idx, &vi ) ||
		!sgs_GetStackItem( C, -1, &val ) )
		return SGS_EBOUNDS;
	ret = vm_setprop( C, &vo, &vi, &val, !prop );
	if( ret == SGS_SUCCESS )
		sgs_Pop( C, 1 );
	return ret;
}

SGSRESULT sgs_StoreIndexP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx )
{
	int ret;
	sgs_Variable val;
	if( !sgs_GetStackItem( C, -1, &val ) )
		return SGS_EINPROC;
	ret = vm_setprop( C, obj, idx, &val, TRUE );
	if( ret == SGS_SUCCESS )
		sgs_Pop( C, 1 );
	return ret;
}

SGSRESULT sgs_PushGlobal( SGS_CTX, const char* name )
{
	int ret;
	sgs_Variable str;
	sgs_VarPtr pos;
	sgs_PushString( C, name );
	pos = stk_getpos( C, -1 );
	str = *pos;
	sgs_Acquire( C, &str );
	ret = sgsSTD_GlobalGet( C, pos, pos, 1 );
	sgs_Release( C, &str );
	if( ret != SGS_SUCCESS )
		sgs_Pop( C, 1 );
	return ret;
}

SGSRESULT sgs_StoreGlobal( SGS_CTX, const char* name )
{
	int ret;
	if( sgs_StackSize( C ) < 1 )
		return SGS_EINPROC;
	sgs_PushString( C, name );
	ret = sgsSTD_GlobalSet( C, stk_getpos( C, -1 ), stk_getpos( C, -2 ), 1 );
	sgs_Pop( C, ret == SGS_SUCCESS ? 2 : 1 );
	return ret;
}


/*
	o = offset (isprop=true) (sizeval)
	p = property (isprop=true) (null-terminated string)
	s = super-secret property (isprop=true) (sizeval, buffer)
	i = index (isprop=false) (sizeval)
	k = key (isprop=false) (null-terminated string)
	n = null-including key (isprop=false) (sizeval, buffer)
*/

static SGSRESULT sgs_PushPathBuf( SGS_CTX, int item, const char* path, int plen, va_list* pargs )
{
#define args *pargs
	int ret = sgs_PushItem( C, item ), i = 0;
	if( ret != SGS_SUCCESS )
		return ret;
	while( path[i] && ( plen < 0 || i < plen ) )
	{
		sgs_SizeVal S = -1;
		char* P = NULL;
		int prop = -1;
		char a = path[ i++ ];
		
		if( a == 'o' ){ prop = 1; S = va_arg( args, sgs_SizeVal ); }
		else if( a == 'p' ){ prop = 1; P = va_arg( args, char* );
			if( !P ) return SGS_EINVAL; }
		else if( a == 's' ){ prop = 1; S = va_arg( args, sgs_SizeVal );
			P = va_arg( args, char* ); if( !P ) return SGS_EINVAL; }
		else if( a == 'i' ){ prop = 0; S = va_arg( args, sgs_SizeVal ); }
		else if( a == 'k' ){ prop = 0; P = va_arg( args, char* );
			if( !P ) return SGS_EINVAL; }
		else if( a == 'n' ){ prop = 0; S = va_arg( args, sgs_SizeVal );
			P = va_arg( args, char* ); if( !P ) return SGS_EINVAL; }
		else
			return SGS_EINVAL;
		
		if( P )
		{
			if( S >= 0 )
				sgs_PushStringBuf( C, P, S );
			else
				sgs_PushString( C, P );
		}
		else if( S >= 0 )
			sgs_PushInt( C, S );
		else
			return SGS_EINPROC;
		
		ret = sgs_PushIndexExt( C, -2, -1, prop );
		if( ret != SGS_SUCCESS )
			return ret;
	}
#undef args
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushPath( SGS_CTX, int item, const char* path, ... )
{
	int ret, ssz = sgs_StackSize( C );
	va_list args;
	va_start( args, path );
	item = stk_absindex( C, item );
	ret = sgs_PushPathBuf( C, item, path, -1, &args );
	if( ret == SGS_SUCCESS )
		sgs_PopSkip( C, sgs_StackSize( C ) - ssz - 1, 1 );
	else
		sgs_Pop( C, sgs_StackSize( C ) - ssz );
	va_end( args );
	return ret;
}

SGSRESULT sgs_StorePath( SGS_CTX, int item, const char* path, ... )
{
	int ret, len = strlen( path ), val, ssz = sgs_StackSize( C );
	va_list args;
	if( ssz < 1 )
		return SGS_EINPROC;
	if( !*path )
		return sgs_StoreItem( C, item );
	va_start( args, path );
	item = stk_absindex( C, item );
	val = stk_absindex( C, -1 );
	ret = sgs_PushPathBuf( C, item, path, len - 1, &args );
	if( ret == SGS_SUCCESS )
	{
		sgs_SizeVal S = -1;
		char* P = NULL;
		int prop = -1;
		char a = path[ len - 1 ];
		ret = SGS_EINVAL;
		if( a == 'o' ){ prop = 1; S = va_arg( args, sgs_SizeVal ); }
		else if( a == 'p' ){ prop = 1; P = va_arg( args, char* );
			if( !P ) goto fail; }
		else if( a == 's' ){ prop = 1; S = va_arg( args, sgs_SizeVal );
			P = va_arg( args, char* ); if( !P ) goto fail; }
		else if( a == 'i' ){ prop = 0; S = va_arg( args, sgs_SizeVal ); }
		else if( a == 'k' ){ prop = 0; P = va_arg( args, char* );
			if( !P ) goto fail; }
		else if( a == 'n' ){ prop = 0; S = va_arg( args, sgs_SizeVal );
			P = va_arg( args, char* ); if( !P ) goto fail; }
		else
			goto fail;
		
		if( P )
		{
			if( S >= 0 )
				sgs_PushStringBuf( C, P, S );
			else
				sgs_PushString( C, P );
		}
		else if( S >= 0 )
			sgs_PushInt( C, S );
		else
		{
			ret = SGS_EINPROC;
			goto fail;
		}
		
		sgs_PushItem( C, val );
		ret = sgs_StoreIndexExt( C, -3, -2, prop );
		ssz--;
	}
fail:
	va_end( args );
	sgs_Pop( C, sgs_StackSize( C ) - ssz );
	return ret;
}

/*
	argument unpacking:
	n - null (not sure why but I had a free letter here)
	b - boolean
	c,w,l,q,i - integers (int8,int16,int32,int64 x2)
	f,d,r - floats (reals) (float32,float64 x2)
	s,m - strings (string,string+size)
	p - function (callable, actually; p stands for "procedure";
		returns a SGSBOOL always, useful only for optional arguments)
	a,t,o - objects (array,dict,specific iface)
	v - any variable (returns sgs_Variable, checks if valid index or non-null if strict)
	x - custom check/return function
	? - check only, no writeback
	! - strict check (no conversions)
	-,+ - signed/unsigned (changes checked ranges and return value signs)
	| - optional arguments follow
	# - check ranges
	^ - clamp ranges
	~ - ignore ranges
	< - move argument pointer 1 item back
	> - move argument pointer 1 item forward
	@ - treat as method (1st arg = this, argnum -= 1)
	. - check if there are no more arguments
*/

int sgs_ArgErrorExt( SGS_CTX, int argid, int gotthis, const char* expect, const char* expfx )
{
	const char* got = sgs_StackSize( C ) <= argid ? "nothing" : 
		sgs_CodeString( SGS_CODE_VT, sgs_ItemType( C, argid ) );
	if( argid == 0 && gotthis )
		return sgs_Printf( C, SGS_WARNING, "'this' - expected %s%s, got %s", expfx, expect, got );
	else
		return sgs_Printf( C, SGS_WARNING, "argument %d - expected %s%s, got %s",
			argid + !gotthis, expfx, expect, got );
}

int sgs_ArgError( SGS_CTX, int argid, int gotthis, int expect, int is_strict )
{
	return sgs_ArgErrorExt( C, argid, gotthis,
		sgs_CodeString( SGS_CODE_VT, expect ), is_strict ? "strict " : "" );
}

#define argerr sgs_ArgError
#define argerrx sgs_ArgErrorExt

SGSMIXED sgs_LoadArgsExtVA( SGS_CTX, int from, const char* cmd, va_list args )
{
	int opt = 0;
	int strict = 0;
	int range = 0; /* <0: clamp, >0: check */
	int isig = 1;
	int nowrite = 0;
	int method = 0;
	while( *cmd )
	{
		switch( *cmd )
		{
		case '|': opt = 1; break;
		case '#': range = 1; break;
		case '^': range = -1; break;
		case '~': range = 0; break;
		case '-': isig = 1; break;
		case '+': isig = 0; break;
		case '!': strict = 1; break;
		case '?': nowrite = 1; break;
		case '<': from--;
			if( from < 0 )
			{
				sgs_Printf( C, SGS_WARNING, "cannot move argument pointer before 0" );
				return SGS_EINVAL;
			}
			break;
		case '>': from++; break;
		case '@': method = 1; break;
		case '.':
			if( from < sgs_StackSize( C ) )
			{
				sgs_Printf( C, SGS_WARNING, "function expects exactly %d arguments, %d given",
					from - method, sgs_StackSize( C ) - method );
				return 0;
			}
			break;
		
		case 'n':
			{
				if( sgs_ItemType( C, from ) != SVT_NULL )
				{
					argerr( C, from, method, SVT_NULL, 0 );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( args, SGSBOOL* ) = 1;
				}
			}
			strict = 0; nowrite = 0; from++; break;
		
		case 'b':
			{
				SGSBOOL b;
				
				if( !sgs_ParseBool( C, from, &b ) ||
					( strict && sgs_ItemType( C, from ) != SVT_BOOL ) )
				{
					argerr( C, from, method, SVT_BOOL, strict );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( args, SGSBOOL* ) = b;
				}
			}
			strict = 0; nowrite = 0; from++; break;
		
		case 'c': case 'w': case 'l': case 'q': case 'i':
			{
				sgs_Int i, imin = INT64_MIN, imax = INT64_MAX;
				
				if( range )
				{
					if( *cmd == 'c' && isig ){ imin = INT8_MIN; imax = INT8_MAX; }
					else if( *cmd == 'c' && !isig ){ imin = 0; imax = UINT8_MAX; }
					else if( *cmd == 'w' && isig ){ imin = INT16_MIN; imax = INT16_MAX; }
					else if( *cmd == 'w' && !isig ){ imin = 0; imax = UINT16_MAX; }
					else if( *cmd == 'l' && isig ){ imin = INT32_MIN; imax = INT32_MAX; }
					else if( *cmd == 'l' && !isig ){ imin = 0; imax = UINT32_MAX; }
				}
				
				if( !sgs_ParseInt( C, from, &i ) ||
					( strict && sgs_ItemType( C, from ) != SVT_INT ) )
				{
					argerr( C, from, method, SVT_INT, strict );
					return opt;
				}
				
				if( range > 0 && ( i < imin || i > imax ) )
				{
					sgs_Printf( C, SGS_WARNING, "integer argument %d (%" PRId64 ") out of range [%"
						PRId64 ",%" PRId64 "]", from, i, imin, imax );
					return opt;
				}
				else if( range < 0 )
				{
					if( i < imin ) i = imin;
					else if( i > imax ) i = imax;
				}
				
				if( !nowrite )
				{
					switch( *cmd )
					{
					case 'c': *va_arg( args, uint8_t* ) = (uint8_t) i; break;
					case 'w': *va_arg( args, uint16_t* ) = (uint16_t) i; break;
					case 'l': *va_arg( args, uint32_t* ) = (uint32_t) i; break;
					case 'q': *va_arg( args, uint64_t* ) = (uint64_t) i; break;
					case 'i': *va_arg( args, sgs_Int* ) = i; break;
					}
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'f': case 'd': case 'r':
			{
				sgs_Real r;
				
				if( !sgs_ParseReal( C, from, &r ) ||
					( strict && sgs_ItemType( C, from ) != SVT_REAL ) )
				{
					argerr( C, from, method, SVT_REAL, strict );
					return opt;
				}
				
				if( !nowrite )
				{
					switch( *cmd )
					{
					case 'f': *va_arg( args, float* ) = (float) r; break;
					case 'd': *va_arg( args, double* ) = r; break;
					case 'r': *va_arg( args, sgs_Real* ) = r; break;
					}
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 's': case 'm':
			{
				char* str;
				sgs_SizeVal sz;
				
				if( ( strict && sgs_ItemType( C, from ) != SVT_STRING ) ||
					!sgs_ParseString( C, from, &str, &sz ) )
				{
					argerr( C, from, method, SVT_STRING, strict );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( args, char** ) = str;
					if( *cmd == 'm' )
						*va_arg( args, sgs_SizeVal* ) = sz;
				}
			}
			strict = 0; nowrite = 0; from++; break;
		
		case 'p':
			{
				if( !sgs_IsCallable( C, from ) )
				{
					argerrx( C, from, method, "callable", "" );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( args, SGSBOOL* ) = 1;
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'a': case 't': case 'h': case 'o':
			{
				int fcf = VTC_OBJECT;
				sgs_ObjCallback* ifc = NULL;
				const char* ostr = "custom object";
				
				if( *cmd == 'a' ){ fcf = VTC_ARRAY; ostr = "array"; }
				if( *cmd == 't' ){ fcf = VTC_DICT; ostr = "dict"; }
				if( *cmd == 'h' ){ fcf = VTC_MAP; ostr = "map"; }
				if( *cmd == 'o' ) ifc = va_arg( args, sgs_ObjCallback* );
				
				if( ( sgs_ItemTypeExt( C, from ) & fcf ) != fcf ||
					( ifc != NULL && !sgs_IsObject( C, from, ifc ) ) )
				{
					argerrx( C, from, method, ostr, "" );
					return opt;
				}
				
				if( !nowrite )
				{
					sgs_VarObj* O = sgs_GetObjectStruct( C, from );
					switch( *cmd )
					{
					case 'a': *va_arg( args, sgs_SizeVal* ) = 
						((sgsstd_array_header_t*) O->data)->size; break;
					case 't':
					case 'h': *va_arg( args, sgs_SizeVal* ) =
						vht_size( ((VHTable*) O->data) ); break;
					case 'o': *va_arg( args, void** ) = O->data; break;
					}
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'v':
			{
				if( from >= sgs_StackSize( C ) ||
					( strict && sgs_ItemType( C, from ) == SVT_NULL ) )
				{
					argerrx( C, from, method, strict ? "non-null" : "any", "" );
					return opt;
				}
				
				if( !nowrite )
				{
					int owr = sgs_GetStackItem( C, from, va_arg( args, sgs_Variable* ) );
					UNUSED( owr );
					sgs_BreakIf( !owr );
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'x':
			{
				sgs_ArgCheckFunc acf = va_arg( args, sgs_ArgCheckFunc );
				int flags = 0;
				
				if( strict )     flags |= SGS_LOADARG_STRICT;
				if( nowrite )    flags |= SGS_LOADARG_NOWRITE;
				if( opt )        flags |= SGS_LOADARG_OPTIONAL;
				if( isig )       flags |= SGS_LOADARG_INTSIGN;
				if( range > 0 )  flags |= SGS_LOADARG_INTRANGE;
				if( range < 0 )  flags |= SGS_LOADARG_INTCLAMP;
				
				if( !acf( C, from, &args, flags ) )
				{
					return opt;
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case ' ': case '\t': case '\n': case '\r':
			break;
			
		default:
			return SGS_EINVAL;
			
		}
		if( opt && from >= sgs_StackSize( C ) )
			break;
		cmd++;
	}
	return 1;
}

SGSMIXED sgs_LoadArgsExt( SGS_CTX, int from, const char* cmd, ... )
{
	SGSMIXED ret;
	va_list args;
	va_start( args, cmd );
	ret = sgs_LoadArgsExtVA( C, from, cmd, args );
	va_end( args );
	return ret;
}

SGSBOOL sgs_LoadArgs( SGS_CTX, const char* cmd, ... )
{
	SGSBOOL ret;
	va_list args;
	va_start( args, cmd );
	ret = sgs_LoadArgsExtVA( C, 0, cmd, args ) > 0;
	va_end( args );
	return ret;
}


SGSRESULT sgs_Pop( SGS_CTX, int count )
{
	if( STACKFRAMESIZE < count || count < 0 )
		return SGS_EINVAL;
	stk_pop( C, count );
	return SGS_SUCCESS;
}
SGSRESULT sgs_PopSkip( SGS_CTX, int count, int skip )
{
	if( STACKFRAMESIZE < count + skip || count < 0 || skip < 0 )
		return SGS_EINVAL;
	stk_popskip( C, count, skip );
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetStackSize( SGS_CTX, int size )
{
	int diff;
	if( size < 0 )
		return SGS_EINVAL;
	diff = STACKFRAMESIZE - size;
	if( diff > 0 )
		stk_pop( C, diff );
	else
		stk_push_nulls( C, -diff );
	return SGS_SUCCESS;
}


/*

	OPERATIONS

*/

SGSRESULT sgs_FCall( SGS_CTX, int args, int expect, int gotthis )
{
	int ret;
	sgs_Variable func;
	int stksize = sgs_StackSize( C );
	gotthis = !!gotthis;
	if( stksize < args + gotthis + 1 )
		return SGS_EINVAL;

	func = *stk_getpos( C, -1 );
	VAR_ACQUIRE( &func );
	sgs_Pop( C, 1 );
	ret = vm_call( C, args, 0, gotthis, expect, &func ) ? SGS_SUCCESS : SGS_EINPROC;
	VAR_RELEASE( &func );
	return ret;
}

SGSRESULT sgs_GlobalCall( SGS_CTX, const char* name, int args, int expect )
{
	int ret;
	ret = sgs_PushGlobal( C, name );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_Call( C, args, expect );
	if( ret != SGS_SUCCESS )
		sgs_Pop( C, 1 );
	return ret;
}

SGSRESULT sgs_TypeOf( SGS_CTX )
{
	return vm_gettype( C );
}

SGSRESULT sgs_DumpVar( SGS_CTX, int maxdepth )
{
	if( sgs_StackSize( C ) < 1 )
		return SGS_EINPROC;

	if( maxdepth <= 0 )
	{
		sgs_Pop( C, 1 );
		sgs_PushString( C, "..." );
		return SGS_SUCCESS;
	}

	{
		int ret = SGS_SUCCESS;
		sgs_Variable* var = stk_getpos( C, -1 );
		switch( BASETYPE( var->type ) )
		{
		case SVT_NULL: sgs_PushString( C, "null" ); break;
		case SVT_BOOL: sgs_PushString( C, var->data.B ? "bool (true)" : "bool (false)" ); break;
		case SVT_INT: { char buf[ 32 ];
			sprintf( buf, "int (%" PRId64 ")", var->data.I );
			sgs_PushString( C, buf ); } break;
		case SVT_REAL: { char buf[ 32 ];
			sprintf( buf, "real (%g)", var->data.R );
			sgs_PushString( C, buf ); } break;
		case SVT_STRING:
			{
				char buf[ 532 ];
				char* bptr = buf;
				char* bend = buf + 512;
				char* source = var_cstr( var );
				sgs_SizeVal len = var->data.S->size;
				char* srcend = source + len;
				sprintf( buf, "string [%d] \"", len );
				bptr += strlen( buf );
				while( source < srcend && bptr < bend )
				{
					if( *source == ' ' || isgraph( *source ) )
						*bptr++ = *source++;
					else
					{
						static const char* hexdigs = "0123456789ABCDEF";
						bptr[ 0 ] = '\\';
						bptr[ 1 ] = 'x';
						bptr[ 2 ] = hexdigs[ (*source & 0xf0) >> 4 ];
						bptr[ 3 ] = hexdigs[ *source & 0xf ];
						bptr += 4;
						source++;
					}
				}
				if( source != srcend )
				{
					*bptr++ = '.';
					*bptr++ = '.';
					*bptr++ = '.';
				}
				*bptr++ = '"';
				sgs_PushStringBuf( C, buf, bptr - buf );
			}
			break;
		case SVT_FUNC: sgs_PushString( C, "SGS function" ); break;
		case SVT_CFUNC: sgs_PushString( C, "C function" ); break;
		case SVT_OBJECT:
			{
				char buf[ 32 ];
				int q, stksz;
				object_t* obj = var->data.O;

				sprintf( buf, "object (%p) [%d] ", (void*) obj, obj->refcount );
				sgs_PushString( C, buf );
				stksz = C->stack_top - C->stack_off;

				ret = obj_exec( C, SOP_DUMP, obj, maxdepth - 1, 1 );
				q = ret == SGS_SUCCESS ? 1 : 0;
				sgs_PopSkip( C, C->stack_top - C->stack_off - stksz - q, q );
				if( q )
					sgs_StringConcat( C );
				if( ret == SGS_ENOTFND ) /* object most probably doesn't support the dumping interface */
					ret = SGS_SUCCESS;
			}
			break;
		default:
			sgs_BreakIf( "Invalid variable type in sgs_DumpVar!" );
			ret = SGS_EINVAL;
			break;
		}
		if( ret == SGS_SUCCESS )
			sgs_PopSkip( C, 1, 1 );
		return ret;
	}
}

SGSRESULT sgs_GCExecute( SGS_CTX )
{
	int ret = SGS_SUCCESS;
	sgs_VarPtr vbeg, vend;
	object_t* p;

	C->redblue = !C->redblue;
	C->gcrun = TRUE;

	/* -- MARK -- */
	/* GCLIST / currently executed "main" function */
	vbeg = C->gclist; vend = vbeg + C->gclist_size;
	while( vbeg < vend ){
		ret = vm_gcmark( C, vbeg );
		if( ret != SGS_SUCCESS )
			goto end;
		vbeg++;
	}

	/* STACK */
	vbeg = C->stack_base; vend = C->stack_top;
	while( vbeg < vend ){
		ret = vm_gcmark( C, vbeg++ );
		if( ret != SGS_SUCCESS )
			goto end;
	}

	/* GLOBALS */
	sgsSTD_GlobalGC( C );

	/* -- SWEEP -- */
	/* destruct objects */
	p = C->objs;
	while( p ){
		object_t* pn = p->next;
		if( p->redblue != C->redblue ){
			ret = obj_exec( C, SOP_DESTRUCT, p, FALSE, 0 );
			if( ret != SGS_SUCCESS && ret != SGS_ENOTFND )
				goto end;
		}
		p = pn;
	}

	/* free variables */
	p = C->objs;
	while( p ){
		object_t* pn = p->next;
		if( p->redblue != C->redblue )
			var_free_object( C, p );
		p = pn;
	}

end:
	C->gcrun = FALSE;
	return ret;
}


SGSRESULT sgs_PadString( SGS_CTX )
{
	const char* padding = "  ";
	const int padsize = 2;

	if( sgs_StackSize( C ) < 1 )
		return SGS_EINPROC;
	{
		int i;
		char* ostr;
		const char* cstr;
		sgs_Variable* var = stk_getpos( C, -1 );
		if( var->type != VTC_STRING )
			return SGS_EINVAL;
		cstr = var_cstr( var );
		for( i = 0; cstr[ i ]; )
			if( cstr[ i ] == '\n' ) i++; else cstr++;
		sgs_PushStringBuf( C, NULL, var->data.S->size + i * padsize );
		cstr = var_cstr( stk_getpos( C, -2 ) );
		ostr = var_cstr( stk_getpos( C, -1 ) );
		while( *cstr )
		{
			*ostr++ = *cstr;
			if( *cstr == '\n' )
			{
				const char* ppd = padding;
				while( *ppd )
					*ostr++ = *ppd++;
			}
			cstr++;
		}
	}
	sgs_PopSkip( C, 1, 1 );
	return SGS_SUCCESS;
}

SGSRESULT sgs_ToPrintSafeString( SGS_CTX )
{
	char* buf = NULL;
	sgs_SizeVal size = 0, i;
	if( !( buf = sgs_ToStringBuf( C, -1, &size ) ) )
		return SGS_EINPROC;
	MemBuf mb = membuf_create();
	for( i = 0; i < size; ++i )
	{
		if( isgraph( buf[ i ] ) || buf[ i ] == ' ' )
			membuf_appchr( &mb, C, buf[ i ] );
		else
		{
			char chrs[32];
			sprintf( chrs, "\\x%02X", (int) buf[ i ] );
			membuf_appbuf( &mb, C, chrs, 4 );
		}
	}
	sgs_Pop( C, 1 );
	sgs_PushStringBuf( C, mb.ptr, mb.size );
	membuf_destroy( &mb, C );
	return SGS_SUCCESS;
}

SGSRESULT sgs_StringConcat( SGS_CTX )
{
	if( sgs_StackSize( C ) < 2 )
		return SGS_EINPROC;
	vm_op_concat( C, stk_absindex( C, -1 ), stk_getpos( C, -2 ), stk_getpos( C, -1 ) );
	stk_popskip( C, 1, 1 );
	return SGS_SUCCESS;
}

SGSRESULT sgs_StringMultiConcat( SGS_CTX, int args )
{
	return vm_op_concat_ex( C, args ) ? SGS_SUCCESS : SGS_EINVAL;
}

SGSRESULT sgs_CloneItem( SGS_CTX, int item )
{
	int ret;
	sgs_Variable copy;
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_EBOUNDS;
	item = stk_absindex( C, item );
	sgs_PushNull( C );
	sgs_GetStackItem( C, item, &copy );
	ret = vm_clone( C, stk_absindex( C, -1 ), &copy );
	if( ret != SGS_SUCCESS )
		sgs_Pop( C, 1 );
	return ret;
}


static int _serialize_function( SGS_CTX, sgs_iFunc* func, MemBuf* out )
{
	sgs_CompFunc F;
	{
		F.consts = membuf_create();
		F.code = membuf_create();
		F.lnbuf = membuf_create();
		F.gotthis = func->gotthis;
		F.numargs = func->numargs;
		F.numtmp = func->numtmp;
		F.numclsr = func->numclsr;
	}
	
	F.consts.ptr = ((char*)(func+1));
	F.consts.size = F.consts.mem = func->instr_off;
	
	F.code.ptr = ((char*)(func+1)) + func->instr_off;
	F.code.size = F.code.mem = func->size - func->instr_off;
	
	F.lnbuf.ptr = (char*) func->lineinfo;
	F.lnbuf.size = ( func->size - func->instr_off ) / 2;
	
	return sgsBC_Func2Buf( C, &F, out );
}

static int _unserialize_function( SGS_CTX, const char* buf, sgs_SizeVal sz, sgs_iFunc** outfn )
{
	func_t* F;
	sgs_CompFunc* nf = NULL;
	if( sgsBC_ValidateHeader( buf, sz ) < SGS_HEADER_SIZE )
		return 0;
	if( sgsBC_Buf2Func( C, "<anonymous>", buf, sz, &nf ) )
		return 0;
	
	F = sgs_Alloc_a( func_t, nf->consts.size + nf->code.size );

	F->refcount = 0;
	F->size = nf->consts.size + nf->code.size;
	F->instr_off = nf->consts.size;
	F->gotthis = nf->gotthis;
	F->numargs = nf->numargs;
	F->numtmp = nf->numtmp;
	F->numclsr = nf->numclsr;

	{
		int lnc = nf->lnbuf.size / sizeof( sgs_LineNum );
		F->lineinfo = sgs_Alloc_n( sgs_LineNum, lnc );
		memcpy( F->lineinfo, nf->lnbuf.ptr, nf->lnbuf.size );
	}
	F->funcname = membuf_create();
	F->linenum = 0;
	F->filename = membuf_create();
	/* set from current if possible? */
	
	memcpy( func_consts( F ), nf->consts.ptr, nf->consts.size );
	memcpy( func_bytecode( F ), nf->code.ptr, nf->code.size );

	membuf_destroy( &nf->consts, C );
	membuf_destroy( &nf->code, C );
	membuf_destroy( &nf->lnbuf, C );
	sgs_Dealloc( nf );
	
	*outfn = F;
	
	return 1;
}

static void serialize_output_func( void* ud,
	SGS_CTX, const void* ptr, sgs_SizeVal datasize )
{
	MemBuf* B = (MemBuf*) ud;
	membuf_appbuf( B, C, ptr, datasize );
}

SGSRESULT sgs_Serialize( SGS_CTX )
{
	int ret = SGS_SUCCESS;
	sgs_Variable V;
	MemBuf B = membuf_create();
	sgs_OutputFunc dofn;
	void* doud;
	int ep = C->output_fn != serialize_output_func;

	if( !sgs_GetStackItem( C, -1, &V ) )
		return SGS_EBOUNDS;

	if( ep )
	{
		dofn = C->output_fn;
		doud = C->output_ctx;
		sgs_SetOutputFunc( C, serialize_output_func, &B );
	}

	if( V.type & SVT_OBJECT )
	{
		int ssz = sgs_StackSize( C );
		ret = obj_exec( C, SOP_SERIALIZE, V.data.O, 0, 0 );
		sgs_Pop( C, sgs_StackSize( C ) - ssz );
		if( ret != SGS_SUCCESS )
			goto fail;
	}
	else if( V.type == VTC_CFUNC )
	{
		sgs_Printf( C, SGS_WARNING, "Cannot serialize C functions" );
		ret = SGS_EINVAL;
		goto fail;
	}
	else
	{
		char pb[2];
		{
			pb[0] = 'P';
			pb[1] = BASETYPE( V.type );
		}
		sgs_Write( C, pb, 2 );
		switch( V.type )
		{
		case VTC_NULL: break;
		case VTC_BOOL: { uint8_t b = V.data.B; sgs_Write( C, &b, 1 ); } break;
		case VTC_INT: sgs_Write( C, &V.data.I, sizeof( sgs_Int ) ); break;
		case VTC_REAL: sgs_Write( C, &V.data.R, sizeof( sgs_Real ) ); break;
		case VTC_STRING:
			sgs_Write( C, &V.data.S->size, 4 );
			sgs_Write( C, var_cstr( &V ), V.data.S->size );
			break;
		case VTC_FUNC:
			{
				MemBuf B = membuf_create();
				ret = _serialize_function( C, V.data.F, &B );
				if( ret != 0 )
				{
					sgs_Write( C, &B.size, 4 );
					sgs_Write( C, B.ptr, B.size );
					ret = SGS_SUCCESS;
				}
				else
					ret = SGS_EINPROC;
				membuf_destroy( &B, C );
				if( ret != SGS_SUCCESS )
					goto fail;
			}
			break;
		default:
			sgs_Printf( C, SGS_ERROR, "sgs_Serialize: Unknown memory error" );
			ret = SGS_EINPROC;
			goto fail;
		}
	}

	sgs_Pop( C, 1 );
fail:
	if( ep )
	{
		sgs_SetOutputFunc( C, dofn, doud );
		if( ret == SGS_SUCCESS )
			sgs_PushStringBuf( C, B.ptr, B.size );
		membuf_destroy( &B, C );
	}
	return ret;
}

SGSRESULT sgs_SerializeObject( SGS_CTX, int args, const char* func )
{
	int len = strlen( func );
	char pb[7] = { 'C', 0, 0, 0, 0, 0, 0 };
	{
		pb[1] = args;
		pb[2] = args>>8;
		pb[3] = args>>16;
		pb[4] = args>>24;
	}
	
	if( len >= 255 )
		return SGS_EINVAL;
	if( C->output_fn != serialize_output_func )
		return SGS_EINPROC;

	pb[ 5 ] = strlen( func );
	sgs_Write( C, pb, 6 );
	sgs_Write( C, func, len );
	sgs_Write( C, pb + 6, 1 );
	return SGS_SUCCESS;
}

SGSRESULT sgs_Unserialize( SGS_CTX )
{
	char* str, *strend;
	sgs_SizeVal size;
	if( !sgs_ParseString( C, -1, &str, &size ) )
		return SGS_EINVAL;

	strend = str + size;
	while( str < strend )
	{
		char c = *str++;
		if( c == 'P' )
		{
			if( str >= strend )
				return SGS_EINPROC;
			c = *str++;
			switch( c )
			{
			case SVT_NULL: sgs_PushNull( C ); break;
			case SVT_BOOL:
				if( str >= strend )
					return SGS_EINPROC;
				sgs_PushBool( C, *str++ );
				break;
			case SVT_INT:
				if( str >= strend-7 )
					return SGS_EINPROC;
				sgs_PushInt( C, AS_INTEGER( str ) );
				str += 8;
				break;
			case SVT_REAL:
				if( str >= strend-7 )
					return SGS_EINPROC;
				sgs_PushReal( C, AS_REAL( str ) );
				str += 8;
				break;
			case SVT_STRING:
				{
					sgs_SizeVal strsz;
					if( str >= strend-3 )
						return SGS_EINPROC;
					strsz = AS_INT32( str );
					str += 4;
					if( str > strend - strsz )
						return SGS_EINPROC;
					sgs_PushStringBuf( C, str, strsz );
					str += strsz;
				}
				break;
			case SVT_FUNC:
				{
					sgs_Variable tmp;
					sgs_SizeVal bcsz;
					sgs_iFunc* fn;
					if( str >= strend-3 )
						return SGS_EINPROC;
					bcsz = AS_INT32( str );
					str += 4;
					if( str > strend - bcsz )
						return SGS_EINPROC;
					if( !_unserialize_function( C, str, bcsz, &fn ) )
						return SGS_EINPROC;
					tmp.type = VTC_FUNC;
					tmp.data.F = fn;
					sgs_PushVariable( C, &tmp );
					str += bcsz;
				}
				break;
			default:
				return SGS_EINPROC;
			}
		}
		else if( c == 'C' )
		{
			int argc, fnsz, ret;
			if( str >= strend-4 )
				return SGS_EINPROC;
			argc = AS_INT32( str );
			str += 4;
			fnsz = *str++ + 1;
			if( str > strend - fnsz )
				return SGS_EINPROC;
			ret = sgs_GlobalCall( C, str, argc, 1 );
			if( ret != SGS_SUCCESS )
				return ret;
			str += fnsz;
		}
		else
		{
			return SGS_EINPROC;
		}
	}
	return SGS_SUCCESS;
}


sgs_Real sgs_CompareF( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 )
{
	return vm_compare( C, v1, v2 );
}

SGSBOOL sgs_EqualTypes( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 )
{
	return v1->type == v2->type && ( !( v1->type & SVT_OBJECT )
		|| v1->data.O->iface == v2->data.O->iface );
}


sgs_Bool sgs_GetBool( SGS_CTX, int item )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	var = stk_getpos( C, item );
	return var_getbool( C, var );
}

sgs_Int sgs_GetInt( SGS_CTX, int item )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	var = stk_getpos( C, item );
	return var_getint( C, var );
}

sgs_Real sgs_GetReal( SGS_CTX, int item )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	var = stk_getpos( C, item );
	return var_getreal( C, var );
}


sgs_Bool sgs_ToBool( SGS_CTX, int item )
{
	if( vm_convert_stack( C, item, VTC_BOOL ) != SGS_SUCCESS )
		return 0;
	return stk_getpos( C, item )->data.B;
}

sgs_Int sgs_ToInt( SGS_CTX, int item )
{
	if( vm_convert_stack( C, item, VTC_INT ) != SGS_SUCCESS )
		return 0;
	return stk_getpos( C, item )->data.I;
}

sgs_Real sgs_ToReal( SGS_CTX, int item )
{
	if( vm_convert_stack( C, item, VTC_REAL ) != SGS_SUCCESS )
		return 0;
	return stk_getpos( C, item )->data.R;
}

char* sgs_ToStringBuf( SGS_CTX, int item, sgs_SizeVal* outsize )
{
	sgs_Variable* var;
	if( sgs_ItemType( C, item ) != SVT_STRING &&
		vm_convert_stack( C, item, VTC_STRING ) != SGS_SUCCESS )
		return NULL;
	var = stk_getpos( C, item );
	if( outsize )
		*outsize = var->data.S->size;
	return var_cstr( var );
}

char* sgs_ToStringBufFast( SGS_CTX, int item, sgs_SizeVal* outsize )
{
	if( !sgs_IsValidIndex( C, item ) )
		return NULL;
	if( stk_getpos( C, item )->type & SVT_OBJECT )
	{
		int g = stk_absindex( C, item );
		sgs_PushItem( C, g );
		if( sgs_TypeOf( C ) )
		{
			sgs_Pop( C, 1 );
			sgs_PushString( C, "object" );
		}
		sgs_StoreItem( C, g );
	}
	return sgs_ToStringBuf( C, item, outsize );
}

SGSRESULT sgs_Convert( SGS_CTX, int item, int type )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_EBOUNDS;
	return vm_convert_stack( C, item, type );
}



SGSRESULT sgs_RegisterType( SGS_CTX, const char* name, sgs_ObjCallback* iface )
{
	int len;
	VHTVar* p;
	if( !iface )
		return SGS_EINVAL;
	len = strlen( name );
	p = vht_get_str( &C->typetable, name, len, sgs_HashFunc( name, len ) );
	if( p )
		return SGS_EINPROC;
	{
		sgs_Variable tmp;
		tmp.type = VTC_INT;
		tmp.data.I = (sgs_Int) (size_t) iface;
		sgs_PushStringBuf( C, name, len );
		vht_set( &C->typetable, C, C->stack_top-1, &tmp );
		sgs_Pop( C, 1 );
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_UnregisterType( SGS_CTX, const char* name )
{
	int len = strlen( name );
	VHTVar* p = vht_get_str( &C->typetable, name, len, sgs_HashFunc( name, len ) );
	if( !p )
		return SGS_ENOTFND;
	vht_unset( &C->typetable, C, &p->key );
	return SGS_SUCCESS;
}

sgs_ObjCallback* sgs_FindType( SGS_CTX, const char* name )
{
	int len = strlen( name );
	VHTVar* p = vht_get_str( &C->typetable, name, len, sgs_HashFunc( name, len ) );
	if( p )
		return (sgs_ObjCallback*) (size_t) p->val.data.I;
	return NULL;
}


SGSBOOL sgs_IsObject( SGS_CTX, int item, sgs_ObjCallback* iface )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	var = stk_getpos( C, item );
	return ( var->type & SVT_OBJECT ) && var->data.O->iface == iface;
}


SGSBOOL sgs_IsCallable( SGS_CTX, int item )
{
	int ty = sgs_ItemTypeExt( C, item );

	if( ty & VTF_CALL )
		return 1;
	if( ty & SVT_OBJECT )
	{
		sgs_ObjCallback* ptr = sgs_GetObjectIface( C, item );
		while( *ptr )
		{
			if( *ptr == SOP_CALL )
				return 1;
			ptr += 2;
		}
	}
	return 0;
}


typedef union intreal_s
{
	sgs_Int i;
	sgs_Real r;
}
intreal_t;

SGSBOOL sgs_IsNumericString( const char* str, sgs_SizeVal size )
{
	intreal_t out;
	const char* ostr = str;
	return util_strtonum( &str, str + size, &out.i, &out.r ) != 0 && str != ostr;
}

SGSBOOL sgs_ParseBool( SGS_CTX, int item, sgs_Bool* out )
{
	int i, ty;
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	ty = sgs_ItemType( C, item );
	if( ty == VTC_NULL || ty == VTC_CFUNC || ty == VTC_FUNC || ty == VTC_STRING )
		return FALSE;
	i = sgs_GetBool( C, item );
	if( out )
		*out = i;
	return TRUE;
}

SGSBOOL sgs_ParseInt( SGS_CTX, int item, sgs_Int* out )
{
	sgs_Int i;
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	var = stk_getpos( C, item );
	if( var->type == VTC_NULL || (var->type & VTF_CALL) )
		return FALSE;
	if( var->type == VTC_STRING )
	{
		intreal_t OIR;
		const char* ostr = var_cstr( var );
		const char* str = ostr;
		int res = util_strtonum( &str, str + var->data.S->size, &OIR.i, &OIR.r );

		if( str == ostr )    return FALSE;
		if( res == 1 )       i = OIR.i;
		else if( res == 2 )  i = (sgs_Int) OIR.r;
		else                 return FALSE;
	}
	else
		i = sgs_GetInt( C, item );
	if( out )
		*out = i;
	return TRUE;
}

SGSBOOL sgs_ParseReal( SGS_CTX, int item, sgs_Real* out )
{
	sgs_Real r;
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	var = stk_getpos( C, item );
	if( var->type == VTC_NULL || (var->type & VTF_CALL) )
		return FALSE;
	if( var->type == VTC_STRING )
	{
		intreal_t OIR;
		const char* ostr = var_cstr( var );
		const char* str = ostr;
		int res = util_strtonum( &str, str + var->data.S->size, &OIR.i, &OIR.r );

		if( str == ostr )    return FALSE;
		if( res == 1 )       r = (sgs_Real) OIR.i;
		else if( res == 2 )  r = OIR.r;
		else                 return FALSE;
	}
	else
		r = sgs_GetReal( C, item );
	if( out )
		*out = r;
	return TRUE;
}

SGSBOOL sgs_ParseString( SGS_CTX, int item, char** out, sgs_SizeVal* size )
{
	char* str;
	int ty;
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	ty = sgs_ItemTypeExt( C, item );
	if( ty == VTC_NULL || (ty & VTF_CALL) )
		return FALSE;
	str = sgs_ToStringBuf( C, item, size );
	if( out )
		*out = str;
	return str != NULL;
}


SGSRESULT sgs_PushIterator( SGS_CTX, int item )
{
	int ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	sgs_PushNull( C );
	ret = vm_forprep( C, stk_absindex( C, -1 ), &var );
	if( ret != SGS_SUCCESS )
		sgs_Pop( C, 1 );
	return ret;
}

SGSMIXED sgs_IterAdvance( SGS_CTX, int item )
{
	int32_t oml = C->minlev;
	sgs_SizeVal ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	C->minlev = INT32_MAX;
	ret = vm_fornext( C, -1, -1, &var );
	C->minlev = oml;
	return ret;
}

SGSMIXED sgs_IterPushData( SGS_CTX, int item, int key, int value )
{
	int32_t oml = C->minlev;
	sgs_SizeVal ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	if( !key && !value )
		return SGS_SUCCESS;
	if( key )
	{
		sgs_PushNull( C );
		key = stk_absindex( C, -1 );
	}
	else key = -1;
	if( value )
	{
		sgs_PushNull( C );
		value = stk_absindex( C, -1 );
	}
	else value = -1;
	C->minlev = INT32_MAX;
	ret = vm_fornext( C, key, value, &var );
	C->minlev = oml;
	return ret;
}

SGSMIXED sgs_ArraySize( SGS_CTX, int item )
{
	if( sgs_ItemTypeExt( C, item ) != VTC_ARRAY )
		return SGS_EINVAL;
	return ((sgsstd_array_header_t*)sgs_GetObjectData( C, item ))->size;
}


/*

	EXTENSION UTILITIES

*/

int sgs_StackSize( SGS_CTX )
{
	return STACKFRAMESIZE;
}

SGSBOOL sgs_IsValidIndex( SGS_CTX, int item )
{
	item = stk_absindex( C, item );
	return ( item >= 0 && item < STACKFRAMESIZE );
}

SGSBOOL sgs_GetStackItem( SGS_CTX, int item, sgs_Variable* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	*out = *stk_getpos( C, item );
	return TRUE;
}

int sgs_ItemType( SGS_CTX, int item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	return BASETYPE( stk_getpos( C, item )->type );
}

int sgs_ItemTypeExt( SGS_CTX, int item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	return stk_getpos( C, item )->type;
}

SGSBOOL sgs_Method( SGS_CTX )
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

void sgs_Acquire( SGS_CTX, sgs_Variable* var )
{
	UNUSED( C );
	VAR_ACQUIRE( var );
}

void sgs_Release( SGS_CTX, sgs_Variable* var )
{
	if( ( var->type & SVT_OBJECT ) && C->gcrun )
	{
		/* if running GC, dereference without destroying */
		(*var->data.pRC) -= 1;
		return;
	}
	VAR_RELEASE( var );
}

SGSRESULT sgs_GCMark( SGS_CTX, sgs_Variable* var )
{
	return vm_gcmark( C, var );
}


#define DBLCHK( what, fval )\
	sgs_BreakIf( what );\
	if( what ) return fval;

char* sgs_GetStringPtr( SGS_CTX, int item )
{
	sgs_Variable* var;
	DBLCHK( !sgs_IsValidIndex( C, item ), NULL )
	var = stk_getpos( C, item );
	DBLCHK( var->type != VTC_STRING, NULL )
	return var_cstr( var );
}

sgs_SizeVal sgs_GetStringSize( SGS_CTX, int item )
{
	sgs_Variable* var;
	DBLCHK( !sgs_IsValidIndex( C, item ), 0 )
	var = stk_getpos( C, item );
	DBLCHK( var->type != VTC_STRING, 0 )
	return var->data.S->size;
}

#define _OBJPREP( ret ) \
	sgs_Variable* var; \
	DBLCHK( !sgs_IsValidIndex( C, item ), ret ) \
	var = stk_getpos( C, item ); \
	DBLCHK( !( var->type & SVT_OBJECT ), ret )

sgs_VarObj* sgs_GetObjectStruct( SGS_CTX, int item )
{
	_OBJPREP( NULL );
	return var->data.O;
}

void* sgs_GetObjectData( SGS_CTX, int item )
{
	_OBJPREP( NULL );
	return var->data.O->data;
}

sgs_ObjCallback* sgs_GetObjectIface( SGS_CTX, int item )
{
	_OBJPREP( NULL );
	return var->data.O->iface;
}

int sgs_SetObjectData( SGS_CTX, int item, void* data )
{
	_OBJPREP( 0 );
	var->data.O->data = data;
	return 1;
}

int sgs_SetObjectIface( SGS_CTX, int item, sgs_ObjCallback* iface )
{
	_OBJPREP( 0 );
	var->data.O->iface = iface;
	return 1;
}

#undef DBLCHK

