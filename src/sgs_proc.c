

#include <math.h>
#include <stdarg.h>
#include <ctype.h>

#define SGS_INTERNAL_STRINGTABLES

#define StkIdx sgs_StkIdx

#include "sgs_xpc.h"
#include "sgs_int.h"


#define TYPENAME( type ) sgs_VarNames[ type ]

#define IS_REFTYPE( type ) ( type == SGS_VT_STRING || type == SGS_VT_FUNC || type == SGS_VT_OBJECT )


#define VAR_ACQUIRE( pvar ) { \
	if( IS_REFTYPE( (pvar)->type ) ) (*(pvar)->data.pRC)++; }
#define VAR_RELEASE( pvar ) { \
	if( IS_REFTYPE( (pvar)->type ) ) var_release( C, pvar ); \
	(pvar)->type = SGS_VT_NULL; }


#define STK_UNITSIZE sizeof( sgs_Variable )


static void stk_popskip( SGS_CTX, StkIdx num, StkIdx skip );
#define stk_pop( C, num ) stk_popskip( C, num, 0 )

#define _STACK_PREPARE ptrdiff_t _stksz = 0;
#define _STACK_PROTECT _stksz = C->stack_off - C->stack_base; C->stack_off = C->stack_top;
#define _STACK_PROTECT_SKIP( n ) do{ _stksz = C->stack_off - C->stack_base; \
	C->stack_off = C->stack_top - (n); }while(0)
#define _STACK_UNPROTECT stk_pop( C, SGS_STACKFRAMESIZE ); C->stack_off = C->stack_base + _stksz;
#define _STACK_UNPROTECT_SKIP( n ) do{ StkIdx __n = (n); \
	stk_popskip( C, SGS_STACKFRAMESIZE - __n, __n ); \
	C->stack_off = C->stack_base + _stksz; }while(0)


typedef union intreal_s
{
	sgs_Int i;
	sgs_Real r;
}
intreal_t;


/*
	Meta-methods
*/

static int _call_metamethod( SGS_CTX, sgs_VarObj* obj, const char* name, size_t namelen, int args, SGSRESULT* ret )
{
	int res;
	if( !obj->metaobj )
		return 0;
	
	/* TODO OPTIMIZE NAME */
	sgs_PushObjectPtr( C, obj->metaobj );
	sgs_PushStringBuf( C, name, (sgs_SizeVal) namelen );
	if( SGS_FAILED( sgs_PushIndexII( C, -2, -1, 0 ) ) )
	{
		sgs_Pop( C, 2 );
		return 0;
	}
	
	sgs_PopSkip( C, 2, 1 );
	res = sgs_ThisCall( C, args, 1 );
	if( SGS_CALL_FAILED( res ) )
		return 0;
	if( ret )
		*ret = res;
	return SGS_SUCCEEDED( ret );
}


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

static void var_free_object( SGS_CTX, sgs_VarObj* O )
{
	if( O->is_iface )
	{
		sgs_VHTVar* p = C->ifacetable.vars;
		sgs_VHTVar* pend = p + C->ifacetable.size;
		while( p < pend )
		{
			if( p->val.type == SGS_VT_OBJECT && p->val.data.O == O )
			{
				sgs_Variable K = p->key;
				O->refcount = 2;
				sgs_vht_unset( &C->ifacetable, C, &K );
				break;
			}
			p++;
		}
	}
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

static void var_destruct_object( SGS_CTX, sgs_VarObj* O )
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
	if( O->metaobj )
	{
		sgs_ObjRelease( C, O->metaobj );
		O->metaobj = NULL;
	}
}
void sgsVM_VarDestroyObject( SGS_CTX, sgs_VarObj* O )
{
	var_destruct_object( C, O );
	var_free_object( C, O );
}

static void var_destroy_string( SGS_CTX, sgs_iStr* S )
{
#if SGS_STRINGTABLE_MAXLEN >= 0
	if( S->size <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_VHTVar* p;
		sgs_Variable tmp;
		tmp.type = SGS_VT_STRING;
		tmp.data.S = S;
		p = sgs_vht_get( &C->stringtable, &tmp );
		if( p )
		{
#  if SGS_DEBUG && SGS_DEBUG_EXTRA
			if( p->key.data.S != S )
			{
				printf( "WATWATWAT: (%d) |my>| [%d,%d] %d,%s |st>| [%d,%d] %d,%s\n",
					(int) sgs_HashFunc( sgs_str_cstr( S ), S->size ),
					(int) S->refcount, (int) S->hash, (int) S->size, sgs_str_cstr( S ),
					(int) p->key.data.S->refcount, (int) p->key.data.S->hash, (int) p->key.data.S->size, sgs_str_cstr( p->key.data.S ) );
			}
#  endif
			sgs_BreakIf( p->key.data.S != S );
			S->refcount = 2; /* the 'less code' way to avoid double free */
			sgs_vht_unset( &C->stringtable, C, &tmp );
		}
	}
#endif
	sgs_Dealloc( S );
}

static void var_release( SGS_CTX, sgs_VarPtr p );
static void var_destroy_func( SGS_CTX, sgs_iFunc* F )
{
	sgs_VarPtr var = (sgs_VarPtr) sgs_func_consts( F ), vend = (sgs_VarPtr) (void*) SGS_ASSUME_ALIGNED( sgs_func_bytecode( F ), 16 );
	while( var < vend )
	{
		VAR_RELEASE( var );
		var++;
	}
	sgs_Dealloc( F->lineinfo );
	sgs_membuf_destroy( &F->funcname, C );
	sgs_membuf_destroy( &F->filename, C );
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
	
	if( (*p->data.pRC) <= 0 )
	{
		switch( type )
		{
		case SGS_VT_STRING: var_destroy_string( C, p->data.S ); break;
		case SGS_VT_FUNC: var_destroy_func( C, p->data.F ); break;
		case SGS_VT_OBJECT: sgsVM_VarDestroyObject( C, p->data.O ); break;
		}
	}
}


static void var_create_0str( SGS_CTX, sgs_VarPtr out, uint32_t len )
{
	out->type = SGS_VT_STRING;
	out->data.S = sgs_Alloc_a( sgs_iStr, len + 1 );
	out->data.S->refcount = 1;
	out->data.S->size = len;
	out->data.S->hash = 0;
	memset( sgs_var_cstr( out ), 0, len + 1 );
}

void sgsVM_VarCreateString( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal len )
{
	sgs_Hash hash;
	uint32_t ulen;
	sgs_BreakIf( !str && len );
	
	ulen = (uint32_t) len; /* WP: string limit */
	
	hash = sgs_HashFunc( str, ulen );
#if SGS_STRINGTABLE_MAXLEN >= 0
	if( ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_VHTVar* var = sgs_vht_get_str( &C->stringtable, str, ulen, hash );
		if( var )
		{
			*out = var->key;
			out->data.S->refcount++;
			return;
		}
	}
#endif
	
	var_create_0str( C, out, ulen );
	memcpy( sgs_str_cstr( out->data.S ), str, ulen );
	out->data.S->hash = hash;
	
	if( (int32_t) ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_vht_set( &C->stringtable, C, out, NULL );
		out->data.S->refcount--;
	}
}

static void var_create_str( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal len )
{
	sgsVM_VarCreateString( C, out, str, len );
}
static void var_create_cstr( SGS_CTX, sgs_Variable* out, const char* str )
{
	sgsVM_VarCreateString( C, out, str, (sgs_SizeVal) SGS_STRINGLENGTHFUNC(str) );
}

static void var_finalize_str( SGS_CTX, sgs_Variable* out )
{
	char* str;
	sgs_Hash hash;
	uint32_t ulen;
	
	str = sgs_str_cstr( out->data.S );
	ulen = out->data.S->size;
	hash = sgs_HashFunc( str, ulen );
	
#if SGS_STRINGTABLE_MAXLEN >= 0
	if( ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_VHTVar* var = sgs_vht_get_str( &C->stringtable, str, ulen, hash );
		if( var )
		{
			sgs_Dealloc( out->data.S ); /* avoid querying the string table here */
			*out = var->key;
			out->data.S->refcount++;
			return;
		}
	}
#endif
	
	out->data.S->hash = hash;
	
	if( (int32_t) ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_vht_set( &C->stringtable, C, out, NULL );
		out->data.S->refcount--;
	}
}

static void var_create_obj( SGS_CTX, sgs_Variable* out, void* data, sgs_ObjInterface* iface, uint32_t xbytes )
{
	sgs_VarObj* obj = NULL;
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
		obj = sgs_Alloc_a( sgs_VarObj, xbytes );
	obj->appsize = xbytes;
	obj->data = data;
	if( xbytes )
		obj->data = ((char*)obj) + sizeof( sgs_VarObj );
	obj->iface = iface;
	obj->redblue = C->redblue;
	obj->next = C->objs;
	obj->prev = NULL;
	obj->refcount = 1;
	if( obj->next ) /* ! */
		obj->next->prev = obj;
	obj->metaobj = NULL;
	obj->mm_enable = SGS_FALSE;
	obj->in_setindex = SGS_FALSE;
	obj->is_iface = SGS_FALSE;
	C->objcount++;
	C->objs = obj;
	
	out->type = SGS_VT_OBJECT;
	out->data.O = obj;
}


/*
	Call stack
*/

static int vm_frame_push( SGS_CTX, sgs_Variable* func, uint16_t* T, sgs_instr_t* code, size_t icnt )
{
	sgs_StackFrame* F;
	
	if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
	{
		sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
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
	if( func && func->type == SGS_VT_FUNC )
	{
		sgs_iFunc* fn = func->data.F;
		F->lptr = F->iptr = F->code = sgs_func_bytecode( fn );
		F->iend = F->iptr + ( ( fn->size - fn->instr_off ) / sizeof( sgs_instr_t ) );
	}
	F->lntable = T;
	F->nfname = NULL;
	F->filename = C->filename;
	F->next = NULL;
	F->prev = C->sf_last;
	F->errsup = 0;
	if( C->sf_last )
	{
		F->errsup = C->sf_last->errsup;
		C->sf_last->next = F;
	}
	else
		C->sf_first = F;
	C->sf_last = F;
	
	if( C->hook_fn )
		C->hook_fn( C->hook_ctx, C, SGS_HOOK_ENTER );
	
	return 1;
}

static void vm_frame_pop( SGS_CTX )
{
	sgs_StackFrame* F = C->sf_last;
	
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
	(C->stack_top++)->type = SGS_VT_NULL;
}
static void stk_push_nulls( SGS_CTX, StkIdx cnt )
{
	sgs_VarPtr tgt;
	stk_makespace( C, cnt );
	tgt = C->stack_top + cnt;
	while( C->stack_top < tgt )
		(C->stack_top++)->type = SGS_VT_NULL;
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
	stk_insert_pos( C, off )->type = SGS_VT_NULL;
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
		cc->var.type = SGS_VT_NULL;
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
	case SGS_VT_NULL: return SGS_FALSE;
	case SGS_VT_BOOL: return var->data.B;
	case SGS_VT_INT: return var->data.I != 0;
	case SGS_VT_REAL: return var->data.R != 0;
	case SGS_VT_STRING: return !!var->data.S->size;
	case SGS_VT_FUNC: return SGS_TRUE;
	case SGS_VT_CFUNC: return SGS_TRUE;
	case SGS_VT_OBJECT:
		{
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( O->mm_enable )
			{
				_STACK_PROTECT;
				sgs_PushObjectPtr( C, O );
				if( _call_metamethod( C, O, "__tobool", sizeof("__tobool")-1, 0, NULL ) &&
					sgs_ItemType( C, 0 ) == SGS_VT_BOOL )
				{
					sgs_Bool v = !!stk_gettop( C )->data.B;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
			if( O->iface->convert )
			{
				SGSRESULT ret = SGS_EINPROC;
				_STACK_PROTECT;
				if( C->sf_count < SGS_MAX_CALL_STACK_SIZE )
				{
					C->sf_count++;
					ret = O->iface->convert( C, O, SGS_VT_BOOL );
					C->sf_count--;
				}
				else
					sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
				
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SGS_VT_BOOL )
				{
					sgs_Bool v = !!stk_gettop( C )->data.B;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
			return SGS_TRUE;
		}
	case SGS_VT_PTR: return var->data.P != NULL;
	}
	return SGS_FALSE;
}

static sgs_Int var_getint( SGS_CTX, sgs_VarPtr var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (sgs_Int) var->data.B;
	case SGS_VT_INT: return var->data.I;
	case SGS_VT_REAL: return (sgs_Int) var->data.R;
	case SGS_VT_STRING: return sgs_util_atoi( sgs_str_cstr( var->data.S ), var->data.S->size );
	case SGS_VT_OBJECT:
		{
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( O->mm_enable )
			{
				_STACK_PROTECT;
				sgs_PushObjectPtr( C, O );
				if( _call_metamethod( C, O, "__toint", sizeof("__toint")-1, 0, NULL ) &&
					sgs_ItemType( C, 0 ) == SGS_VT_INT )
				{
					sgs_Int v = stk_gettop( C )->data.I;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
			if( O->iface->convert )
			{
				SGSRESULT ret = SGS_EINPROC;
				_STACK_PROTECT;
				if( C->sf_count < SGS_MAX_CALL_STACK_SIZE )
				{
					C->sf_count++;
					ret = O->iface->convert( C, O, SGS_VT_INT );
					C->sf_count--;
				}
				else
					sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
				
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SGS_VT_INT )
				{
					sgs_Int v = stk_gettop( C )->data.I;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
		}
		break;
	case SGS_VT_PTR: return (sgs_Int) (size_t) var->data.P;
	}
	return 0;
}

static sgs_Real var_getreal( SGS_CTX, sgs_Variable* var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (sgs_Real) var->data.B;
	case SGS_VT_INT: return (sgs_Real) var->data.I;
	case SGS_VT_REAL: return var->data.R;
	case SGS_VT_STRING: return sgs_util_atof( sgs_str_cstr( var->data.S ), var->data.S->size );
	case SGS_VT_OBJECT:
		{
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( O->mm_enable )
			{
				_STACK_PROTECT;
				sgs_PushObjectPtr( C, O );
				if( _call_metamethod( C, O, "__toreal", sizeof("__toreal")-1, 0, NULL ) &&
					sgs_ItemType( C, 0 ) == SGS_VT_REAL )
				{
					sgs_Real v = stk_gettop( C )->data.R;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
			if( O->iface->convert )
			{
				SGSRESULT ret = SGS_EINPROC;
				_STACK_PROTECT;
				if( C->sf_count < SGS_MAX_CALL_STACK_SIZE )
				{
					C->sf_count++;
					ret = O->iface->convert( C, O, SGS_VT_REAL );
					C->sf_count--;
				}
				else
					sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
				
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SGS_VT_REAL )
				{
					sgs_Real v = stk_gettop( C )->data.R;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
		}
		break;
	case SGS_VT_PTR: return (sgs_Real) (size_t) var->data.P;
	}
	return 0;
}

static void* var_getptr( SGS_CTX, sgs_VarPtr var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (void*) (size_t) var->data.B;
	case SGS_VT_INT: return (void*) (size_t) var->data.I;
	case SGS_VT_REAL: return (void*) (size_t) var->data.R;
	case SGS_VT_STRING: return (void*) (size_t) sgs_str_cstr( var->data.S );
	case SGS_VT_OBJECT:
		{
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			if( O->mm_enable )
			{
				_STACK_PROTECT;
				sgs_PushObjectPtr( C, O );
				if( _call_metamethod( C, O, "__toptr", sizeof("__toptr")-1, 0, NULL ) &&
					sgs_ItemType( C, 0 ) == SGS_VT_PTR )
				{
					void* v = stk_gettop( C )->data.P;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
			if( O->iface->convert )
			{
				SGSRESULT ret = SGS_EINPROC;
				_STACK_PROTECT;
				if( C->sf_count < SGS_MAX_CALL_STACK_SIZE )
				{
					C->sf_count++;
					ret = O->iface->convert( C, O, SGS_VT_PTR );
					C->sf_count--;
				}
				else
					sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
				
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SGS_VT_PTR )
				{
					void* v = stk_gettop( C )->data.P;
					_STACK_UNPROTECT;
					return v;
				}
				_STACK_UNPROTECT;
			}
			return O->data;
		}
	case SGS_VT_PTR: return var->data.P;
	}
	return NULL;
}

static SGS_INLINE sgs_Int var_getint_simple( sgs_VarPtr var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (sgs_Int) var->data.B;
	case SGS_VT_INT: return var->data.I;
	case SGS_VT_REAL: return (sgs_Int) var->data.R;
	case SGS_VT_STRING: return sgs_util_atoi( sgs_str_cstr( var->data.S ), var->data.S->size );
	}
	return 0;
}

static SGS_INLINE sgs_Real var_getreal_simple( sgs_Variable* var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (sgs_Real) var->data.B;
	case SGS_VT_INT: return (sgs_Real) var->data.I;
	case SGS_VT_REAL: return var->data.R;
	case SGS_VT_STRING: return sgs_util_atof( sgs_str_cstr( var->data.S ), var->data.S->size );
	}
	return 0;
}

#define var_initnull( v ) \
do{ sgs_VarPtr __var = (v); __var->type = SGS_VT_NULL; }while(0)
#define var_initbool( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SGS_VT_BOOL; __var->data.B = value; }while(0)
#define var_initint( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SGS_VT_INT; __var->data.I = value; }while(0)
#define var_initreal( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SGS_VT_REAL; __var->data.R = value; }while(0)
#define var_initptr( v, value ) \
do{ sgs_VarPtr __var = (v); __var->type = SGS_VT_PTR; __var->data.P = value; }while(0)

#define var_setnull( C, v ) \
do{ sgs_VarPtr var = (v); VAR_RELEASE( var ); var->type = SGS_VT_NULL; }while(0)
#define var_setbool( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SGS_VT_BOOL ) \
	{ VAR_RELEASE( var ); var->type = SGS_VT_BOOL; } var->data.B = value; }while(0)
#define var_setint( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SGS_VT_INT ) \
	{ VAR_RELEASE( var ); var->type = SGS_VT_INT; } var->data.I = value; }while(0)
#define var_setreal( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SGS_VT_REAL ) \
	{ VAR_RELEASE( var ); var->type = SGS_VT_REAL; } var->data.R = value; }while(0)
#define var_setptr( C, v, value ) \
do{ sgs_VarPtr var = (v); if( var->type != SGS_VT_PTR ) \
	{ VAR_RELEASE( var ); var->type = SGS_VT_PTR; } var->data.P = value; }while(0)


static void init_var_string( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	char buf[ 32 ];
	out->type = SGS_VT_NULL;
	out->data.S = NULL;
	switch( var->type )
	{
	case SGS_VT_NULL: var_create_str( C, out, "null", 4 ); break;
	case SGS_VT_BOOL: if( var->data.B ) var_create_str( C, out, "true", 4 ); else var_create_str( C, out, "false", 5 ); break;
	case SGS_VT_INT: sprintf( buf, "%" PRId64, var->data.I ); var_create_cstr( C, out, buf ); break;
	case SGS_VT_REAL: sprintf( buf, "%g", var->data.R ); var_create_cstr( C, out, buf ); break;
	case SGS_VT_FUNC: var_create_str( C, out, "function", 8 ); break;
	case SGS_VT_CFUNC: var_create_str( C, out, "C function", 10 ); break;
	case SGS_VT_OBJECT:
		{
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			
			if( O->mm_enable )
			{
				_STACK_PROTECT;
				sgs_PushObjectPtr( C, O );
				if( _call_metamethod( C, O, "__tostring", sizeof("__tostring")-1, 0, NULL ) &&
					sgs_ItemType( C, 0 ) == SGS_VT_STRING )
				{
					*out = *stk_gettop( C );
					out->data.S->refcount++; /* cancel release from stack to transfer successfully */
					_STACK_UNPROTECT;
					break;
				}
				_STACK_UNPROTECT;
			}
			if( O->iface->convert )
			{
				SGSRESULT ret = SGS_EINPROC;
				_STACK_PROTECT;
				if( C->sf_count < SGS_MAX_CALL_STACK_SIZE )
				{
					C->sf_count++;
					ret = O->iface->convert( C, O, SGS_VT_STRING );
					C->sf_count--;
				}
				else
					sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
				
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 && stk_gettop( C )->type == SGS_VT_STRING )
				{
					*out = *stk_gettop( C );
					out->data.S->refcount++; /* cancel release from stack to transfer successfully */
					_STACK_UNPROTECT;
					break;
				}
				_STACK_UNPROTECT;
			}
			var_create_cstr( C, out, O->iface->name );
		}
		break;
	case SGS_VT_PTR:
		sprintf( buf, "ptr(%p)", var->data.P );
		var_create_cstr( C, out, buf );
		break;
	}
	sgs_BreakIf( out->type != SGS_VT_STRING );
}


static void vm_convert_bool( SGS_CTX, sgs_Variable* var )
{
	sgs_Bool value;
	if( var->type == SGS_VT_BOOL )
		return;
	value = var_getbool( C, var );
	VAR_RELEASE( var );
	var_initbool( var, value );
}

static void vm_convert_int( SGS_CTX, sgs_Variable* var )
{
	sgs_Int value;
	if( var->type == SGS_VT_INT )
		return;
	value = var_getint( C, var );
	VAR_RELEASE( var );
	var_initint( var, value );
}

static void vm_convert_real( SGS_CTX, sgs_Variable* var )
{
	sgs_Real value;
	if( var->type == SGS_VT_REAL )
		return;
	value = var_getreal( C, var );
	VAR_RELEASE( var );
	var_initreal( var, value );
}

static void vm_convert_string( SGS_CTX, sgs_Variable* var )
{
	sgs_Variable out;
	if( var->type == SGS_VT_STRING )
		return;
	init_var_string( C, &out, var );
	VAR_RELEASE( var );
	*var = out;
}

static void vm_convert_ptr( SGS_CTX, sgs_Variable* var )
{
	void* value;
	if( var->type == SGS_VT_PTR )
		return;
	value = var_getptr( C, var );
	VAR_RELEASE( var );
	var_initptr( var, value );
}


#define CI stk_getpos( C, item )
static void vm_convert_stack_bool( SGS_CTX, StkIdx item )
{
	if( CI->type != SGS_VT_BOOL )
	{
		sgs_Bool v = var_getbool( C, CI );
		var_setbool( C, CI, v );
	}
}

static void vm_convert_stack_int( SGS_CTX, StkIdx item )
{
	if( CI->type != SGS_VT_INT )
	{
		sgs_Int v = var_getint( C, CI );
		var_setint( C, CI, v );
	}
}

static void vm_convert_stack_real( SGS_CTX, StkIdx item )
{
	if( CI->type != SGS_VT_REAL )
	{
		sgs_Real v = var_getreal( C, CI );
		var_setreal( C, CI, v );
	}
}

static void vm_convert_stack_string( SGS_CTX, StkIdx item )
{
	sgs_Variable out;
	if( CI->type == SGS_VT_STRING )
		return;
	init_var_string( C, &out, CI );
	stk_setvar_leave( C, item, &out );
}

static void vm_convert_stack_ptr( SGS_CTX, StkIdx item )
{
	if( CI->type != SGS_VT_PTR )
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
	case SGS_VT_NULL:   ty = "null"; break;
	case SGS_VT_BOOL:   ty = "bool"; break;
	case SGS_VT_INT:    ty = "int"; break;
	case SGS_VT_REAL:   ty = "real"; break;
	case SGS_VT_STRING: ty = "string"; break;
	case SGS_VT_CFUNC:  ty = "cfunc"; break;
	case SGS_VT_FUNC:   ty = "func"; break;
	case SGS_VT_OBJECT:
		{
			sgs_VarObj* O = A->data.O;
			_STACK_PREPARE;
			
			if( O->mm_enable )
			{
				_STACK_PROTECT;
				sgs_PushObjectPtr( C, O );
				if( _call_metamethod( C, O, "__typeof", sizeof("__typeof")-1, 0, NULL ) &&
					sgs_ItemType( C, 0 ) == SGS_VT_STRING )
				{
					_STACK_UNPROTECT_SKIP( 1 );
					return SGS_SUCCESS;
				}
				_STACK_UNPROTECT;
			}
			if( O->iface->convert )
			{
				SGSRESULT ret = SGS_EINPROC;
				_STACK_PROTECT;
				if( C->sf_count < SGS_MAX_CALL_STACK_SIZE )
				{
					C->sf_count++;
					ret = O->iface->convert( C, O, SGS_CONVOP_TYPEOF );
					C->sf_count--;
				}
				else
					sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
				
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 && sgs_ItemType( C, 0 ) == SGS_VT_STRING )
				{
					_STACK_UNPROTECT_SKIP( 1 );
					return SGS_SUCCESS;
				}
				_STACK_UNPROTECT;
			}
			ty = O->iface->name ? O->iface->name : "object";
		}
		break;
	case SGS_VT_PTR:    ty = "pointer"; break;
	}
	
	sgs_PushString( C, ty );
	return SGS_SUCCESS;
}

static SGSRESULT vm_gcmark( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SGS_VT_OBJECT ||
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
		if( O->metaobj )
		{
			if( SGS_FAILED( sgs_ObjGCMark( C, O->metaobj ) ) )
				return SGS_EINPROC;
		}
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
	if( SGS_FAILED( ret ) )
		return sgs_Msg( C, SGS_WARNING, "failed with error %d (%s)", ret,
			sgs_CodeString( SGS_CODE_ER, ret ) );
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
	
	sgs_PushItem( C, 1 );
	for( i = 0; i < asize; ++i )
		sgs_PushNumIndex( C, 2, i );
	sgs_PushItem( C, 0 );
	ret = sgs_ThisCall( C, asize, C->sf_last->expected );
	if( SGS_FAILED( ret ) )
		return sgs_Msg( C, SGS_WARNING, "failed with error %d (%s)", ret,
			sgs_CodeString( SGS_CODE_ER, ret ) );
	return C->sf_last->expected;
}


static int vm_getidx_builtin( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx )
{
	int res;
	sgs_Int pos, size;
	if( obj->type == SGS_VT_STRING )
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
		sgs_InitStringBuf( C, outmaybe, sgs_var_cstr( obj ) + pos, 1 );
		return 0;
	}

	sgs_Msg( C, SGS_WARNING, "Cannot index variable of type '%s'", TYPENAME( obj->type ) );
	return SGS_ENOTFND;
}

static int vm_getprop_builtin( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx )
{
	if( idx->type == SGS_VT_STRING )
	{
		const char* prop = sgs_var_cstr( idx );
		
		switch( obj->type )
		{
		case SGS_VT_STRING:
			if( !strcmp( prop, "length" ) )
			{
				outmaybe->type = SGS_VT_INT;
				outmaybe->data.I = obj->data.S->size;
				return 0;
			}
			break;
		case SGS_VT_FUNC:
		case SGS_VT_CFUNC:
			if( !strcmp( prop, "call" ) )
			{
				outmaybe->type = SGS_VT_CFUNC;
				outmaybe->data.C = sgs_specfn_call;
				return 0;
			}
			if( !strcmp( prop, "apply" ) )
			{
				outmaybe->type = SGS_VT_CFUNC;
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



/* PREDECL */
static SGSMIXED vm_getprop( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx, int isprop );

static SGSRESULT vm_runerr_getprop( SGS_CTX, SGSRESULT type, StkIdx origsize,
	sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	if( type == SGS_ENOTFND )
	{
		char* p;
		const char* err;
		sgs_Variable cidx = *idx;
		
		if( obj->type == SGS_VT_OBJECT && obj->data.O->metaobj )
		{
			sgs_Variable tmp;
			if( obj->data.O->mm_enable )
			{
				_STACK_PREPARE;
				_STACK_PROTECT;
				sgs_PushObjectPtr( C, obj->data.O );
				sgs_PushVariable( C, idx );
				if( _call_metamethod( C, obj->data.O, "__getindex", sizeof("__getindex")-1, 1, NULL )
					&& C->num_last_returned > 0 )
				{
					_STACK_UNPROTECT_SKIP( 1 );
					return 1;
				}
				_STACK_UNPROTECT;
			}
			
			tmp.type = SGS_VT_OBJECT;
			tmp.data.O = obj->data.O->metaobj;
			return vm_getprop( C, outmaybe, &tmp, &cidx, isprop );
		}
		
		err = isprop ? "Readable property not found" : "Cannot find readable value by index";
		stk_push( C, &cidx );
		p = sgs_ToString( C, -1 );
		sgs_Msg( C, SGS_WARNING, "%s: \"%s\"", err, p );
	}
	else if( type == SGS_EBOUNDS )
	{
		sgs_Msg( C, SGS_WARNING, "Index out of bounds" );
	}
	else if( type == SGS_EINVAL )
	{
		sgs_Msg( C, SGS_WARNING, "Invalid value type used for %s read",
			isprop ? "property" : "index" );
	}
	else if( type == SGS_EINPROC )
	{
		sgs_Cntl( C, SGS_CNTL_ERRSUP, 0 ); /* fatal error */
		sgs_Msg( C, SGS_ERROR, "%s read process interrupted, possibly by infinite recursion",
			isprop ? "Property" : "Index" );
	}
	else if( type == SGS_ENOTSUP )
	{
		sgs_Msg( C, SGS_WARNING, "%s read operation not supported on the given variable type",
			isprop ? "Property" : "Index" );
	}
	else
	{
		sgs_Msg( C, SGS_WARNING, "Unknown error on %s read", isprop ? "property" : "index" );
	}
	
	stk_pop( C, SGS_STACKFRAMESIZE - origsize );
	return type;
}
#define VM_GETPROP_ERR( type ) vm_runerr_getprop( C, type, origsize, outmaybe, obj, idx, isprop )

static SGSRESULT vm_runerr_setprop( SGS_CTX, SGSRESULT type, StkIdx origsize, sgs_Variable* idx, int isprop )
{
	if( type == SGS_ENOTFND )
	{
		char* p;
		const char* err = isprop ? "Writable property not found" : "Cannot find writable value by index";
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
		sgs_Msg( C, SGS_WARNING, "Invalid value type used for %s write",
			isprop ? "property" : "index" );
	}
	else if( type == SGS_EINPROC )
	{
		sgs_Cntl( C, SGS_CNTL_ERRSUP, 0 ); /* fatal error */
		sgs_Msg( C, SGS_ERROR, "%s write process interrupted, possibly by infinite recursion",
			isprop ? "Property" : "Index" );
	}
	else if( type == SGS_ENOTSUP )
	{
		sgs_Msg( C, SGS_WARNING, "%s write operation not supported on the given variable type",
			isprop ? "Property" : "Index" );
	}
	else
	{
		sgs_Msg( C, SGS_WARNING, "Unknown error on %s write", isprop ? "property" : "index" );
	}
	
	stk_pop( C, SGS_STACKFRAMESIZE - origsize );
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
	int ret = SGS_ENOTSUP, isobj = obj->type == SGS_VT_OBJECT;
	StkIdx origsize = SGS_STACKFRAMESIZE;
	
	if( isobj && obj->data.O->iface == sgsstd_dict_iface )
	{
		sgs_VHTable* ht = (sgs_VHTable*) obj->data.O->data;
		if( idx->type == SGS_VT_INT && isprop )
		{
			int32_t off = (int32_t) idx->data.I;
			if( off < 0 || off >= sgs_vht_size( ht ) )
				return VM_GETPROP_ERR( SGS_EBOUNDS );
			else
			{
				var_setvar( C, outmaybe, &ht->vars[ off ].val );
				return 0;
			}
		}
		else if( idx->type == SGS_VT_STRING )
		{
			sgs_VHTVar* var = sgs_vht_get( ht, idx );
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
				sgs_VHTVar* var = sgs_vht_get( ht, stk_gettop( C ) );
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
		sgs_VHTVar* var;
		sgs_VHTable* ht = (sgs_VHTable*) obj->data.O->data;
		/* sgs_vht_get does not modify search key */
		var = sgs_vht_get( ht, idx );
		
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
		if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 )
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
	int ret = 0;
	StkIdx origsize = SGS_STACKFRAMESIZE;
	
	if( isprop && idx->type != SGS_VT_INT && idx->type != SGS_VT_STRING )
	{
		ret = SGS_EINVAL;
	}
	else if( obj->type == SGS_VT_OBJECT && obj->data.O->metaobj &&
		obj->data.O->mm_enable && !obj->data.O->in_setindex )
	{
		_STACK_PREPARE;
		_STACK_PROTECT;
		sgs_PushObjectPtr( C, obj->data.O );
		sgs_PushVariable( C, idx );
		sgs_PushVariable( C, src );
		obj->data.O->in_setindex = SGS_TRUE;
		if( !_call_metamethod( C, obj->data.O, "__setindex", sizeof("__setindex")-1, 2, NULL ) )
		{
			_STACK_UNPROTECT;
			goto nextcase;
		}
		obj->data.O->in_setindex = SGS_FALSE;
		_STACK_UNPROTECT;
	}
	else if( obj->type == SGS_VT_OBJECT && obj->data.O->iface->setindex )
	{
nextcase:;
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
	
	if( SGS_FAILED( ret ) )
		return VM_SETPROP_ERR( ret );
	
	stk_pop( C, SGS_STACKFRAMESIZE - origsize );
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
	if( var->type == SGS_VT_OBJECT )
	{
		int ret = SGS_ENOTFND;
		sgs_VarObj* O = var->data.O;
		if( O->mm_enable )
		{
			_STACK_PREPARE;
			_STACK_PROTECT;
			sgs_PushObjectPtr( C, O );
			if( _call_metamethod( C, O, "__clone", sizeof("__clone")-1, 0, NULL ) )
			{
				_STACK_UNPROTECT_SKIP( 1 );
				return SGS_SUCCESS;
			}
		}
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
	if( SGS_STACKFRAMESIZE < args )
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
		memcpy( sgs_var_cstr( &N ) + curoff, sgs_var_cstr( var ), var->data.S->size );
		curoff += var->data.S->size;
	}
	var_finalize_str( C, &N );
	stk_setvar_leave( C, -args, &N );
	stk_pop( C, args - 1 );
	return 1;
}

static void vm_op_concat( SGS_CTX, StkIdx out, sgs_Variable* A, sgs_Variable* B )
{
	sgs_Variable lA = *A, lB = *B;
	int ssz = SGS_STACKFRAMESIZE;
	stk_push( C, &lA );
	stk_push( C, &lB );
	vm_op_concat_ex( C, 2 );
	stk_setlvar( C, out, stk_gettop( C ) );
	stk_pop( C, SGS_STACKFRAMESIZE - ssz );
}

static SGSBOOL vm_op_negate( SGS_CTX, sgs_Variable* out, sgs_Variable* A )
{
	sgs_Variable lA = *A;
	VAR_ACQUIRE( &lA );
	VAR_RELEASE( out );
	switch( lA.type )
	{
	case SGS_VT_NULL: /* guaranteed to be NULL after release */ break;
	case SGS_VT_BOOL: var_initint( out, -lA.data.B ); break;
	case SGS_VT_INT: var_initint( out, -lA.data.I ); break;
	case SGS_VT_REAL: var_initreal( out, -lA.data.R ); break;
	case SGS_VT_OBJECT:
		{
			int ret = SGS_ENOTFND;
			sgs_VarObj* O = lA.data.O;
			/* WP: stack limit */
			if( O->mm_enable )
			{
				_STACK_PREPARE;
				StkIdx ofs;
				_STACK_PROTECT;
				ofs = (StkIdx) ( out - C->stack_off );
				sgs_PushObjectPtr( C, O );
				if( _call_metamethod( C, O, "__negate", sizeof("__negate")-1, 0, NULL ) )
				{
					C->stack_off[ ofs ] = *stk_gettop( C );
					stk_pop1nr( C );
					_STACK_UNPROTECT;
					goto done;
				}
				_STACK_UNPROTECT;
			}
			if( O->iface->expr )
			{
				_STACK_PREPARE;
				StkIdx ofs;
				_STACK_PROTECT;
				ofs = (StkIdx) ( out - C->stack_off );
				ret = O->iface->expr( C, O, A, NULL, SGS_EOP_NEGATE );
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 )
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
		VAR_RELEASE( &lA );
		return 0;
	default:
		sgs_Msg( C, SGS_WARNING, "Negating variable of type %s is not supported.", TYPENAME( lA.type ) );
		VAR_RELEASE( &lA );
		return 0;
	}
	
done:
	VAR_RELEASE( &lA );
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

static SGSRESULT vm_op_incdec( SGS_CTX, sgs_VarPtr out, sgs_Variable *A, int diff )
{
	switch( A->type )
	{
	case SGS_VT_INT: var_setint( C, out, A->data.I + diff ); break;
	case SGS_VT_REAL: var_setreal( C, out, A->data.R + diff ); break;
	default:
		var_setnull( C, out );
		sgs_Msg( C, SGS_ERROR, "Cannot %screment non-numeric variables!", diff > 0 ? "in" : "de" );
		return SGS_EINVAL;
	}
	return SGS_SUCCESS;
}


#define ARITH_OP_ADD	SGS_EOP_ADD
#define ARITH_OP_SUB	SGS_EOP_SUB
#define ARITH_OP_MUL	SGS_EOP_MUL
#define ARITH_OP_DIV	SGS_EOP_DIV
#define ARITH_OP_MOD	SGS_EOP_MOD
static const char* mm_arith_ops[] =
{
	"__add",
	"__sub",
	"__mul",
	"__div",
	"__mod",
};
static SGSRESULT vm_arith_op( SGS_CTX, sgs_VarPtr out, sgs_VarPtr a, sgs_VarPtr b, int op )
{
	if( a->type == SGS_VT_REAL && b->type == SGS_VT_REAL )
	{
		sgs_Real A = a->data.R, B = b->data.R, R;
		switch( op ){
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			case ARITH_OP_DIV: if( B == 0 ) goto div0err; R = A / B; break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = fmod( A, B ); break;
			default: R = 0; break;
		}
		var_setreal( C, out, R );
		return SGS_SUCCESS;
	}
	if( a->type == SGS_VT_INT && b->type == SGS_VT_INT )
	{
		sgs_Int A = a->data.I, B = b->data.I, R;
		switch( op ){
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			case ARITH_OP_DIV: if( B == 0 ) goto div0err;
				var_setreal( C, out, ((sgs_Real) A) / ((sgs_Real) B) ); return SGS_SUCCESS; break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = A % B; break;
			default: R = 0; break;
		}
		var_setint( C, out, R );
		return SGS_SUCCESS;
	}
	
	if( a->type == SGS_VT_OBJECT || b->type == SGS_VT_OBJECT )
	{
		int ret;
		StkIdx ofs;
		sgs_Variable lA = *a, lB = *b;
		VAR_ACQUIRE( &lA );
		VAR_ACQUIRE( &lB );
		/* WP: stack limit */
		ofs = (StkIdx) ( out - C->stack_off );
		
		if( a->type == SGS_VT_OBJECT && a->data.O->mm_enable )
		{
			sgs_VarObj* O = a->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			sgs_PushObjectPtr( C, O );
			sgs_PushObjectPtr( C, O );
			sgs_PushObjectPtr( C, b->data.O );
			if( _call_metamethod( C, O, mm_arith_ops[ op ], 5, 2, NULL ) )
			{
				_STACK_UNPROTECT_SKIP( 1 );
				stk_setlvar_leave( C, ofs, C->stack_top - 1 );
				C->stack_top--; /* skip acquire/release */
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return SGS_SUCCESS;
			}
			else
			{
				_STACK_UNPROTECT;
			}
		}
		
		if( b->type == SGS_VT_OBJECT && b->data.O->mm_enable )
		{
			sgs_VarObj* O = b->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			sgs_PushObjectPtr( C, O );
			sgs_PushObjectPtr( C, a->data.O );
			sgs_PushObjectPtr( C, O );
			if( _call_metamethod( C, O, mm_arith_ops[ op ], 5, 2, NULL ) )
			{
				_STACK_UNPROTECT_SKIP( 1 );
				stk_setlvar_leave( C, ofs, C->stack_top - 1 );
				C->stack_top--; /* skip acquire/release */
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return SGS_SUCCESS;
			}
			else
			{
				_STACK_UNPROTECT;
			}
		}
		
		if( a->type == SGS_VT_OBJECT && a->data.O->iface->expr )
		{
			sgs_VarObj* O = a->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, op );
			USING_STACK
			ret = SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1;
			if( ret )
			{
				_STACK_UNPROTECT_SKIP( 1 );
				stk_setlvar_leave( C, ofs, C->stack_top - 1 );
				C->stack_top--; /* skip acquire/release */
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return SGS_SUCCESS;
			}
			else
			{
				_STACK_UNPROTECT;
			}
		}
		
		if( b->type == SGS_VT_OBJECT && b->data.O->iface->expr )
		{
			sgs_VarObj* O = b->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, op );
			USING_STACK
			ret = SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1;
			if( ret )
			{
				_STACK_UNPROTECT_SKIP( 1 );
				stk_setlvar_leave( C, ofs, C->stack_top - 1 );
				C->stack_top--; /* skip acquire/release */
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return SGS_SUCCESS;
			}
			else
			{
				_STACK_UNPROTECT;
			}
		}
		
		VAR_RELEASE( &lA );
		VAR_RELEASE( &lB );
		goto fail;
	}
	
	/* if either variable is of a basic callable type */
	if( a->type == SGS_VT_FUNC || a->type == SGS_VT_CFUNC ||
		b->type == SGS_VT_FUNC || b->type == SGS_VT_CFUNC ||
		a->type == SGS_VT_PTR  || b->type == SGS_VT_PTR   )
		goto fail;
	
	/* if either are REAL or STRING */
	if( a->type == SGS_VT_REAL || b->type == SGS_VT_STRING ||
		a->type == SGS_VT_STRING || b->type == SGS_VT_REAL )
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
				var_setreal( C, out, ((sgs_Real) A) / ((sgs_Real) B) ); return SGS_SUCCESS;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = A % B; break;
			default: R = 0; break;
		}
		var_setint( C, out, R );
	}
	return SGS_SUCCESS;
	
div0err:
	VAR_RELEASE( out );
	sgs_Msg( C, SGS_ERROR, "Division by 0" );
	return SGS_EINPROC;
fail:
	VAR_RELEASE( out );
	sgs_Msg( C, SGS_ERROR, "Specified arithmetic operation is not supported on the given set of arguments" );
	return SGS_EINVAL;
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
	if( ta == SGS_VT_INT && tb == SGS_VT_INT ) return _SGS_SIGNDIFF( a->data.I, b->data.I );
	/* both are REAL */
	if( ta == SGS_VT_REAL && tb == SGS_VT_REAL ) return _SGS_SIGNDIFF( a->data.R, b->data.R );
	
	/* either is OBJECT */
	if( ta == SGS_VT_OBJECT || tb == SGS_VT_OBJECT )
	{
		int ret = SGS_ENOTSUP, suc;
		sgs_Real out = _SGS_SIGNDIFF( ta, tb );
		sgs_Variable lA = *a, lB = *b;
		VAR_ACQUIRE( &lA );
		VAR_ACQUIRE( &lB );
		
		if( ta == SGS_VT_OBJECT && a->data.O->mm_enable )
		{
			sgs_VarObj* O = a->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			sgs_PushObjectPtr( C, O );
			sgs_PushObjectPtr( C, O );
			sgs_PushObjectPtr( C, b->data.O );
			if( _call_metamethod( C, O, "__compare", sizeof("__compare")-1, 2, NULL ) )
			{
				out = var_getreal( C, C->stack_top - 1 );
				_STACK_UNPROTECT;
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return _SGS_SIGNDIFF( out, 0 );
			}
			else
			{
				_STACK_UNPROTECT;
			}
		}
		
		if( tb == SGS_VT_OBJECT && b->data.O->mm_enable )
		{
			sgs_VarObj* O = b->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			sgs_PushObjectPtr( C, O );
			sgs_PushObjectPtr( C, a->data.O );
			sgs_PushObjectPtr( C, O );
			if( _call_metamethod( C, O, "__compare", sizeof("__compare")-1, 2, NULL ) )
			{
				out = var_getreal( C, C->stack_top - 1 );
				_STACK_UNPROTECT;
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return _SGS_SIGNDIFF( out, 0 );
			}
			else
			{
				_STACK_UNPROTECT;
			}
		}
		
		if( ta == SGS_VT_OBJECT && a->data.O->iface->expr )
		{
			sgs_VarObj* O = a->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, SGS_EOP_COMPARE );
			USING_STACK
			suc = SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1;
			if( suc )
				out = var_getreal( C, C->stack_top - 1 );
			_STACK_UNPROTECT;
			if( suc )
			{
				VAR_RELEASE( &lA );
				VAR_RELEASE( &lB );
				return _SGS_SIGNDIFF( out, 0 );
			}
		}
		
		if( tb == SGS_VT_OBJECT && b->data.O->iface->expr )
		{
			sgs_VarObj* O = b->data.O;
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->expr( C, O, &lA, &lB, SGS_EOP_COMPARE );
			USING_STACK
			suc = SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1;
			if( suc )
				out = var_getreal( C, C->stack_top - 1 );
			_STACK_UNPROTECT;
			if( suc )
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
	if( ( ta == SGS_VT_FUNC || ta == SGS_VT_CFUNC ) &&
		( tb == SGS_VT_FUNC || tb == SGS_VT_CFUNC ) )
	{
		if( ta != tb )
			return _SGS_SIGNDIFF( ta, tb );
		if( ta == SGS_VT_FUNC )
			return _SGS_SIGNDIFF( a->data.F, b->data.F );
		else
			return _SGS_SIGNDIFF( (void*)a->data.C, (void*)b->data.C );
	}
	
	/* either is STRING */
	if( ta == SGS_VT_STRING || tb == SGS_VT_STRING )
	{
		/* other is NULL */
		if( ta == SGS_VT_NULL || tb == SGS_VT_NULL )
			return _SGS_SIGNDIFF( ta, tb );
		
		ptrdiff_t out;
		sgs_Variable A = *a, B = *b;
		stk_push( C, &A );
		stk_push( C, &B );
		vm_convert_stack_string( C, -2 );
		vm_convert_stack_string( C, -1 );
		a = stk_getpos( C, -2 );
		b = stk_getpos( C, -1 );
#if SGS_STRINGTABLE_MAXLEN >= 0x7fffffff
		if( a->data.S == b->data.S )
		{
			stk_pop2( C );
			return 0;
		}
#endif
		out = memcmp( sgs_var_cstr( a ), sgs_var_cstr( b ), SGS_MIN( a->data.S->size, b->data.S->size ) );
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
	
	if( obj->type != SGS_VT_OBJECT )
	{
		sgs_Msg( C, SGS_ERROR, "Variable of type '%s' "
			"doesn't have an iterator", TYPENAME( obj->type ) );
		return SGS_ENOTSUP;
	}
	
	if( O->iface->convert )
	{
		_STACK_PREPARE;
		_STACK_PROTECT;
		ret = SGS_SUCCEEDED( O->iface->convert( C, O, SGS_CONVOP_TOITER ) ) && SGS_STACKFRAMESIZE >= 1;
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
	
	if( iter->type != SGS_VT_OBJECT )
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
	if( SGS_FAILED( ret ) || SGS_STACKFRAMESIZE < expargs )
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


static int vm_exec( SGS_CTX, sgs_Variable* consts, sgs_rcpos_t constcount );


/*
	Call the virtual machine.
	Args must equal the number of arguments pushed before the function
	
	- function expects the following items on stack: [this][args]
	- before call, stack is set up to display only the [args] part
	- [this] can be uncovered with sgs_Method
	- arguments are transposed to form [invextraargs][this][reqargs]
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
	
	sgs_BreakIf( SGS_STACKFRAMESIZE < args + gotthis );
	allowed = vm_frame_push( C, &V, NULL, NULL, 0 );
	C->stack_off = C->stack_top - args;
	C->clstk_off = C->clstk_top - clsr;
	
	if( allowed )
	{
		/* WP (x2): stack size limit */
		C->sf_last->argbeg = (StkIdx) stkcallbase;
		C->sf_last->argend = (StkIdx) ( C->stack_top - C->stack_base );
		/* WP: argument count limit */
		C->sf_last->argcount = (uint8_t) args;
		C->sf_last->inexp = (uint8_t) args;
		/* WP: returned value count limit */
		C->sf_last->expected = (uint8_t) expect;
		C->sf_last->flags = gotthis ? SGS_SF_METHOD : 0;
		
		if( V.type == SGS_VT_CFUNC )
		{
			rvc = (*V.data.C)( C );
			if( rvc > SGS_STACKFRAMESIZE )
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
		else if( V.type == SGS_VT_FUNC )
		{
			sgs_iFunc* F = V.data.F;
			int stkargs = args + ( F->gotthis && gotthis );
			int expargs = F->numargs + F->gotthis;
			
			C->sf_last->inexp = F->numargs;
			
			/* if <this> was expected but wasn't properly passed, insert a NULL in its place */
			if( F->gotthis && !gotthis )
			{
				stk_insert_null( C, 0 );
				C->stack_off++;
				C->sf_last->argend++;
				gotthis = SGS_TRUE;
				stkargs = args + ( F->gotthis && gotthis );
			}
			/* if <this> wasn't expected but was passed, ignore it */
			
			/* add flag to specify presence of "this" */
			if( F->gotthis )
				C->sf_last->flags |= SGS_SF_HASTHIS;
			/* fix argument stack */
			if( stkargs > expargs )
			{
				int first = F->numargs + gotthis;
				int all = args + gotthis;
				stk_transpose( C, first, all );
				C->stack_off += all - first;
			}
			else
				stk_push_nulls( C, expargs - stkargs );
			
			stk_push_nulls( C, F->numtmp );

			if( F->gotthis && gotthis ) C->stack_off--;
			{
				/* WP: const limit */
				sgs_rcpos_t constcnt = (sgs_rcpos_t) ( F->instr_off / sizeof( sgs_Variable* ) );
				rvc = vm_exec( C, sgs_func_consts( F ), constcnt );
			}
			if( F->gotthis && gotthis ) C->stack_off++;
		}
		else if( V.type == SGS_VT_OBJECT )
		{
			sgs_VarObj* O = V.data.O;
			
			rvc = SGS_ENOTSUP;
			if( O->mm_enable )
			{
				sgs_Variable objfunc;
				sgs_PushString( C, "__call" );
				rvc = sgs_GetIndexPIP( C, &V, -1, &objfunc, 0 );
				stk_pop1( C );
				if( SGS_SUCCEEDED( rvc ) )
				{
					if( gotthis )
						sgs_Method( C );
					else
					{
						sgs_InsertVariable( C, 0, &V );
						gotthis = 1;
					}
					if( vm_call( C, args, clsr, gotthis, expect, &objfunc ) )
						rvc = expect;
					else
						rvc = SGS_EINPROC;
					sgs_Release( C, &objfunc );
				}
				else
				{
					rvc = SGS_ENOTSUP;
				}
			}
			
			if( rvc == SGS_ENOTSUP && O->iface->call )
				rvc = O->iface->call( C, O );
			
			if( rvc > SGS_STACKFRAMESIZE )
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
				"cannot be called", TYPENAME( V.type ) );
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
	{
		if( ret && C->sf_last->flags & SGS_SF_ABORTED )
			ret = -1;
		vm_frame_pop( C );
	}
	
	C->num_last_returned = rvc;
	if( rvc > expect )
		stk_pop( C, rvc - expect );
	else
		stk_push_nulls( C, expect - rvc );
	
	return ret;
}


#if SGS_DEBUG && SGS_DEBUG_VALIDATE
static SGS_INLINE sgs_Variable* const_getvar( sgs_Variable* consts, sgs_rcpos_t count, sgs_rcpos_t off )
{
	sgs_BreakIf( off >= count );
	return consts + off;
}
#endif

/*
	Main VM execution loop
*/
static int vm_exec( SGS_CTX, sgs_Variable* consts, sgs_rcpos_t constcount )
{
	sgs_StackFrame* SF = C->sf_last;
	int32_t ret = 0;
	sgs_Variable* cptr = consts;
	const sgs_instr_t* pend = SF->iend;

#if SGS_DEBUG && SGS_DEBUG_VALIDATE
	ptrdiff_t stkoff = C->stack_top - C->stack_off;
#  define RESVAR( v ) ( SGS_CONSTVAR(v) ? const_getvar( cptr, constcount, SGS_CONSTDEC(v) ) : stk_getlpos( C, (v) ) )
#else
#  define RESVAR( v ) ( SGS_CONSTVAR(v) ? ( cptr + SGS_CONSTDEC(v) ) : stk_getlpos( C, (v) ) )
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
		const sgs_instr_t I = *SF->iptr;
#define pp SF->iptr
#define instr SGS_INSTR_GET_OP(I)
#define argA SGS_INSTR_GET_A(I)
#define argB SGS_INSTR_GET_B(I)
#define argC SGS_INSTR_GET_C(I)
#define argE SGS_INSTR_GET_E(I)
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

		if( C->hook_fn )
			C->hook_fn( C->hook_ctx, C, SGS_HOOK_STEP );

		switch( instr )
		{
		case SGS_SI_NOP: break;

		case SGS_SI_PUSH:
		{
			sgs_Variable var = *RESVAR( argB );
			stk_push( C, &var );
			break;
		}
		
		case SGS_SI_INT:
		{
			switch( argC )
			{
			case SGS_INT_ERRSUP_INC: SF->errsup++; break;
			case SGS_INT_ERRSUP_DEC: SF->errsup--; break;
			}
			break;
		}

		case SGS_SI_RETN:
		{
			ret = argA;
			sgs_BreakIf( ( C->stack_top - C->stack_off ) - stkoff < ret &&
				"internal error: stack was corrupted" );
			pp = pend;
			break;
		}

		case SGS_SI_JUMP:
		{
			int16_t off = (int16_t) argE;
			pp += off;
			sgs_BreakIf( pp+1 > pend || pp+1 < SF->code );
			break;
		}

		case SGS_SI_JMPT:
		{
			int16_t off = (int16_t) argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}
		case SGS_SI_JMPF:
		{
			int16_t off = (int16_t) argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( !var_getbool( C, RESVAR( argC ) ) )
				pp += off;
			break;
		}
		case SGS_SI_JMPN:
		{
			int16_t off = (int16_t) argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( RESVAR( argC )->type == SGS_VT_NULL )
				pp += off;
			break;
		}

		case SGS_SI_CALL:
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

		case SGS_SI_FORPREP: vm_forprep( C, argA, RESVAR( argB ) ); break;
		case SGS_SI_FORLOAD:
			vm_fornext( C,
				argB < 0x100 ? (int)argB : -1,
				argC < 0x100 ? (int)argC : -1,
				stk_getlpos( C, argA ) );
			break;
		case SGS_SI_FORJUMP:
		{
			sgs_rcpos_t off = argE;
			sgs_BreakIf( pp+1 > pend || pp+1 < SF->code );
			if( vm_fornext( C, -1, -1, RESVAR( argC ) ) < 1 )
				pp += off;
			break;
		}

#define a1 argA
#define ARGS_2 const sgs_VarPtr p2 = RESVAR( argB );
#define ARGS_3 const sgs_VarPtr p2 = RESVAR( argB ), p3 = RESVAR( argC );
		case SGS_SI_LOADCONST: { stk_setlvar( C, argC, cptr + argE ); break; }
		case SGS_SI_GETVAR: { ARGS_2; sgsSTD_GlobalGet( C, p1, p2 ); break; }
		case SGS_SI_SETVAR: { ARGS_3; sgsSTD_GlobalSet( C, p2, p3 ); break; }
		case SGS_SI_GETPROP: { ARGS_3; vm_getprop_safe( C, a1, p2, p3, SGS_TRUE ); break; }
		case SGS_SI_SETPROP: { ARGS_3; vm_setprop( C, p1, p2, p3, SGS_TRUE ); break; }
		case SGS_SI_GETINDEX: { ARGS_3; vm_getprop_safe( C, a1, p2, p3, SGS_FALSE ); break; }
		case SGS_SI_SETINDEX: { ARGS_3; vm_setprop( C, p1, p2, p3, SGS_FALSE ); break; }
		
		case SGS_SI_GENCLSR: clstk_push_nulls( C, argA ); break;
		case SGS_SI_PUSHCLSR: clstk_push( C, clstk_get( C, argA ) ); break;
		case SGS_SI_MAKECLSR: vm_make_closure( C, argC, RESVAR( argB ), (int16_t) argA ); clstk_pop( C, argC ); break;
		case SGS_SI_GETCLSR: stk_setlvar( C, argA, &clstk_get( C, argB )->var ); break;
		case SGS_SI_SETCLSR: { sgs_VarPtr p3 = RESVAR( argC ), cv = &clstk_get( C, argB )->var;
			VAR_RELEASE( cv ); *cv = *p3; VAR_ACQUIRE( RESVAR( argC ) ); } break;

		case SGS_SI_SET: { ARGS_2; VAR_RELEASE( p1 ); *p1 = *p2; VAR_ACQUIRE( p1 ); break; }
		case SGS_SI_MCONCAT: { vm_op_concat_ex( C, argB ); stk_setlvar_leave( C, argA, stk_gettop( C ) ); stk_pop1nr( C ); break; }
		case SGS_SI_CONCAT: { ARGS_3; vm_op_concat( C, a1, p2, p3 ); break; }
		case SGS_SI_NEGATE: { ARGS_2; vm_op_negate( C, p1, p2 ); break; }
		case SGS_SI_BOOL_INV: { ARGS_2; vm_op_boolinv( C, (int16_t) a1, p2 ); break; }
		case SGS_SI_INVERT: { ARGS_2; vm_op_invert( C, (int16_t) a1, p2 ); break; }

		case SGS_SI_INC: { ARGS_2; vm_op_incdec( C, p1, p2, +1 ); break; }
		case SGS_SI_DEC: { ARGS_2; vm_op_incdec( C, p1, p2, -1 ); break; }
		case SGS_SI_ADD: { ARGS_3;
			if( p2->type == SGS_VT_REAL && p3->type == SGS_VT_REAL ){ var_setreal( C, p1, p2->data.R + p3->data.R ); break; }
			if( p2->type == SGS_VT_INT && p3->type == SGS_VT_INT ){ var_setint( C, p1, p2->data.I + p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_ADD ); break; }
		case SGS_SI_SUB: { ARGS_3;
			if( p2->type == SGS_VT_REAL && p3->type == SGS_VT_REAL ){ var_setreal( C, p1, p2->data.R - p3->data.R ); break; }
			if( p2->type == SGS_VT_INT && p3->type == SGS_VT_INT ){ var_setint( C, p1, p2->data.I - p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_SUB ); break; }
		case SGS_SI_MUL: { ARGS_3;
			if( p2->type == SGS_VT_REAL && p3->type == SGS_VT_REAL ){ var_setreal( C, p1, p2->data.R * p3->data.R ); break; }
			if( p2->type == SGS_VT_INT && p3->type == SGS_VT_INT ){ var_setint( C, p1, p2->data.I * p3->data.I ); break; }
			vm_arith_op( C, p1, p2, p3, ARITH_OP_MUL ); break; }
		case SGS_SI_DIV: { ARGS_3; vm_arith_op( C, p1, p2, p3, ARITH_OP_DIV ); break; }
		case SGS_SI_MOD: { ARGS_3; vm_arith_op( C, p1, p2, p3, ARITH_OP_MOD ); break; }

		case SGS_SI_AND: { ARGS_3; vm_op_and( C, (int16_t) a1, p2, p3 ); break; }
		case SGS_SI_OR: { ARGS_3; vm_op_or( C, (int16_t) a1, p2, p3 ); break; }
		case SGS_SI_XOR: { ARGS_3; vm_op_xor( C, (int16_t) a1, p2, p3 ); break; }
		case SGS_SI_LSH: { ARGS_3; vm_op_lsh( C, (int16_t) a1, p2, p3 ); break; }
		case SGS_SI_RSH: { ARGS_3; vm_op_rsh( C, (int16_t) a1, p2, p3 ); break; }

#define STRICTLY_EQUAL( val ) if( p2->type != p3->type || ( p2->type == SGS_VT_OBJECT && \
								p2->data.O->iface != p3->data.O->iface ) ) { var_setbool( C, p1, val ); break; }
#define VCOMPARE( op ) { int cr = vm_compare( C, p2, p3 ) op 0; var_setbool( C, C->stack_off + a1, cr ); }
		case SGS_SI_SEQ: { ARGS_3; STRICTLY_EQUAL( SGS_FALSE ); VCOMPARE( == ); break; }
		case SGS_SI_SNEQ: { ARGS_3; STRICTLY_EQUAL( SGS_TRUE ); VCOMPARE( != ); break; }
		case SGS_SI_EQ: { ARGS_3; VCOMPARE( == ); break; }
		case SGS_SI_NEQ: { ARGS_3; VCOMPARE( != ); break; }
		case SGS_SI_LT: { ARGS_3; VCOMPARE( < ); break; }
		case SGS_SI_GTE: { ARGS_3; VCOMPARE( >= ); break; }
		case SGS_SI_GT: { ARGS_3; VCOMPARE( > ); break; }
		case SGS_SI_LTE: { ARGS_3; VCOMPARE( <= ); break; }
		case SGS_SI_RAWCMP: { ARGS_3; var_setint( C, C->stack_off + a1, vm_compare( C, p2, p3 ) ); break; }

		case SGS_SI_ARRAY: { vm_make_array( C, argB, argA ); break; }
		case SGS_SI_DICT: { vm_make_dict( C, argB, argA ); break; }
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
		sgs_BreakIf( SGS_STACKFRAMESIZE < stkoff );
		
		SF->lptr = ++SF->iptr;
	}

#if SGS_DEBUG && SGS_DEBUG_STATE
	/* TODO restore memcheck */
	sgsVM_StackDump( C );
#endif
	return ret;
}


/* INTERNAL INERFACE */

static size_t funct_size( const sgs_iFunc* f )
{
	size_t sz = f->size + f->funcname.mem + f->filename.mem;
	const sgs_Variable* beg = (const sgs_Variable*) (const void*) sgs_func_c_consts( f );
	const sgs_Variable* end = (const sgs_Variable*) (const void*) SGS_ASSUME_ALIGNED( sgs_func_c_bytecode( f ), 4 );
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
	case SGS_VT_FUNC: out += funct_size( var->data.F ); break;
	/* case SGS_VT_OBJECT: break; */
	case SGS_VT_STRING: out += var->data.S->size + sizeof( sgs_iStr ); break;
	}
	return out;
}

void sgsVM_VarDump( const sgs_Variable* var )
{
	/* WP: variable size limit */
	printf( "%s (size:%d)", TYPENAME( var->type ), (uint32_t) sgsVM_VarSize( var ) );
	switch( var->type )
	{
	case SGS_VT_NULL: break;
	case SGS_VT_BOOL: printf( " = %s", var->data.B ? "True" : "False" ); break;
	case SGS_VT_INT: printf( " = %" PRId64, var->data.I ); break;
	case SGS_VT_REAL: printf( " = %f", var->data.R ); break;
	case SGS_VT_STRING: printf( " [rc:%"PRId32"] = \"", var->data.S->refcount );
		sgs_print_safe( stdout, sgs_var_cstr( var ), SGS_MIN( var->data.S->size, 16 ) );
		printf( var->data.S->size > 16 ? "...\"" : "\"" ); break;
	case SGS_VT_FUNC: printf( " [rc:%"PRId32"]", var->data.F->refcount ); break;
	case SGS_VT_CFUNC: printf( " = %p", (void*)(size_t) var->data.C ); break;
	case SGS_VT_OBJECT: printf( "TODO [object impl]" ); break;
	case SGS_VT_PTR: printf( " = %p", var->data.P ); break;
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
	allowed = vm_frame_push( C, NULL, T, (sgs_instr_t*) code, codesize / sizeof( sgs_instr_t ) );
	stk_push_nulls( C, numtmp );
	if( allowed )
	{
		/* WP: const limit */
		rvc = vm_exec( C, (sgs_Variable*) data, (sgs_rcpos_t) ( datasize / sizeof( sgs_Variable* ) ) );
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
	out->type = SGS_VT_NULL;
}

void sgs_InitBool( sgs_Variable* out, sgs_Bool value )
{
	out->type = SGS_VT_BOOL;
	out->data.B = value ? 1 : 0;
}

void sgs_InitInt( sgs_Variable* out, sgs_Int value )
{
	out->type = SGS_VT_INT;
	out->data.I = value;
}

void sgs_InitReal( sgs_Variable* out, sgs_Real value )
{
	out->type = SGS_VT_REAL;
	out->data.R = value;
}

void sgs_InitStringBuf( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal size )
{
	sgs_BreakIf( !str && size && "sgs_InitStringBuf: str = NULL" );
	var_create_str( C, out, str, size );
}

void sgs_InitString( SGS_CTX, sgs_Variable* out, const char* str )
{
	size_t sz;
	sgs_BreakIf( !str && "sgs_InitString: str = NULL" );
	sz = SGS_STRINGLENGTHFUNC(str);
	sgs_BreakIf( sz > 0x7fffffff && "sgs_InitString: size exceeded" );
	/* WP: error detection */
	var_create_str( C, out, str, (sgs_SizeVal) sz );
}

void sgs_InitCFunction( sgs_Variable* out, sgs_CFunc func )
{
	out->type = SGS_VT_CFUNC;
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
	out->type = SGS_VT_PTR;
	out->data.P = ptr;
}

void sgs_InitObjectPtr( sgs_Variable* out, sgs_VarObj* obj )
{
	out->type = SGS_VT_OBJECT;
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

SGSONE sgs_PushNull( SGS_CTX )
{
	stk_push_null( C );
	return 1;
}

SGSONE sgs_PushBool( SGS_CTX, sgs_Bool value )
{
	sgs_Variable var;
	var.type = SGS_VT_BOOL;
	var.data.B = value ? 1 : 0;
	stk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushInt( SGS_CTX, sgs_Int value )
{
	sgs_Variable var;
	var.type = SGS_VT_INT;
	var.data.I = value;
	stk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushReal( SGS_CTX, sgs_Real value )
{
	sgs_Variable var;
	var.type = SGS_VT_REAL;
	var.data.R = value;
	stk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushStringBuf( SGS_CTX, const char* str, sgs_SizeVal size )
{
	sgs_Variable var;
	sgs_BreakIf( !str && size && "sgs_PushStringBuf: str = NULL" );
	var_create_str( C, &var, str, size );
	stk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushString( SGS_CTX, const char* str )
{
	size_t sz;
	sgs_Variable var;
	sgs_BreakIf( !str && "sgs_InitString: str = NULL" );
	sz = SGS_STRINGLENGTHFUNC(str);
	sgs_BreakIf( sz > 0x7fffffff && "sgs_InitString: size exceeded" );
	/* WP: error detection */
	var_create_str( C, &var, str, (sgs_SizeVal) sz );
	stk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushCFunction( SGS_CTX, sgs_CFunc func )
{
	sgs_Variable var;
	var.type = SGS_VT_CFUNC;
	var.data.C = func;
	stk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushObject( SGS_CTX, void* data, sgs_ObjInterface* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, data, iface, 0 );
	stk_push_leave( C, &var );
	return 1;
}

void* sgs_PushObjectIPA( SGS_CTX, uint32_t added, sgs_ObjInterface* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, NULL, iface, added );
	stk_push_leave( C, &var );
	return var.data.O->data;
}

SGSONE sgs_PushPtr( SGS_CTX, void* ptr )
{
	sgs_Variable var;
	var.type = SGS_VT_PTR;
	var.data.P = ptr;
	stk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushObjectPtr( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SGS_VT_OBJECT;
	var.data.O = obj;
	stk_push( C, &var ); /* a new reference must be created */
	return 1;
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
	if( !SGS_STACKFRAMESIZE )
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


/* string generation */
char* sgs_PushStringAlloc( SGS_CTX, sgs_SizeVal size )
{
	sgs_Variable var;
	var_create_0str( C, &var, (uint32_t) size );
	stk_push_leave( C, &var );
	return sgs_var_cstr( &var );
}

char* sgs_InitStringAlloc( SGS_CTX, sgs_Variable* var, sgs_SizeVal size )
{
	var_create_0str( C, var, (uint32_t) size );
	return sgs_var_cstr( var );
}

SGSRESULT sgs_FinalizeStringAlloc( SGS_CTX, sgs_StkIdx item )
{
	SGSRESULT res;
	sgs_Variable var;
	if( !sgs_PeekStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	res = sgs_FinalizeStringAllocP( C, &var );
	*stk_getpos( C, item ) = var;
	return res;
}

SGSRESULT sgs_FinalizeStringAllocP( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SGS_VT_STRING )
		return SGS_EINVAL;
	var_finalize_str( C, var );
	return SGS_SUCCESS;
}



#define _EL_BACKUP int32_t oel = C->minlev;
#define _EL_SETMAX C->minlev = C->apilev;
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
	if( !sgs_PeekStackItem( C, -1, &val ) )
		return SGS_EINPROC;
	ret = sgs_SetIndexPPP( C, obj, idx, &val, isprop );
	if( SGS_SUCCEEDED( ret ) )
		stk_pop1( C );
	return ret;
}


SGSRESULT sgs_GetIndexIPP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_Variable* out, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_GetIndexPPP( C, &vO, idx, out, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexPIP( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_Variable* out, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_GetIndexPPP( C, obj, &vI, out, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexIIP( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_Variable* out, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
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
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_GetIndexPPP( C, &vO, idx, &vV, isprop ) ) ||
		( res = sgs_SetStackItem( C, out, &vV ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexPII( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_StkIdx out, int isprop )
{
	int res; sgs_Variable vI, vV;
	if( ( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_GetIndexPPP( C, obj, &vI, &vV, isprop ) ) ||
		( res = sgs_SetStackItem( C, out, &vV ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_GetIndexIII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_StkIdx out, int isprop )
{
	int res; sgs_Variable vO, vI, vV;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_GetIndexPPP( C, &vO, &vI, &vV, isprop ) ) ||
		( res = sgs_SetStackItem( C, out, &vV ) ) ) return res;
	return SGS_SUCCESS;
}


SGSRESULT sgs_SetIndexIPP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_Variable* val, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_SetIndexPPP( C, &vO, idx, val, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexPIP( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_Variable* val, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_SetIndexPPP( C, obj, &vI, val, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexIIP( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_Variable* val, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_SetIndexPPP( C, &vO, &vI, val, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexIPI( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, sgs_StkIdx val, int isprop )
{
	int res; sgs_Variable vO, vV;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, val, &vV ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_SetIndexPPP( C, &vO, idx, &vV, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexPII( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, sgs_StkIdx val, int isprop )
{
	int res; sgs_Variable vI, vV;
	if( ( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, val, &vV ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_SetIndexPPP( C, obj, &vI, &vV, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_SetIndexIII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, sgs_StkIdx val, int isprop )
{
	int res; sgs_Variable vO, vI, vV;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, val, &vV ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_SetIndexPPP( C, &vO, &vI, &vV, isprop ) ) ) return res;
	return SGS_SUCCESS;
}


SGSRESULT sgs_PushIndexIP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PushIndexPP( C, &vO, idx, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushIndexPI( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PushIndexPP( C, obj, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_PushIndexII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PushIndexPP( C, &vO, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIndexIP( SGS_CTX, sgs_StkIdx obj, sgs_Variable* idx, int isprop )
{
	int res; sgs_Variable vO;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_StoreIndexPP( C, &vO, idx, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIndexPI( SGS_CTX, sgs_Variable* obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vI;
	if( ( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_StoreIndexPP( C, obj, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}

SGSRESULT sgs_StoreIndexII( SGS_CTX, sgs_StkIdx obj, sgs_StkIdx idx, int isprop )
{
	int res; sgs_Variable vO, vI;
	if( ( res = sgs_PeekStackItem( C, obj, &vO ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_PeekStackItem( C, idx, &vI ) ? SGS_SUCCESS : SGS_EBOUNDS ) != SGS_SUCCESS ||
		( res = sgs_StoreIndexPP( C, &vO, &vI, isprop ) ) ) return res;
	return SGS_SUCCESS;
}


SGSRESULT sgs_GetGlobalPP( SGS_CTX, sgs_Variable* idx, sgs_Variable* out )
{
	int ret;
	_EL_BACKUP;
	out->type = SGS_VT_NULL;
	_EL_SETMAX;
	ret = sgsSTD_GlobalGet( C, out, idx );
	_EL_RESET;
	return ret;
}

SGSRESULT sgs_SetGlobalPP( SGS_CTX, sgs_Variable* idx, sgs_Variable* val )
{
	sgs_Variable vO; vO.type = SGS_VT_OBJECT; vO.data.O = C->_G;
	return sgs_SetIndexPPP( C, &vO, idx, val, 0 );
}


SGSRESULT sgs_PushProperty( SGS_CTX, sgs_StkIdx obj, const char* name )
{
	int ret;
	if( !sgs_IsValidIndex( C, obj ) )
		return SGS_EBOUNDS;
	obj = stk_absindex( C, obj );
	sgs_PushString( C, name );
	ret = sgs_PushIndexII( C, obj, -1, SGS_TRUE );
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
	ret = sgs_SetIndexIII( C, obj, -1, -2, SGS_TRUE );
	stk_pop( C, SGS_SUCCEEDED( ret ) ? 2 : 1 );
	return ret;
}

SGSRESULT sgs_PushNumIndex( SGS_CTX, StkIdx obj, sgs_Int idx )
{
	sgs_Variable ivar;
	ivar.type = SGS_VT_INT;
	ivar.data.I = idx;
	return sgs_PushIndexIP( C, obj, &ivar, SGS_FALSE );
}

SGSRESULT sgs_StoreNumIndex( SGS_CTX, StkIdx obj, sgs_Int idx )
{
	sgs_Variable ivar;
	ivar.type = SGS_VT_INT;
	ivar.data.I = idx;
	return sgs_StoreIndexIP( C, obj, &ivar, SGS_FALSE );
}

SGSRESULT sgs_PushGlobal( SGS_CTX, const char* name )
{
	int ret;
	_EL_BACKUP;
	sgs_Variable str;
	sgs_VarPtr pos;
	sgs_PushString( C, name );
	pos = stk_getpos( C, -1 );
	str = *pos;
	sgs_Acquire( C, &str );
	_EL_SETMAX;
	ret = sgsSTD_GlobalGet( C, pos, pos );
	_EL_RESET;
	sgs_Release( C, &str );
	if( ret != SGS_SUCCESS )
		sgs_Pop( C, 1 );
	return ret;
}

SGSRESULT sgs_StoreGlobal( SGS_CTX, const char* name )
{
	int ret;
	_EL_BACKUP;
	if( sgs_StackSize( C ) < 1 )
		return SGS_EINPROC;
	sgs_PushString( C, name );
	_EL_SETMAX;
	ret = sgsSTD_GlobalSet( C, stk_getpos( C, -1 ), stk_getpos( C, -2 ) );
	_EL_RESET;
	sgs_Pop( C, SGS_SUCCEEDED( ret ) ? 2 : 1 );
	return ret;
}

SGSRESULT sgs_PushPropertyP( SGS_CTX, sgs_Variable* var, const char* name )
{
	int ret;
	sgs_PushString( C, name );
	ret = sgs_PushIndexPI( C, var, -1, SGS_TRUE );
	stk_popskip( C, 1, SGS_SUCCEEDED( ret ) ? 1 : 0 );
	return ret;
}

SGSRESULT sgs_StorePropertyP( SGS_CTX, sgs_Variable* var, const char* name )
{
	int ret;
	sgs_PushString( C, name );
	ret = sgs_SetIndexPII( C, var, -1, -2, SGS_TRUE );
	stk_pop( C, SGS_SUCCEEDED( ret ) ? 2 : 1 );
	return ret;
}

void sgs_GetEnv( SGS_CTX, sgs_Variable* out )
{
	sgs_InitObjectPtr( out, C->_G );
}

SGSRESULT sgs_SetEnv( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SGS_VT_OBJECT || var->data.O->iface != sgsstd_dict_iface )
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
	if( !sgs_PeekStackItem( C, -1, &var ) )
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
	StkIdx ssz = SGS_STACKFRAMESIZE;
	va_list args;
	va_start( args, path );
	item = stk_absindex( C, item );
	ret = sgs_PushPathBuf( C, item, path, strlen(path), &args );
	if( ret == SGS_SUCCESS )
		stk_popskip( C, SGS_STACKFRAMESIZE - ssz - 1, 1 );
	else
		stk_pop( C, SGS_STACKFRAMESIZE - ssz );
	va_end( args );
	return ret;
}

SGSRESULT sgs_StorePath( SGS_CTX, StkIdx item, const char* path, ... )
{
	int ret;
	size_t len = strlen( path );
	StkIdx val, ssz = SGS_STACKFRAMESIZE;
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
			goto fail;
		ssz--;
	}
fail:
	va_end( args );
	stk_pop( C, SGS_STACKFRAMESIZE - ssz );
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

SGSMIXED sgs_LoadArgsExtVA( SGS_CTX, int from, const char* cmd, va_list* args )
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
				if( sgs_ItemType( C, from ) != SGS_VT_NULL )
				{
					argerr( C, from, method, SGS_VT_NULL, 0 );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( *args, SGSBOOL* ) = 1;
				}
			}
			strict = 0; nowrite = 0; from++; break;
		
		case 'b':
			{
				sgs_Bool b;
				
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
						va_arg( *args, sgs_Bool* );
					break;
				}
				
				if( !sgs_ParseBool( C, from, &b ) ||
					( strict && sgs_ItemType( C, from ) != SGS_VT_BOOL ) )
				{
					argerr( C, from, method, SGS_VT_BOOL, strict );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( *args, sgs_Bool* ) = b;
				}
			}
			strict = 0; nowrite = 0; from++; break;
		
		case 'c': case 'w': case 'l': case 'q': case 'i':
			{
				sgs_Int i, imin = INT64_MIN, imax = INT64_MAX;
				
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
					{
						switch( *cmd )
						{
						case 'c': va_arg( *args, uint8_t* ); break;
						case 'w': va_arg( *args, uint16_t* ); break;
						case 'l': va_arg( *args, uint32_t* ); break;
						case 'q': va_arg( *args, uint64_t* ); break;
						case 'i': va_arg( *args, sgs_Int* ); break;
						}
					}
					break;
				}
				
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
					( strict && sgs_ItemType( C, from ) != SGS_VT_INT ) )
				{
					argerr( C, from, method, SGS_VT_INT, strict );
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
					case 'c': *va_arg( *args, uint8_t* ) = (uint8_t) i; break;
					case 'w': *va_arg( *args, uint16_t* ) = (uint16_t) i; break;
					case 'l': *va_arg( *args, uint32_t* ) = (uint32_t) i; break;
					case 'q': *va_arg( *args, uint64_t* ) = (uint64_t) i; break;
					case 'i': *va_arg( *args, sgs_Int* ) = i; break;
					}
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'f': case 'd': case 'r':
			{
				sgs_Real r;
				
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
					{
						switch( *cmd )
						{
						case 'f': va_arg( *args, float* ); break;
						case 'd': va_arg( *args, double* ); break;
						case 'r': va_arg( *args, sgs_Real* ); break;
						}
					}
					break;
				}
				
				if( !sgs_ParseReal( C, from, &r ) ||
					( strict && sgs_ItemType( C, from ) != SGS_VT_REAL ) )
				{
					argerr( C, from, method, SGS_VT_REAL, strict );
					return opt;
				}
				
				if( !nowrite )
				{
					switch( *cmd )
					{
					case 'f': *va_arg( *args, float* ) = (float) r; break;
					case 'd': *va_arg( *args, double* ) = r; break;
					case 'r': *va_arg( *args, sgs_Real* ) = r; break;
					}
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 's': case 'm':
			{
				char* str;
				sgs_SizeVal sz;
				
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
					{
						va_arg( *args, char** );
						if( *cmd == 'm' )
							va_arg( *args, sgs_SizeVal* );
					}
					break;
				}
				
				if( ( strict && sgs_ItemType( C, from ) != SGS_VT_STRING ) ||
					!sgs_ParseString( C, from, &str, &sz ) )
				{
					argerr( C, from, method, SGS_VT_STRING, strict );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( *args, char** ) = str;
					if( *cmd == 'm' )
						*va_arg( *args, sgs_SizeVal* ) = sz;
				}
			}
			strict = 0; nowrite = 0; from++; break;
		
		case 'p':
			{
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
						va_arg( *args, SGSBOOL* );
					break;
				}
				
				if( !sgs_IsCallable( C, from ) )
				{
					argerrx( C, from, method, "callable", "" );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( *args, SGSBOOL* ) = 1;
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
				if( *cmd == 'o' ) ifc = va_arg( *args, sgs_ObjInterface* );
				
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
					{
						switch( *cmd )
						{
						case 'a': va_arg( *args, sgs_SizeVal* ); break;
						case 't':
						case 'h': va_arg( *args, sgs_SizeVal* ); break;
						case 'o': va_arg( *args, void** ); break;
						}
					}
					break;
				}
				
				if( sgs_ItemType( C, from ) != SGS_VT_OBJECT ||
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
					case 'a': *va_arg( *args, sgs_SizeVal* ) = 
						((sgsstd_array_header_t*) O->data)->size; break;
					case 't':
					case 'h': *va_arg( *args, sgs_SizeVal* ) =
						sgs_vht_size( ((sgs_VHTable*) O->data) ); break;
					case 'o': *va_arg( *args, void** ) = O->data; break;
					}
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case '&':
			{
				void* ptr;
				
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
						va_arg( *args, void** );
					break;
				}
				
				if( !sgs_ParsePtr( C, from, &ptr ) )
				{
					argerrx( C, from, method, "pointer", "" );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( *args, void** ) = ptr;
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'v':
			{
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
						va_arg( *args, sgs_Variable* );
					break;
				}
				
				if( from >= sgs_StackSize( C ) ||
					( strict && sgs_ItemType( C, from ) == SGS_VT_NULL ) )
				{
					argerrx( C, from, method, strict ? "non-null" : "any", "" );
					return opt;
				}
				
				if( !nowrite )
				{
					int owr = sgs_PeekStackItem( C, from, va_arg( *args, sgs_Variable* ) );
					UNUSED( owr );
					sgs_BreakIf( !owr );
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'x':
			{
				sgs_ArgCheckFunc acf = va_arg( *args, sgs_ArgCheckFunc );
				int flags = 0;
				
				if( strict )     flags |= SGS_LOADARG_STRICT;
				if( !nowrite )   flags |= SGS_LOADARG_WRITE;
				if( opt )        flags |= SGS_LOADARG_OPTIONAL;
				if( isig )       flags |= SGS_LOADARG_INTSIGN;
				if( range > 0 )  flags |= SGS_LOADARG_INTRANGE;
				if( range < 0 )  flags |= SGS_LOADARG_INTCLAMP;
				
				if( !acf( C, from, args, flags ) )
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
	ret = sgs_LoadArgsExtVA( C, from, cmd, &args );
	va_end( args );
	return ret;
}

SGSBOOL sgs_LoadArgs( SGS_CTX, const char* cmd, ... )
{
	SGSBOOL ret;
	va_list args;
	va_start( args, cmd );
	ret = sgs_LoadArgsExtVA( C, 0, cmd, &args ) > 0;
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
		return SGS_FALSE;
	}
	*ptrout = sgs_GetObjectData( C, 0 );
	sgs_ForceHideThis( C );
	return SGS_TRUE;
}


int sgs_ArgCheck_Object( SGS_CTX, int argid, va_list* args, int flags )
{
	uint32_t ity;
	sgs_VarObj** out = NULL;
	if( flags & SGS_LOADARG_WRITE )
		out = va_arg( *args, sgs_VarObj** );
	
	ity = sgs_ItemType( C, argid );
	if( ity == SGS_VT_OBJECT || ( !( flags & SGS_LOADARG_STRICT ) && ity == SGS_VT_NULL ) )
	{
		if( out )
			*out = ity != SGS_VT_NULL ? sgs_GetObjectStruct( C, argid ) : NULL;
		return 1;
	}
	if( flags & SGS_LOADARG_OPTIONAL )
		return 1;
	return sgs_ArgError( C, argid, 0, SGS_VT_OBJECT, !!( flags & SGS_LOADARG_STRICT ) );
}


SGSRESULT sgs_Pop( SGS_CTX, StkIdx count )
{
	if( SGS_STACKFRAMESIZE < count || count < 0 )
		return SGS_EINVAL;
	stk_pop( C, count );
	return SGS_SUCCESS;
}

SGSRESULT sgs_PopSkip( SGS_CTX, StkIdx count, StkIdx skip )
{
	if( SGS_STACKFRAMESIZE < count + skip || count < 0 || skip < 0 )
		return SGS_EINVAL;
	stk_popskip( C, count, skip );
	return SGS_SUCCESS;
}

StkIdx sgs_StackSize( SGS_CTX )
{
	return SGS_STACKFRAMESIZE;
}

SGSRESULT sgs_SetStackSize( SGS_CTX, StkIdx size )
{
	StkIdx diff;
	if( size < 0 )
		return SGS_EINVAL;
	diff = SGS_STACKFRAMESIZE - size;
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
	return ( item >= 0 && item < SGS_STACKFRAMESIZE );
}

SGSBOOL sgs_PeekStackItem( SGS_CTX, StkIdx item, sgs_Variable* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	*out = *stk_getpos( C, item );
	return SGS_TRUE;
}

SGSBOOL sgs_GetStackItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* out )
{
	if( !sgs_IsValidIndex( C, item ) )
	{
		out->type = SGS_VT_NULL;
		return SGS_FALSE;
	}
	*out = *stk_getpos( C, item );
	VAR_ACQUIRE( out );
	return SGS_TRUE;
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
	if( var_to == var_from )
		return;
	VAR_RELEASE( var_to );
	*var_to = *var_from;
	VAR_ACQUIRE( var_to );
}

SGSRESULT sgs_ArithOp( SGS_CTX, sgs_Variable* out, sgs_Variable* A, sgs_Variable* B, int op )
{
	if( op < 0 || op > SGS_EOP_NEGATE )
	{
		VAR_RELEASE( out );
		return SGS_ENOTSUP;
	}
	if( op == SGS_EOP_NEGATE )
		return vm_op_negate( C, out, A ) ? SGS_SUCCESS : SGS_EINVAL;
	return vm_arith_op( C, out, A, B, op );
}

SGSRESULT sgs_IncDec( SGS_CTX, sgs_Variable* out, sgs_Variable* A, int inc )
{
	return vm_op_incdec( C, out, A, inc ? 1 : -1 );
}


/*
	
	CLOSURES
	
*/

SGSRESULT sgs_ClPushNulls( SGS_CTX, sgs_StkIdx num )
{
	if( num < 0 )
		return SGS_EINVAL;
	clstk_push_nulls( C, num );
	return SGS_SUCCESS;
}

SGSRESULT sgs_ClPushItem( SGS_CTX, sgs_StkIdx item )
{
	if( item < 0 || C->clstk_off + item >= C->clstk_top )
		return SGS_EBOUNDS;
	clstk_push( C, clstk_get( C, item ) );
	return SGS_SUCCESS;
}

SGSRESULT sgs_ClPop( SGS_CTX, sgs_StkIdx num )
{
	if( C->clstk_top + num > C->clstk_top )
		return SGS_EINPROC;
	clstk_pop( C, num );
	return SGS_SUCCESS;
}

SGSRESULT sgs_MakeClosure( SGS_CTX, sgs_Variable* func, sgs_StkIdx clcount, sgs_Variable* out )
{
	int ret;
	if( C->clstk_top - C->clstk_off < clcount )
		return SGS_EINPROC;
	/* WP: range not affected by conversion */
	ret = sgsSTD_MakeClosure( C, func, (uint32_t) clcount );
	sgs_BreakIf( ret != SGS_SUCCESS );
	UNUSED( ret );
	
	*out = *stk_gettop( C );
	stk_pop1nr( C );
	clstk_pop( C, clcount );
	return SGS_SUCCESS;
}

SGSRESULT sgs_ClGetItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* out )
{
	if( item < 0 || C->clstk_off + item >= C->clstk_top )
		return SGS_EBOUNDS;
	*out = clstk_get( C, item )->var;
	VAR_ACQUIRE( out );
	return SGS_SUCCESS;
}

SGSRESULT sgs_ClSetItem( SGS_CTX, sgs_StkIdx item, sgs_Variable* var )
{
	if( item < 0 || C->clstk_off + item >= C->clstk_top )
		return SGS_EBOUNDS;
	sgs_Variable* cv = &clstk_get( C, item )->var;
	VAR_RELEASE( cv );
	*cv = *var;
	VAR_ACQUIRE( var );
	return SGS_SUCCESS;
}


/*

	OPERATIONS

*/

SGSRESULT sgs_FCallP( SGS_CTX, sgs_Variable* callable, int args, int expect, int gotthis )
{
	int ret;
	if( SGS_STACKFRAMESIZE < args + ( gotthis ? 1 : 0 ) )
		return SGS_EINVAL;
	ret = vm_call( C, args, 0, gotthis, expect, callable );
	if( ret == -1 ) return SGS_SUCABRT;
	return ret ? SGS_SUCCESS : SGS_EINPROC;
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
	ret = vm_call( C, args, 0, gotthis, expect, &func );
	VAR_RELEASE( &func );
	if( ret == -1 ) return SGS_SUCABRT;
	return ret ? SGS_SUCCESS : SGS_EINPROC;
}

SGSRESULT sgs_GlobalCall( SGS_CTX, const char* name, int args, int expect )
{
	int ret;
	ret = sgs_PushGlobal( C, name );
	if( SGS_FAILED( ret ) ) return ret;
	ret = sgs_Call( C, args, expect );
	if( ret == SGS_EINVAL )
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
		case SGS_VT_NULL: sgs_PushString( C, "null" ); break;
		case SGS_VT_BOOL: sgs_PushString( C, var->data.B ? "bool (true)" : "bool (false)" ); break;
		case SGS_VT_INT: { char buf[ 32 ];
			sprintf( buf, "int (%" PRId64 ")", var->data.I );
			sgs_PushString( C, buf ); } break;
		case SGS_VT_REAL: { char buf[ 32 ];
			sprintf( buf, "real (%g)", var->data.R );
			sgs_PushString( C, buf ); } break;
		case SGS_VT_STRING:
			{
				char buf[ 532 ];
				char* bptr = buf;
				char* bend = buf + 512;
				char* source = sgs_var_cstr( var );
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
		case SGS_VT_FUNC:
			{
				sgs_MemBuf mb = sgs_membuf_create();
				sgs_iFunc* F = var->data.F;
				
				const char* str1 = F->funcname.size ? "SGS function '" : "SGS function <anonymous>";
				const char* str2 = F->funcname.size ? "' defined at " : " defined at ";
				const char* str3 = "'";
				
				sgs_membuf_appbuf( &mb, C, str1, strlen(str1) );
				if( F->funcname.size )
					sgs_membuf_appbuf( &mb, C, F->funcname.ptr, F->funcname.size );
				if( F->filename.size )
				{
					char lnbuf[ 32 ];
					sgs_membuf_appbuf( &mb, C, str2, strlen(str2) );
					sgs_membuf_appbuf( &mb, C, F->filename.ptr, F->filename.size );
					sprintf( lnbuf, ":%d", (int) F->linenum );
					sgs_membuf_appbuf( &mb, C, lnbuf, strlen(lnbuf) );
				}
				else if( F->funcname.size )
					sgs_membuf_appbuf( &mb, C, str3, strlen(str3) );
				
				/* WP: various limits */
				sgs_PushStringBuf( C, mb.ptr, (sgs_SizeVal) mb.size );
				sgs_membuf_destroy( &mb, C );
			}
			break;
		case SGS_VT_CFUNC:
			{
				char buf[ 32 ];
				sprintf( buf, "C function (%p)", var->data.C );
				sgs_PushString( C, buf );
			}
			break;
		case SGS_VT_OBJECT:
			{
				char buf[ 32 ];
				int q = 0;
				_STACK_PREPARE;
				sgs_VarObj* obj = var->data.O;
				
				if( obj->iface->dump )
				{
					_STACK_PROTECT;
					ret = obj->iface->dump( C, obj, maxdepth - 1 );
					q = SGS_SUCCEEDED( ret );
					_STACK_UNPROTECT_SKIP( q );
				}
				
				if( !q )
				{
					sprintf( buf, "object (%p) [%"PRId32"] %s", (void*) obj, obj->refcount,
						obj->iface->name ? obj->iface->name : "<unnamed>" );
					sgs_PushString( C, buf );
				}
				else
					sgs_ToString( C, -1 );
			}
			break;
		case SGS_VT_PTR:
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
		if( SGS_SUCCEEDED( ret ) )
		{
			sgs_PopSkip( C, 1, 1 );
			ret = SGS_SUCCESS;
		}
		return ret;
	}
}

SGSRESULT sgs_GCExecute( SGS_CTX )
{
	int ret = SGS_SUCCESS;
	sgs_VarPtr vbeg, vend;
	sgs_VarObj* p;

	C->redblue = !C->redblue;
	C->gcrun = SGS_TRUE;

	/* -- MARK -- */
	/* GCLIST / currently executed "main" function */
	vbeg = C->gclist; vend = vbeg + C->gclist_size;
	while( vbeg < vend ){
		ret = vm_gcmark( C, vbeg );
		if( SGS_FAILED( ret ) )
			goto end;
		vbeg++;
	}

	/* STACK */
	vbeg = C->stack_base; vend = C->stack_top;
	while( vbeg < vend ){
		ret = vm_gcmark( C, vbeg++ );
		if( SGS_FAILED( ret ) )
			goto end;
	}

	/* GLOBALS */
	sgsSTD_GlobalGC( C );

	/* -- SWEEP -- */
	/* destruct objects */
	p = C->objs;
	while( p ){
		sgs_VarObj* pn = p->next;
		if( p->redblue != C->redblue ){
			var_destruct_object( C, p );
		}
		p = pn;
	}

	/* free variables */
	p = C->objs;
	while( p ){
		sgs_VarObj* pn = p->next;
		if( p->redblue != C->redblue )
			var_free_object( C, p );
		p = pn;
	}

end:
	C->gcrun = SGS_FALSE;
	return ret;
}


const char* sgs_DebugDumpVarExt( SGS_CTX, sgs_StkIdx item, int maxdepth )
{
	if( SGS_FAILED( sgs_PushItem( C, item ) ) )
	{
		char buf[128];
		sprintf( buf, "<stack index out of bounds: %d (stack size=%d)>", (int) item, (int) sgs_StackSize( C ) );
		sgs_PushString( C, buf );
	}
	else if( maxdepth < 0 )
	{
		return sgs_ToString( C, -1 );
	}
	else
	{
		if( SGS_FAILED( sgs_DumpVar( C, maxdepth ) ) )
		{
			sgs_Pop( C, 1 );
			sgs_PushString( C, "<failed to dump variable>" );
		}
	}
	return sgs_GetStringPtr( C, -1 );
}

const char* sgs_DebugDumpVarExtP( SGS_CTX, sgs_Variable* var, int maxdepth )
{
	sgs_PushVariable( C, var );
	if( maxdepth < 0 )
	{
		return sgs_ToString( C, -1 );
	}
	else
	{
		if( SGS_FAILED( sgs_DumpVar( C, maxdepth ) ) )
		{
			sgs_Pop( C, 1 );
			sgs_PushString( C, "<failed to dump variable>" );
		}
		return sgs_GetStringPtr( C, -1 );
	}
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
		if( var->type != SGS_VT_STRING )
			return SGS_EINVAL;
		cstr = sgs_var_cstr( var );
		for( i = 0; cstr[ i ]; )
			if( cstr[ i ] == '\n' ) i++; else cstr++;
		if( var->data.S->size + i * padsize > 0x7fffffff )
			return SGS_EINPROC;
		
		/* WP: implemented error condition */
		sgs_PushStringAlloc( C, (sgs_SizeVal)( var->data.S->size + i * padsize ) );
		cstr = sgs_var_cstr( stk_getpos( C, -2 ) );
		ostr = sgs_var_cstr( stk_getpos( C, -1 ) );
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
		sgs_PopSkip( C, 1, 1 );
		return sgs_FinalizeStringAlloc( C, -1 );
	}
}

SGSRESULT sgs_ToPrintSafeString( SGS_CTX )
{
	char* buf = NULL;
	sgs_SizeVal size = 0, i;
	if( !( buf = sgs_ToStringBuf( C, -1, &size ) ) )
		return SGS_EINPROC;
	sgs_MemBuf mb = sgs_membuf_create();
	for( i = 0; i < size; ++i )
	{
		if( isgraph( (int)buf[ i ] ) || buf[ i ] == ' ' )
			sgs_membuf_appchr( &mb, C, buf[ i ] );
		else
		{
			char chrs[32];
			sprintf( chrs, "\\x%02X", (int) (unsigned char) buf[ i ] );
			sgs_membuf_appbuf( &mb, C, chrs, 4 );
		}
	}
	sgs_Pop( C, 1 );
	if( mb.size > 0x7fffffff )
		return SGS_EINPROC;
	/* WP: implemented error condition */
	sgs_PushStringBuf( C, mb.ptr, (sgs_SizeVal) mb.size );
	sgs_membuf_destroy( &mb, C );
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
	if( !sgs_PeekStackItem( C, item, &copy ) )
		return SGS_EBOUNDS;
	ret = vm_clone( C, &copy );
	if( SGS_SUCCEEDED( ret ) )
		return SGS_SUCCESS;
	return ret;
}


static int _serialize_function( SGS_CTX, sgs_iFunc* func, sgs_MemBuf* out )
{
	sgs_CompFunc F;
	{
		F.consts = sgs_membuf_create();
		F.code = sgs_membuf_create();
		F.lnbuf = sgs_membuf_create();
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
	sgs_iFunc* F;
	sgs_CompFunc* nf = NULL;
	if( sgsBC_ValidateHeader( buf, sz ) < SGS_HEADER_SIZE )
		return 0;
	if( sgsBC_Buf2Func( C, "<anonymous>", buf, sz, &nf ) )
		return 0;
	
	F = sgs_Alloc_a( sgs_iFunc, nf->consts.size + nf->code.size );

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
	F->funcname = sgs_membuf_create();
	F->linenum = 0;
	F->filename = sgs_membuf_create();
	/* set from current if possible? */
	
	memcpy( sgs_func_consts( F ), nf->consts.ptr, nf->consts.size );
	memcpy( sgs_func_bytecode( F ), nf->code.ptr, nf->code.size );

	sgs_membuf_destroy( &nf->consts, C );
	sgs_membuf_destroy( &nf->code, C );
	sgs_membuf_destroy( &nf->lnbuf, C );
	sgs_Dealloc( nf );
	
	*outfn = F;
	
	return 1;
}


typedef struct sgs_serialize1_data
{
	int mode;
	sgs_MemBuf data;
}
sgs_serialize1_data;

SGSRESULT sgs_SerializeInt_V1( SGS_CTX )
{
	int ret = SGS_SUCCESS;
	sgs_Variable V;
	void* prev_serialize_state = C->serialize_state;
	sgs_serialize1_data SD = { 1, sgs_membuf_create() }, *pSD;
	int ep = !C->serialize_state || *(int*)C->serialize_state != 1;

	if( !sgs_PeekStackItem( C, -1, &V ) )
		return SGS_EBOUNDS;

	if( ep )
	{
		C->serialize_state = &SD;
	}
	pSD = (sgs_serialize1_data*) C->serialize_state;

	if( V.type == SGS_VT_OBJECT )
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
		if( SGS_FAILED( ret ) )
			goto fail;
	}
	else if( V.type == SGS_VT_CFUNC )
	{
		sgs_Msg( C, SGS_WARNING, "Cannot serialize C functions" );
		ret = SGS_EINVAL;
		goto fail;
	}
	else if( V.type == SGS_VT_PTR )
	{
		sgs_Msg( C, SGS_WARNING, "Cannot serialize pointers" );
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
		sgs_membuf_appbuf( &pSD->data, C, pb, 2 );
		switch( V.type )
		{
		case SGS_VT_NULL: break;
		/* WP: V.data.B uses only bit 0 */
		case SGS_VT_BOOL: { char b = (char) V.data.B; sgs_membuf_appbuf( &pSD->data, C, &b, 1 ); } break;
		case SGS_VT_INT: sgs_membuf_appbuf( &pSD->data, C, &V.data.I, sizeof( sgs_Int ) ); break;
		case SGS_VT_REAL: sgs_membuf_appbuf( &pSD->data, C, &V.data.R, sizeof( sgs_Real ) ); break;
		case SGS_VT_STRING:
			sgs_membuf_appbuf( &pSD->data, C, &V.data.S->size, 4 );
			sgs_membuf_appbuf( &pSD->data, C, sgs_var_cstr( &V ), V.data.S->size );
			break;
		case SGS_VT_FUNC:
			{
				size_t szbefore = pSD->data.size;
				if( !_serialize_function( C, V.data.F, &pSD->data ) )
				{
					ret = SGS_EINPROC;
					goto fail;
				}
				else
				{
					uint32_t szdiff = (uint32_t) ( pSD->data.size - szbefore );
					sgs_membuf_insbuf( &pSD->data, C, szbefore, &szdiff, sizeof(szdiff) );
				}
			}
			break;
		default:
			sgs_Msg( C, SGS_ERROR, "sgs_Serialize: Unknown memory error" );
			ret = SGS_EINPROC;
			goto fail;
		}
	}

fail:
	if( ep )
	{
		if( ret == SGS_SUCCESS )
		{
			if( SD.data.size > 0x7fffffff )
			{
				sgs_Msg( C, SGS_WARNING, "Serialized string too long" );
				ret = SGS_EINVAL;
			}
			else
			{
				sgs_Pop( C, 1 );
				/* WP: added error condition */
				sgs_PushStringBuf( C, SD.data.ptr, (sgs_SizeVal) SD.data.size );
			}
		}
		sgs_membuf_destroy( &SD.data, C );
		C->serialize_state = prev_serialize_state;
	}
	else
		sgs_Pop( C, 1 );
	return ret;
}

SGSRESULT sgs_SerializeObjectInt_V1( SGS_CTX, StkIdx args, const char* func, size_t fnsize )
{
	sgs_serialize1_data* pSD;
	char pb[7] = { 'C', 0, 0, 0, 0, 0, 0 };
	{
		/* WP: they were pointless */
		pb[1] = (char)((args)&0xff);
		pb[2] = (char)((args>>8)&0xff);
		pb[3] = (char)((args>>16)&0xff);
		pb[4] = (char)((args>>24)&0xff);
	}
	
	if( !C->serialize_state || *(int*)C->serialize_state != 1 )
		return SGS_EINPROC;
	pSD = (sgs_serialize1_data*) C->serialize_state;
	
	/* WP: have error condition + sign interpretation doesn't matter */
	pb[ 5 ] = (char) fnsize;
	sgs_membuf_appbuf( &pSD->data, C, pb, 6 );
	sgs_membuf_appbuf( &pSD->data, C, func, fnsize );
	sgs_membuf_appbuf( &pSD->data, C, pb + 6, 1 );
	return SGS_SUCCESS;
}

SGSRESULT sgs_UnserializeInt_V1( SGS_CTX, char* str, char* strend )
{
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
			case SGS_VT_NULL: sgs_PushNull( C ); break;
			case SGS_VT_BOOL:
				if( str >= strend )
					return SGS_EINPROC;
				sgs_PushBool( C, *str++ );
				break;
			case SGS_VT_INT:
				if( str >= strend-7 )
					return SGS_EINPROC;
				else
				{
					sgs_Int val;
					SGS_AS_INTEGER( val, str );
					sgs_PushInt( C, val );
				}
				str += 8;
				break;
			case SGS_VT_REAL:
				if( str >= strend-7 )
					return SGS_EINPROC;
				else
				{
					sgs_Real val;
					SGS_AS_REAL( val, str );
					sgs_PushReal( C, val );
				}
				str += 8;
				break;
			case SGS_VT_STRING:
				{
					sgs_SizeVal strsz;
					if( str >= strend-3 )
						return SGS_EINPROC;
					SGS_AS_INT32( strsz, str );
					str += 4;
					if( str > strend - strsz )
						return SGS_EINPROC;
					sgs_PushStringBuf( C, str, strsz );
					str += strsz;
				}
				break;
			case SGS_VT_FUNC:
				{
					sgs_Variable tmp;
					sgs_SizeVal bcsz;
					sgs_iFunc* fn;
					if( str >= strend-3 )
						return SGS_EINPROC;
					SGS_AS_INT32( bcsz, str );
					str += 4;
					if( str > strend - bcsz )
						return SGS_EINPROC;
					/* WP: conversion does not affect values */
					if( !_unserialize_function( C, str, (size_t) bcsz, &fn ) )
						return SGS_EINPROC;
					tmp.type = SGS_VT_FUNC;
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
			SGS_AS_INT32( argc, str );
			str += 4;
			fnsz = *str++ + 1;
			if( str > strend - fnsz )
				return SGS_EINPROC;
			ret = sgs_GlobalCall( C, str, argc, 1 );
			if( SGS_FAILED( ret ) )
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


typedef struct sgs_serialize2_data
{
	int mode;
	sgs_VHTable servartable;
	sgs_MemBuf argarray;
	sgs_VarObj* curObj;
}
sgs_serialize2_data;

SGSRESULT sgs_SerializeInt_V2( SGS_CTX )
{
	int ret = SGS_SUCCESS;
	sgs_Variable V;
	void* prev_serialize_state = C->serialize_state;
	sgs_serialize2_data SD, *pSD;
	int ep = !C->serialize_state || *(int*)C->serialize_state != 2;
	
	if( !sgs_PeekStackItem( C, -1, &V ) )
		return SGS_EBOUNDS;
	
	if( ep )
	{
		SD.mode = 2;
		sgs_vht_init( &SD.servartable, C, 64, 64 );
		SD.argarray = sgs_membuf_create();
		SD.curObj = NULL;
		C->serialize_state = &SD;
	}
	pSD = (sgs_serialize2_data*) C->serialize_state;
	
	if( V.type == SGS_VT_OBJECT )
	{
		sgs_VarObj* O = V.data.O;
		sgs_VarObj* prevObj = pSD->curObj;
		_STACK_PREPARE;
		if( !O->iface->serialize )
		{
			sgs_Msg( C, SGS_WARNING, "Cannot serialize object of type '%s'", O->iface->name );
			ret = SGS_ENOTSUP;
			goto fail;
		}
		pSD->curObj = O;
		_STACK_PROTECT;
		ret = O->iface->serialize( C, O );
		_STACK_UNPROTECT;
		pSD->curObj = prevObj;
		if( SGS_FAILED( ret ) )
			goto fail;
	}
	else if( V.type == SGS_VT_CFUNC )
	{
		sgs_Msg( C, SGS_WARNING, "Cannot serialize C functions" );
		ret = SGS_EINVAL;
		goto fail;
	}
	else if( V.type == SGS_VT_PTR )
	{
		sgs_Msg( C, SGS_WARNING, "Cannot serialize pointers" );
		ret = SGS_EINVAL;
		goto fail;
	}
	else
	{
		sgs_StkIdx argidx;
		sgs_VHTVar* vv = sgs_vht_get( &pSD->servartable, &V );
		if( vv )
			argidx = (sgs_StkIdx) ( vv - pSD->servartable.vars );
		else
		{
			sgs_Variable idxvar;
			argidx = sgs_vht_size( &pSD->servartable );
			idxvar.type = SGS_VT_INT;
			idxvar.data.I = argidx;
			sgs_vht_set( &pSD->servartable, C, &V, &idxvar );
		}
		sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
	}
	
fail:
	if( ep )
	{
		if( ret == SGS_SUCCESS )
		{
			sgs_MemBuf B = sgs_membuf_create();
			
			/* combine all serialization info */
			{
				sgs_VHTVar* p = SD.servartable.vars;
				sgs_VHTVar* pend = p + SD.servartable.size;
				while( p < pend )
				{
					sgs_Variable* pKV = &p->key;
					if( pKV->type == SGS_VT_OBJECT )
					{
						/* object, serialized to block */
						sgs_membuf_appbuf( &B, C, sgs_var_cstr( &p->val ), p->val.data.S->size );
					}
					else
					{
						/* other, non-CFunc variables */
						char pb[2];
						{
							pb[0] = 'P';
							/* WP: basetype limited to bits 0-7, sign interpretation does not matter */
							pb[1] = (char) pKV->type;
						}
						sgs_membuf_appbuf( &B, C, pb, 2 );
						switch( pKV->type )
						{
						case SGS_VT_NULL: break;
						/* WP: pKV->data.B uses only bit 0 */
						case SGS_VT_BOOL: { char b = (char) pKV->data.B; sgs_membuf_appbuf( &B, C, &b, 1 ); } break;
						case SGS_VT_INT: sgs_membuf_appbuf( &B, C, &pKV->data.I, sizeof( sgs_Int ) ); break;
						case SGS_VT_REAL: sgs_membuf_appbuf( &B, C, &pKV->data.R, sizeof( sgs_Real ) ); break;
						case SGS_VT_STRING:
							sgs_membuf_appbuf( &B, C, &pKV->data.S->size, 4 );
							sgs_membuf_appbuf( &B, C, sgs_var_cstr( pKV ), pKV->data.S->size );
							break;
						case SGS_VT_FUNC:
							{
								size_t szbefore = B.size;
								if( !_serialize_function( C, pKV->data.F, &B ) )
								{
									ret = SGS_EINPROC;
									goto fail2;
								}
								else
								{
									uint32_t szdiff = (uint32_t) ( B.size - szbefore );
									sgs_membuf_insbuf( &B, C, szbefore, &szdiff, sizeof(szdiff) );
								}
							}
							break;
						default:
							sgs_Msg( C, SGS_ERROR, "sgs_Serialize: Unknown memory error" );
							ret = SGS_EINPROC;
							goto fail2;
						}
					}
					p++;
				}
			}
			
			if( B.size > 0x7fffffff )
			{
				sgs_Msg( C, SGS_WARNING, "Serialized string too long" );
				ret = SGS_EINVAL;
			}
			else
			{
				sgs_Pop( C, 1 );
				/* WP: added error condition */
				sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
			}
fail2:
			sgs_membuf_destroy( &B, C );
		}
		sgs_vht_free( &SD.servartable, C );
		sgs_membuf_destroy( &SD.argarray, C );
		C->serialize_state = prev_serialize_state;
	}
	else
		sgs_Pop( C, 1 );
	return ret;
}

SGSRESULT sgs_SerializeObjectInt_V2( SGS_CTX, StkIdx args, const char* func, size_t fnsize )
{
	size_t argsize;
	sgs_VHTVar* vv;
	sgs_Variable V;
	sgs_StkIdx argidx;
	sgs_serialize2_data* pSD;
	if( !C->serialize_state || *(int*)C->serialize_state != 2 )
		return SGS_EINPROC;
	pSD = (sgs_serialize2_data*) C->serialize_state;
	
	if( args < 0 || (size_t) args > pSD->argarray.size / 4 )
		return SGS_EINVAL; /* too many arguments specified */
	/* WP: added error condition */
	argsize = sizeof(sgs_StkIdx) * (size_t) args;
	
	V.type = SGS_VT_OBJECT;
	V.data.O = pSD->curObj;
	vv = sgs_vht_get( &pSD->servartable, &V );
	if( vv )
		argidx = (sgs_StkIdx) ( vv - pSD->servartable.vars );
	else
	{
		sgs_Variable srlzdata;
		char* pbuf;
		char pb[6] = { 'C', 0, 0, 0, 0, 0 };
		{
			/* WP: they were pointless */
			pb[1] = (char)((args)&0xff);
			pb[2] = (char)((args>>8)&0xff);
			pb[3] = (char)((args>>16)&0xff);
			pb[4] = (char)((args>>24)&0xff);
		}
		
		/* WP: TODO error condition */
		sgs_InitStringAlloc( C, &srlzdata, (sgs_SizeVal)( 6 + argsize + fnsize + 1 ) );
		pbuf = sgs_var_cstr( &srlzdata );
		/* WP: have error condition + sign interpretation doesn't matter */
		pb[ 5 ] = (char) fnsize;
		memcpy( pbuf, pb, 6 );
		memcpy( pbuf + 6, pSD->argarray.ptr + pSD->argarray.size - argsize, argsize );
		memcpy( pbuf + 6 + argsize, func, fnsize );
		pbuf[ 6 + argsize + fnsize ] = 0;
		
		argidx = sgs_vht_size( &pSD->servartable );
		sgs_vht_set( &pSD->servartable, C, &V, &srlzdata );
		sgs_Release( C, &srlzdata );
	}
	sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - argsize, pSD->argarray.size );
	sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
	
	return SGS_SUCCESS;
}

SGSRESULT sgs_UnserializeInt_V2( SGS_CTX, char* str, char* strend )
{
	sgs_Variable var;
	SGSRESULT res = SGS_EINPROC;
	sgs_MemBuf mb = sgs_membuf_create();
	_STACK_PREPARE;
	
	_STACK_PROTECT;
	while( str < strend )
	{
		char c = *str++;
		if( c == 'P' )
		{
			if( str >= strend )
				goto fail;
			c = *str++;
			switch( c )
			{
			case SGS_VT_NULL: sgs_InitNull( &var ); break;
			case SGS_VT_BOOL:
				if( str >= strend )
					goto fail;
				sgs_InitBool( &var, *str++ );
				break;
			case SGS_VT_INT:
				if( str >= strend-7 )
					goto fail;
				else
				{
					sgs_Int val;
					SGS_AS_INTEGER( val, str );
					sgs_InitInt( &var, val );
				}
				str += 8;
				break;
			case SGS_VT_REAL:
				if( str >= strend-7 )
					goto fail;
				else
				{
					sgs_Real val;
					SGS_AS_REAL( val, str );
					sgs_InitReal( &var, val );
				}
				str += 8;
				break;
			case SGS_VT_STRING:
				{
					sgs_SizeVal strsz;
					if( str >= strend-3 )
						goto fail;
					SGS_AS_INT32( strsz, str );
					str += 4;
					if( str > strend - strsz )
						goto fail;
					sgs_InitStringBuf( C, &var, str, strsz );
					str += strsz;
				}
				break;
			case SGS_VT_FUNC:
				{
					sgs_SizeVal bcsz;
					sgs_iFunc* fn;
					if( str >= strend-3 )
						goto fail;
					SGS_AS_INT32( bcsz, str );
					str += 4;
					if( str > strend - bcsz )
						goto fail;
					/* WP: conversion does not affect values */
					if( !_unserialize_function( C, str, (size_t) bcsz, &fn ) )
						goto fail;
					var.type = SGS_VT_FUNC;
					var.data.F = fn;
					fn->refcount++;
					str += bcsz;
				}
				break;
			default:
				goto fail;
			}
		}
		else if( c == 'C' )
		{
			sgs_StkIdx subsz;
			int32_t i, pos, argc;
			int fnsz, ret;
			if( str >= strend-5 )
				goto fail;
			SGS_AS_INT32( argc, str );
			str += 4;
			fnsz = *str++ + 1;
			for( i = 0; i < argc; ++i )
			{
				if( str >= strend-4 )
					goto fail;
				SGS_AS_INT32( pos, str );
				str += 4;
				if( pos < 0 || (size_t) pos >= mb.size / sizeof(sgs_Variable) )
					goto fail;
				sgs_PushVariable( C, ((sgs_Variable*) mb.ptr) + pos );
			}
			if( str > strend - fnsz )
				goto fail;
			subsz = sgs_StackSize( C ) - argc;
			ret = sgs_GlobalCall( C, str, argc, 1 );
			if( SGS_FAILED( ret ) || sgs_StackSize( C ) - subsz < 1 )
			{
				res = ret;
				goto fail;
			}
			sgs_GetStackItem( C, -1, &var );
			sgs_SetStackSize( C, subsz );
			str += fnsz;
		}
		else
		{
			goto fail;
		}
		sgs_membuf_appbuf( &mb, C, &var, sizeof(var) );
	}
	
	if( mb.size )
		sgs_PushVariable( C, (sgs_Variable*)( mb.ptr + mb.size - sizeof(sgs_Variable) ) );
	res = mb.size ? SGS_SUCCESS : SGS_EINPROC;
fail:
	_STACK_UNPROTECT_SKIP( SGS_SUCCEEDED( res ) );
	{
		sgs_Variable* ptr = (sgs_Variable*) mb.ptr;
		sgs_Variable* pend = (sgs_Variable*) ( mb.ptr + mb.size );
		while( ptr < pend )
		{
			sgs_Release( C, ptr++ );
		}
	}
	sgs_membuf_destroy( &mb, C );
	return res;
}


SGSRESULT sgs_Serialize( SGS_CTX )
{
	SGSRESULT res;
	sgs_StkIdx stksz = sgs_StackSize( C );
	if( !stksz )
		return SGS_EBOUNDS;
	
	if( C->state & SGS_CNTL_SERIALMODE )
		res = sgs_SerializeInt_V2( C );
	else
		res = sgs_SerializeInt_V1( C );
	return res;
}

SGSRESULT sgs_SerializeObject( SGS_CTX, StkIdx args, const char* func )
{
	SGSRESULT res;
	size_t fnsize = strlen( func );
	if( fnsize >= 255 )
		return SGS_EINVAL;
	if( C->state & SGS_CNTL_SERIALMODE )
		res = sgs_SerializeObjectInt_V2( C, args, func, fnsize );
	else
		res = sgs_SerializeObjectInt_V1( C, args, func, fnsize );
	return res;
}

SGSRESULT sgs_Unserialize( SGS_CTX )
{
	SGSRESULT res;
	char* str, *strend;
	sgs_SizeVal size;
	_STACK_PREPARE;
	if( !sgs_ParseString( C, -1, &str, &size ) || !size )
		return SGS_EINVAL;
	
	strend = str + size;
	
	_STACK_PROTECT;
	if( C->state & SGS_CNTL_SERIALMODE )
		res = sgs_UnserializeInt_V2( C, str, strend );
	else
		res = sgs_UnserializeInt_V1( C, str, strend );
	_STACK_UNPROTECT_SKIP( SGS_SUCCEEDED( res ) );
	return res;
}


SGSRESULT sgs_SerializeV1( SGS_CTX )
{
	int32_t old;
	SGSRESULT res;
	old = sgs_Cntl( C, SGS_CNTL_SERIALMODE, 1 );
	res = sgs_Serialize( C );
	sgs_Cntl( C, SGS_CNTL_SERIALMODE, old );
	return res;
}

SGSRESULT sgs_UnserializeV1( SGS_CTX )
{
	int32_t old;
	SGSRESULT res;
	old = sgs_Cntl( C, SGS_CNTL_SERIALMODE, 1 );
	res = sgs_Unserialize( C );
	sgs_Cntl( C, SGS_CNTL_SERIALMODE, old );
	return res;
}

SGSRESULT sgs_SerializeV2( SGS_CTX )
{
	int32_t old;
	SGSRESULT res;
	old = sgs_Cntl( C, SGS_CNTL_SERIALMODE, 2 );
	res = sgs_Serialize( C );
	sgs_Cntl( C, SGS_CNTL_SERIALMODE, old );
	return res;
}

SGSRESULT sgs_UnserializeV2( SGS_CTX )
{
	int32_t old;
	SGSRESULT res;
	old = sgs_Cntl( C, SGS_CNTL_SERIALMODE, 2 );
	res = sgs_Unserialize( C );
	sgs_Cntl( C, SGS_CNTL_SERIALMODE, old );
	return res;
}


int sgs_Compare( SGS_CTX, sgs_Variable* v1, sgs_Variable* v2 )
{
	return vm_compare( C, v1, v2 );
}

SGSBOOL sgs_EqualTypes( sgs_Variable* v1, sgs_Variable* v2 )
{
	return v1->type == v2->type && ( v1->type != SGS_VT_OBJECT
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
	return sgs_var_cstr( var );
}

char* sgs_ToStringBufFastP( SGS_CTX, sgs_Variable* var, sgs_SizeVal* outsize )
{
	if( var->type == SGS_VT_OBJECT )
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
	return var->type == SGS_VT_OBJECT && var->data.O->iface == iface;
}

SGSBOOL sgs_IsCallableP( sgs_Variable* var )
{
	uint32_t ty = var->type;
	if( ty == SGS_VT_FUNC || ty == SGS_VT_CFUNC )
		return 1;
	if( ty == SGS_VT_OBJECT && var->data.O->iface->call )
		return 1;
	return 0;
}

SGSBOOL sgs_ParseBoolP( SGS_CTX, sgs_Variable* var, sgs_Bool* out )
{
	uint32_t ty = var->type;
	if( ty == SGS_VT_NULL || ty == SGS_VT_CFUNC || ty == SGS_VT_FUNC || ty == SGS_VT_STRING )
		return SGS_FALSE;
	if( out )
		*out = sgs_GetBoolP( C, var );
	return SGS_TRUE;
}

SGSBOOL sgs_ParseIntP( SGS_CTX, sgs_Variable* var, sgs_Int* out )
{
	sgs_Int i;
	if( var->type == SGS_VT_NULL || var->type == SGS_VT_FUNC || var->type == SGS_VT_CFUNC )
		return SGS_FALSE;
	if( var->type == SGS_VT_STRING )
	{
		intreal_t OIR;
		const char* ostr = sgs_var_cstr( var );
		const char* str = ostr;
		int res = sgs_util_strtonum( &str, str + var->data.S->size, &OIR.i, &OIR.r );
		
		if( str == ostr )    return SGS_FALSE;
		if( res == 1 )       i = OIR.i;
		else if( res == 2 )  i = (sgs_Int) OIR.r;
		else                 return SGS_FALSE;
	}
	else
		i = sgs_GetIntP( C, var );
	if( out )
		*out = i;
	return SGS_TRUE;
}

SGSBOOL sgs_ParseRealP( SGS_CTX, sgs_Variable* var, sgs_Real* out )
{
	sgs_Real r;
	if( var->type == SGS_VT_NULL || var->type == SGS_VT_FUNC || var->type == SGS_VT_CFUNC )
		return SGS_FALSE;
	if( var->type == SGS_VT_STRING )
	{
		intreal_t OIR;
		const char* ostr = sgs_var_cstr( var );
		const char* str = ostr;
		int res = sgs_util_strtonum( &str, str + var->data.S->size, &OIR.i, &OIR.r );
		
		if( str == ostr )    return SGS_FALSE;
		if( res == 1 )       r = (sgs_Real) OIR.i;
		else if( res == 2 )  r = OIR.r;
		else                 return SGS_FALSE;
	}
	else
		r = sgs_GetRealP( C, var );
	if( out )
		*out = r;
	return SGS_TRUE;
}

SGSBOOL sgs_ParseStringP( SGS_CTX, sgs_Variable* var, char** out, sgs_SizeVal* size )
{
	char* str;
	uint32_t ty;
	ty = var->type;
	if( ty == SGS_VT_NULL || ty == SGS_VT_FUNC || ty == SGS_VT_CFUNC )
		return SGS_FALSE;
	str = sgs_ToStringBufP( C, var, size );
	if( out )
		*out = str;
	return str != NULL;
}

SGSBOOL sgs_ParseObjectPtrP( SGS_CTX, sgs_Variable* var,
	sgs_ObjInterface* iface, sgs_VarObj** out, int strict )
{
	if( !strict && var->type == SGS_VT_NULL )
	{
		if( *out )
			sgs_ObjRelease( C, *out );
		*out = NULL;
		return SGS_TRUE;
	}
	if( sgs_IsObjectP( var, iface ) )
	{
		if( *out )
			sgs_ObjRelease( C, *out );
		*out = sgs_GetObjectStructP( var );
		sgs_ObjAcquire( C, *out );
		return SGS_TRUE;
	}
	return SGS_FALSE;
}

SGSBOOL sgs_ParsePtrP( SGS_CTX, sgs_Variable* var, void** out )
{
	if( var->type != SGS_VT_NULL && var->type != SGS_VT_PTR )
		return SGS_FALSE;
	if( out )
		*out = var->type != SGS_VT_NULL ? var->data.P : NULL;
	return SGS_TRUE;
}

SGSMIXED sgs_ArraySizeP( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SGS_VT_OBJECT ||
		var->data.O->iface != SGSIFACE_ARRAY )
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
	return sgs_var_cstr( var );
}

char* sgs_ToStringBufFast( SGS_CTX, StkIdx item, sgs_SizeVal* outsize )
{
	if( !sgs_IsValidIndex( C, item ) )
		return NULL;
	if( stk_getpos( C, item )->type == SGS_VT_OBJECT )
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
		return SGS_FALSE;
	var = stk_getpos( C, item );
	return var->type == SGS_VT_OBJECT && var->data.O->iface == iface;
}

SGSBOOL sgs_IsCallable( SGS_CTX, StkIdx item )
{
	uint32_t ty = sgs_ItemType( C, item );
	
	if( ty == SGS_VT_FUNC || ty == SGS_VT_CFUNC )
		return 1;
	if( ty == SGS_VT_OBJECT && sgs_GetObjectIface( C, item )->call )
		return 1;
	return 0;
}

SGSBOOL sgs_ParseBool( SGS_CTX, StkIdx item, sgs_Bool* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	return sgs_ParseBoolP( C, stk_getpos( C, item ), out );
}

SGSBOOL sgs_ParseInt( SGS_CTX, StkIdx item, sgs_Int* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	return sgs_ParseIntP( C, stk_getpos( C, item ), out );
}

SGSBOOL sgs_ParseReal( SGS_CTX, StkIdx item, sgs_Real* out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	return sgs_ParseRealP( C, stk_getpos( C, item ), out );
}

SGSBOOL sgs_ParseString( SGS_CTX, StkIdx item, char** out, sgs_SizeVal* size )
{
	char* str;
	uint32_t ty;
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	ty = sgs_ItemType( C, item );
	if( ty == SGS_VT_NULL || ty == SGS_VT_FUNC || ty == SGS_VT_CFUNC )
		return SGS_FALSE;
	str = sgs_ToStringBuf( C, item, size );
	if( out )
		*out = str;
	return str != NULL;
}

SGSBOOL sgs_ParseObjectPtr( SGS_CTX, sgs_StkIdx item,
	sgs_ObjInterface* iface, sgs_VarObj** out, int strict )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	return sgs_ParseObjectPtrP( C, stk_getpos( C, item ), iface, out, strict );
}

SGSBOOL sgs_ParsePtr( SGS_CTX, StkIdx item, void** out )
{
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	return sgs_ParsePtrP( C, stk_getpos( C, item ), out );
}

SGSMIXED sgs_ArraySize( SGS_CTX, StkIdx item )
{
	if( sgs_ItemType( C, item ) != SGS_VT_OBJECT ||
		sgs_GetObjectIface( C, item ) != SGSIFACE_ARRAY )
		return SGS_EINVAL;
	return ((sgsstd_array_header_t*)sgs_GetObjectData( C, item ))->size;
}


sgs_Bool sgs_GlobalBool( SGS_CTX, const char* name )
{
	sgs_Bool v;
	if( SGS_FAILED( sgs_PushGlobal( C, name ) ) )
		return 0;
	v = var_getbool( C, stk_gettop( C ) );
	stk_pop1( C );
	return v;
}

sgs_Int sgs_GlobalInt( SGS_CTX, const char* name )
{
	sgs_Int v;
	if( SGS_FAILED( sgs_PushGlobal( C, name ) ) )
		return 0;
	v = var_getint( C, stk_gettop( C ) );
	stk_pop1( C );
	return v;
}

sgs_Real sgs_GlobalReal( SGS_CTX, const char* name )
{
	sgs_Real v;
	if( SGS_FAILED( sgs_PushGlobal( C, name ) ) )
		return 0;
	v = var_getreal( C, stk_gettop( C ) );
	stk_pop1( C );
	return v;
}

char* sgs_GlobalStringBuf( SGS_CTX, const char* name, sgs_SizeVal* outsize )
{
	char* buf;
	if( SGS_FAILED( sgs_PushGlobal( C, name ) ) )
		return NULL;
	if( !sgs_ParseString( C, -1, &buf, outsize ) )
	{
		stk_pop1( C );
		return NULL;
	}
	return buf;
}


SGSRESULT sgs_PushIteratorP( SGS_CTX, sgs_Variable* var )
{
	int ret;
	sgs_PushNull( C );
	ret = vm_forprep( C, stk_absindex( C, -1 ), var );
	if( SGS_FAILED( ret ) )
		stk_pop1( C );
	return ret;
}

SGSRESULT sgs_GetIteratorP( SGS_CTX, sgs_Variable* var, sgs_Variable* out )
{
	int ret;
	sgs_PushNull( C );
	ret = vm_forprep( C, stk_absindex( C, -1 ), var );
	if( SGS_FAILED( ret ) )
		stk_pop1( C );
	else
		sgs_StoreVariable( C, out );
	return ret;
}

SGSMIXED sgs_IterAdvanceP( SGS_CTX, sgs_Variable* var )
{
	sgs_SizeVal ret;
	_EL_BACKUP;
	_EL_SETMAX;
	ret = vm_fornext( C, -1, -1, var );
	_EL_RESET;
	return ret;
}

SGSMIXED sgs_IterPushDataP( SGS_CTX, sgs_Variable* var, int key, int value )
{
	_EL_BACKUP;
	sgs_SizeVal ret;
	StkIdx idkey, idval;
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
	ret = vm_fornext( C, idkey, idval, var );
	_EL_RESET;
	if( SGS_FAILED( ret ) )
		stk_pop( C, !!key + !!value );
	return ret;
}

SGSMIXED sgs_IterGetDataP( SGS_CTX, sgs_Variable* var, sgs_Variable* key, sgs_Variable* value )
{
	_EL_BACKUP;
	sgs_SizeVal ret;
	if( !key && !value )
		return SGS_SUCCESS;
	if( key ) sgs_PushNull( C );
	if( value ) sgs_PushNull( C );
	_EL_SETMAX;
	ret = vm_fornext( C, key?stk_absindex(C,value?-2:-1):-1, value?stk_absindex(C,-1):-1, var );
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


SGSRESULT sgs_PushIterator( SGS_CTX, StkIdx item )
{
	sgs_Variable var;
	if( !sgs_PeekStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	return sgs_PushIteratorP( C, &var );
}

SGSRESULT sgs_GetIterator( SGS_CTX, StkIdx item, sgs_Variable* out )
{
	sgs_Variable var;
	if( !sgs_PeekStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	return sgs_GetIteratorP( C, &var, out );
}

SGSMIXED sgs_IterAdvance( SGS_CTX, StkIdx item )
{
	sgs_Variable var;
	if( !sgs_PeekStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	return sgs_IterAdvanceP( C, &var );
}

SGSMIXED sgs_IterPushData( SGS_CTX, StkIdx item, int key, int value )
{
	sgs_Variable var;
	if( !sgs_PeekStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	return sgs_IterPushDataP( C, &var, key, value );
}

SGSMIXED sgs_IterGetData( SGS_CTX, sgs_StkIdx item, sgs_Variable* key, sgs_Variable* value )
{
	sgs_Variable var;
	if( !sgs_PeekStackItem( C, item, &var ) )
		return SGS_EBOUNDS;
	return sgs_IterGetDataP( C, &var, key, value );
}


SGSBOOL sgs_IsNumericString( const char* str, sgs_SizeVal size )
{
	intreal_t out;
	const char* ostr = str;
	return sgs_util_strtonum( &str, str + size, &out.i, &out.r ) != 0 && str != ostr;
}


/*

	EXTENSION UTILITIES

*/

SGSBOOL sgs_Method( SGS_CTX )
{
	if( C->sf_last && SGS_HAS_FLAG( C->sf_last->flags, SGS_SF_METHOD ) &&
		!SGS_HAS_FLAG( C->sf_last->flags, SGS_SF_HASTHIS ) )
	{
		C->stack_off--;
		C->sf_last->flags |= SGS_SF_HASTHIS;
		return SGS_TRUE;
	}
	return SGS_FALSE;
}

SGSBOOL sgs_HideThis( SGS_CTX )
{
	if( C->sf_last && SGS_HAS_FLAG( C->sf_last->flags, (SGS_SF_METHOD|SGS_SF_HASTHIS) ) )
	{
		C->stack_off++;
		/* WP: implicit conversion to int */
		C->sf_last->flags &= (uint8_t) ~SGS_SF_HASTHIS;
		return SGS_TRUE;
	}
	return SGS_FALSE;
}

SGSBOOL sgs_ForceHideThis( SGS_CTX )
{
	if( !C->sf_last )
		return SGS_FALSE;
	if( SGS_HAS_FLAG( C->sf_last->flags, SGS_SF_METHOD ) )
		return sgs_HideThis( C );
	if( SGS_STACKFRAMESIZE < 1 )
		return SGS_FALSE;
	C->stack_off++;
	C->sf_last->flags = ( C->sf_last->flags | SGS_SF_METHOD ) & (uint8_t) (~SGS_SF_HASTHIS);
	return SGS_TRUE;
}


void sgs_Acquire( SGS_CTX, sgs_Variable* var )
{
	UNUSED( C );
	VAR_ACQUIRE( var );
}

void sgs_AcquireArray( SGS_CTX, sgs_Variable* var, sgs_SizeVal count )
{
	sgs_Variable* vend = var + count;
	while( var < vend )
		sgs_Acquire( C, var++ );
}

void sgs_Release( SGS_CTX, sgs_Variable* var )
{
	if( var->type == SGS_VT_OBJECT && C->gcrun )
	{
		/* if running GC, dereference without destroying */
		(*var->data.pRC) -= 1;
		return;
	}
	VAR_RELEASE( var );
}

void sgs_ReleaseArray( SGS_CTX, sgs_Variable* var, sgs_SizeVal count )
{
	sgs_Variable* vend = var + count;
	while( var < vend )
		sgs_Release( C, var++ );
}

SGSRESULT sgs_GCMark( SGS_CTX, sgs_Variable* var )
{
	return vm_gcmark( C, var );
}

SGSRESULT sgs_GCMarkArray( SGS_CTX, sgs_Variable* var, sgs_SizeVal count )
{
	SGSRESULT res = SGS_SUCCESS;
	sgs_Variable* vend = var + count;
	while( var < vend )
	{
		if( SGS_FAILED( vm_gcmark( C, var++ ) ) )
			res = SGS_EINPROC;
	}
	return res;
}

void sgs_ObjAcquire( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SGS_VT_OBJECT;
	var.data.O = obj;
	sgs_Acquire( C, &var );
}

void sgs_ObjRelease( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SGS_VT_OBJECT;
	var.data.O = obj;
	sgs_Release( C, &var );
}

SGSRESULT sgs_ObjGCMark( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SGS_VT_OBJECT;
	var.data.O = obj;
	return sgs_GCMark( C, &var );
}

void sgs_ObjAssign( SGS_CTX, sgs_VarObj** dest, sgs_VarObj* src )
{
	if( *dest )
		sgs_ObjRelease( C, *dest );
	*dest = src;
	if( src )
		sgs_ObjAcquire( C, src );
}

void sgs_ObjCallDtor( SGS_CTX, sgs_VarObj* obj )
{
	var_destruct_object( C, obj );
}

void sgs_ObjSetMetaObj( SGS_CTX, sgs_VarObj* obj, sgs_VarObj* metaobj )
{
	if( obj->metaobj )
		sgs_ObjRelease( C, obj->metaobj );
	obj->metaobj = metaobj;
	if( metaobj )
		sgs_ObjAcquire( C, metaobj );
}

sgs_VarObj* sgs_ObjGetMetaObj( sgs_VarObj* obj )
{
	return obj->metaobj;
}

void sgs_ObjSetMetaMethodEnable( sgs_VarObj* obj, SGSBOOL enable )
{
	obj->mm_enable = !!enable;
}

SGSBOOL sgs_ObjGetMetaMethodEnable( sgs_VarObj* obj )
{
	return obj->mm_enable;
}


#define DBLCHK( what, fval )\
	sgs_BreakIf( what );\
	if( what ) return fval;


#define _OBJPREP_P( ret ) \
	DBLCHK( var->type != SGS_VT_OBJECT, ret )

char* sgs_GetStringPtrP( sgs_Variable* var )
{
	DBLCHK( var->type != SGS_VT_STRING, NULL )
	return sgs_var_cstr( var );
}

sgs_SizeVal sgs_GetStringSizeP( sgs_Variable* var )
{
	DBLCHK( var->type != SGS_VT_STRING, 0 )
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

SGSBOOL sgs_SetObjectDataP( sgs_Variable* var, void* data )
{
	_OBJPREP_P( 0 );
	var->data.O->data = data;
	return 1;
}

SGSBOOL sgs_SetObjectIfaceP( sgs_Variable* var, sgs_ObjInterface* iface )
{
	_OBJPREP_P( 0 );
	var->data.O->iface = iface;
	return 1;
}


#define _OBJPREP( ret ) \
	sgs_Variable* var; \
	DBLCHK( !sgs_IsValidIndex( C, item ), ret ) \
	var = stk_getpos( C, item ); \
	DBLCHK( var->type != SGS_VT_OBJECT, ret )

char* sgs_GetStringPtr( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	DBLCHK( !sgs_IsValidIndex( C, item ), NULL )
	var = stk_getpos( C, item );
	DBLCHK( var->type != SGS_VT_STRING, NULL )
	return sgs_var_cstr( var );
}

sgs_SizeVal sgs_GetStringSize( SGS_CTX, StkIdx item )
{
	sgs_Variable* var;
	DBLCHK( !sgs_IsValidIndex( C, item ), 0 )
	var = stk_getpos( C, item );
	DBLCHK( var->type != SGS_VT_STRING, 0 )
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

SGSBOOL sgs_SetObjectData( SGS_CTX, StkIdx item, void* data )
{
	_OBJPREP( 0 );
	var->data.O->data = data;
	return 1;
}

SGSBOOL sgs_SetObjectIface( SGS_CTX, StkIdx item, sgs_ObjInterface* iface )
{
	_OBJPREP( 0 );
	var->data.O->iface = iface;
	return 1;
}

#undef DBLCHK


#undef StkIdx

