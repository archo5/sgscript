

#include <math.h>
#include <stdarg.h>
#include <ctype.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL
#define SGS_INTERNAL_STRINGTABLES

#include "sgs_xpc.h"
#include "sgs_int.h"


#define TYPENAME( type ) sgs_VarNames[ type ]

#define IS_REFTYPE( type ) ( type == SVT_STRING || type == SVT_FUNC || type == SVT_OBJECT )


#define VAR_ACQUIRE( pvar ) { if( IS_REFTYPE( (pvar)->type ) ) (*(pvar)->data.pRC)++; }
#define VAR_RELEASE( pvar ) { if( IS_REFTYPE( (pvar)->type ) ) var_release( C, pvar ); }


#define STK_UNITSIZE sizeof( sgs_Variable )


static void stk_popskip( SGS_CTX, StkIdx num, StkIdx skip );
#define stk_pop( C, num ) stk_popskip( C, num, 0 )

#define _STACK_PREPARE ptrdiff_t _stksz = 0;
#define _STACK_PROTECT _stksz = C->stack_off - C->stack_base; C->stack_off = C->stack_top;
#define _STACK_PROTECT_SKIP( n ) do{ _stksz = C->stack_off - C->stack_base; \
	C->stack_off = C->stack_top - (n); }while(0)
#define _STACK_UNPROTECT stk_pop( C, STACKFRAMESIZE ); C->stack_off = C->stack_base + _stksz;
#define _STACK_UNPROTECT_SKIP( n ) do{ StkIdx __n = (n); \
	stk_popskip( C, STACKFRAMESIZE - __n, __n ); \
	C->stack_off = C->stack_base + _stksz; }while(0)


typedef union intreal_s
{
	sgs_Int i;
	sgs_Real r;
}
intreal_t;


/* to work with both insertion and removal algorithms, this function has the following rules:
- return the index of the first found item with the right size or just after the right size
- if all sizes are less than specified, return the size of the object pool
*/
static int32_t objpool_binary_search( SGS_CTX, uint32_t appsize )
{
	int32_t pmin = 0, pmax = C->objpool_size - 1;
	while( pmin <= pmax )
	{
		int32_t pos = ( pmax + pmin ) / 2;
		uint32_t ssize = C->objpool_data[ pos ].appsize;
		if( ssize == appsize )
			return pos;
		else if( ssize < appsize )
			pmin = pos + 1;
		else if( ssize > appsize )
			pmax = pos - 1;
	}
	return pmin;
}

static void var_free_object( SGS_CTX, object_t* O )
{
	if( O->prev ) O->prev->next = O->next;
	if( O->next ) O->next->prev = O->prev;
	if( C->objs == O )
		C->objs = O->next;
#if SGS_OBJPOOL_SIZE > 0
	if( O->appsize <= SGS_OBJPOOL_MAX_APPMEM )
	{
		int32_t pos = 0;
		if( C->objpool_size )
		{
			pos = objpool_binary_search( C, O->appsize );
			if( C->objpool_size < SGS_OBJPOOL_SIZE && pos < C->objpool_size )
			{
				memmove( C->objpool_data + pos + 1, C->objpool_data + pos,
					sizeof( sgs_ObjPoolItem ) * (size_t) ( C->objpool_size - pos ) );
			}
			if( pos >= SGS_OBJPOOL_SIZE )
				pos = SGS_OBJPOOL_SIZE - 1;
			if( C->objpool_size >= SGS_OBJPOOL_SIZE )
				sgs_Dealloc( C->objpool_data[ pos ].obj );
		}
		C->objpool_data[ pos ].obj = O;
		C->objpool_data[ pos ].appsize = O->appsize;
		if( C->objpool_size < SGS_OBJPOOL_SIZE )
			C->objpool_size++;
	}
	else
		sgs_Dealloc( O );
#else
	sgs_Dealloc( O );
#endif
	C->objcount--;
}

static void var_destruct_object( SGS_CTX, object_t* O )
{
	if( O->iface->destruct )
	{
		int ret;
		_STACK_PREPARE;
		_STACK_PROTECT;
		ret = O->iface->destruct( C, O );
		_STACK_UNPROTECT;
		if( SGS_FAILED( ret ) )
			sgs_Msg( C, SGS_ERROR, "failed to call the destructor" );
	}
}
void sgsVM_VarDestroyObject( SGS_CTX, object_t* O )
{
	var_destruct_object( C, O );
	var_free_object( C, O );
}

static void var_destroy_string( SGS_CTX, string_t* S )
{
#if SGS_STRINGTABLE_MAXLEN >= 0
	sgs_Variable tmp;
	tmp.type = SVT_STRING;
	tmp.data.S = S;
	VHTVar* p = (int32_t) S->size <= SGS_STRINGTABLE_MAXLEN ? vht_get( &C->stringtable, &tmp ) : NULL;
	if( p && p->key.data.S == S )
	{
		S->refcount = 2; /* the 'less code' way to avoid double free */
		vht_unset( &C->stringtable, C, &tmp );
		printf( "unset string %s\n", str_cstr(S) );
	}
#endif
	sgs_Dealloc( S );
}

static void var_release( SGS_CTX, sgs_VarPtr p );
static void var_destroy_func( SGS_CTX, func_t* F )
{
	sgs_VarPtr var = (sgs_VarPtr) func_consts( F ), vend = (sgs_VarPtr) (void*) ASSUME_ALIGNED( func_bytecode( F ), 16 );
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
	if( IS_REFTYPE( p->type ) )
		(*p->data.pRC)++;
}

static void var_release( SGS_CTX, sgs_VarPtr p )
{
	uint32_t type = p->type;
	(*p->data.pRC) -= 1;
	p->type = SVT_NULL;
	
	if( (*p->data.pRC) <= 0 )
	{
		switch( type )
		{
		case SVT_STRING: var_destroy_string( C, p->data.S ); break;
		case SVT_FUNC: var_destroy_func( C, p->data.F ); break;
		case SVT_OBJECT: var_destroy_object( C, p->data.O ); break;
		}
	}
}


static void var_create_0str( SGS_CTX, sgs_VarPtr out, uint32_t len )
{
	out->type = SVT_STRING;
	out->data.S = sgs_Alloc_a( string_t, len + 1 );
	out->data.S->refcount = 1;
	out->data.S->size = len;
	out->data.S->hash = 0;
	out->data.S->isconst = 0;
	var_cstr( out )[ len ] = 0;
}

void sgsVM_VarCreateString( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal len )
{
	sgs_Hash hash;
	uint32_t ulen = (uint32_t) len; /* WP: string limit */
	sgs_BreakIf( !str );
	
	if( len < 0 )
		ulen = (uint32_t) strlen( str ); /* WP: string limit */
	
	hash = sgs_HashFunc( str, ulen );
	if( (int32_t) ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		VHTVar* var = vht_get_str( &C->stringtable, str, ulen, hash );
		if( var )
		{
			*out = var->key;
			out->data.S->refcount++;
			return;
		}
	}
	
	var_create_0str( C, out, ulen );
	memcpy( str_cstr( out->data.S ), str, ulen );
	out->data.S->hash = hash;
	out->data.S->isconst = 1;
	
	if( (int32_t) ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		vht_set( &C->stringtable, C, out, NULL );
		out->data.S->refcount--;
	}
}

static void var_create_str( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal len )
{
	uint32_t ulen = (uint32_t) len; /* WP: string limit */
	sgs_BreakIf( !str );
	
	if( len < 0 )
		ulen = (uint32_t) strlen( str ); /* WP: string limit */
	
	var_create_0str( C, out, ulen );
	memcpy( str_cstr( out->data.S ), str, ulen );
}

static void var_create_obj( SGS_CTX, sgs_Variable* out, void* data, sgs_ObjInterface* iface, uint32_t xbytes )
{
	object_t* obj = NULL;
#if SGS_OBJPOOL_SIZE > 0
	if( xbytes <= SGS_OBJPOOL_MAX_APPMEM )
	{
		int32_t pos = objpool_binary_search( C, xbytes );
		if( pos < C->objpool_size && C->objpool_data[ pos ].appsize == xbytes )
		{
			obj = C->objpool_data[ pos ].obj;
			C->objpool_size--;
			if( pos < C->objpool_size )
			{
				memmove( C->objpool_data + pos, C->objpool_data + pos + 1,
					sizeof( sgs_ObjPoolItem ) * (size_t) ( C->objpool_size - pos ) );
			}
		}
	}
#endif
	if( !obj )
		obj = sgs_Alloc_a( object_t, xbytes );
	obj->appsize = xbytes;
	obj->data = data;
	if( xbytes )
		obj->data = ((char*)obj) + sizeof( object_t );
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

static int vm_frame_push( SGS_CTX, sgs_Variable* func, uint16_t* T, instr_t* code, size_t icnt )
{
	sgs_StackFrame* F;
	
	if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
	{
		sgs_Msg( C, SGS_ERROR, "Max. call stack size reached" );
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
	if( func && func->type == SVT_FUNC )
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

static SGS_INLINE StkIdx stk_absindex( SGS_CTX, StkIdx stkid )
{
	/* WP: stack limit */
	if( stkid < 0 ) return (StkIdx) ( C->stack_top - C->stack_off ) + stkid;
	else return stkid;
}

static SGS_INLINE sgs_VarPtr stk_getpos( SGS_CTX, StkIdx stkid )
{
#if SGS_DEBUG && SGS_DEBUG_VALIDATE && SGS_DEBUG_EXTRA
	DBG_STACK_CHECK
	if( stkid < 0 ) sgs_BreakIf( -stkid > C->stack_top - C->stack_off )
	else            sgs_BreakIf( stkid >= C->stack_top - C->stack_off )
#endif
	if( stkid < 0 )	return C->stack_top + stkid;
	else			return C->stack_off + stkid;
}

static SGS_INLINE void stk_setvar( SGS_CTX, StkIdx stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
	VAR_ACQUIRE( vpos );
}
static SGS_INLINE void stk_setvar_leave( SGS_CTX, StkIdx stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
}
static SGS_INLINE void stk_setvar_null( SGS_CTX, StkIdx stkid )
{
	sgs_VarPtr vpos = stk_getpos( C, stkid );
	VAR_RELEASE( vpos );
	/* already set to SVT_NULL */
}

#define stk_getlpos( C, stkid ) (C->stack_off + stkid)
static SGS_INLINE void stk_setlvar( SGS_CTX, StkIdx stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
	VAR_ACQUIRE( vpos );
}
static SGS_INLINE void stk_setlvar_leave( SGS_CTX, StkIdx stkid, sgs_VarPtr var )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
}
static SGS_INLINE void stk_setlvar_null( SGS_CTX, StkIdx stkid )
{
	sgs_VarPtr vpos = stk_getlpos( C, stkid );
	VAR_RELEASE( vpos );
}

static void stk_makespace( SGS_CTX, StkIdx num )
{
	/* StkIdx item stack limit */
	ptrdiff_t stkoff, stkend;
	size_t nsz;
	StkIdx stksz = (StkIdx) ( C->stack_top - C->stack_base );
	sgs_BreakIf( num < 0 );
	if( stksz + num <= (StkIdx) C->stack_mem )
		return;
	sgs_BreakIf( stksz + num < 0 ); /* overflow test */
	stkoff = C->stack_off - C->stack_base;
	stkend = C->stack_top - C->stack_base;
	DBG_STACK_CHECK
	nsz = (size_t) ( stksz + num ) + C->stack_mem * 2; /* MAX( stksz + num, C->stack_mem * 2 ); */
	C->stack_base = (sgs_VarPtr) sgs_Realloc( C, C->stack_base, sizeof( sgs_Variable ) * nsz );
	C->stack_mem = (uint32_t) nsz;
	C->stack_off = C->stack_base + stkoff;
	C->stack_top = C->stack_base + stkend;
}

static void stk_push( SGS_CTX, sgs_VarPtr var )
{
	stk_makespace( C, 1 );
	*C->stack_top++ = *var;
	VAR_ACQUIRE( var );
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
static void stk_push_nulls( SGS_CTX, StkIdx cnt )
{
	sgs_VarPtr tgt;
	stk_makespace( C, cnt );
	tgt = C->stack_top + cnt;
	while( C->stack_top < tgt )
		(C->stack_top++)->type = SVT_NULL;
}

static sgs_Variable* stk_insert_pos( SGS_CTX, StkIdx off )
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

static void stk_insert_null( SGS_CTX, StkIdx off )
{
	stk_insert_pos( C, off )->type = SVT_NULL;
}

static void stk_clean( SGS_CTX, sgs_VarPtr from, sgs_VarPtr to )
{
	/* WP: stack limit */
	size_t oh = (size_t) ( C->stack_top - to );
	sgs_VarPtr p = from, pend = to;
	sgs_BreakIf( C->stack_top < to );
	sgs_BreakIf( to < from );
	sgs_BreakIf( from < C->stack_base );
	DBG_STACK_CHECK
	
	while( p < pend )
	{
		VAR_RELEASE( p );
		p++;
	}
	
	C->stack_top -= to - from;
	
	if( oh )
		memmove( from, to, oh * STK_UNITSIZE );
}

static void stk_popskip( SGS_CTX, StkIdx num, StkIdx skip )
{
	sgs_VarPtr off, ptr;
	if( num <= 0 ) return;
	DBG_STACK_CHECK
	off = C->stack_top - skip;
	ptr = off - num;
	stk_clean( C, ptr, off );
}

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

static void varr_reverse( sgs_Variable* beg, sgs_Variable* end )
{
	sgs_Variable tmp;
	end--;
	while( beg < end )
	{
		tmp = *beg;
		*beg = *end;
		*end = tmp;
		beg++;
		end--;
	}
}

static void stk_transpose( SGS_CTX, StkIdx first, StkIdx all )
{
	/* assuming:
		- first >= 0 (1+)
		- all > first (implies all > 1 -- 2+)
	*/
	varr_reverse( C->stack_top - all, C->stack_top - all + first );
	varr_reverse( C->stack_top - all, C->stack_top );
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

static void clstk_makespace( SGS_CTX, StkIdx num )
{
	/* WP: stack limit */
	ptrdiff_t stkoff, stkend;
	size_t nsz;
	StkIdx stksz = (StkIdx) ( C->clstk_top - C->clstk_base );
	sgs_BreakIf( num < 0 );
	if( stksz + num <= (StkIdx) C->clstk_mem )
		return;
	sgs_BreakIf( stksz + num < 0 ); /* overflow test */
	stkoff = C->clstk_off - C->clstk_base;
	stkend = C->clstk_top - C->clstk_base;
	
	nsz = (size_t) ( stksz + num ) + C->clstk_mem * 2; /* MAX( stksz + num, C->clstk_mem * 2 ); */
	C->clstk_base = (sgs_Closure**) sgs_Realloc( C, C->clstk_base, CLSTK_UNITSIZE * nsz );
	C->clstk_mem = (uint32_t) nsz;
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
		cc->var.type = SVT_NULL;
		*C->clstk_top++ = cc;
		num--;
	}
}

static void clstk_clean( SGS_CTX, sgs_Closure** from, sgs_Closure** to )
{
	/* WP: stack limit */
	size_t oh = (size_t) ( C->clstk_top - to );
	sgs_Closure** p = from, **pend = to;
	sgs_BreakIf( C->clstk_top < to );
	sgs_BreakIf( to < from );
	sgs_BreakIf( from < C->clstk_base );
	
	while( p < pend )
	{
		closure_deref( C, *p );
		p++;
	}
	
	C->clstk_top -= to - from;
	
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


static sgs_Bool var_getbool( SGS_CTX, const sgs_VarPtr var )
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
		{
			int ret;
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( !O->iface->convert )
				return TRUE;
			_STACK_PROTECT;
			ret = O->iface->convert( C, O, SVT_BOOL );
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SVT_BOOL )
			{
				sgs_Bool v = stk_gettop( C )->data.B;
				_STACK_UNPROTECT;
				return v;
			}
			_STACK_UNPROTECT;
			return TRUE;
		}
	case SVT_PTR: return var->data.P != NULL;
	default: return FALSE;
	}
}

static sgs_Int var_getint( SGS_CTX, sgs_VarPtr var )
{
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Int) var->data.B;
	case SVT_INT: return var->data.I;
	case SVT_REAL: return (sgs_Int) var->data.R;
	case SVT_STRING: return util_atoi( str_cstr( var->data.S ), var->data.S->size );
	case SVT_OBJECT:
		{
			int ret;
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( !O->iface->convert )
				break;
			_STACK_PROTECT;
			ret = O->iface->convert( C, O, SVT_INT );
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SVT_INT )
			{
				sgs_Int v = stk_gettop( C )->data.I;
				_STACK_UNPROTECT;
				return v;
			}
			_STACK_UNPROTECT;
		}
		break;
	case SVT_PTR: return (sgs_Int) (size_t) var->data.P;
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
		{
			int ret;
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( !O->iface->convert )
				break;
			_STACK_PROTECT;
			ret = O->iface->convert( C, O, SVT_REAL );
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SVT_REAL )
			{
				sgs_Real v = stk_gettop( C )->data.R;
				_STACK_UNPROTECT;
				return v;
			}
			_STACK_UNPROTECT;
		}
		break;
	case SVT_PTR: return (sgs_Real) (size_t) var->data.P;
	}
	return 0;
}

static void* var_getptr( SGS_CTX, sgs_VarPtr var )
{
	switch( var->type )
	{
	case SVT_BOOL: return (void*) (size_t) var->data.B;
	case SVT_INT: return (void*) (size_t) var->data.I;
	case SVT_REAL: return (void*) (size_t) var->data.R;
	case SVT_STRING: return (void*) (size_t) str_cstr( var->data.S );
	case SVT_OBJECT:
		{
			int ret;
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( !O->iface->convert )
				break;
			_STACK_PROTECT;
			ret = O->iface->convert( C, O, SVT_PTR );
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SVT_PTR )
			{
				void* v = stk_gettop( C )->data.P;
				_STACK_UNPROTECT;
				return v;
			}
			_STACK_UNPROTECT;
			return O->data;
		}
	case SVT_PTR: return var->data.P;
	}
	return NULL;
}

static SGS_INLINE sgs_Int var_getint_simple( sgs_VarPtr var )
{
	switch( var->type )
	{
	case SVT_BOOL: return (sgs_Int) var->data.B;
	case SVT_INT: return var->data.I;
	case SVT_REAL: return (sgs_Int) var->data.R;
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

#define var_initnull( v ) \
do{ sgs_VarPtr __var = (v); __var->type = SVT_NULL; }while(0)
#define var_initbool( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SVT_BOOL; __var->data.B = value; }while(0)
#define var_initint( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SVT_INT; __var->data.I = value; }while(0)
#define var_initreal( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SVT_REAL; __var->data.R = value; }while(0)
#define var_initptr( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SVT_PTR; __var->data.P = value; }while(0)

#define var_setnull( C, v ) \
do{ sgs_VarPtr var = (v); VAR_RELEASE( var ); var->type = SVT_NULL; }while(0)
#define var_setbool( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SVT_BOOL ) \
	{ VAR_RELEASE( var ); var->type = SVT_BOOL; } var->data.B = value; }while(0)
#define var_setint( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SVT_INT ) \
	{ VAR_RELEASE( var ); var->type = SVT_INT; } var->data.I = value; }while(0)
#define var_setreal( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SVT_REAL ) \
	{ VAR_RELEASE( var ); var->type = SVT_REAL; } var->data.R = value; }while(0)
#define var_setptr( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SVT_PTR ) \
	{ VAR_RELEASE( var ); var->type = SVT_PTR; } var->data.P = value; }while(0)


static void init_var_string( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	char buf[ 32 ];
	out->type = SVT_NULL;
	out->data.S = NULL;
	switch( var->type )
	{
	case SVT_NULL: var_create_str( C, out, "null", 4 ); break;
	case SVT_BOOL: if( var->data.B ) var_create_str( C, out, "true", 4 ); else var_create_str( C, out, "false", 5 ); break;
	case SVT_INT: sprintf( buf, "%" PRId64, var->data.I ); var_create_str( C, out, buf, -1 ); break;
	case SVT_REAL: sprintf( buf, "%g", var->data.R ); var_create_str( C, out, buf, -1 ); break;
	case SVT_FUNC: var_create_str( C, out, "function", 8 ); break;
	case SVT_CFUNC: var_create_str( C, out, "C function", 10 ); break;
	case SVT_OBJECT:
		{
			int ret;
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( !O->iface->convert )
			{
				var_create_str( C, out, O->iface->name, -1 );
				break;
			}
			_STACK_PROTECT;
			if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
				ret = SGS_EINPROC;
			else
			{
				C->sf_count++;
				ret = O->iface->convert( C, O, SVT_STRING );
				C->sf_count--;
			}
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SVT_STRING )
			{
				*out = *stk_gettop( C );
				out->data.S->refcount++; /* cancel release from stack to transfer successfully */
				_STACK_UNPROTECT;
			}
			else
			{
				var_create_str( C, out, O->iface->name, -1 );
				_STACK_UNPROTECT;
			}
		}
		break;
	case SVT_PTR: sprintf( buf, "ptr(%p)", var->data.P ); var_create_str( C, out, buf, -1 ); break;
	}
	sgs_BreakIf( out->type != SVT_STRING );
}


static void vm_convert_bool( SGS_CTX, sgs_Variable* var )
{
	sgs_Bool value;
	if( var->type == SVT_BOOL )
		return;
	value = var_getbool( C, var );
	VAR_RELEASE( var );
	var_initbool( var, value );
}

static void vm_convert_int( SGS_CTX, sgs_Variable* var )
{
	sgs_Int value;
	if( var->type == SVT_INT )
		return;
	value = var_getint( C, var );
	VAR_RELEASE( var );
	var_initint( var, value );
}

static void vm_convert_real( SGS_CTX, sgs_Variable* var )
{
	sgs_Real value;
	if( var->type == SVT_REAL )
		return;
	value = var_getreal( C, var );
	VAR_RELEASE( var );
	var_initreal( var, value );
}

static void vm_convert_string( SGS_CTX, sgs_Variable* var )
{
	sgs_Variable out;
	if( var->type == SVT_STRING )
		return;
	init_var_string( C, &out, var );
	VAR_RELEASE( var );
	*var = out;
}

static void vm_convert_ptr( SGS_CTX, sgs_Variable* var )
{
	void* value;
	if( var->type == SVT_PTR )
		return;
	value = var_getptr( C, var );
	VAR_RELEASE( var );
	var_initptr( var, value );
}


#define CI stk_getpos( C, item )
static void vm_convert_stack_bool( SGS_CTX, StkIdx item )
{
	if( CI->type != SVT_BOOL )
	{
		sgs_Bool v = var_getbool( C, CI );
		var_setbool( C, CI, v );
	}
}

static void vm_convert_stack_int( SGS_CTX, StkIdx item )
{
	if( CI->type != SVT_INT )
	{
		sgs_Int v = var_getint( C, CI );
		var_setint( C, CI, v );
	}
}

static void vm_convert_stack_real( SGS_CTX, StkIdx item )
{
	if( CI->type != SVT_REAL )
	{
		sgs_Real v = var_getreal( C, CI );
		var_setreal( C, CI, v );
	}
}

static void vm_convert_stack_string( SGS_CTX, StkIdx item )
{
	sgs_Variable out;
	if( CI->type == SVT_STRING )
		return;
	init_var_string( C, &out, CI );
	stk_setvar_leave( C, item, &out );
}

static void vm_convert_stack_ptr( SGS_CTX, StkIdx item )
{
	if( CI->type != SVT_PTR )
	{
		void* v = var_getptr( C, CI );
		var_setptr( C, CI, v );
	}
}
#undef CI


/*
	VM mutation
*/

static SGSRESULT vm_gettype( SGS_CTX, StkIdx item )
{
	const char* ty = "ERROR";
	sgs_Variable* A;
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_EBOUNDS;
	A = stk_getpos( C, item );
	
	switch( A->type )
	{
	case SVT_NULL:   ty = "null"; break;
	case SVT_BOOL:   ty = "bool"; break;
	case SVT_INT:    ty = "int"; break;
	case SVT_REAL:   ty = "real"; break;
	case SVT_STRING: ty = "string"; break;
	case SVT_CFUNC:  ty = "cfunc"; break;
	case SVT_FUNC:   ty = "func"; break;
	case SVT_OBJECT:
		{
			sgs_VarObj* O = A->data.O;
			if( O->iface->convert )
			{
				int ret;
				_STACK_PREPARE;
				_STACK_PROTECT;
				ret = O->iface->convert( C, O, SGS_CONVOP_TYPEOF );
				if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
				{
					_STACK_UNPROTECT_SKIP( 1 );
					return SGS_SUCCESS;
				}
				_STACK_UNPROTECT;
			}
			ty = O->iface->name ? O->iface->name : "object";
		}
		break;
	case SVT_PTR:    ty = "pointer"; break;
	}
	
	sgs_PushString( C, ty );
	return SGS_SUCCESS;
}

static SGSRESULT vm_gcmark( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SVT_OBJECT ||
		var->data.O->redblue == C->redblue ||
		!var->data.O->iface->gcmark )
		return SGS_SUCCESS;
	else
	{
		int ret;
		sgs_VarObj* O = var->data.O;
		_STACK_PREPARE;
		O->redblue = C->redblue;
		_STACK_PROTECT;
		ret = O->iface->gcmark( C, O );
		_STACK_UNPROTECT;
		return ret;
	}
}

/*
	Object property / array accessor handling
*/

int sgs_specfn_call( SGS_CTX )
{
	int ret;
	int method_call = sgs_Method( C );
	SGSFN( "call" );
	if( !sgs_IsCallable( C, 0 ) )
		return sgs_Msg( C, SGS_WARNING, "not called on function type" );
	if( sgs_StackSize( C ) < 2 )
	{
		sgs_Msg( C, SGS_WARNING, method_call ?
			"at least one argument expected (this)" :
			"at least two arguments expected (function, this)" );
		return 0;
	}
	
	sgs_PushItem( C, 0 );
	ret = sgs_ThisCall( C, sgs_StackSize( C ) - 3, C->sf_last->expected );
	if( ret != SGS_SUCCESS )
		return sgs_Msg( C, SGS_WARNING, "failed with error %d", ret );
	return C->sf_last->expected;
}

int sgs_specfn_apply( SGS_CTX )
{
	int ret;
	int method_call = sgs_Method( C );
	sgs_SizeVal i, asize;
	SGSFN( "apply" );
	if( !sgs_IsCallable( C, 0 ) )
		return sgs_Msg( C, SGS_WARNING, "not called on function type" );
	if( ( asize = sgs_ArraySize( C, 2 ) ) < 0 )
		return sgs_Msg( C, SGS_WARNING, "argument %d is not an array", method_call ? 2 : 3 );
	if( sgs_StackSize( C ) < 3 )
	{
		return sgs_Msg( C, SGS_WARNING, method_call ?
			"two argument expected (this, args)" :
			"three arguments expected (function, this, args)" );
	}
	
	for( i = 0; i < asize; ++i )
		sgs_PushNumIndex( C, 2, i );
	sgs_PushItem( C, 0 );
	ret = sgs_ThisCall( C, asize, C->sf_last->expected );
	if( ret != SGS_SUCCESS )
		return sgs_Msg( C, SGS_WARNING, "failed with error %d", ret );
	return C->sf_last->expected;
}


static int vm_getidx_builtin( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx )
{
	int res;
	sgs_Int pos, size;
	if( obj->type == SVT_STRING )
	{
		size = obj->data.S->size;
		sgs_PushVariable( C, idx );
		res = sgs_ParseInt( C, -1, &pos );
		stk_pop1( C );
		if( !res )
		{
			sgs_Msg( C, SGS_WARNING, "Expected integer as string index" );
			return SGS_EINVAL;
		}
		if( pos >= size || pos < -size )
		{
			sgs_Msg( C, SGS_WARNING, "String index out of bounds" );
			return SGS_EBOUNDS;
		}
		pos = ( pos + size ) % size;
		sgs_InitStringBuf( C, outmaybe, var_cstr( obj ) + pos, 1 );
		return 0;
	}

	sgs_Msg( C, SGS_WARNING, "Cannot index variable of type '%s'", TYPENAME( obj->type ) );
	return SGS_ENOTFND;
}

static int vm_getprop_builtin( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx )
{
	if( idx->type == SVT_STRING )
	{
		const char* prop = var_cstr( idx );
		
		switch( obj->type )
		{
		case SVT_STRING:
			if( !strcmp( prop, "length" ) )
			{
				outmaybe->type = SVT_INT;
				outmaybe->data.I = obj->data.S->size;
				return 0;
			}
			break;
		case SVT_FUNC:
		case SVT_CFUNC:
			if( !strcmp( prop, "call" ) )
			{
				outmaybe->type = SVT_CFUNC;
				outmaybe->data.C = sgs_specfn_call;
				return 0;
			}
			if( !strcmp( prop, "apply" ) )
			{
				outmaybe->type = SVT_CFUNC;
				outmaybe->data.C = sgs_specfn_apply;
				return 0;
			}
			break;
		}
		
		sgs_Msg( C, SGS_WARNING, "Property '%s' not found on "
			"object of type '%s'", prop, TYPENAME( obj->type ) );
	}
	
	sgs_Msg( C, SGS_WARNING, "Property of type '%s' not found on "
		"object of type '%s'", TYPENAME( idx->type ), TYPENAME( obj->type ) );
	return SGS_ENOTFND;
}


extern int sgsstd_array_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* idx, int prop );
extern int sgsstd_dict_getindex( SGS_CTX, sgs_VarObj* data, sgs_Variable* idx, int prop );



static SGSRESULT vm_runerr_getprop( SGS_CTX, SGSRESULT type, StkIdx origsize, sgs_Variable* idx, int isprop )
{
	if( type == SGS_ENOTFND )
	{
		char* p;
		const char* err = isprop ? "Property not found" : "Cannot find value by index";
		stk_push( C, idx );
		p = sgs_ToString( C, -1 );
		sgs_Msg( C, SGS_WARNING, "%s: \"%s\"", err, p );
	}
	else if( type == SGS_EBOUNDS )
	{
		sgs_Msg( C, SGS_WARNING, "Index out of bounds" );
	}
	else if( type == SGS_EINVAL )
	{
		sgs_Msg( C, SGS_WARNING, "Invalid value type used for %s",
			isprop ? "property read" : "index read" );
	}
	else if( type == SGS_EINPROC )
	{
		sgs_Msg( C, SGS_ERROR, "%s read process interrupted, possibly by infinite recursion",
			isprop ? "Property" : "Index" );
	}
	else
	{
		sgs_Msg( C, SGS_WARNING, "Unknown error on %s",
			isprop ? "property read" : "index read" );
	}
	
	stk_pop( C, STACKFRAMESIZE - origsize );
	return type;
}
#define VM_GETPROP_ERR( type ) vm_runerr_getprop( C, type, origsize, idx, isprop )

static SGSRESULT vm_runerr_setprop( SGS_CTX, SGSRESULT type, StkIdx origsize, sgs_Variable* idx, int isprop )
{
	if( type == SGS_ENOTFND )
	{
		char* p;
		const char* err = isprop ? "Property not found" : "Cannot find value by index";
		stk_push( C, idx );
		p = sgs_ToString( C, -1 );
		sgs_Msg( C, SGS_WARNING, "%s: \"%s\"", err, p );
	}
	else if( type == SGS_EBOUNDS )
	{
		sgs_Msg( C, SGS_WARNING, "Index out of bounds" );
	}
	else if( type == SGS_EINVAL )
	{
		sgs_Msg( C, SGS_WARNING, "Invalid value type used for %s",
			isprop ? "property write" : "index write" );
	}
	else if( type == SGS_EINPROC )
	{
		sgs_Msg( C, SGS_ERROR, "%s write process interrupted, possibly by infinite recursion",
			isprop ? "Property" : "Index" );
	}
	else
	{
		sgs_Msg( C, SGS_WARNING, "Unknown error on %s",
			isprop ? "property write" : "index write" );
	}
	
	stk_pop( C, STACKFRAMESIZE - origsize );
	return type;
}
#define VM_SETPROP_ERR( type ) vm_runerr_setprop( C, type, origsize, idx, isprop )


#define var_setvar( C, to, from ) do{ *(to) = *(from); var_acquire( C, (to) ); }while(0)

/* VM_GETPROP
- two output states:
-- 1 is returned, there is a value in the stack
-- 0 is returned, valmaybe receives data
- if error is returned, no value is available anywhere */
#define VM_GETPROP_RETTOP( ret, ptr ) \
	do{ if( !ret ){ stk_push_leave( C, (ptr) ); } }while(0)
#define VM_GETPROP_RETIDX( ret, ptr, idx ) \
	do{ if( ret ){ stk_setlvar_leave( C, idx, stk_gettop( C ) ); stk_pop1nr( C ); } \
	else{ stk_setlvar_leave( C, idx, (ptr) ); } }while(0)
#define VM_GETPROP_RETPTR( ret, ptr ) \
	do{ if( ret ){ *(ptr) = *stk_gettop( C ); VAR_ACQUIRE( (ptr) ); stk_pop1( C ); } }while(0)

static SGSMIXED vm_getprop( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	int ret = SGS_ENOTSUP, isobj = obj->type == SVT_OBJECT;
	StkIdx origsize = STACKFRAMESIZE;
	
	if( isobj && obj->data.O->iface == sgsstd_dict_iface )
	{
		VHTable* ht = (VHTable*) obj->data.O->data;
		if( idx->type == SVT_INT && isprop )
		{
			int32_t off = (int32_t) idx->data.I;
			if( off < 0 || off >= vht_size( ht ) )
				return VM_GETPROP_ERR( SGS_EBOUNDS );
			else
			{
				var_setvar( C, outmaybe, &ht->vars[ off ].val );
				return 0;
			}
		}
		else if( idx->type == SVT_STRING )
		{
			VHTVar* var = vht_get( ht, idx );
			if( !var )
				return VM_GETPROP_ERR( SGS_ENOTFND );
			else
			{
				var_setvar( C, outmaybe, &var->val );
				return 0;
			}
		}
		else
		{
			stk_push( C, idx );
			if( !sgs_ToString( C, -1 ) )
				return VM_GETPROP_ERR( SGS_EINVAL );
			else
			{
				VHTVar* var = vht_get( ht, stk_gettop( C ) );
				if( !var )
					return VM_GETPROP_ERR( SGS_ENOTFND );
				else
				{
					var_setvar( C, outmaybe, &var->val );
					stk_pop1( C );
					return 0;
				}
			}
		}
	}
	else if( isobj && obj->data.O->iface == sgsstd_array_iface )
	{
		sgs_Variable idxvar = *idx;
		VAR_ACQUIRE( &idxvar );
		ret = sgsstd_array_getindex( C, obj->data.O, &idxvar, isprop );
		VAR_RELEASE( &idxvar );
		if( SGS_SUCCEEDED( ret ) )
			ret = 1;
		/* assuming that on success, the stack has one additional value on top */
	}
	else if( isobj && obj->data.O->iface == sgsstd_map_iface )
	{
		VHTVar* var;
		VHTable* ht = (VHTable*) obj->data.O->data;
		/* vht_get does not modify search key */
		var = vht_get( ht, idx );
		
		if( !var )
			return VM_GETPROP_ERR( SGS_ENOTFND );
		else
		{
			var_setvar( C, outmaybe, &var->val );
			return 0;
		}
	}
	else if( isobj && obj->data.O->iface->getindex )
	{
		sgs_VarObj* O = obj->data.O;
		sgs_Variable idxvar = *idx;
		_STACK_PREPARE;
		
		if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
			return SGS_EINPROC;
		C->sf_count++;
		
		_STACK_PROTECT;
		VAR_ACQUIRE( &idxvar );
		ret = O->iface->getindex( C, O, &idxvar, isprop );
		VAR_RELEASE( &idxvar );
		
		C->sf_count--;
		if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
		{
			_STACK_UNPROTECT_SKIP( 1 );
			ret = 1;
		}
		else
		{
			_STACK_UNPROTECT;
			ret = SGS_ENOTFND;
		}
	}
	else
	{
		return isprop ?
			vm_getprop_builtin( C, outmaybe, obj, idx ) :
			vm_getidx_builtin( C, outmaybe, obj, idx );
	}
	
	if( SGS_FAILED( ret ) )
		return VM_GETPROP_ERR( ret );
	return ret;
}

static void vm_getprop_safe( SGS_CTX, sgs_StkIdx out, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	sgs_Variable tmp;
	SGSRESULT res = vm_getprop( C, &tmp, obj, idx, isprop );
	if( SGS_FAILED( res ) )
	{
		stk_setlvar_null( C, out );
		return;
	}
	VM_GETPROP_RETIDX( res, &tmp, out );
}

static SGSRESULT vm_setprop( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* src, int isprop )
{
	int ret;
	StkIdx origsize = STACKFRAMESIZE;
	
	if( isprop && idx->type != SVT_INT && idx->type != SVT_STRING )
	{
		ret = SGS_EINVAL;
	}
	else if( obj->type == SVT_OBJECT && obj->data.O->iface->setindex )
	{
		sgs_Variable idxvar = *idx;
		sgs_Variable srcvar = *src;
		sgs_VarObj* O = obj->data.O;
		_STACK_PREPARE;
		
		if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
			return SGS_EINPROC;
		C->sf_count++;
		
		_STACK_PROTECT;
		VAR_ACQUIRE( &idxvar );
		VAR_ACQUIRE( &srcvar );
		ret = O->iface->setindex( C, O, &idxvar, &srcvar, isprop );
		VAR_RELEASE( &idxvar );
		VAR_RELEASE( &srcvar );
		
		C->sf_count--;
		_STACK_UNPROTECT;
	}
	else
		ret = SGS_ENOTSUP;
	
	if( ret != SGS_SUCCESS )
		return VM_SETPROP_ERR( ret );
	
	stk_pop( C, STACKFRAMESIZE - origsize );
	return ret;
}


/*
	OPs
*/

static SGSRESULT vm_clone( SGS_CTX, sgs_Variable* var )
{
	/*
		strings are supposed to be immutable
		(even though C functions can accidentally
		or otherwise modify them with relative ease)
	*/
	if( var->type == SVT_OBJECT )
	{
		int ret = SGS_ENOTFND;
		sgs_VarObj* O = var->data.O;
		if( O->iface->convert )
		{
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->convert( C, O, SGS_CONVOP_CLONE );
			_STACK_UNPROTECT_SKIP( SGS_FAILED( ret ) ? 0 : 1 );
		}
		if( SGS_FAILED( ret ) )
			return ret;
	}
	else
	{
		/* even though functions are immutable, they're also impossible to modify,
			thus there is little need for showing an error when trying to convert one,
			especially if it's a part of some object to be cloned */
		stk_push( C, var );
	}
	return SGS_SUCCESS;
}

static SGSBOOL vm_op_concat_ex( SGS_CTX, StkIdx args )
{
	StkIdx i;
	uint32_t totsz = 0, curoff = 0;
	sgs_Variable N;
	if( args < 2 )
		return 1;
	if( STACKFRAMESIZE < args )
		return 0;
	for( i = 1; i <= args; ++i )
	{
		vm_convert_stack_string( C, -i );
		totsz += stk_getpos( C, -i )->data.S->size;
	}
	var_create_0str( C, &N, totsz );
	for( i = args; i >= 1; --i )
	{
		sgs_Variable* var = stk_getpos( C, -i );
		memcpy( var_cstr( &N ) + curoff, var_cstr( var ), var->data.S->size );
		curoff += var->data.S->size;
	}
	stk_setvar_leave( C, -args, &N );
	stk_pop( C, args - 1 );
	return 1;
}

static void vm_op_concat( SGS_CTX, StkIdx out, sgs_Variable* A, sgs_Variable* B )
{
	sgs_Variable lA = *A, lB = *B;
	int ssz = STACKFRAMESIZE;
	stk_push( C, &lA );
	stk_push( C, &lB );
	vm_op_concat_ex( C, 2 );
	stk_setlvar( C, out, stk_gettop( C ) );
	stk_pop( C, STACKFRAMESIZE - ssz );
}

static SGSBOOL vm_op_negate( SGS_CTX, sgs_Variable* out, sgs_Variable* A )
{
	VAR_RELEASE( out );
	switch( A->type )
	{
	case SVT_NULL: /* guaranteed to be NULL after release */ break;
	case SVT_BOOL: var_initint( out, -A->data.B ); break;
	case SVT_INT: var_initint( out, -A->data.I ); break;
	case SVT_REAL: var_initreal( out, -A->data.R ); break;
	case SVT_OBJECT:
		{
			int ret = SGS_ENOTFND;
			sgs_VarObj* O = A->data.O;
			/* WP: stack limit */
			if( O->iface->expr )
			{
				_STACK_PREPARE;
				StkIdx ofs = (StkIdx) ( out - C->stack_off );
				_STACK_PROTECT;
				ret = O->iface->expr( C, O, A, NULL, SGS_EOP_NEGATE );
				if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
				{
					C->stack_off[ ofs ] = *stk_gettop( C );
					stk_pop1nr( C );
				}
				_STACK_UNPROTECT;
			}
			if( SGS_FAILED( ret ) )
			{
				sgs_Msg( C, SGS_ERROR, "Given object does not support negation." );
				/* guaranteed to be NULL after release */
			}
		}
		return 0;
	default:
		sgs_Msg( C, SGS_WARNING, "Negating variable of type %s is not supported.", TYPENAME( A->type ) );
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
		case SVT_INT: var_setint( C, out, A->data.I op ); break; \
		case SVT_REAL: var_setreal( C, out, A->data.R op ); break; \
		default: var_setnull( C, out ); \
			sgs_Msg( C, SGS_ERROR, "Cannot " #pfx "rement non-numeric variables!" ); break; \
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
	
	if( a->type == SVT_OBJECT || b->type == SVT_OBJECT )
	{
		int ret;
		sgs_Variable lA = *a, lB = *b;
		VAR_ACQUIRE( &lA );
		VAR_ACQUIRE( &lB );
		/* WP: stack limit */
		StkIdx ofs = (StkIdx) ( out - C->stack_off );
		
		if( a->type == SVT_OBJECT && a->data.O->iface->expr )
		{
			sgs_VarObj* O = a->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, op );
			USING_STACK
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
				stk_setlvar( C, ofs, C->stack_top - 1 );
			_STACK_UNPROTECT;
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
			{
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return;
			}
		}
		
		if( b->type == SVT_OBJECT && b->data.O->iface->expr )
		{
			sgs_VarObj* O = b->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, op );
			USING_STACK
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
				stk_setlvar( C, ofs, C->stack_top - 1 );
			_STACK_UNPROTECT;
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
			{
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return;
			}
		}
		
		VAR_RELEASE( &lA );
		VAR_RELEASE( &lB );
		goto fail;
	}
	
	/* if either variable is of a basic callable type */
	if( a->type == SVT_FUNC || a->type == SVT_CFUNC ||
		b->type == SVT_FUNC || b->type == SVT_CFUNC ||
		a->type == SVT_PTR  || b->type == SVT_PTR   )
		goto fail;
	
	/* if either are REAL or STRING */
	if( a->type == SVT_REAL || b->type == SVT_STRING ||
		a->type == SVT_STRING || b->type == SVT_REAL )
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
	VAR_RELEASE( out );
	sgs_Msg( C, SGS_ERROR, "Division by 0" );
	return;
fail:
	VAR_RELEASE( out );
	sgs_Msg( C, SGS_ERROR, "Specified arithmetic operation is not supported on the given set of arguments" );
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
#define _SGS_SIGNDIFF( a, b ) ((a)==(b)?0:((a)<(b)?-1:1))
static int vm_compare( SGS_CTX, sgs_VarPtr a, sgs_VarPtr b )
{
	const uint32_t ta = a->type, tb = b->type;
	
	/* both are INT */
	if( ta == SVT_INT && tb == SVT_INT ) return _SGS_SIGNDIFF( a->data.I, b->data.I );
	/* both are REAL */
	if( ta == SVT_REAL && tb == SVT_REAL ) return _SGS_SIGNDIFF( a->data.R, b->data.R );
	
	/* either is OBJECT */
	if( ta == SVT_OBJECT || tb == SVT_OBJECT )
	{
		int ret = SGS_ENOTSUP;
		sgs_Int out = _SGS_SIGNDIFF( ta, tb );
		sgs_Variable lA = *a, lB = *b;
		VAR_ACQUIRE( &lA );
		VAR_ACQUIRE( &lB );
		
		if( ta == SVT_OBJECT && a->data.O->iface->expr )
		{
			sgs_VarObj* O = a->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, SGS_EOP_COMPARE );
			USING_STACK
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
				out = var_getint( C, C->stack_top - 1 );
			_STACK_UNPROTECT;
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
			{
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return _SGS_SIGNDIFF( out, 0 );
			}
		}
		
		if( tb == SVT_OBJECT && b->data.O->iface->expr )
		{
			sgs_VarObj* O = b->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, SGS_EOP_COMPARE );
			USING_STACK
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
				out = var_getint( C, C->stack_top - 1 );
			_STACK_UNPROTECT;
			if( SGS_SUCCEEDED( ret ) && STACKFRAMESIZE >= 1 )
			{
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return _SGS_SIGNDIFF( out, 0 );
			}
		}
		
		VAR_RELEASE( &lA );
		VAR_RELEASE( &lB );
		/* fallback: check for equality */
		if( ta == tb )
			return _SGS_SIGNDIFF( a->data.O, b->data.O );
		else
			return _SGS_SIGNDIFF( ta, tb );
	}
	
	/* both are FUNC/CFUNC */
	if( ( ta == SVT_FUNC || ta == SVT_CFUNC ) &&
		( tb == SVT_FUNC || tb == SVT_CFUNC ) )
	{
		if( ta != tb )
			return _SGS_SIGNDIFF( ta, tb );
		if( ta == SVT_FUNC )
			return _SGS_SIGNDIFF( a->data.F, b->data.F );
		else
			return _SGS_SIGNDIFF( a->data.C, b->data.C );
	}
	
	/* both are STRING */
	if( ta == SVT_STRING && tb == SVT_STRING )
	{
		ptrdiff_t out;
		sgs_Variable A = *a, B = *b;
		stk_push( C, &A );
		stk_push( C, &B );
		vm_convert_stack_string( C, -2 );
		vm_convert_stack_string( C, -1 );
		a = stk_getpos( C, -2 );
		b = stk_getpos( C, -1 );
		out = memcmp( var_cstr( a ), var_cstr( b ), MIN( a->data.S->size, b->data.S->size ) );
		if( out == 0 && a->data.S->size != b->data.S->size )
			out = (ptrdiff_t) ( a->data.S->size - b->data.S->size );
		stk_pop2( C );
		return _SGS_SIGNDIFF( out, 0 );
	}
	
	/* default comparison */
	{
		sgs_Real ra = var_getreal( C, a );
		sgs_Real rb = var_getreal( C, b );
		return _SGS_SIGNDIFF( ra, rb );
	}
}


static int vm_forprep( SGS_CTX, StkIdx outiter, sgs_VarPtr obj )
{
	int ret = SGS_ENOTSUP;
	sgs_VarObj* O = obj->data.O;
	
	VAR_RELEASE( stk_getlpos( C, outiter ) );
	
	if( obj->type != SVT_OBJECT )
	{
		sgs_Msg( C, SGS_ERROR, "Variable of type '%s' "
			"doesn't have an iterator", TYPENAME( obj->type ) );
		return SGS_ENOTSUP;
	}
	
	if( O->iface->convert )
	{
		_STACK_PREPARE;
		_STACK_PROTECT;
		ret = SGS_SUCCEEDED( O->iface->convert( C, O, SGS_CONVOP_TOITER ) ) && STACKFRAMESIZE >= 1;
		_STACK_UNPROTECT_SKIP( ret );
		if( ret )
		{
			stk_setlvar_leave( C, outiter, stk_gettop( C ) );
			stk_pop1nr( C );
			ret = SGS_SUCCESS;
		}
		else
			ret = SGS_EINPROC;
	}
	
	if( SGS_FAILED( ret ) )
	{
		sgs_Msg( C, SGS_ERROR, "Object '%s' doesn't have an iterator", obj->data.O->iface->name );
		return SGS_ENOTSUP;
	}
	return SGS_SUCCESS;
}

static SGSMIXED vm_fornext( SGS_CTX, StkIdx outkey, StkIdx outval, sgs_VarPtr iter )
{
	StkIdx expargs = 0;
	int flags = 0, ret = SGS_ENOTSUP;
	sgs_VarObj* O = iter->data.O;
	_STACK_PREPARE;
	
	if( iter->type != SVT_OBJECT )
	{
		sgs_Msg( C, SGS_ERROR, "Iterator is not an object" );
		return SGS_EINVAL;
	}
	
	if( O->iface == sgsstd_array_iter_iface )
	{
		sgsstd_array_iter_t* it = (sgsstd_array_iter_t*) O->data;
		sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) it->ref.data.O->data;
		if( it->size != hdr->size )
			return SGS_EINPROC;
		else if( outkey < 0 && outval < 0 )
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
	
	if( outkey >= 0 ){ flags |= SGS_GETNEXT_KEY; expargs++; }
	if( outval >= 0 ){ flags |= SGS_GETNEXT_VALUE; expargs++; }
	
	_STACK_PROTECT;
	if( O->iface->getnext )
		ret = O->iface->getnext( C, O, flags );
	if( SGS_FAILED( ret ) || STACKFRAMESIZE < expargs )
	{
		_STACK_UNPROTECT;
		sgs_Msg( C, SGS_ERROR, "Failed to retrieve data from iterator" );
		return ret;
	}
	_STACK_UNPROTECT_SKIP( expargs );
	
	if( !flags )
		return ret;
	else
	{
		if( outkey >= 0 ) stk_setlvar( C, outkey, stk_getpos( C, -2 + (outval<0) ) );
		if( outval >= 0 ) stk_setlvar( C, outval, stk_gettop( C ) );
		stk_pop( C, expargs );
	}
	
	return SGS_SUCCESS;
}


static void vm_make_array( SGS_CTX, int args, int outpos )
{
	int ret;
	sgs_Variable arr;
	sgs_BreakIf( sgs_StackSize( C ) < args );
	ret = sgsSTD_MakeArray( C, &arr, args );
	sgs_BreakIf( ret != SGS_SUCCESS );
	UNUSED( ret );
	
	stk_setvar_leave( C, outpos, &arr );
}

static void vm_make_dict( SGS_CTX, int args, int outpos )
{
	int ret;
	sgs_Variable arr;
	sgs_BreakIf( sgs_StackSize( C ) < args );
	ret = sgsSTD_MakeDict( C, &arr, args );
	sgs_BreakIf( ret != SGS_SUCCESS );
	UNUSED( ret );
	
	stk_setvar_leave( C, outpos, &arr );
}

static void vm_make_closure( SGS_CTX, int args, sgs_Variable* func, int16_t outpos )
{
	int ret;
	sgs_BreakIf( C->clstk_top - C->clstk_off < args );
	/* WP: range not affected by conversion */
	ret = sgsSTD_MakeClosure( C, func, (uint32_t) args );
	sgs_BreakIf( ret != SGS_SUCCESS );
	UNUSED( ret );
	
	stk_setvar_leave( C, outpos, stk_gettop( C ) );
	stk_pop1nr( C );
}


static int vm_exec( SGS_CTX, sgs_Variable* consts, rcpos_t constcount );


/*
	Call the virtual machine.
	Args must equal the number of arguments pushed before the function
	
	- function expects the following items on stack: [this][args]
	- before call, stack is set up to display only the [args] part
	- [this] can be uncovered with sgs_Method
	- return value count is checked against the active range at the moment of return
	- upon return, this function replaces [this][args] with [expect]
*/
static int vm_call( SGS_CTX, int args, int clsr, int gotthis, int expect, sgs_Variable* func )
{
	sgs_Variable V = *func;
	ptrdiff_t stkcallbase,
		stkoff = C->stack_off - C->stack_base,
		clsoff = C->clstk_off - C->clstk_base;
	int rvc = 0, ret = 1, allowed;
	
	gotthis = !!gotthis;
	stkcallbase = C->stack_top - args - gotthis - C->stack_base;
	
	sgs_BreakIf( STACKFRAMESIZE < args + gotthis );
	allowed = vm_frame_push( C, &V, NULL, NULL, 0 );
	C->stack_off = C->stack_top - args;
	C->clstk_off = C->clstk_top - clsr;
	
	if( allowed )
	{
		C->sf_last->argbeg = C->stack_off - C->stack_base;
		C->sf_last->argend = C->stack_top - C->stack_base;
		/* WP: argument count limit */
		C->sf_last->argcount = (uint8_t) args;
		C->sf_last->inexp = (uint8_t) args;
		/* WP: returned value count limit */
		C->sf_last->expected = (uint8_t) expect;
		C->sf_last->flags = gotthis ? SGS_SF_METHOD : 0;
		
		if( func->type == SVT_CFUNC )
		{
			rvc = (*func->data.C)( C );
			if( rvc > STACKFRAMESIZE )
			{
				sgs_Msg( C, SGS_ERROR, "Function returned more variables than there was on the stack" );
				rvc = 0;
				ret = 0;
			}
			if( rvc < 0 )
			{
				sgs_Msg( C, SGS_ERROR, "The function could not be called" );
				rvc = 0;
				ret = 0;
			}
		}
		else if( func->type == SVT_FUNC )
		{
			func_t* F = func->data.F;
			int stkargs = args + ( F->gotthis && gotthis );
			int expargs = F->numargs + F->gotthis;
			
			C->sf_last->inexp = F->numargs;
			
			/* add flag to specify presence of "this" */
			if( F->gotthis )
				C->sf_last->flags |= SGS_SF_HASTHIS;
			/* fix argument stack */
			if( stkargs > expargs )
			{
				stk_transpose( C, expargs, stkargs );
				C->stack_off += stkargs - expargs;
			}
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
				/* WP: const limit */
				rcpos_t constcnt = (rcpos_t) ( F->instr_off / sizeof( sgs_Variable* ) );
				rvc = vm_exec( C, func_consts( F ), constcnt );
			}
			if( F->gotthis && gotthis ) C->stack_off++;
		}
		else if( func->type == SVT_OBJECT )
		{
			sgs_VarObj* O = func->data.O;
			
			rvc = SGS_ENOTSUP;
			if( O->iface->call )
				rvc = O->iface->call( C, O );
			
			if( rvc > STACKFRAMESIZE )
			{
				sgs_Msg( C, SGS_ERROR, "Object returned more variables than there was on the stack" );
				rvc = 0;
				ret = 0;
			}
			if( rvc < 0 )
			{
				sgs_Msg( C, SGS_ERROR, "The object could not be called" );
				rvc = 0;
				ret = 0;
			}
		}
		else
		{
			sgs_Msg( C, SGS_ERROR, "Variable of type '%s' "
				"cannot be called", TYPENAME( func->type ) );
			ret = 0;
		}
	}

	/* remove all stack items before the returned ones */
	stk_clean( C, C->stack_base + stkcallbase, C->stack_top - rvc );
	C->stack_off = C->stack_base + stkoff;
	
	/* remove all closures used in the function */
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
static SGS_INLINE sgs_Variable* const_getvar( sgs_Variable* consts, rcpos_t count, rcpos_t off )
{
	sgs_BreakIf( off >= count );
	return consts + off;
}
#endif

/*
	Main VM execution loop
*/
static int vm_exec( SGS_CTX, sgs_Variable* consts, rcpos_t constcount )
{
	sgs_StackFrame* SF = C->sf_last;
	int32_t ret = 0;
	sgs_Variable* cptr = consts;
	const instr_t* pend = SF->iend;

#if SGS_DEBUG && SGS_DEBUG_VALIDATE
	ptrdiff_t stkoff = C->stack_top - C->stack_off;
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

		case SI_RETN:
		{
			ret = argA;
			sgs_BreakIf( ( C->stack_top - C->stack_off ) - stkoff < ret &&
				"internal error: stack was corrupted" );
			pp = pend;
			break;
		}

		case SI_JUMP:
		{
			int16_t off = (int16_t) argE;
			pp += off;
			sgs_BreakIf( pp+1 > pend || pp+1 < SF->code );
			break;
		}

		case SI_JMPT:
		{
			int16_t off = (int16_t) argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}
		case SI_JMPF:
		{
			int16_t off = (int16_t) argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( !var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}

		case SI_CALL:
		{
			sgs_Variable fnvar = *stk_getlpos( C, argC );
			int i, expect = argA, args_from = argB & 0xff, args_to = argC;
			int gotthis = ( argB & 0x100 ) != 0;
			
			stk_makespace( C, args_to - args_from );
			for( i = args_from; i < args_to; ++i )
			{
				*C->stack_top++ = C->stack_off[ i ];
				VAR_ACQUIRE( C->stack_off + i );
			}
			
			vm_call( C, args_to - args_from - gotthis, 0, gotthis, expect, &fnvar );
			
			if( expect )
			{
				for( i = expect - 1; i >= 0; --i )
					stk_setlvar( C, args_from + i, C->stack_top - expect + i );
				stk_pop( C, expect );
			}
			break;
		}

		case SI_FORPREP: vm_forprep( C, argA, RESVAR( argB ) ); break;
		case SI_FORLOAD: vm_fornext( C, argB < 0x100 ? (int)argB : -1, argC < 0x100 ? (int)argC : -1, RESVAR( argA ) ); break;
		case SI_FORJUMP:
		{
			rcpos_t off = argE;
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
		case SI_GETPROP: { ARGS_3; vm_getprop_safe( C, a1, p2, p3, TRUE ); break; }
		case SI_SETPROP: { ARGS_3; vm_setprop( C, p1, p2, p3, TRUE ); break; }
		case SI_GETINDEX: { ARGS_3; vm_getprop_safe( C, a1, p2, p3, FALSE ); break; }
		case SI_SETINDEX: { ARGS_3; vm_setprop( C, p1, p2, p3, FALSE ); break; }
		
		case SI_GENCLSR: clstk_push_nulls( C, argA ); break;
		case SI_PUSHCLSR: clstk_push( C, clstk_get( C, argA ) ); break;
		case SI_MAKECLSR: vm_make_closure( C, argC, RESVAR( argB ), (int16_t) argA ); clstk_pop( C, argC ); break;
		case SI_GETCLSR: stk_setlvar( C, argA, &clstk_get( C, argB )->var ); break;
		case SI_SETCLSR: { sgs_VarPtr p3 = RESVAR( argC ), cv = &clstk_get( C, argB )->var;
			VAR_RELEASE( cv ); *cv = *p3; VAR_ACQUIRE( RESVAR( argC ) ); } break;

		case SI_SET: { ARGS_2; VAR_RELEASE( p1 ); *p1 = *p2; VAR_ACQUIRE( p1 ); break; }
		case SI_MCONCAT: { vm_op_concat_ex( C, argB ); stk_setlvar_leave( C, argA, stk_gettop( C ) ); stk_pop1nr( C ); break; }
		case SI_CONCAT: { ARGS_3; vm_op_concat( C, a1, p2, p3 ); break; }
		case SI_NEGATE: { ARGS_2; vm_op_negate( C, p1, p2 ); break; }
		case SI_BOOL_INV: { ARGS_2; vm_op_boolinv( C, (int16_t) a1, p2 ); break; }
		case SI_INVERT: { ARGS_2; vm_op_invert( C, (int16_t) a1, p2 ); break; }

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

		case SI_AND: { ARGS_3; vm_op_and( C, (int16_t) a1, p2, p3 ); break; }
		case SI_OR: { ARGS_3; vm_op_or( C, (int16_t) a1, p2, p3 ); break; }
		case SI_XOR: { ARGS_3; vm_op_xor( C, (int16_t) a1, p2, p3 ); break; }
		case SI_LSH: { ARGS_3; vm_op_lsh( C, (int16_t) a1, p2, p3 ); break; }
		case SI_RSH: { ARGS_3; vm_op_rsh( C, (int16_t) a1, p2, p3 ); break; }

#define STRICTLY_EQUAL( val ) if( p2->type != p3->type || ( p2->type == SVT_OBJECT && \
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
			sgs_Msg( C, SGS_ERROR, "Illegal instruction executed: 0x%08X", I );
			break;
		}
		sgs_BreakIf( STACKFRAMESIZE < stkoff );
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

static size_t funct_size( const func_t* f )
{
	size_t sz = f->size + f->funcname.mem + f->filename.mem;
	const sgs_Variable* beg = (const sgs_Variable*) (const void*) func_c_consts( f );
	const sgs_Variable* end = (const sgs_Variable*) (const void*) ASSUME_ALIGNED( func_c_bytecode( f ), 4 );
	while( beg < end )
		sz += sgsVM_VarSize( beg++ );
	return sz;
}

size_t sgsVM_VarSize( const sgs_Variable* var )
{
	size_t out;
	if( !var )
		return 0;

	out = sizeof( sgs_Variable );
	switch( var->type )
	{
	case SVT_FUNC: out += funct_size( var->data.F ); break;
	/* case SVT_OBJECT: break; */
	case SVT_STRING: out += var->data.S->size + sizeof( string_t ); break;
	}
	return out;
}

void sgsVM_VarDump( const sgs_Variable* var )
{
	/* WP: variable size limit */
	printf( "%s (size:%d)", TYPENAME( var->type ), (uint32_t) sgsVM_VarSize( var ) );
	switch( var->type )
	{
	case SVT_NULL: break;
	case SVT_BOOL: printf( " = %s", var->data.B ? "True" : "False" ); break;
	case SVT_INT: printf( " = %" PRId64, var->data.I ); break;
	case SVT_REAL: printf( " = %f", var->data.R ); break;
	case SVT_STRING: printf( " [rc:%"PRId32"] = \"", var->data.S->refcount );
		print_safe( stdout, var_cstr( var ), MIN( var->data.S->size, 16 ) );
		printf( var->data.S->size > 16 ? "...\"" : "\"" ); break;
	case SVT_FUNC: printf( " [rc:%"PRId32"]", var->data.F->refcount ); break;
	case SVT_CFUNC: printf( " = %p", (void*)(size_t) var->data.C ); break;
	case SVT_OBJECT: printf( "TODO [object impl]" ); break;
	case SVT_PTR: printf( " = %p", var->data.P ); break;
	}
}

void sgsVM_StackDump( SGS_CTX )
{
	ptrdiff_t i, stksz = C->stack_top - C->stack_base;
	/* WP: stack limit */
	printf( "STACK (size=%d, bytes=%d/%d)--\n", (int) stksz, (int)( stksz * (ptrdiff_t) STK_UNITSIZE ), (int)( C->stack_mem * STK_UNITSIZE ) );
	for( i = 0; i < stksz; ++i )
	{
		sgs_VarPtr var = C->stack_base + i;
		if( var == C->stack_off )
			printf( "-- offset --\n" );
		printf( "  " ); sgsVM_VarDump( var ); printf( "\n" );
	}
	printf( "--\n" );
}

int sgsVM_ExecFn( SGS_CTX, int numtmp, void* code, size_t codesize, void* data, size_t datasize, int clean, uint16_t* T )
{
	ptrdiff_t stkoff = C->stack_off - C->stack_base;
	int rvc = 0, allowed;
	allowed = vm_frame_push( C, NULL, T, (instr_t*) code, codesize / sizeof( instr_t ) );
	stk_push_nulls( C, numtmp );
	if( allowed )
	{
		/* WP: const limit */
		rvc = vm_exec( C, (sgs_Variable*) data, (rcpos_t) ( datasize / sizeof( sgs_Variable* ) ) );
	}
	C->stack_off = C->stack_base + stkoff;
	if( clean )
		stk_pop( C, (StkIdx) ( C->stack_top - C->stack_off ) ); /* WP: stack limit */
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
	
	OBJECT CREATION
	
*/

void sgs_InitNull( sgs_Variable* out )
{
	out->type = SVT_NULL;
}

void sgs_InitBool( sgs_Variable* out, sgs_Bool value )
{
	out->type = SVT_BOOL;
	out->data.B = value ? 1 : 0;
}

void sgs_InitInt( sgs_Variable* out, sgs_Int value )
{
	out->type = SVT_INT;
	out->data.I = value;
}

void sgs_InitReal( sgs_Variable* out, sgs_Real value )
{
	out->type = SVT_REAL;
	out->data.R = value;
}

void sgs_InitStringBuf( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal size )
{
	if( str )
		var_create_str( C, out, str, size );
	else
		var_create_0str( C, out, (uint32_t) size ); /* WP: string limit */
}

void sgs_InitString( SGS_CTX, sgs_Variable* out, const char* str )
{
	sgs_BreakIf( !str && "sgs_InitString: str = NULL" );
	var_create_str( C, out, str, -1 );
}

void sgs_InitCFunction( sgs_Variable* out, sgs_CFunc func )
{
	out->type = SVT_CFUNC;
	out->data.C = func;
}

void sgs_InitObject( SGS_CTX, sgs_Variable* out, void* data, sgs_ObjInterface* iface )
{
	var_create_obj( C, out, data, iface, 0 );
}

void* sgs_InitObjectIPA( SGS_CTX, sgs_Variable* out, uint32_t added, sgs_ObjInterface* iface )
{
	var_create_obj( C, out, NULL, iface, added );
	return out->data.O->data;
}

void sgs_InitPtr( sgs_Variable* out, void* ptr )
{
	out->type = SVT_PTR;
	out->data.P = ptr;
}

void sgs_InitObjectPtr( sgs_Variable* out, sgs_VarObj* obj )
{
	out->type = SVT_OBJECT;
	out->data.O = obj;
	VAR_ACQUIRE( out );
}


SGSRESULT sgs_InitArray( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems )
{
	return sgsSTD_MakeArray( C, out, numitems );
}

SGSRESULT sgs_InitDict( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems )
{
	return sgsSTD_MakeDict( C, out, numitems );
}

SGSRESULT sgs_InitMap( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems )
{
	return sgsSTD_MakeMap( C, out, numitems );
}


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
	var.type = SVT_BOOL;
	var.data.B = value ? 1 : 0;
	stk_push_leave( C, &var );
}

void sgs_PushInt( SGS_CTX, sgs_Int value )
{
	sgs_Variable var;
	var.type = SVT_INT;
	var.data.I = value;
	stk_push_leave( C, &var );
}

void sgs_PushReal( SGS_CTX, sgs_Real value )
{
	sgs_Variable var;
	var.type = SVT_REAL;
	var.data.R = value;
	stk_push_leave( C, &var );
}

void sgs_PushStringBuf( SGS_CTX, const char* str, sgs_SizeVal size )
{
	sgs_Variable var;
	if( str )
		var_create_str( C, &var, str, size );
	else
		var_create_0str( C, &var, (uint32_t) size ); /* WP: string limit */
	stk_push_leave( C, &var );
}

void sgs_PushString( SGS_CTX, const char* str )
{
	sgs_Variable var;
	sgs_BreakIf( !str && "sgs_PushString: str = NULL" );
	var_create_str( C, &var, str, -1 );
	stk_push_leave( C, &var );
}

void sgs_PushCFunction( SGS_CTX, sgs_CFunc func )
{
	sgs_Variable var;
	var.type = SVT_CFUNC;
	var.data.C = func;
	stk_push_leave( C, &var );
}

void sgs_PushObject( SGS_CTX, void* data, sgs_ObjInterface* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, data, iface, 0 );
	stk_push_leave( C, &var );
}

void* sgs_PushObjectIPA( SGS_CTX, uint32_t added, sgs_ObjInterface* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, NULL, iface, added );
	stk_push_leave( C, &var );
	return var.data.O->data;
}

void sgs_PushPtr( SGS_CTX, void* ptr )
{
	sgs_Variable var;
	var.type = SVT_PTR;
	var.data.P = ptr;
	stk_push_leave( C, &var );
}

void sgs_PushObjectPtr( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SVT_OBJECT;
	var.data.O = obj;
	stk_push( C, &var ); /* a new reference must be created */
}


SGSRESULT sgs_PushArray( SGS_CTX, sgs_SizeVal numitems )
{
	int res;
	sgs_Variable var;
	if( SGS_FAILED( res = sgsSTD_MakeArray( C, &var, numitems ) ) )
		return res;
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushDict( SGS_CTX, sgs_SizeVal numitems )
{
	int res;
	sgs_Variable var;
	if( SGS_FAILED( res = sgsSTD_MakeDict( C, &var, numitems ) ) )
		return res;
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushMap( SGS_CTX, sgs_SizeVal numitems )
{
	int res;
	sgs_Variable var;
	if( SGS_FAILED( res = sgsSTD_MakeMap( C, &var, numitems ) ) )
		return res;
	stk_push_leave( C, &var );
	return SGS_SUCCESS;
}


SGSRESULT sgs_PushVariable( SGS_CTX, sgs_Variable* var )
{
	if( var->type >= SGS_VT__COUNT )
		return SGS_EINVAL;
	stk_push( C, var );
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreVariable( SGS_CTX, sgs_Variable* var )
{
	if( !STACKFRAMESIZE )
		return SGS_ENOTFND;
	*var = *stk_gettop( C );
	VAR_ACQUIRE( var );
	stk_pop1( C );
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushItem( SGS_CTX, StkIdx item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_EBOUNDS;

	{
		sgs_Variable copy = *stk_getpos( C, item );
		stk_push( C, &copy );
		return SGS_SUCCESS;
	}
}

SGSRESULT sgs_StoreItem( SGS_CTX, StkIdx item )
{
	int ret;
	StkIdx g;
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


SGSRESULT sgs_InsertVariable( SGS_CTX, int pos, sgs_Variable* var )
{
	sgs_Variable* vp;
	if( pos > sgs_StackSize( C ) || pos < -sgs_StackSize( C ) - 1 )
		return SGS_EBOUNDS;
	if( pos < 0 )
		pos = sgs_StackSize( C ) + pos + 1;
	vp = stk_insert_pos( C, pos );
	*vp = *var;
	VAR_ACQUIRE( var );
	return SGS_SUCCESS;
}


#define _EL_BACKUP int32_t oel = C->minlev;
#define _EL_SETMAX C->minlev = INT32_MAX;
#define _EL_RESET C->minlev = oel;

SGSRESULT sgs_GetIndexPPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* out, int isprop )
{
	int ret;
	_EL_BACKUP;
	_EL_SETMAX;
	ret = vm_getprop( C, out, obj, idx, isprop );
	if( SGS_SUCCEEDED( ret ) )
		VM_GETPROP_RETPTR( ret, out );
	_EL_RESET;
	return SGS_FAILED( ret ) ? ret : SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexPPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* val, int isprop )
{
	int ret;
	_EL_BACKUP;
	_EL_SETMAX;
	ret = vm_setprop( C, obj, idx, val, isprop );
	_EL_RESET;
	return SGS_FAILED( ret ) ? ret : SGS_SUCCESS;
}

SGSRESULT sgs_PushIndexPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	int ret;
	sgs_Variable tmp;
	_EL_BACKUP;
	_EL_SETMAX;
	ret = vm_getprop( C, &tmp, obj, idx, isprop );
	if( SGS_SUCCEEDED( ret ) )
		VM_GETPROP_RETTOP( ret, &tmp );
	_EL_RESET;
	return SGS_FAILED( ret ) ? ret : SGS_SUCCESS;
}

SGSRESULT sgs_StoreIndexPP( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	int ret;
	sgs_Variable val;
	if( !sgs_GetStackItem( C, -1, &val ) )
		return SGS_EINPROC;
	ret = sgs_SetIndexPPP( C, obj, idx, &val, isprop );
	if( SGS_SUCCEEDED( ret ) )
		stk_pop1( C );
	return ret;
}


SGSRESULT sgs_GetIndexIPP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_Variable* out, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetIndexPPP( C, &vO, idx, out, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexPIP( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_Variable* out, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetIndexPPP( C, obj, &vI, out, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexIIP( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_Variable* out, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetIndexPPP( C, &vO, &vI, out, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexPPI( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_StkIdx out, int isprop )
{
	int res; sgs_Variable vV;
	if( ( res = sgs_GetIndexPPP( C, obj, idx, &vV, isprop ) ) ||
		( res = sgs_SetStackItem( C, out, &vV ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexIPI( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_StkIdx out, int isprop )
{
	int res; sgs_Variable vO, vV;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetIndexPPP( C, &vO, idx, &vV, isprop ) ) ||
		( res = sgs_SetStackItem( C, out, &vV ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexPII( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_StkIdx out, int isprop )
{
	int res; sgs_Variable vI, vV;
	if( ( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetIndexPPP( C, obj, &vI, &vV, isprop ) ) ||
		( res = sgs_SetStackItem( C, out, &vV ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexIII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_StkIdx out, int isprop )
{
	int res; sgs_Variable vO, vI, vV;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetIndexPPP( C, &vO, &vI, &vV, isprop ) ) ||
		( res = sgs_SetStackItem( C, out, &vV ) ) ) return res;
	return SGS_SUCCESS;
}


SGSRESULT sgs_SetIndexIPP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_Variable* val, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_SetIndexPPP( C, &vO, idx, val, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexPIP( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_Variable* val, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_SetIndexPPP( C, obj, &vI, val, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexIIP( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_Variable* val, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_SetIndexPPP( C, &vO, &vI, val, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexIPI( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_StkIdx val, int isprop )
{
	int res; sgs_Variable vO, vV;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, val, &vV ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_SetIndexPPP( C, &vO, idx, &vV, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexPII( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_StkIdx val, int isprop )
{
	int res; sgs_Variable vI, vV;
	if( ( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, val, &vV ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_SetIndexPPP( C, obj, &vI, &vV, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexIII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_StkIdx val, int isprop )
{
	int res; sgs_Variable vO, vI, vV;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, val, &vV ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_SetIndexPPP( C, &vO, &vI, &vV, isprop ) ) ) return res;
	return SGS_SUCCESS;
}


SGSRESULT sgs_PushIndexIP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_PushIndexPP( C, &vO, idx, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushIndexPI( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_PushIndexPP( C, obj, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushIndexII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_PushIndexPP( C, &vO, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIndexIP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_StoreIndexPP( C, &vO, idx, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIndexPI( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_StoreIndexPP( C, obj, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIndexII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_GetStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_GetStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) ||
		( res = sgs_StoreIndexPP( C, &vO, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}


SGSRESULT sgs_PushProperty( SGS_CTX, sgs_StkIdx obj, const char* name )
{
	int ret;
	if( !sgs_IsValidIndex( C, obj ) )
		return SGS_EBOUNDS;
	obj = stk_absindex( C, obj );
	sgs_PushString( C, name );
	ret = sgs_PushIndexII( C, obj, -1, TRUE );
	stk_popskip( C, 1, SGS_SUCCEEDED( ret ) ? 1 : 0 );
	return ret;
}

SGSRESULT sgs_StoreProperty( SGS_CTX, StkIdx obj, const char* name )
{
	int ret;
	if( !sgs_IsValidIndex( C, obj ) )
		return SGS_EBOUNDS;
	obj = stk_absindex( C, obj );
	sgs_PushString( C, name );
	ret = sgs_SetIndexIII( C, obj, -1, -2, TRUE );
	stk_pop( C, ret == SGS_SUCCESS ? 2 : 1 );
	return ret;
}

SGSRESULT sgs_PushNumIndex( SGS_CTX, StkIdx obj, sgs_Int idx )
{
	sgs_Variable ivar;
	ivar.type = SVT_INT;
	ivar.data.I = idx;
	return sgs_PushIndexIP( C, obj, &ivar, FALSE );
}

SGSRESULT sgs_StoreNumIndex( SGS_CTX, StkIdx obj, sgs_Int idx )
{
	sgs_Variable ivar;
	ivar.type = SVT_INT;
	ivar.data.I = idx;
	return sgs_StoreIndexIP( C, obj, &ivar, FALSE );
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

void sgs_GetEnv( SGS_CTX, sgs_Variable* out )
{
	sgs_InitObjectPtr( out, C->_G );
	VAR_ACQUIRE( out );
}

SGSRESULT sgs_SetEnv( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SVT_OBJECT || var->data.O->iface != sgsstd_dict_iface )
		return SGS_ENOTSUP;
	sgs_ObjRelease( C, C->_G );
	C->_G = var->data.O;
	VAR_ACQUIRE( var );
	return SGS_SUCCESS;
}

void sgs_PushEnv( SGS_CTX )
{
	sgs_PushObjectPtr( C, C->_G );
}

SGSRESULT sgs_StoreEnv( SGS_CTX )
{
	int ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, -1, &var ) )
		return SGS_EINPROC;
	ret = sgs_SetEnv( C, &var );
	if( SGS_SUCCEEDED( ret ) )
		stk_pop1( C );
	return SGS_SUCCESS;
}


/*
	o = offset (isprop=true) (sizeval)
	p = property (isprop=true) (null-terminated string)
	s = super-secret property (isprop=true) (sizeval, buffer)
	i = index (isprop=false) (sizeval)
	k = key (isprop=false) (null-terminated string)
	n = null-including key (isprop=false) (sizeval, buffer)
*/

static SGSRESULT sgs_PushPathBuf( SGS_CTX, StkIdx item, const char* path, size_t plen, va_list* pargs )
{
#define args *pargs
	int ret = sgs_PushItem( C, item );
	size_t i = 0;
	if( ret != SGS_SUCCESS )
		return ret;
	while( i < plen )
	{
		sgs_Variable key;
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
				sgs_InitStringBuf( C, &key, P, S );
			else
				sgs_InitString( C, &key, P );
		}
		else if( S >= 0 )
			sgs_InitInt( &key, S );
		else
			return SGS_EINPROC;
		
		ret = sgs_PushIndexIP( C, -1, &key, prop );
		VAR_RELEASE( &key );
		if( ret != SGS_SUCCESS )
			return ret;
		stk_popskip( C, 1, 1 );
	}
#undef args
	return ret;
}

SGSRESULT sgs_PushPath( SGS_CTX, StkIdx item, const char* path, ... )
{
	int ret;
	StkIdx ssz = STACKFRAMESIZE;
	va_list args;
	va_start( args, path );
	item = stk_absindex( C, item );
	ret = sgs_PushPathBuf( C, item, path, strlen(path), &args );
	if( ret == SGS_SUCCESS )
		stk_popskip( C, STACKFRAMESIZE - ssz - 1, 1 );
	else
		stk_pop( C, STACKFRAMESIZE - ssz );
	va_end( args );
	return ret;
}

SGSRESULT sgs_StorePath( SGS_CTX, StkIdx item, const char* path, ... )
{
	int ret;
	size_t len = strlen( path );
	StkIdx val, ssz = STACKFRAMESIZE;
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
		sgs_Variable key;
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
				sgs_InitStringBuf( C, &key, P, S );
			else
				sgs_InitString( C, &key, P );
		}
		else if( S >= 0 )
			sgs_InitInt( &key, S );
		else
		{
			ret = SGS_EINPROC;
			goto fail;
		}
		
		ret = sgs_SetIndexIPI( C, -1, &key, val, prop );
		VAR_RELEASE( &key );
		if( ret != SGS_SUCCESS )
			return ret;
		ssz--;
	}
fail:
	va_end( args );
	stk_pop( C, STACKFRAMESIZE - ssz );
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
	a,t,h,o - objects (array,dict,map,specific iface)
	& - pointer (void*)
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
		sgs_CodeString( SGS_CODE_VT, (int) sgs_ItemType( C, argid ) );
	if( argid == 0 && gotthis )
		return sgs_Msg( C, SGS_WARNING, "'this' - expected %s%s, got %s", expfx, expect, got );
	else
		return sgs_Msg( C, SGS_WARNING, "argument %d - expected %s%s, got %s",
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
				sgs_Msg( C, SGS_WARNING, "cannot move argument pointer before 0" );
				return SGS_EINVAL;
			}
			break;
		case '>': from++; break;
		case '@': method = 1; break;
		case '.':
			if( from < sgs_StackSize( C ) )
			{
				sgs_Msg( C, SGS_WARNING, "function expects exactly %d arguments, %d given",
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
				sgs_Bool b;
				
				if( !sgs_ParseBool( C, from, &b ) ||
					( strict && sgs_ItemType( C, from ) != SVT_BOOL ) )
				{
					argerr( C, from, method, SVT_BOOL, strict );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( args, sgs_Bool* ) = b;
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
					sgs_Msg( C, SGS_WARNING, "integer argument %d (%" PRId64 ") out of range [%"
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
				sgs_ObjInterface* ifc = NULL;
				const char* ostr = "custom object";
				
				if( *cmd == 'a' ){ ifc = sgsstd_array_iface; ostr = "array"; }
				if( *cmd == 't' ){ ifc = sgsstd_dict_iface; ostr = "dict"; }
				if( *cmd == 'h' ){ ifc = sgsstd_map_iface; ostr = "map"; }
				if( *cmd == 'o' ) ifc = va_arg( args, sgs_ObjInterface* );
				
				if( sgs_ItemType( C, from ) != SVT_OBJECT ||
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
			
		case '&':
			{
				void* ptr;
				if( !sgs_ParsePtr( C, from, &ptr ) )
				{
					argerrx( C, from, method, "pointer", "" );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( args, void** ) = ptr;
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
				if( !nowrite )   flags |= SGS_LOADARG_WRITE;
				if( opt )        flags |= SGS_LOADARG_OPTIONAL;
				if( isig )       flags |= SGS_LOADARG_INTSIGN;
				if( range > 0 )  flags |= SGS_LOADARG_INTRANGE;
				if( range < 0 )  flags |= SGS_LOADARG_INTCLAMP;
				
				sgs_VAList va;
				memcpy( &va.args, &args, sizeof(va) );
				if( !acf( C, from, &va, flags ) )
				{
					return opt;
				}
				memcpy( &args, &va.args, sizeof(va) );
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

SGSBOOL sgs_ParseMethod( SGS_CTX, sgs_ObjInterface* iface, void** ptrout,
	const char* method_name, const char* func_name )
{
	int method_call = sgs_Method( C );
	SGSFN( method_call ? method_name : func_name );
	if( !sgs_IsObject( C, 0, iface ) )
	{
		sgs_ArgErrorExt( C, 0, method_call, iface->name, "" );
		return FALSE;
	}
	*ptrout = sgs_GetObjectData( C, 0 );
	sgs_ForceHideThis( C );
	return TRUE;
}


SGSRESULT sgs_Pop( SGS_CTX, StkIdx count )
{
	if( STACKFRAMESIZE < count || count < 0 )
		return SGS_EINVAL;
	stk_pop( C, count );
	return SGS_SUCCESS;
}

SGSRESULT sgs_PopSkip( SGS_CTX, StkIdx count, StkIdx skip )
{
	if( STACKFRAMESIZE < count + skip || count < 0 || skip < 0 )
		return SGS_EINVAL;
	stk_popskip( C, count, skip );
	return SGS_SUCCESS;
}

StkIdx sgs_StackSize( SGS_CTX )
{
	return STACKFRAMESIZE;
}

SGSRESULT sgs_SetStackSize( SGS_CTX, StkIdx size )
{
	StkIdx diff;
	if( size < 0 )
		return SGS_EINVAL;
	diff = STACKFRAMESIZE - size;
	if( diff > 0 )
		stk_pop( C, diff );
	else
		stk_push_nulls( C, -diff );
	return SGS_SUCCESS;
}

StkIdx sgs_AbsIndex( SGS_CTX, StkIdx item )
{
	return stk_absindex( C, item );
}

SGSBOOL sgs_IsValidIndex( SGS_CTX, StkIdx item )
{
	item = stk_absindex( C, item );
	return ( item >= 0 && item < STACKFRAMESIZE );
}

SGSBOOL sgs_GetStackItem( SGS_CTX, StkIdx item, sgs_Variable* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	*out = *stk_getpos( C, item );
	return TRUE;
}

SGSRESULT sgs_SetStackItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* val )
{
	if( val->type >= SGS_VT__COUNT )
		return SGS_EINVAL;
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_EBOUNDS;
	stk_setvar( C, item, val );
	return SGS_SUCCESS;
}

uint32_t sgs_ItemType( SGS_CTX, StkIdx item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	return stk_getpos( C, item )->type;
}


void sgs_Assign( SGS_CTX, sgs_Variable* var_to, sgs_Variable* var_from )
{
	VAR_RELEASE( var_to );
	*var_to = *var_from;
	VAR_ACQUIRE( var_to );
}


/*

	OPERATIONS

*/

SGSRESULT sgs_FCallP( SGS_CTX, sgs_Variable* callable, int args, int expect, int gotthis )
{
	if( STACKFRAMESIZE < args + ( gotthis ? 1 : 0 ) )
		return SGS_EINVAL;
	return vm_call( C, args, 0, gotthis, expect, callable ) ? SGS_SUCCESS : SGS_EINPROC;
}

SGSRESULT sgs_FCall( SGS_CTX, int args, int expect, int gotthis )
{
	int ret;
	sgs_Variable func;
	int stksize = sgs_StackSize( C );
	gotthis = gotthis ? 1 : 0;
	if( stksize < args + gotthis + 1 )
		return SGS_EINVAL;
	
	func = *stk_getpos( C, -1 );
	stk_pop1nr( C );
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

SGSRESULT sgs_TypeOf( SGS_CTX, StkIdx item )
{
	return vm_gettype( C, item );
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
		switch( var->type )
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
				uint32_t len = var->data.S->size;
				char* srcend = source + len;
				sprintf( buf, "string [%"PRId32"] \"", len );
				bptr += strlen( buf );
				while( source < srcend && bptr < bend )
				{
					if( *source == ' ' || isgraph( (int)*source ) )
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
				/* WP: string limit */
				sgs_PushStringBuf( C, buf, (sgs_SizeVal) ( bptr - buf ) );
			}
			break;
		case SVT_FUNC:
			{
				MemBuf mb = membuf_create();
				func_t* F = var->data.F;
				
				const char* str1 = F->funcname.size ? "SGS function '" : "SGS function <anonymous>";
				const char* str2 = F->funcname.size ? "' defined at " : " defined at ";
				const char* str3 = "'";
				
				membuf_appbuf( &mb, C, str1, strlen(str1) );
				if( F->funcname.size )
					membuf_appbuf( &mb, C, F->funcname.ptr, F->funcname.size );
				if( F->filename.size )
				{
					char lnbuf[ 32 ];
					membuf_appbuf( &mb, C, str2, strlen(str2) );
					membuf_appbuf( &mb, C, F->filename.ptr, F->filename.size );
					sprintf( lnbuf, ":%d", (int) F->linenum );
					membuf_appbuf( &mb, C, lnbuf, strlen(lnbuf) );
				}
				else if( F->funcname.size )
					membuf_appbuf( &mb, C, str3, strlen(str3) );
				
				/* WP: various limits */
				sgs_PushStringBuf( C, mb.ptr, (sgs_SizeVal) mb.size );
				membuf_destroy( &mb, C );
			}
			break;
		case SVT_CFUNC: sgs_PushString( C, "C function" ); break;
		case SVT_OBJECT:
			{
				char buf[ 32 ];
				int q;
				_STACK_PREPARE;
				object_t* obj = var->data.O;
				
				sprintf( buf, "object (%p) [%"PRId32"] ", (void*) obj, obj->refcount );
				sgs_PushString( C, buf );
				
				if( obj->iface->dump )
				{
					_STACK_PROTECT;
					ret = obj->iface->dump( C, obj, maxdepth - 1 );
					q = SGS_SUCCEEDED( ret );
					_STACK_UNPROTECT_SKIP( q );
					
					if( q )
						sgs_StringConcat( C, 2 );
				}
			}
			break;
		case SVT_PTR:
			{
				char buf[ 32 ];
				sprintf( buf, "pointer (%p)", var->data.P );
				sgs_PushString( C, buf );
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
			var_destruct_object( C, p );
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
	const uint32_t padsize = 2;

	if( sgs_StackSize( C ) < 1 )
		return SGS_EINPROC;
	{
		uint32_t i;
		char* ostr;
		const char* cstr;
		sgs_Variable* var = stk_getpos( C, -1 );
		if( var->type != SVT_STRING )
			return SGS_EINVAL;
		cstr = var_cstr( var );
		for( i = 0; cstr[ i ]; )
			if( cstr[ i ] == '\n' ) i++; else cstr++;
		if( var->data.S->size + i * padsize > 0x7fffffff )
			return SGS_EINPROC;
		/* WP: implemented error condition */
		sgs_PushStringBuf( C, NULL, (StkIdx) ( var->data.S->size + i * padsize ) );
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
		if( isgraph( (int)buf[ i ] ) || buf[ i ] == ' ' )
			membuf_appchr( &mb, C, buf[ i ] );
		else
		{
			char chrs[32];
			sprintf( chrs, "\\x%02X", (int) buf[ i ] );
			membuf_appbuf( &mb, C, chrs, 4 );
		}
	}
	sgs_Pop( C, 1 );
	if( mb.size > 0x7fffffff )
		return SGS_EINPROC;
	/* WP: implemented error condition */
	sgs_PushStringBuf( C, mb.ptr, (sgs_SizeVal) mb.size );
	membuf_destroy( &mb, C );
	return SGS_SUCCESS;
}

SGSRESULT sgs_StringConcat( SGS_CTX, StkIdx args )
{
	return vm_op_concat_ex( C, args ) ? SGS_SUCCESS : SGS_EINVAL;
}

SGSRESULT sgs_CloneItem( SGS_CTX, StkIdx item )
{
	int ret;
	sgs_Variable copy;
	if( !sgs_GetStackItem( C, item, &copy ) )
		return SGS_EBOUNDS;
	ret = vm_clone( C, &copy );
	if( SGS_SUCCEEDED( ret ) )
		return SGS_SUCCESS;
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

static int _unserialize_function( SGS_CTX, const char* buf, size_t sz, sgs_iFunc** outfn )
{
	func_t* F;
	sgs_CompFunc* nf = NULL;
	if( sgsBC_ValidateHeader( buf, sz ) < SGS_HEADER_SIZE )
		return 0;
	if( sgsBC_Buf2Func( C, "<anonymous>", buf, sz, &nf ) )
		return 0;
	
	F = sgs_Alloc_a( func_t, nf->consts.size + nf->code.size );

	F->refcount = 0;
	/* WP: const/instruction limits */
	F->size = (uint32_t) ( nf->consts.size + nf->code.size );
	F->instr_off = (uint32_t) nf->consts.size;
	F->gotthis = nf->gotthis;
	F->numargs = nf->numargs;
	F->numtmp = nf->numtmp;
	F->numclsr = nf->numclsr;

	{
		size_t lnc = nf->lnbuf.size / sizeof( sgs_LineNum );
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
	SGS_CTX, const void* ptr, size_t datasize )
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

	if( V.type == SVT_OBJECT )
	{
		sgs_VarObj* O = V.data.O;
		_STACK_PREPARE;
		if( !O->iface->serialize )
		{
			sgs_Msg( C, SGS_WARNING, "Cannot serialize object of type '%s'", O->iface->name );
			ret = SGS_ENOTSUP;
			goto fail;
		}
		_STACK_PROTECT;
		ret = O->iface->serialize( C, O );
		_STACK_UNPROTECT;
		if( ret != SGS_SUCCESS )
			goto fail;
	}
	else if( V.type == SVT_CFUNC )
	{
		sgs_Msg( C, SGS_WARNING, "Cannot serialize C functions" );
		ret = SGS_EINVAL;
		goto fail;
	}
	else
	{
		char pb[2];
		{
			pb[0] = 'P';
			/* WP: basetype limited to bits 0-7, sign interpretation does not matter */
			pb[1] = (char) V.type;
		}
		sgs_Write( C, pb, 2 );
		switch( V.type )
		{
		case SVT_NULL: break;
		/* WP: V.data.B uses only bit 0 */
		case SVT_BOOL: { char b = (char) V.data.B; sgs_Write( C, &b, 1 ); } break;
		case SVT_INT: sgs_Write( C, &V.data.I, sizeof( sgs_Int ) ); break;
		case SVT_REAL: sgs_Write( C, &V.data.R, sizeof( sgs_Real ) ); break;
		case SVT_STRING:
			sgs_Write( C, &V.data.S->size, 4 );
			sgs_Write( C, var_cstr( &V ), V.data.S->size );
			break;
		case SVT_FUNC:
			{
				MemBuf Bf = membuf_create();
				ret = _serialize_function( C, V.data.F, &Bf );
				if( ret != 0 )
				{
					sgs_Write( C, &Bf.size, 4 );
					sgs_Write( C, Bf.ptr, Bf.size );
					ret = SGS_SUCCESS;
				}
				else
					ret = SGS_EINPROC;
				membuf_destroy( &Bf, C );
				if( ret != SGS_SUCCESS )
					goto fail;
			}
			break;
		default:
			sgs_Msg( C, SGS_ERROR, "sgs_Serialize: Unknown memory error" );
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
		{
			sgs_Msg( C, SGS_WARNING, "Serialized string too long" );
			if( B.size > 0x7fffffff )
				ret = SGS_EINVAL;
			else
				/* WP: added error condition */
				sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
		}
		membuf_destroy( &B, C );
	}
	return ret;
}

SGSRESULT sgs_SerializeObject( SGS_CTX, StkIdx args, const char* func )
{
	size_t len = strlen( func );
	char pb[7] = { 'C', 0, 0, 0, 0, 0, 0 };
	{
		/* WP: they were pointless */
		pb[1] = (char)((args)&0xff);
		pb[2] = (char)((args>>8)&0xff);
		pb[3] = (char)((args>>16)&0xff);
		pb[4] = (char)((args>>24)&0xff);
	}
	
	if( len >= 255 )
		return SGS_EINVAL;
	if( C->output_fn != serialize_output_func )
		return SGS_EINPROC;
	
	/* WP: have error condition + sign interpretation doesn't matter */
	pb[ 5 ] = (char) len;
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
				else
				{
					sgs_Int val;
					AS_INTEGER( val, str );
					sgs_PushInt( C, val );
				}
				str += 8;
				break;
			case SVT_REAL:
				if( str >= strend-7 )
					return SGS_EINPROC;
				else
				{
					sgs_Real val;
					AS_REAL( val, str );
					sgs_PushReal( C, val );
				}
				str += 8;
				break;
			case SVT_STRING:
				{
					sgs_SizeVal strsz;
					if( str >= strend-3 )
						return SGS_EINPROC;
					AS_INT32( strsz, str );
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
					AS_INT32( bcsz, str );
					str += 4;
					if( str > strend - bcsz )
						return SGS_EINPROC;
					/* WP: conversion does not affect values */
					if( !_unserialize_function( C, str, (size_t) bcsz, &fn ) )
						return SGS_EINPROC;
					tmp.type = SVT_FUNC;
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
			int32_t argc;
			int fnsz, ret;
			if( str >= strend-4 )
				return SGS_EINPROC;
			AS_INT32( argc, str );
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


int sgs_Compare( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 )
{
	return vm_compare( C, v1, v2 );
}

SGSBOOL sgs_EqualTypes( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 )
{
	return v1->type == v2->type && ( v1->type != SVT_OBJECT
		|| v1->data.O->iface == v2->data.O->iface );
}


/*
	
	CONVERSION / RETRIEVAL
	
*/
/* pointer versions */
sgs_Bool sgs_GetBoolP( SGS_CTX, sgs_Variable* var ){ return var_getbool( C, var ); }
sgs_Int sgs_GetIntP( SGS_CTX, sgs_Variable* var ){ return var_getint( C, var ); }
sgs_Real sgs_GetRealP( SGS_CTX, sgs_Variable* var ){ return var_getreal( C, var ); }
void* sgs_GetPtrP( SGS_CTX, sgs_Variable* var ){ return var_getptr( C, var ); }

sgs_Bool sgs_ToBoolP( SGS_CTX, sgs_Variable* var )
{
	vm_convert_bool( C, var );
	return var->data.B;
}

sgs_Int sgs_ToIntP( SGS_CTX, sgs_Variable* var )
{
	vm_convert_int( C, var );
	return var->data.I;
}

sgs_Real sgs_ToRealP( SGS_CTX, sgs_Variable* var )
{
	vm_convert_real( C, var );
	return var->data.R;
}

void* sgs_ToPtrP( SGS_CTX, sgs_Variable* var )
{
	vm_convert_ptr( C, var );
	return var->data.P;
}

char* sgs_ToStringBufP( SGS_CTX, sgs_Variable* var, sgs_SizeVal* outsize )
{
	vm_convert_string( C, var );
	if( outsize )
		/* WP: string limit */
		*outsize = (sgs_SizeVal) var->data.S->size;
	return var_cstr( var );
}

char* sgs_ToStringBufFastP( SGS_CTX, sgs_Variable* var, sgs_SizeVal* outsize )
{
	if( var->type == SVT_OBJECT )
	{
		sgs_PushVariable( C, var );
		sgs_TypeOf( C, -1 );
		sgs_StoreVariable( C, var );
		stk_pop1( C );
	}
	return sgs_ToStringBufP( C, var, outsize );
}


SGSBOOL sgs_IsObjectP( sgs_Variable* var, sgs_ObjInterface* iface )
{
	return var->type == SVT_OBJECT && var->data.O->iface == iface;
}

SGSBOOL sgs_IsCallableP( sgs_Variable* var )
{
	uint32_t ty = var->type;
	if( ty == SVT_FUNC || ty == SVT_CFUNC )
		return 1;
	if( ty == SVT_OBJECT && var->data.O->iface->call )
		return 1;
	return 0;
}

SGSBOOL sgs_ParseBoolP( SGS_CTX, sgs_Variable* var, sgs_Bool* out )
{
	uint32_t ty = var->type;
	if( ty == SVT_NULL || ty == SVT_CFUNC || ty == SVT_FUNC || ty == SVT_STRING )
		return FALSE;
	if( out )
		*out = sgs_GetBoolP( C, var );
	return TRUE;
}

SGSBOOL sgs_ParseIntP( SGS_CTX, sgs_Variable* var, sgs_Int* out )
{
	sgs_Int i;
	if( var->type == SVT_NULL || var->type == SVT_FUNC || var->type == SVT_CFUNC )
		return FALSE;
	if( var->type == SVT_STRING )
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
		i = sgs_GetIntP( C, var );
	if( out )
		*out = i;
	return TRUE;
}

SGSBOOL sgs_ParseRealP( SGS_CTX, sgs_Variable* var, sgs_Real* out )
{
	sgs_Real r;
	if( var->type == SVT_NULL || var->type == SVT_FUNC || var->type == SVT_CFUNC )
		return FALSE;
	if( var->type == SVT_STRING )
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
		r = sgs_GetRealP( C, var );
	if( out )
		*out = r;
	return TRUE;
}

SGSBOOL sgs_ParseStringP( SGS_CTX, sgs_Variable* var, char** out, sgs_SizeVal* size )
{
	char* str;
	uint32_t ty;
	ty = var->type;
	if( ty == SVT_NULL || ty == SVT_FUNC || ty == SVT_CFUNC )
		return FALSE;
	str = sgs_ToStringBufP( C, var, size );
	if( out )
		*out = str;
	return str != NULL;
}

SGSBOOL sgs_ParsePtrP( SGS_CTX, sgs_Variable* var, void** out )
{
	if( var->type != SVT_NULL && var->type != SVT_PTR )
		return FALSE;
	if( out )
		*out = var->type != SVT_NULL ? var->data.P : NULL;
	return TRUE;
}

SGSMIXED sgs_ArraySizeP( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SVT_OBJECT ||
		var->data.O->iface != sgsstd_array_iface )
		return SGS_EINVAL;
	return ((sgsstd_array_header_t*)var->data.O->data)->size;
}


/* index versions */
sgs_Bool sgs_GetBool( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	var = stk_getpos( C, item );
	return var_getbool( C, var );
}

sgs_Int sgs_GetInt( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	var = stk_getpos( C, item );
	return var_getint( C, var );
}

sgs_Real sgs_GetReal( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	var = stk_getpos( C, item );
	return var_getreal( C, var );
}

void* sgs_GetPtr( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return NULL;
	var = stk_getpos( C, item );
	return var_getptr( C, var );
}


sgs_Bool sgs_ToBool( SGS_CTX, StkIdx item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	vm_convert_stack_bool( C, item );
	return stk_getpos( C, item )->data.B;
}

sgs_Int sgs_ToInt( SGS_CTX, StkIdx item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	vm_convert_stack_int( C, item );
	return stk_getpos( C, item )->data.I;
}

sgs_Real sgs_ToReal( SGS_CTX, StkIdx item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	vm_convert_stack_real( C, item );
	return stk_getpos( C, item )->data.R;
}

void* sgs_ToPtr( SGS_CTX, StkIdx item )
{
	if( !sgs_IsValidIndex( C, item ) )
		return 0;
	vm_convert_stack_ptr( C, item );
	return stk_getpos( C, item )->data.P;
}

char* sgs_ToStringBuf( SGS_CTX, StkIdx item, sgs_SizeVal* outsize )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return NULL;
	vm_convert_stack_string( C, item );
	var = stk_getpos( C, item );
	if( outsize )
		/* WP: string limit */
		*outsize = (sgs_SizeVal) var->data.S->size;
	return var_cstr( var );
}

char* sgs_ToStringBufFast( SGS_CTX, StkIdx item, sgs_SizeVal* outsize )
{
	if( !sgs_IsValidIndex( C, item ) )
		return NULL;
	if( stk_getpos( C, item )->type == SVT_OBJECT )
	{
		StkIdx g = stk_absindex( C, item );
		stk_push( C, stk_getlpos( C, g ) );
		sgs_TypeOf( C, -1 );
		stk_setlvar( C, g, stk_getpos( C, -1 ) );
		stk_pop2( C );
	}
	return sgs_ToStringBuf( C, item, outsize );
}


SGSBOOL sgs_IsObject( SGS_CTX, StkIdx item, sgs_ObjInterface* iface )
{
	sgs_Variable* var;
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	var = stk_getpos( C, item );
	return var->type == SVT_OBJECT && var->data.O->iface == iface;
}

SGSBOOL sgs_IsCallable( SGS_CTX, StkIdx item )
{
	uint32_t ty = sgs_ItemType( C, item );
	
	if( ty == SVT_FUNC || ty == SVT_CFUNC )
		return 1;
	if( ty == SVT_OBJECT && sgs_GetObjectIface( C, item )->call )
		return 1;
	return 0;
}

SGSBOOL sgs_ParseBool( SGS_CTX, StkIdx item, sgs_Bool* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	return sgs_ParseBoolP( C, stk_getpos( C, item ), out );
}

SGSBOOL sgs_ParseInt( SGS_CTX, StkIdx item, sgs_Int* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	return sgs_ParseIntP( C, stk_getpos( C, item ), out );
}

SGSBOOL sgs_ParseReal( SGS_CTX, StkIdx item, sgs_Real* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	return sgs_ParseRealP( C, stk_getpos( C, item ), out );
}

SGSBOOL sgs_ParseString( SGS_CTX, StkIdx item, char** out, sgs_SizeVal* size )
{
	char* str;
	uint32_t ty;
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	ty = sgs_ItemType( C, item );
	if( ty == SVT_NULL || ty == SVT_FUNC || ty == SVT_CFUNC )
		return FALSE;
	str = sgs_ToStringBuf( C, item, size );
	if( out )
		*out = str;
	return str != NULL;
}

SGSBOOL sgs_ParsePtr( SGS_CTX, StkIdx item, void** out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return FALSE;
	return sgs_ParsePtrP( C, stk_getpos( C, item ), out );
}

SGSMIXED sgs_ArraySize( SGS_CTX, StkIdx item )
{
	if( sgs_ItemType( C, item ) != SVT_OBJECT ||
		sgs_GetObjectIface( C, item ) != sgsstd_array_iface )
		return SGS_EINVAL;
	return ((sgsstd_array_header_t*)sgs_GetObjectData( C, item ))->size;
}


SGSRESULT sgs_PushIterator( SGS_CTX, StkIdx item )
{
	int ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	sgs_PushNull( C );
	ret = vm_forprep( C, stk_absindex( C, -1 ), &var );
	if( SGS_FAILED( ret ) )
		stk_pop1( C );
	return ret;
}

SGSRESULT sgs_GetIterator( SGS_CTX, StkIdx item, sgs_Variable* out )
{
	int ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	sgs_PushNull( C );
	ret = vm_forprep( C, stk_absindex( C, -1 ), &var );
	if( SGS_FAILED( ret ) )
		stk_pop1( C );
	else
		sgs_StoreVariable( C, out );
	return ret;
}

SGSMIXED sgs_IterAdvance( SGS_CTX, StkIdx item )
{
	_EL_BACKUP;
	sgs_SizeVal ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	_EL_SETMAX;
	ret = vm_fornext( C, -1, -1, &var );
	_EL_RESET;
	return ret;
}

SGSMIXED sgs_IterPushData( SGS_CTX, StkIdx item, int key, int value )
{
	_EL_BACKUP;
	sgs_SizeVal ret;
	sgs_Variable var;
	StkIdx idkey, idval;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	if( !key && !value )
		return SGS_SUCCESS;
	if( key )
	{
		sgs_PushNull( C );
		idkey = stk_absindex( C, -1 );
	}
	else idkey = -1;
	if( value )
	{
		sgs_PushNull( C );
		idval = stk_absindex( C, -1 );
	}
	else idval = -1;
	_EL_SETMAX;
	ret = vm_fornext( C, idkey, idval, &var );
	_EL_RESET;
	if( SGS_FAILED( ret ) )
		stk_pop( C, !!key + !!value );
	return ret;
}

SGSMIXED sgs_IterGetData( SGS_CTX, sgs_StkIdx item, sgs_Variable* key, sgs_Variable* value )
{
	_EL_BACKUP;
	sgs_SizeVal ret;
	sgs_Variable var;
	if( !sgs_GetStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	if( !key && !value )
		return SGS_SUCCESS;
	if( key ) sgs_PushNull( C );
	if( value ) sgs_PushNull( C );
	_EL_SETMAX;
	ret = vm_fornext( C, key?stk_absindex(C,value?-2:-1):-1, value?stk_absindex(C,-1):-1, &var );
	_EL_RESET;
	if( SGS_SUCCEEDED( ret ) )
	{
		if( value ) sgs_StoreVariable( C, value );
		if( key ) sgs_StoreVariable( C, key );
	}
	else
		stk_pop( C, !!key + !!value );
	return ret;
}


SGSBOOL sgs_IsNumericString( const char* str, sgs_SizeVal size )
{
	intreal_t out;
	const char* ostr = str;
	return util_strtonum( &str, str + size, &out.i, &out.r ) != 0 && str != ostr;
}


/*

	EXTENSION UTILITIES

*/

SGSBOOL sgs_Method( SGS_CTX )
{
	if( C->sf_last && HAS_FLAG( C->sf_last->flags, SGS_SF_METHOD ) &&
		!HAS_FLAG( C->sf_last->flags, SGS_SF_HASTHIS ) )
	{
		C->stack_off--;
		C->sf_last->flags |= SGS_SF_HASTHIS;
		return TRUE;
	}
	return FALSE;
}

SGSBOOL sgs_HideThis( SGS_CTX )
{
	if( C->sf_last && HAS_FLAG( C->sf_last->flags, (SGS_SF_METHOD|SGS_SF_HASTHIS) ) )
	{
		C->stack_off++;
		/* WP: implicit conversion to int */
		C->sf_last->flags &= (uint8_t) ~SGS_SF_HASTHIS;
		return TRUE;
	}
	return FALSE;
}

SGSBOOL sgs_ForceHideThis( SGS_CTX )
{
	if( !C->sf_last )
		return FALSE;
	if( HAS_FLAG( C->sf_last->flags, SGS_SF_METHOD ) )
		return sgs_HideThis( C );
	if( STACKFRAMESIZE < 1 )
		return FALSE;
	C->stack_off++;
	C->sf_last->flags = ( C->sf_last->flags | SGS_SF_METHOD ) & (uint8_t) (~SGS_SF_HASTHIS);
	return TRUE;
}


void sgs_Acquire( SGS_CTX, sgs_Variable* var )
{
	UNUSED( C );
	VAR_ACQUIRE( var );
}

void sgs_Release( SGS_CTX, sgs_Variable* var )
{
	if( var->type == SVT_OBJECT && C->gcrun )
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

void sgs_ObjAcquire( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SVT_OBJECT;
	var.data.O = obj;
	sgs_Acquire( C, &var );
}

void sgs_ObjRelease( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SVT_OBJECT;
	var.data.O = obj;
	sgs_Release( C, &var );
}

SGSRESULT sgs_ObjGCMark( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SVT_OBJECT;
	var.data.O = obj;
	return sgs_GCMark( C, &var );
}


#define DBLCHK( what, fval )\
	sgs_BreakIf( what );\
	if( what ) return fval;


#define _OBJPREP_P( ret ) \
	DBLCHK( var->type != SVT_OBJECT, ret )

char* sgs_GetStringPtrP( sgs_Variable* var )
{
	DBLCHK( var->type != SVT_STRING, NULL )
	return var_cstr( var );
}

sgs_SizeVal sgs_GetStringSizeP( sgs_Variable* var )
{
	DBLCHK( var->type != SVT_STRING, 0 )
	/* WP: string limit */
	return (sgs_SizeVal) var->data.S->size;
}

sgs_VarObj* sgs_GetObjectStructP( sgs_Variable* var )
{
	_OBJPREP_P( NULL );
	return var->data.O;
}

void* sgs_GetObjectDataP( sgs_Variable* var )
{
	_OBJPREP_P( NULL );
	return var->data.O->data;
}

sgs_ObjInterface* sgs_GetObjectIfaceP( sgs_Variable* var )
{
	_OBJPREP_P( NULL );
	return var->data.O->iface;
}

int sgs_SetObjectDataP( sgs_Variable* var, void* data )
{
	_OBJPREP_P( 0 );
	var->data.O->data = data;
	return 1;
}

int sgs_SetObjectIfaceP( sgs_Variable* var, sgs_ObjInterface* iface )
{
	_OBJPREP_P( 0 );
	var->data.O->iface = iface;
	return 1;
}


#define _OBJPREP( ret ) \
	sgs_Variable* var; \
	DBLCHK( !sgs_IsValidIndex( C, item ), ret ) \
	var = stk_getpos( C, item ); \
	DBLCHK( var->type != SVT_OBJECT, ret )

char* sgs_GetStringPtr( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	DBLCHK( !sgs_IsValidIndex( C, item ), NULL )
	var = stk_getpos( C, item );
	DBLCHK( var->type != SVT_STRING, NULL )
	return var_cstr( var );
}

sgs_SizeVal sgs_GetStringSize( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	DBLCHK( !sgs_IsValidIndex( C, item ), 0 )
	var = stk_getpos( C, item );
	DBLCHK( var->type != SVT_STRING, 0 )
	/* WP: string limit */
	return (sgs_SizeVal) var->data.S->size;
}

sgs_VarObj* sgs_GetObjectStruct( SGS_CTX, StkIdx item )
{
	_OBJPREP( NULL );
	return var->data.O;
}

void* sgs_GetObjectData( SGS_CTX, StkIdx item )
{
	_OBJPREP( NULL );
	return var->data.O->data;
}

sgs_ObjInterface* sgs_GetObjectIface( SGS_CTX, StkIdx item )
{
	_OBJPREP( NULL );
	return var->data.O->iface;
}

int sgs_SetObjectData( SGS_CTX, StkIdx item, void* data )
{
	_OBJPREP( 0 );
	var->data.O->data = data;
	return 1;
}

int sgs_SetObjectIface( SGS_CTX, StkIdx item, sgs_ObjInterface* iface )
{
	_OBJPREP( 0 );
	var->data.O->iface = iface;
	return 1;
}

#undef DBLCHK

