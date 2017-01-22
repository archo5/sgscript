

#include <math.h>
#include <limits.h>

#define SGS_INTERNAL_STRINGTABLES

#define StkIdx sgs_StkIdx

#include "sgs_int.h"


#define _EL_BACKUP int oapi = ( C->state & SGS_STATE_INSIDE_API ) != 0
#define _EL_SETAPI(is) C->state = ( C->state & ~SGS_STATE_INSIDE_API ) | ( is ? SGS_STATE_INSIDE_API : 0 )
#define _EL_RESET _EL_SETAPI(oapi)


#define TYPENAME( type ) sgs_VarNames[ type ]


#define STK_UNITSIZE sizeof( sgs_Variable )


typedef union intreal_t
{
	sgs_Int i;
	sgs_Real r;
}
intreal_t;


/* to work with both insertion and removal algorithms, this function has the following rules:
- return the index of the first found item with the right size or just after the right size
- if all sizes are less than specified, return the size of the object pool
*/
static int32_t objpool_binary_search( SGS_SHCTX, uint32_t appsize )
{
	int32_t pmin = 0, pmax = S->objpool_size - 1;
	while( pmin <= pmax )
	{
		int32_t pos = ( pmax + pmin ) / 2;
		uint32_t ssize = S->objpool_data[ pos ].appsize;
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
	SGS_SHCTX_USE;
	if( O->is_iface )
	{
		sgs_VHTVar* p = S->ifacetable.vars;
		sgs_VHTVar* pend = p + S->ifacetable.size;
		while( p < pend )
		{
			if( p->val.type == SGS_VT_OBJECT && p->val.data.O == O )
			{
				sgs_Variable K = p->key;
				O->refcount = 2;
				sgs_vht_unset( &S->ifacetable, C, &K );
				break;
			}
			p++;
		}
	}
	if( O->prev ) O->prev->next = O->next;
	if( O->next ) O->next->prev = O->prev;
	if( S->objs == O )
		S->objs = O->next;
#if SGS_OBJPOOL_SIZE > 0
	if( O->appsize <= SGS_OBJPOOL_MAX_APPMEM )
	{
		int32_t pos = 0;
		if( S->objpool_size )
		{
			pos = objpool_binary_search( S, O->appsize );
			if( S->objpool_size < SGS_OBJPOOL_SIZE && pos < S->objpool_size )
			{
				memmove( S->objpool_data + pos + 1, S->objpool_data + pos,
					sizeof( sgs_ObjPoolItem ) * (size_t) ( S->objpool_size - pos ) );
			}
			if( pos >= SGS_OBJPOOL_SIZE )
				pos = SGS_OBJPOOL_SIZE - 1;
			if( S->objpool_size >= SGS_OBJPOOL_SIZE )
				sgs_Dealloc( S->objpool_data[ pos ].obj );
		}
		S->objpool_data[ pos ].obj = O;
		S->objpool_data[ pos ].appsize = O->appsize;
		if( S->objpool_size < SGS_OBJPOOL_SIZE )
			S->objpool_size++;
	}
	else
		sgs_Dealloc( O );
#else
	sgs_Dealloc( O );
#endif
	S->objcount--;
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

static void var_destroy_string( SGS_CTX, sgs_iStr* str )
{
#if SGS_STRINGTABLE_MAXLEN >= 0
	if( str->size <= SGS_STRINGTABLE_MAXLEN )
	{
		SGS_SHCTX_USE;
		sgs_VHTVar* p;
		sgs_Variable tmp;
		tmp.type = SGS_VT_STRING;
		tmp.data.S = str;
		p = sgs_vht_get( &S->stringtable, &tmp );
		if( p )
		{
#  if SGS_DEBUG && SGS_DEBUG_EXTRA
			if( p->key.data.S != str )
			{
				printf( "WATWATWAT: (%d) |my>| [%d,%d] %d,%s |st>| [%d,%d] %d,%s\n",
					(int) sgs_HashFunc( sgs_str_cstr( str ), str->size ),
					(int) str->refcount, (int) str->hash, (int) str->size, sgs_str_cstr( str ),
					(int) p->key.data.S->refcount, (int) p->key.data.S->hash, (int) p->key.data.S->size, sgs_str_cstr( p->key.data.S ) );
			}
#  endif
			sgs_BreakIf( p->key.data.S != str );
			str->refcount = 2; /* the 'less code' way to avoid double free */
			sgs_vht_unset( &S->stringtable, C, &tmp );
		}
	}
#endif
	sgs_Dealloc( str );
}

static void var_destroy_func( SGS_CTX, sgs_iFunc* F )
{
	sgs_Variable *var = (sgs_Variable*) sgs_func_consts( F ),
		*vend = SGS_ASSUME_ALIGNED( sgs_func_bytecode( F ), sgs_Variable );
	while( var < vend )
	{
		VAR_RELEASE( var );
		var++;
	}
	sgs_Dealloc( F->lineinfo );
	if( F->dbg_varinfo ) sgs_Dealloc( F->dbg_varinfo );
	if( --F->sfuncname->refcount <= 0 ) var_destroy_string( C, F->sfuncname );
	if( --F->sfilename->refcount <= 0 ) var_destroy_string( C, F->sfilename );
	sgs_Dealloc( F );
}

void sgsVM_DestroyVar( SGS_CTX, sgs_Variable* p )
{
	switch( p->type )
	{
	case SGS_VT_STRING: var_destroy_string( C, p->data.S ); break;
	case SGS_VT_FUNC: var_destroy_func( C, p->data.F ); break;
	case SGS_VT_OBJECT: if( !C->shared->gcrun ) sgsVM_VarDestroyObject( C, p->data.O ); break;
	case SGS_VT_THREAD: sgsCTX_FreeState( p->data.T ); break;
	}
}


static void var_create_0str( SGS_CTX, sgs_Variable* out, uint32_t len )
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
	SGS_SHCTX_USE;
	sgs_BreakIf( !str && len );
	
	ulen = (uint32_t) len; /* WP: string limit */
	
	hash = sgs_HashFunc( str, ulen );
#if SGS_STRINGTABLE_MAXLEN >= 0
	if( ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_VHTVar* var = sgs_vht_get_str( &S->stringtable, str, ulen, hash );
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
	
	if( ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_vht_set( &S->stringtable, C, out, NULL );
		out->data.S->refcount--;
	}
}

int sgsVM_VarGetString( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal len )
{
	uint32_t ulen = (uint32_t) len; /* WP: string limit */
	if( ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		SGS_SHCTX_USE;
		sgs_Hash hash = sgs_HashFunc( str, ulen );
		sgs_VHTVar* var = sgs_vht_get_str( &S->stringtable, str, ulen, hash );
		if( var )
		{
			*out = var->key;
			out->data.S->refcount++;
			return 1;
		}
		return 0;
	}
	else
	{
		sgsVM_VarCreateString( C, out, str, len );
		return 1;
	}
}

static void var_finalize_str( SGS_CTX, sgs_Variable* out )
{
	char* str;
	sgs_Hash hash;
	uint32_t ulen;
	SGS_SHCTX_USE;
	
	str = sgs_str_cstr( out->data.S );
	ulen = out->data.S->size;
	hash = sgs_HashFunc( str, ulen );
	
#if SGS_STRINGTABLE_MAXLEN >= 0
	if( ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_VHTVar* var = sgs_vht_get_str( &S->stringtable, str, ulen, hash );
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
	
	if( ulen <= SGS_STRINGTABLE_MAXLEN )
	{
		sgs_vht_set( &S->stringtable, C, out, NULL );
		out->data.S->refcount--;
	}
}

static void var_create_obj( SGS_CTX, sgs_Variable* out, void* data, sgs_ObjInterface* iface, uint32_t xbytes )
{
	SGS_SHCTX_USE;
	sgs_VarObj* obj = NULL;
#if SGS_OBJPOOL_SIZE > 0
	if( xbytes <= SGS_OBJPOOL_MAX_APPMEM )
	{
		int32_t pos = objpool_binary_search( S, xbytes );
		if( pos < S->objpool_size && S->objpool_data[ pos ].appsize == xbytes )
		{
			obj = S->objpool_data[ pos ].obj;
			S->objpool_size--;
			if( pos < S->objpool_size )
			{
				memmove( S->objpool_data + pos, S->objpool_data + pos + 1,
					sizeof( sgs_ObjPoolItem ) * (size_t) ( S->objpool_size - pos ) );
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
	obj->redblue = S->redblue;
	obj->next = S->objs;
	obj->prev = NULL;
	obj->refcount = 1;
	if( obj->next ) /* ! */
		obj->next->prev = obj;
	obj->metaobj = NULL;
	obj->mm_enable = SGS_FALSE;
	obj->in_setindex = SGS_FALSE;
	obj->is_iface = SGS_FALSE;
	S->objcount++;
	S->objs = obj;
	
	out->type = SGS_VT_OBJECT;
	out->data.O = obj;
}


/*
	Call stack
*/

int sgsVM_PushStackFrame( SGS_CTX, sgs_Variable* func )
{
	sgs_StackFrame* F;
	
	if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
	{
		sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
		return 0;
	}
	
	F = sgsCTX_AllocFrame( C );
	C->sf_count++;
	
	if( func->type == SGS_VT_OBJECT && func->data.O->iface == sgsstd_closure_iface )
	{
		uint8_t* cl = (uint8_t*) func->data.O->data;
		F->clsrlist = SGS_ASSUME_ALIGNED( cl + sizeof(sgs_Variable) + sizeof(sgs_clsrcount_t), sgs_Closure* );
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
		F->clsrcount = (uint8_t) *SGS_ASSUME_ALIGNED( cl + sizeof(sgs_Variable), sgs_clsrcount_t );
#endif
		F->clsrref = func->data.O;
		F->clsrref->refcount++; /* acquire closure */
		p_setvar_race( func, SGS_ASSUME_ALIGNED( cl, sgs_Variable ) );
		
		if( func->type == SGS_VT_FUNC )
		{
			int i;
			sgs_iFunc* fn = func->data.F;
			for( i = fn->inclsr; i < fn->numclsr; ++i )
			{
				if( --F->clsrlist[ i ]->refcount <= 0 )
				{
					VAR_RELEASE( &F->clsrlist[ i ]->var );
					sgs_Dealloc( F->clsrlist[ i ] );
				}
				sgs_Closure* nc = sgs_Alloc( sgs_Closure );
				nc->refcount = 1;
				nc->var.type = SGS_VT_NULL;
				F->clsrlist[ i ] = nc;
			}
		}
	}
	else
	{
		F->clsrref = NULL;
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
		F->clsrlist = NULL;
		F->clsrcount = 0;
#endif
	}
	
	F->func = func;
	if( func->type == SGS_VT_FUNC )
	{
		sgs_iFunc* fn = func->data.F;
		F->iptr = sgs_func_bytecode( fn );
		F->cptr = sgs_func_consts( fn );
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
		F->code = F->iptr;
		F->iend = F->iptr + sgs_func_instr_count( fn );
		/* WP: const limit */
		F->constcount = (int32_t) sgs_func_const_count( fn );
#endif
	}
	else
	{
		F->iptr = NULL;
		F->cptr = NULL;
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
		F->code = NULL;
		F->iend = NULL;
		F->constcount = 0;
#endif
	}
	F->next = NULL;
	F->prev = C->sf_last;
	F->errsup = 0;
	F->flags = 0;
	F->nfname = NULL;
	if( C->sf_last )
	{
		F->errsup = C->sf_last->errsup;
		C->sf_last->next = F;
	}
	else
	{
		C->sf_first = F;
	}
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
	
	if( F->clsrref )
		sgs_ObjRelease( C, F->clsrref );
	
	/* F->func is on stack */
	C->sf_count--;
	if( F->prev )
		F->prev->next = NULL;
	C->sf_last = F->prev;
	if( C->sf_first == F )
		C->sf_first = NULL;
	sgsCTX_FreeFrame( F );
}


/*
	Stack management
*/

static SGS_INLINE sgs_Variable* stk_getpos( SGS_CTX, StkIdx stkid )
{
#if SGS_DEBUG && SGS_DEBUG_VALIDATE && SGS_DEBUG_EXTRA
	if( stkid < 0 ) sgs_BreakIf( -stkid > C->stack_top - C->stack_off )
	else            sgs_BreakIf( stkid >= C->stack_top - C->stack_off )
#endif
	if( stkid < 0 )	return C->stack_top + stkid;
	else			return C->stack_off + stkid;
}

static SGS_INLINE void stk_setvar( SGS_CTX, StkIdx stkid, sgs_Variable* var )
{
	sgs_Variable* vpos = stk_getpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
	VAR_ACQUIRE( vpos );
}
static SGS_INLINE void stk_setvar_leave( SGS_CTX, StkIdx stkid, sgs_Variable* var )
{
	sgs_Variable* vpos = stk_getpos( C, stkid );
	VAR_RELEASE( vpos );
	*vpos = *var;
}

static void stk_rebase_pointers( SGS_CTX, sgs_Variable* to, sgs_Variable* from )
{
	sgs_StackFrame* sf = C->sf_first;
	while( sf )
	{
		if( sf->func )
			sf->func = ( sf->func - from ) + to;
		sf = sf->next;
	}
}

void fstk_resize( SGS_CTX, size_t nsz )
{
	/* StkIdx item stack limit */
	sgs_Variable* prevbase;
	ptrdiff_t stkoff, stkend;
	
	stkoff = ptr_sub( C->stack_off, C->stack_base );
	stkend = ptr_sub( C->stack_top, C->stack_base );
	prevbase = C->stack_base;
	
#if DEBUG_STACK /* dangling pointer / wrong makespace size test */
	
	C->stack_base = sgs_Alloc_n( sgs_Variable, nsz );
	memcpy( C->stack_base, prevbase, (size_t) stkend );
	sgs_Dealloc( prevbase );
	
#else
	
	nsz += C->stack_mem * 2; /* cheaper alternative to MAX( nsz, C->stack_mem * 2 ); */
	C->stack_base = (sgs_Variable*) sgs_Realloc( C, C->stack_base, sizeof( sgs_Variable ) * nsz );
	
#endif
	
	stk_rebase_pointers( C, C->stack_base, prevbase );
	C->stack_mem = (uint32_t) nsz;
	C->stack_off = SGS_ASSUME_ALIGNED( ptr_add( C->stack_base, stkoff ), sgs_Variable );
	C->stack_top = SGS_ASSUME_ALIGNED( ptr_add( C->stack_base, stkend ), sgs_Variable );
}

void fstk_push( SGS_CTX, sgs_Variable* vp ){ stk_push( C, vp ); }
void fstk_push_leave( SGS_CTX, sgs_Variable* vp ){ stk_push_leave( C, vp ); }
void fstk_push2( SGS_CTX, sgs_Variable* vp1, sgs_Variable* vp2 ){ stk_push2( C, vp1, vp2 ); }
void fstk_push_null( SGS_CTX ){ stk_push_null( C ); }
void fstk_umpush( SGS_CTX, sgs_Variable* vp, sgs_SizeVal cnt )
{
	sgs_Variable* srcp = vp;
	sgs_Variable* srcend = srcp + cnt;
	while( srcp != srcend )
	{
		VAR_ACQUIRE( srcp );
		*(C)->stack_top++ = *srcp++;
	}
}
void fstk_pop1( SGS_CTX ){ stk_pop1( C ); }

static void stk_push_nulls( SGS_CTX, StkIdx cnt )
{
	sgs_Variable* tgt;
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

static sgs_Variable* stk_insert_posn( SGS_CTX, StkIdx off )
{
	sgs_Variable *op, *p;
	stk_makespace( C, 1 );
	op = C->stack_top - off, p = C->stack_top;
	while( p != op )
	{
		*p = *(p-1);
		p--;
	}
	C->stack_top++;
	return op;
}

void fstk_clean( SGS_CTX, sgs_Variable* from, sgs_Variable* to )
{
	ptrdiff_t oh;
	sgs_Variable *p = from, *pend = to;
	sgs_BreakIf( C->stack_top < to );
	sgs_BreakIf( to < from );
	sgs_BreakIf( from < C->stack_base );
	
	while( p < pend )
	{
		VAR_RELEASE( p );
		p++;
	}
	
	oh = ptr_sub( C->stack_top, to );
	ptr_dec( C->stack_top, ptr_sub( to, from ) );
	
	if( oh )
		memmove( from, to, (size_t) oh );
}

void fstk_pop( SGS_CTX, sgs_StkIdx num )
{
	sgs_Variable* to = C->stack_top - num;
	sgs_BreakIf( num < 0 || num > SGS_STACKFRAMESIZE );
	stk_popto( C, to );
}

static void stk_pop2( SGS_CTX )
{
	sgs_BreakIf( C->stack_top - C->stack_off < 1 );
	
	C->stack_top -= 2;
	VAR_RELEASE( C->stack_top );
	VAR_RELEASE( C->stack_top + 1 );
}

static void stk_deltasize( SGS_CTX, int diff )
{
	if( diff < 0 )
	{
		fstk_pop( C, -diff );
	}
	else
	{
		stk_push_nulls( C, diff );
	}
}
#define stk_resize_expected( C, expect, rvc ) stk_deltasize( C, expect - rvc )
#define stk_setsize( C, size ) stk_deltasize( C, size - SGS_STACKFRAMESIZE )
#define stk_downsize( C, size ) fstk_pop( C, SGS_STACKFRAMESIZE - size )
#define stk_downsize_keep( C, size, keep ) stk_popskip( C, SGS_STACKFRAMESIZE - size - keep, keep )

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

/* transfer top of stack to *out (which must not be on the stack), skip acquire/release */
#define SGS_STACK_TOP_TO_NONSTACK( out ) *(out) = *--C->stack_top


/*
	Meta-methods
*/

#define _push_metamethod( C, obj, name ) _push_metamethod_buf_( C, (obj)->metaobj, SGS_STRLITBUF( name ) )
#define _push_metamethod_buf( C, obj, name, namelen ) _push_metamethod_buf_( C, (obj)->metaobj, name, namelen )
static int _push_metamethod_buf_( SGS_CTX, sgs_VarObj* metaobj, const char* name, size_t namelen )
{
	int ret;
	sgs_Variable fvar, kvar, ovar = sgs_MakeObjPtrNoRef( metaobj );
	if( !metaobj )
		return 0;
	sgs_InitStringBuf( C, &kvar, name, (sgs_SizeVal) namelen );
	ret = sgs_GetIndex( C, ovar, kvar, &fvar, 0 );
	VAR_RELEASE( &kvar );
	if( ret )
	{
		stk_push_leave( C, &fvar );
		return 1;
	}
	return 0;
}


/*
	Conversions
*/


static sgs_Bool var_getbool( SGS_CTX, const sgs_Variable* var )
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
			if( O->mm_enable && _push_metamethod( C, O, "__tobool" ) )
			{
				sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
				sgs_PushObjectPtr( C, O );
				if( sgs_XThisCall( C, 0 ) > 0 && stk_gettop( C )->type == SGS_VT_BOOL )
				{
					sgs_Bool v = !!stk_gettop( C )->data.B;
					stk_downsize( C, ssz );
					return v;
				}
				stk_downsize( C, ssz );
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
	case SGS_VT_THREAD: return var->data.T != NULL;
	}
	return SGS_FALSE;
}

static sgs_Int var_getint( sgs_Variable* var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (sgs_Int) var->data.B;
	case SGS_VT_INT: return var->data.I;
	case SGS_VT_REAL: return (sgs_Int) var->data.R;
	case SGS_VT_STRING: return sgs_util_atoi( sgs_str_cstr( var->data.S ), var->data.S->size );
	case SGS_VT_PTR: return (sgs_Int) (intptr_t) var->data.P;
	case SGS_VT_THREAD: return (sgs_Int) (intptr_t) var->data.T;
	}
	return 0;
}

static sgs_Real var_getreal( sgs_Variable* var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (sgs_Real) var->data.B;
	case SGS_VT_INT: return (sgs_Real) var->data.I;
	case SGS_VT_REAL: return var->data.R;
	case SGS_VT_STRING: return sgs_util_atof( sgs_str_cstr( var->data.S ), var->data.S->size );
	case SGS_VT_PTR: return (sgs_Real) (intptr_t) var->data.P;
	case SGS_VT_THREAD: return (sgs_Real) (intptr_t) var->data.T;
	}
	return 0;
}

static void* var_getptr( sgs_Variable* var )
{
	switch( var->type )
	{
	case SGS_VT_BOOL: return (void*) (size_t) var->data.B;
	case SGS_VT_INT: return (void*) (size_t) var->data.I;
	case SGS_VT_REAL: return (void*) (size_t) var->data.R;
	case SGS_VT_STRING: return (void*) (size_t) sgs_str_cstr( var->data.S );
	case SGS_VT_OBJECT: return var->data.O->data;
	case SGS_VT_PTR: return var->data.P;
	case SGS_VT_THREAD: return var->data.T;
	}
	return NULL;
}

#define var_getfin( C, var ) \
	( ((var)->type == SGS_VT_THREAD && (var)->data.T->sf_last == NULL ) \
	||( (var)->type == SGS_VT_BOOL && (var)->data.B != 0 ) \
	||( (var)->type == SGS_VT_REAL && (var)->data.R <= C->wait_timer ) \
	||( (var)->type == SGS_VT_INT && (var)->data.I <= C->wait_timer ) \
	||( (var)->type == SGS_VT_OBJECT && var_getbool( C, var ) ))

#define var_initnull( v ) (v)->type = SGS_VT_NULL
#define var_initint( v, value ) \
do{ sgs_Variable* __var = (v); __var->type = SGS_VT_INT; __var->data.I = value; }while(0)
#define var_initreal( v, value ) \
do{ sgs_Variable* __var = (v); __var->type = SGS_VT_REAL; __var->data.R = value; }while(0)

#define var_setnull( C, v ) \
do{ sgs_Variable* var = (v); VAR_RELEASE( var ); var->type = SGS_VT_NULL; }while(0)
#define var_setbool( C, v, value ) \
do{ sgs_Variable* var = (v); if( var->type != SGS_VT_BOOL ) \
	{ VAR_RELEASE( var ); var->type = SGS_VT_BOOL; } var->data.B = value; }while(0)
#define var_setint( C, v, value ) \
do{ sgs_Variable* var = (v); if( var->type != SGS_VT_INT ) \
	{ VAR_RELEASE( var ); var->type = SGS_VT_INT; } var->data.I = value; }while(0)
#define var_setreal( C, v, value ) \
do{ sgs_Variable* var = (v); if( var->type != SGS_VT_REAL ) \
	{ VAR_RELEASE( var ); var->type = SGS_VT_REAL; } var->data.R = value; }while(0)


static void init_var_string( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	char buf[ 32 ];
	switch( var->type )
	{
	case SGS_VT_NULL: sgs_InitStringLit( C, out, "null" ); break;
	case SGS_VT_BOOL:
		if( var->data.B ) sgs_InitStringLit( C, out, "true" );
		else sgs_InitStringLit( C, out, "false" );
		break;
	case SGS_VT_INT: sprintf( buf, "%" PRId64, var->data.I ); sgs_InitString( C, out, buf ); break;
	case SGS_VT_REAL: snprintf( buf, 31, "%g", var->data.R ); sgs_InitString( C, out, buf ); break;
	case SGS_VT_FUNC: sgs_InitStringLit( C, out, "function" ); break;
	case SGS_VT_CFUNC: sgs_InitStringLit( C, out, "C function" ); break;
	case SGS_VT_OBJECT:
		{
			sgs_VarObj* O = var->data.O;
			_STACK_PREPARE;
			
			if( O->mm_enable && _push_metamethod( C, O, "__tostring" ) )
			{
				sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
				sgs_PushObjectPtr( C, O );
				if( sgs_XThisCall( C, 0 ) > 0 && stk_gettop( C )->type == SGS_VT_STRING )
				{
					SGS_STACK_TOP_TO_NONSTACK( out );
					stk_downsize( C, ssz );
					break;
				}
				stk_downsize( C, ssz );
			}
			if( O->iface->convert )
			{
				if( C->sf_count < SGS_MAX_CALL_STACK_SIZE )
				{
					SGSRESULT ret = SGS_EINPROC;
					_STACK_PROTECT;
					
					C->sf_count++;
					ret = O->iface->convert( C, O, SGS_VT_STRING );
					C->sf_count--;
					
					if( SGS_SUCCEEDED( ret ) &&
						SGS_STACKFRAMESIZE >= 1 &&
						stk_gettop( C )->type == SGS_VT_STRING )
					{
						SGS_STACK_TOP_TO_NONSTACK( out );
						_STACK_UNPROTECT;
						break;
					}
					_STACK_UNPROTECT;
				}
				else
					sgs_Msg( C, SGS_ERROR, SGS_ERRMSG_CALLSTACKLIMIT );
			}
			sgs_InitString( C, out, O->iface->name );
		}
		break;
	case SGS_VT_PTR:
		sprintf( buf, "ptr(%p)", var->data.P );
		sgs_InitString( C, out, buf );
		break;
	case SGS_VT_THREAD:
		sprintf( buf, "thread(%p)", var->data.T );
		sgs_InitString( C, out, buf );
		break;
	default: sgs_InitStringLit( C, out, "<bad typeid>" ); break;
	}
	sgs_BreakIf( out->type != SGS_VT_STRING );
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

static void vm_convert_stack_string( SGS_CTX, StkIdx item )
{
	sgs_Variable out;
	if( stk_getpos( C, item )->type == SGS_VT_STRING )
		return;
	init_var_string( C, &out, stk_getpos( C, item ) );
	stk_setvar_leave( C, item, &out );
}


/*
	VM mutation
*/

#define obj_gcmark( S, O ) { if( (O)->redblue != (S)->redblue ) obj_gcmark_do( S, O ); }
#define vm_gcmark( S, var ) { if( var->type == SGS_VT_OBJECT ) obj_gcmark( S, var->data.O ); }
static void obj_gcmark_do( SGS_SHCTX, sgs_VarObj* O )
{
	O->redblue = S->redblue;
	if( O->iface->gcmark )
	{
		SGS_CTX = S->ctx_root;
		_STACK_PREPARE;
		_STACK_PROTECT;
		O->iface->gcmark( C, O );
		_STACK_UNPROTECT;
	}
	if( O->metaobj )
	{
		obj_gcmark( S, O->metaobj );
	}
}

/*
	Object property / array accessor handling
*/

static const sgs_ObjProp* sgs_FindProp( sgs_VarObj* O, sgs_Variable* idx )
{
	const sgs_ObjProp* prop = O->iface->proplist;
	const char* str;
	sgs_SizeVal slen;
	
	if( prop == NULL || idx->type != SGS_VT_STRING )
		return NULL;
	str = sgs_var_cstr( idx );
	slen = idx->data.S->size;
	
	while( prop->name )
	{
		if( prop->nmlength != slen || memcmp( prop->name, str, slen ) != 0 )
		{
			prop++;
			continue;
		}
		return prop;
	}
	return NULL;
}

static SGSRESULT sgs_ReadProp( SGS_CTX, sgs_VarObj* O, sgs_Variable* idx, sgs_Variable* outvar )
{
	void* mem;
	const sgs_ObjProp* prop = sgs_FindProp( O, idx );
	if( !prop || ( prop->flags & SGS_OBJPROP_NOREAD ) )
		return SGS_ENOTFND;
	
	mem = (void*)( ((char*) O->data) + (uint32_t)prop->offset_or_getcb );
	switch( prop->type )
	{
	case SGS_OBJPROPTYPE_U8BOOL:
		outvar->type = SGS_VT_BOOL;
		outvar->data.B = *(uint8_t*)mem != 0;
		return 0;
		
	case SGS_OBJPROPTYPE_U32BOOL:
		outvar->type = SGS_VT_BOOL;
		outvar->data.B = *(uint32_t*)mem != 0;
		return 0;
		
	case SGS_OBJPROPTYPE_ICHAR:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(signed char*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_UCHAR:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(unsigned char*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_I8:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(int8_t*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_U8:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(uint8_t*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_ISHORT:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(signed short*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_USHORT:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(unsigned short*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_I16:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(int16_t*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_U16:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(uint16_t*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_IINT:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(signed int*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_UINT:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(unsigned int*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_ILONG:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(signed long*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_ULONG:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(unsigned long*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_I32:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(int32_t*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_U32:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(uint32_t*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_I2LONG:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(signed long long*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_I64:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(int64_t*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_SIZEVAL:
		outvar->type = SGS_VT_INT;
		outvar->data.I = *(sgs_SizeVal*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_FLOAT:
		outvar->type = SGS_VT_REAL;
		outvar->data.R = *(float*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_DOUBLE:
		outvar->type = SGS_VT_REAL;
		outvar->data.R = *(double*)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_VOIDP:
		outvar->type = SGS_VT_PTR;
		outvar->data.P = *(void**)mem;
		return 0;
		
	case SGS_OBJPROPTYPE_VAR:
		*outvar = *(sgs_Variable*)mem;
		VAR_ACQUIRE( outvar );
		return 0;
		
	case SGS_OBJPROPTYPE_VAROBJ:
		outvar->type = SGS_VT_OBJECT;
		outvar->data.O = *(sgs_VarObj**)mem;
		outvar->data.O->refcount++;
		return 0;
		
	case SGS_OBJPROPTYPE_VARSTR:
		outvar->type = SGS_VT_STRING;
		outvar->data.S = *(sgs_iStr**)mem;
		outvar->data.S->refcount++;
		return 0;
		
	case SGS_OBJPROPTYPE_THREAD:
		outvar->type = SGS_VT_THREAD;
		outvar->data.T = *(sgs_Context**)mem;
		outvar->data.T->refcount++;
		return 0;
		
	case SGS_OBJPROPTYPE_OBJBOOL:
		outvar->type = SGS_VT_BOOL;
		outvar->data.B = O->data != NULL;
		return 0;
		
	case SGS_OBJPROPTYPE_ICONST:
		outvar->type = SGS_VT_INT;
		outvar->data.I = (sgs_Int)(intptr_t) prop->offset_or_getcb;
		return 0;
		
	case SGS_OBJPROPTYPE_CBFUNC:
		outvar->type = SGS_VT_CFUNC;
		outvar->data.C = (sgs_CFunc) prop->offset_or_getcb;
		return 0;
		
	case SGS_OBJPROPTYPE_CUSTOM:
		if( prop->offset_or_getcb )
		{
			int ret;
			sgs_OC_Prop cb = (sgs_OC_Prop) prop->offset_or_getcb;
			_STACK_PREPARE;
			_EL_BACKUP;
			
			if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
				return SGS_EINPROC;
			C->sf_count++;
			
			_STACK_PROTECT;
			_EL_SETAPI(0);
			ret = SGS_FAILED( cb( C, O ) ) ? SGS_EINPROC : 1;
			_EL_RESET;
			
			C->sf_count--;
			
			if( ret == 1 && SGS_STACKFRAMESIZE >= 1 )
			{
				_STACK_UNPROTECT_SKIP( 1 );
			}
			else
			{
				_STACK_UNPROTECT;
				ret = 0;
			}
			
			outvar->type = SGS_VT_NULL;
			return ret;
		}
		break;
	}
	return SGS_ENOTSUP;
}

SGSBOOL sgs_ParseBoolP( SGS_CTX, sgs_Variable* var, sgs_Bool* out );
SGSBOOL sgs_ParseIntP( SGS_CTX, sgs_Variable* var, sgs_Int* out );
SGSBOOL sgs_ParseRealP( SGS_CTX, sgs_Variable* var, sgs_Real* out );
SGSBOOL sgs_ParsePtrP( SGS_CTX, sgs_Variable* var, void** out );

static SGSRESULT sgs_WriteProp( SGS_CTX, sgs_VarObj* O, sgs_Variable* idx, sgs_Variable* val )
{
	void* mem;
	const sgs_ObjProp* prop = sgs_FindProp( O, idx );
	if( !prop || ( prop->flags & SGS_OBJPROP_NOWRITE ) )
		return SGS_ENOTFND;
	
	mem = (void*)( ((char*) O->data) + (uint32_t)prop->offset_or_getcb );
	switch( prop->type )
	{
	case SGS_OBJPROPTYPE_U8BOOL:
	case SGS_OBJPROPTYPE_U32BOOL:
	case SGS_OBJPROPTYPE_OBJBOOL: {
		sgs_Bool v;
		if( sgs_ParseBoolP( C, val, &v ) )
		{
			switch( prop->type )
			{
			case SGS_OBJPROPTYPE_U8BOOL:  *(uint8_t*)mem = v;     break;
			case SGS_OBJPROPTYPE_U32BOOL: *(uint32_t*)mem = v;    break;
			case SGS_OBJPROPTYPE_OBJBOOL: O->data = v ? O : NULL; break;
			}
			return 0;
		}
		return SGS_EINVAL; }
		
	case SGS_OBJPROPTYPE_ICHAR:
	case SGS_OBJPROPTYPE_UCHAR:
	case SGS_OBJPROPTYPE_I8:
	case SGS_OBJPROPTYPE_U8:
	case SGS_OBJPROPTYPE_ISHORT:
	case SGS_OBJPROPTYPE_USHORT:
	case SGS_OBJPROPTYPE_I16:
	case SGS_OBJPROPTYPE_U16:
	case SGS_OBJPROPTYPE_IINT:
	case SGS_OBJPROPTYPE_UINT:
	case SGS_OBJPROPTYPE_ILONG:
	case SGS_OBJPROPTYPE_ULONG:
	case SGS_OBJPROPTYPE_I32:
	case SGS_OBJPROPTYPE_U32:
	case SGS_OBJPROPTYPE_I2LONG:
	case SGS_OBJPROPTYPE_I64:
	case SGS_OBJPROPTYPE_SIZEVAL: {
		sgs_Int v;
		if( sgs_ParseIntP( C, val, &v ) )
		{
			switch( prop->type )
			{
			case SGS_OBJPROPTYPE_ICHAR:  *(signed char*)mem = v;      break;
			case SGS_OBJPROPTYPE_UCHAR:  *(unsigned char*)mem = v;    break;
			case SGS_OBJPROPTYPE_I8:     *(int8_t*)mem = v;           break;
			case SGS_OBJPROPTYPE_U8:     *(uint8_t*)mem = v;          break;
			case SGS_OBJPROPTYPE_ISHORT: *(signed short*)mem = v;     break;
			case SGS_OBJPROPTYPE_USHORT: *(unsigned short*)mem = v;   break;
			case SGS_OBJPROPTYPE_I16:    *(int16_t*)mem = v;          break;
			case SGS_OBJPROPTYPE_U16:    *(uint16_t*)mem = v;         break;
			case SGS_OBJPROPTYPE_IINT:   *(signed int*)mem = v;       break;
			case SGS_OBJPROPTYPE_UINT:   *(unsigned int*)mem = v;     break;
			case SGS_OBJPROPTYPE_ILONG:  *(signed long*)mem = v;      break;
			case SGS_OBJPROPTYPE_ULONG:  *(unsigned long*)mem = v;    break;
			case SGS_OBJPROPTYPE_I32:    *(int32_t*)mem = v;          break;
			case SGS_OBJPROPTYPE_U32:    *(uint32_t*)mem = v;         break;
			case SGS_OBJPROPTYPE_I2LONG: *(signed long long*)mem = v; break;
			case SGS_OBJPROPTYPE_I64:    *(int64_t*)mem = v;          break;
			case SGS_OBJPROPTYPE_SIZEVAL:*(sgs_SizeVal*)mem = v;      break;
			}
			return 0;
		}
		return SGS_EINVAL; }
		
	case SGS_OBJPROPTYPE_FLOAT:
	case SGS_OBJPROPTYPE_DOUBLE: {
		sgs_Real v;
		if( sgs_ParseRealP( C, val, &v ) )
		{
			if( prop->type == SGS_OBJPROPTYPE_FLOAT )
				*(float*)mem = v;
			else
				*(double*)mem = v;
			return 0;
		}
		return SGS_EINVAL; }
		
	case SGS_OBJPROPTYPE_VOIDP: {
		void* v;
		if( sgs_ParsePtrP( C, val, &v ) )
		{
			*(void**)mem = v;
			return 0;
		}
		return SGS_EINVAL; }
		
	case SGS_OBJPROPTYPE_VAR:
		VAR_RELEASE( (sgs_Variable*)mem );
		*(sgs_Variable*)mem = *val;
		VAR_ACQUIRE( val );
		return 0;
		
	case SGS_OBJPROPTYPE_VAROBJ:
		if( val->type == SGS_VT_NULL && !( prop->flags & SGS_OBJPROP_STRICT ) )
		{
			if( *(sgs_VarObj**)mem )
				sgs_ObjRelease( C, *(sgs_VarObj**)mem );
			*(sgs_VarObj**)mem = NULL;
			return 0;
		}
		else if( val->type == SGS_VT_OBJECT )
		{
			if( *(sgs_VarObj**)mem )
				sgs_ObjRelease( C, *(sgs_VarObj**)mem );
			*(sgs_VarObj**)mem = val->data.O;
			val->data.O->refcount++;
			return 0;
		}
		return SGS_EINVAL;
		
	case SGS_OBJPROPTYPE_VARSTR:
		if( val->type == SGS_VT_NULL && !( prop->flags & SGS_OBJPROP_STRICT ) )
		{
			if( *(sgs_iStr**)mem && --(*(sgs_iStr**)mem)->refcount <= 0 )
				var_destroy_string( C, *(sgs_iStr**)mem );
			*(sgs_iStr**)mem = NULL;
			return 0;
		}
		else if( val->type == SGS_VT_STRING )
		{
			if( *(sgs_iStr**)mem && --(*(sgs_iStr**)mem)->refcount <= 0 )
				var_destroy_string( C, *(sgs_iStr**)mem );
			*(sgs_iStr**)mem = val->data.S;
			val->data.S->refcount++;
			return 0;
		}
		return SGS_EINVAL;
		
	case SGS_OBJPROPTYPE_THREAD:
		if( val->type == SGS_VT_NULL && !( prop->flags & SGS_OBJPROP_STRICT ) )
		{
			if( *(sgs_Context**)mem && --(*(sgs_Context**)mem)->refcount <= 0 )
				sgsCTX_FreeState( *(sgs_Context**)mem );
			*(sgs_Context**)mem = NULL;
			return 0;
		}
		else if( val->type == SGS_VT_THREAD )
		{
			if( *(sgs_Context**)mem && --(*(sgs_Context**)mem)->refcount <= 0 )
				sgsCTX_FreeState( *(sgs_Context**)mem );
			*(sgs_Context**)mem = val->data.T;
			val->data.S->refcount++;
			return 0;
		}
		return SGS_EINVAL;
		
		/* cannot edit constants */
	case SGS_OBJPROPTYPE_ICONST:
	case SGS_OBJPROPTYPE_CBFUNC:
		return SGS_ENOTSUP;
		
	case SGS_OBJPROPTYPE_CUSTOM:
		if( prop->setcb )
		{
			int ret;
			sgs_OC_Prop cb = (sgs_OC_Prop) prop->setcb;
			_STACK_PREPARE;
			_EL_BACKUP;
			
			if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
				return SGS_EINPROC;
			C->sf_count++;
			
			_STACK_PROTECT;
			stk_push( C, val );
			_EL_SETAPI(0);
			ret = SGS_FAILED( cb( C, O ) ) ? SGS_EINPROC : SGS_SUCCESS;
			_EL_RESET;
			_STACK_UNPROTECT;
			
			C->sf_count--;
			
			return ret;
		}
	}
	return SGS_ENOTSUP;
}

int sgs_specfn_apply( SGS_CTX )
{
	sgs_SizeVal i, asize;
	sgs_Variable v_func, v_this, v_args;
	SGSFN( "apply" );
	sgs_Method( C );
	/* method, test arg0 = proc, get arg0, get arg1, get array size @ arg2, get arg2, req. count = 3 */
	if( !sgs_LoadArgs( C, "@?p<vva<v.", &v_func, &v_this, &asize, &v_args ) )
		return 0;
	
	fstk_push2( C, &v_func, &v_this );
	for( i = 0; i < asize; ++i )
		sgs_PushNumIndex( C, v_args, i );
	return sgs_XThisCall( C, asize );
}


static int vm_getidx_builtin( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx )
{
	int res;
	sgs_Int pos, size;
	if( obj->type == SGS_VT_STRING )
	{
		size = obj->data.S->size;
		fstk_push( C, idx );
		res = sgs_ParseInt( C, -1, &pos );
		fstk_pop1( C );
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
		sgsVM_VarCreateString( C, outmaybe, sgs_var_cstr( obj ) + pos, 1 );
		return 0;
	}

	sgs_Msg( C, SGS_WARNING, "Cannot index variable of type '%s'", TYPENAME( obj->type ) );
	return SGS_ENOTFND;
}

int sgsstd_end_on( SGS_CTX );
int sgsstd_co_resume( SGS_CTX );
int sgsstd_abort( SGS_CTX );
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
				*outmaybe = sgs_MakeInt( obj->data.S->size );
				return 0;
			}
			break;
		case SGS_VT_FUNC:
		case SGS_VT_CFUNC:
			if( !strcmp( prop, "apply" ) )
			{
				*outmaybe = sgs_MakeCFunc( sgs_specfn_apply );
				return 0;
			}
			break;
		case SGS_VT_THREAD:
			if( !strcmp( prop, "was_aborted" ) )
			{
				int wa = ( obj->data.T->state & SGS_STATE_LASTFUNCABORT ) != 0 ||
					( obj->data.T->sf_last && ( obj->data.T->sf_last->flags & SGS_SF_ABORTED ) != 0 );
				*outmaybe = sgs_MakeBool( wa );
				return 0;
			}
			if( !strcmp( prop, "not_started" ) )
			{
				*outmaybe = sgs_MakeBool( obj->data.T->state & SGS_STATE_COROSTART );
				return 0;
			}
			if( !strcmp( prop, "running" ) )
			{
				*outmaybe = sgs_MakeBool( obj->data.T->sf_count );
				return 0;
			}
			if( !strcmp( prop, "can_resume" ) )
			{
				*outmaybe = sgs_MakeBool(
					( obj->data.T->state & SGS_STATE_COROSTART ) ||
					obj->data.T->sf_count
				);
				return 0;
			}
			if( !strcmp( prop, "end_on" ) )
			{
				*outmaybe = sgs_MakeCFunc( sgsstd_end_on );
				return 0;
			}
			if( !strcmp( prop, "resume" ) )
			{
				*outmaybe = sgs_MakeCFunc( sgsstd_co_resume );
				return 0;
			}
			if( !strcmp( prop, "abort" ) )
			{
				*outmaybe = sgs_MakeCFunc( sgsstd_abort );
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



/* PREDECL */
static SGSRESULT vm_getprop( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx, int isprop );

static SGSRESULT vm_runerr_getprop( SGS_CTX, SGSRESULT type, StkIdx origsize,
	sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	if( type == SGS_ENOTFND )
	{
		char* p;
		const char* err;
		sgs_VarObj* O = obj->data.O;
		sgs_Variable cidx = *idx;
		
		if( obj->type == SGS_VT_OBJECT && obj->data.O->metaobj )
		{
			sgs_Variable tmp;
			if( obj->data.O->mm_enable && _push_metamethod( C, O, "__getindex" ) )
			{
				sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
				sgs_PushObjectPtr( C, O );
				fstk_push( C, idx );
				if( sgs_XThisCall( C, 1 ) > 0 )
				{
					stk_downsize_keep( C, ssz, 1 );
					return 1;
				}
				stk_downsize( C, ssz );
			}
			
			tmp.type = SGS_VT_OBJECT;
			tmp.data.O = obj->data.O->metaobj;
			return vm_getprop( C, outmaybe, &tmp, &cidx, isprop );
		}
		
		err = isprop ? "Readable property not found" : "Cannot find readable value by index";
		fstk_push( C, &cidx );
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
	
	fstk_pop( C, SGS_STACKFRAMESIZE - origsize );
	return type;
}
#define VM_GETPROP_ERR( type ) vm_runerr_getprop( C, type, origsize, outmaybe, obj, idx, isprop )

static SGSRESULT vm_runerr_setprop( SGS_CTX, SGSRESULT type, sgs_Variable* idx, int isprop )
{
	if( type == SGS_ENOTFND )
	{
		const char* err = isprop ? "Writable property not found" : "Cannot find writable value by index";
		fstk_push( C, idx );
		sgs_Msg( C, SGS_WARNING, "%s: \"%s\"", err, sgs_ToString( C, -1 ) );
		fstk_pop1( C );
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
	
	return type;
}
#define VM_SETPROP_ERR( type ) vm_runerr_setprop( C, type, idx, isprop )


/* VM_GETPROP
- two output states:
-- 1 is returned, there is a value in the stack
-- 0 is returned, valmaybe receives data
- if error is returned, no value is available anywhere */
#define VM_GETPROP_RETTOP( ret, ptr ) \
	do{ if( !ret ){ stk_push_leave( C, (ptr) ); } }while(0)
#define VM_GETPROP_RETPTR( ret, ptr ) \
	do{ if( ret ){ *(ptr) = *--C->stack_top; } }while(0)

static SGSRESULT vm_getprop( SGS_CTX, sgs_Variable* outmaybe, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	int ret = SGS_ENOTSUP;
	StkIdx origsize = SGS_STACKFRAMESIZE;
	
	if( obj->type == SGS_VT_OBJECT )
	{
		sgs_VarObj* O = obj->data.O;
		if( O->iface == sgsstd_dict_iface )
		{
			sgs_VHTable* ht = (sgs_VHTable*) O->data;
			if( idx->type == SGS_VT_STRING )
			{
				sgs_VHTVar* var = sgs_vht_get( ht, idx );
				if( !var )
					return VM_GETPROP_ERR( SGS_ENOTFND );
				else
				{
					p_initvar( outmaybe, &var->val );
					return 0;
				}
			}
			else
			{
				fstk_push( C, idx );
				sgs_ToString( C, -1 );
				{
					sgs_VHTVar* var = sgs_vht_get( ht, stk_gettop( C ) );
					if( !var )
						return VM_GETPROP_ERR( SGS_ENOTFND );
					else
					{
						p_initvar( outmaybe, &var->val );
						fstk_pop1( C );
						return 0;
					}
				}
			}
		}
		else if( O->iface == sgsstd_map_iface )
		{
			sgs_VHTVar* var;
			sgs_VHTable* ht = (sgs_VHTable*) O->data;
			/* sgs_vht_get does not modify search key */
			var = sgs_vht_get( ht, idx );
			
			if( !var )
				return VM_GETPROP_ERR( SGS_ENOTFND );
			else
			{
				p_initvar( outmaybe, &var->val );
				return 0;
			}
		}
		else if( O->iface == sgsstd_array_iface && !isprop )
		{
			sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) O->data;
			sgs_Variable* ptr = hdr->data;
			sgs_Int i = var_getint( idx );
			if( i < 0 || i >= hdr->size )
			{
				sgs_Msg( C, SGS_WARNING, "array index out of bounds" );
				return VM_GETPROP_ERR( SGS_EBOUNDS );
			}
			p_initvar( outmaybe, &ptr[ i ] );
			return 0;
		}
		else if( O->iface->getindex )
		{
			_STACK_PREPARE;
			_EL_BACKUP;
			int arg = C->object_arg;
			
			if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
				return SGS_EINPROC;
			C->sf_count++;
			
			_STACK_PROTECT;
			_EL_SETAPI(0);
			fstk_push( C, idx );
			C->object_arg = isprop;
			ret = O->iface->getindex( C, O );
			C->object_arg = arg;
			_EL_RESET;
			
			C->sf_count--;
			if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 )
			{
				_STACK_UNPROTECT_SKIP( 1 );
				ret = 1;
			}
			else
			{
				_STACK_UNPROTECT;
				if( SGS_SUCCEEDED( ret = sgs_ReadProp( C, O, idx, outmaybe ) ) )
					return ret;
				else
					ret = SGS_ENOTFND;
			}
		}
		else if( SGS_SUCCEEDED( ret = sgs_ReadProp( C, O, idx, outmaybe ) ) )
			return ret;
		else
			ret = SGS_ENOTFND;
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

static void vm_getprop_safe( SGS_CTX, sgs_Variable* out, sgs_Variable* obj, sgs_Variable* idx, int isprop )
{
	SGSRESULT res = vm_getprop( C, out, obj, idx, isprop );
	if( SGS_FAILED( res ) )
		return;
	VM_GETPROP_RETPTR( res, out );
}

static SGSRESULT vm_setprop( SGS_CTX, sgs_Variable* obj, sgs_Variable* idx, sgs_Variable* src, int isprop )
{
	int ret = SGS_ENOTSUP;
	
	if( isprop && idx->type != SGS_VT_INT && idx->type != SGS_VT_STRING )
	{
		ret = SGS_EINVAL;
	}
	else if( obj->type == SGS_VT_OBJECT )
	{
		sgs_VarObj* O = obj->data.O;
		ret = SGS_ENOTFND;
		if( O->metaobj && O->mm_enable && !O->in_setindex &&
			_push_metamethod( C, O, "__setindex" ) )
		{
			sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
			sgs_PushObjectPtr( C, O );
			fstk_push2( C, idx, src );
			O->in_setindex = SGS_TRUE; /* prevent recursion */
			sgs_ThisCall( C, 2, 1 );
			O->in_setindex = SGS_FALSE;
			ret = sgs_GetBool( C, -1 );
			stk_downsize( C, ssz );
			if( !ret ) /* was not handled by metamethod */
				goto nextcase;
		}
		else if( O->iface->setindex )
		{
nextcase:;
			int arg = C->object_arg;
			_STACK_PREPARE;
			_EL_BACKUP;
			
			if( C->sf_count >= SGS_MAX_CALL_STACK_SIZE )
				return SGS_EINPROC;
			C->sf_count++;
			
			_EL_SETAPI(0);
			_STACK_PROTECT;
			fstk_push2( C, idx, src );
			C->object_arg = isprop;
			ret = O->iface->setindex( C, O );
			C->object_arg = arg;
			_EL_RESET;
			
			C->sf_count--;
			_STACK_UNPROTECT;
		}
		if( SGS_FAILED( ret ) && isprop )
		{
			ret = sgs_WriteProp( C, O, idx, src );
		}
	}
	
	if( SGS_FAILED( ret ) )
		return VM_SETPROP_ERR( ret );
	
	return ret;
}


/*
	OPs
*/

static SGSBOOL vm_op_concat_ex( SGS_CTX, StkIdx args )
{
	StkIdx i;
	uint32_t totsz = 0, curoff = 0;
	sgs_Variable N;
	if( args < 2 )
	{
		if( args < 1 )
			sgs_PushStringLit( C, "" );
		return 1;
	}
	if( SGS_STACKFRAMESIZE < args )
		return 0;
	for( i = 1; i <= args; ++i )
	{
		vm_convert_stack_string( C, -i );
		totsz += stk_ptop( C, -i )->data.S->size;
	}
	var_create_0str( C, &N, totsz );
	for( i = args; i >= 1; --i )
	{
		sgs_Variable* var = stk_ptop( C, -i );
		memcpy( sgs_var_cstr( &N ) + curoff, sgs_var_cstr( var ), var->data.S->size );
		curoff += var->data.S->size;
	}
	var_finalize_str( C, &N );
	stk_setvar_leave( C, -args, &N );
	fstk_pop( C, args - 1 );
	return 1;
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
			if( O->mm_enable && _push_metamethod( C, O, "__negate" ) )
			{
				sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
				sgs_PushObjectPtr( C, O );
				if( sgs_XThisCall( C, 0 ) > 0 )
				{
					SGS_STACK_TOP_TO_NONSTACK( out );
					stk_downsize( C, ssz );
					goto done;
				}
				stk_downsize( C, ssz );
			}
			if( O->iface->expr )
			{
				int arg = C->object_arg;
				_STACK_PREPARE;
				_STACK_PROTECT;
				fstk_push( C, A );
				C->object_arg = SGS_EOP_NEGATE;
				ret = O->iface->expr( C, O );
				C->object_arg = arg;
				if( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 )
				{
					SGS_STACK_TOP_TO_NONSTACK( out );
				}
				_STACK_UNPROTECT;
			}
			if( SGS_FAILED( ret ) )
			{
				sgs_Msg( C, SGS_ERROR, "Given object does not support negation." );
				/* guaranteed to be NULL after release */
			}
			VAR_RELEASE( &lA );
			return SGS_SUCCEEDED( ret );
		}
	default:
		sgs_Msg( C, SGS_WARNING, "Negating variable of type %s is not supported.", TYPENAME( lA.type ) );
		VAR_RELEASE( &lA );
		return SGS_FALSE;
	}
	
done:
	VAR_RELEASE( &lA );
	return SGS_TRUE;
}

static void vm_op_boolinv( SGS_CTX, int16_t out, sgs_Variable *A )
{
	int val = !var_getbool( C, A );
	var_setbool( C, C->stack_off + out, val );
}

static void vm_op_invert( SGS_CTX, int16_t out, sgs_Variable *A )
{
	sgs_Int val = ~var_getint( A );
	var_setint( C, C->stack_off + out, val );
}

static void vm_op_incdec( SGS_CTX, sgs_Variable* out, sgs_Variable *A, int diff )
{
	switch( A->type )
	{
	case SGS_VT_INT: var_setint( C, out, A->data.I + diff ); break;
	case SGS_VT_REAL: var_setreal( C, out, A->data.R + diff ); break;
	default:
		var_setnull( C, out );
		sgs_Msg( C, SGS_ERROR, "Cannot %screment non-numeric variables!", diff > 0 ? "in" : "de" );
		break;
	}
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

static SGSBOOL vm_arith_op_obj_meta( SGS_CTX, sgs_Variable* out,
	sgs_Variable* a, sgs_Variable* b, sgs_Variable* mmo, int op )
{
	int ret = 0;
	if( mmo->type == SGS_VT_OBJECT && mmo->data.O->mm_enable &&
		_push_metamethod_buf( C, mmo->data.O, mm_arith_ops[ op ], 5 ) )
	{
		sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
		stk_makespace( C, 3 );
		*C->stack_top++ = *mmo;
		*C->stack_top++ = *a;
		*C->stack_top++ = *b;
		(*mmo->data.pRC) += 1;
		(*a->data.pRC) += 1;
		(*b->data.pRC) += 1;
		ret = sgs_XThisCall( C, 2 ) > 0;
		if( ret )
		{
			SGS_STACK_TOP_TO_NONSTACK( out );
		}
		stk_downsize( C, ssz );
	}
	return ret;
}

static SGSBOOL vm_arith_op_obj_iface( SGS_CTX, sgs_Variable* out,
	sgs_Variable* a, sgs_Variable* b, sgs_Variable* mmo, int op )
{
	int ret = 0;
	if( mmo->type == SGS_VT_OBJECT && mmo->data.O->iface->expr )
	{
		int prev_arg = C->object_arg;
		sgs_VarObj* O = mmo->data.O;
		_STACK_PREPARE;
		_STACK_PROTECT;
		fstk_push2( C, a, b );
		C->object_arg = op;
		
		ret = SGS_SUCCEEDED( O->iface->expr( C, O ) ) && SGS_STACKFRAMESIZE >= 1;
		
		C->object_arg = prev_arg;
		if( ret )
		{
			SGS_STACK_TOP_TO_NONSTACK( out );
		}
		_STACK_UNPROTECT;
	}
	return ret;
}

static SGSBOOL vm_is_nonarith_type( uint32_t t )
{
	return t == SGS_VT_FUNC
		|| t == SGS_VT_CFUNC
		|| t == SGS_VT_PTR
		|| t == SGS_VT_THREAD;
}

static SGSBOOL vm_arith_op( SGS_CTX, sgs_Variable* out, sgs_Variable* a, sgs_Variable* b, int op )
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
		return SGS_TRUE;
	}
	if( a->type == SGS_VT_INT && b->type == SGS_VT_INT )
	{
		sgs_Int A = a->data.I, B = b->data.I, R;
		switch( op ){
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			case ARITH_OP_DIV: if( B == 0 ) goto div0err;
				var_setreal( C, out, ((sgs_Real) A) / ((sgs_Real) B) ); return SGS_TRUE; break;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = A % B; break;
			default: R = 0; break;
		}
		var_setint( C, out, R );
		return SGS_TRUE;
	}
	
	if( a->type == SGS_VT_OBJECT || b->type == SGS_VT_OBJECT )
	{
		int ret;
		sgs_Variable lA = *a, lB = *b;
		VAR_ACQUIRE( &lA );
		VAR_ACQUIRE( &lB );
		
		ret = vm_arith_op_obj_meta( C, out, a, b, a, op ) ||
			vm_arith_op_obj_meta( C, out, a, b, b, op ) ||
			vm_arith_op_obj_iface( C, out, a, b, a, op ) ||
			vm_arith_op_obj_iface( C, out, a, b, b, op );
		
		VAR_RELEASE( &lA );
		VAR_RELEASE( &lB );
		
		if( ret )
			return 1;
		goto fail;
	}
	
	/* if either variable cannot participate in arithmetic ops */
	if( vm_is_nonarith_type( a->type ) ||
		vm_is_nonarith_type( b->type ) )
		goto fail;
	
	/* if either are REAL or STRING */
	if( a->type == SGS_VT_REAL || b->type == SGS_VT_STRING ||
		a->type == SGS_VT_STRING || b->type == SGS_VT_REAL )
	{
		sgs_Real A = var_getreal( a ), B = var_getreal( b ), R;
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
		sgs_Int A = var_getint( a ), B = var_getint( b ), R;
		switch( op ){
			case ARITH_OP_ADD: R = A + B; break;
			case ARITH_OP_SUB: R = A - B; break;
			case ARITH_OP_MUL: R = A * B; break;
			case ARITH_OP_DIV: if( B == 0 ) goto div0err;
				var_setreal( C, out, ((sgs_Real) A) / ((sgs_Real) B) ); return SGS_TRUE;
			case ARITH_OP_MOD: if( B == 0 ) goto div0err; R = A % B; break;
			default: R = 0; break;
		}
		var_setint( C, out, R );
	}
	return SGS_TRUE;
	
div0err:
	VAR_RELEASE( out );
	sgs_Msg( C, SGS_ERROR, "Division by 0" );
	return SGS_FALSE;
fail:
	VAR_RELEASE( out );
	sgs_Msg( C, SGS_ERROR, "Specified arithmetic operation is not supported on the given set of arguments" );
	return SGS_FALSE;
}


#define VAR_IOP( pfx, op ) \
static void vm_op_##pfx( SGS_CTX, int16_t out, sgs_Variable* a, sgs_Variable* b ) { \
	sgs_Int A = var_getint( a ); \
	sgs_Int B = var_getint( b ); \
	var_setint( C, C->stack_off + out, A op B ); \
	}

VAR_IOP( and, & )
VAR_IOP( or, | )
VAR_IOP( xor, ^ )
VAR_IOP( lsh, << )
VAR_IOP( rsh, >> )


static SGSBOOL vm_compare_obj_meta( SGS_CTX, sgs_Real* out,
	sgs_Variable* a, sgs_Variable* b, sgs_Variable* mmo )
{
	int ret = 0;
	if( mmo->type == SGS_VT_OBJECT && mmo->data.O->mm_enable &&
		_push_metamethod( C, mmo->data.O, "__compare" ) )
	{
		sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
		stk_makespace( C, 3 );
		*C->stack_top++ = *mmo;
		*C->stack_top++ = *a;
		*C->stack_top++ = *b;
		(*mmo->data.pRC) += 1;
		(*a->data.pRC) += 1;
		(*b->data.pRC) += 1;
		ret = sgs_XThisCall( C, 2 ) > 0;
		if( ret )
		{
			*out = var_getreal( stk_gettop( C ) );
		}
		stk_downsize( C, ssz );
	}
	return ret;
}

static SGSBOOL vm_compare_obj_iface( SGS_CTX, sgs_Real* out,
	sgs_Variable* a, sgs_Variable* b, sgs_Variable* mmo )
{
	int ret = 0;
	if( mmo->type == SGS_VT_OBJECT && mmo->data.O->iface->expr )
	{
		int prev_arg = C->object_arg;
		sgs_VarObj* O = mmo->data.O;
		_STACK_PREPARE;
		_STACK_PROTECT;
		fstk_push2( C, a, b );
		C->object_arg = SGS_EOP_COMPARE;
		
		ret = SGS_SUCCEEDED( O->iface->expr( C, O ) ) && SGS_STACKFRAMESIZE >= 1;
		
		C->object_arg = prev_arg;
		if( ret )
		{
			*out = var_getreal( stk_gettop( C ) );
		}
		_STACK_UNPROTECT;
	}
	return ret;
}

/* returns 0 if equal, >0 if A is bigger, <0 if B is bigger */
#define _SGS_SIGNDIFF( a, b ) ((a)==(b)?0:((a)<(b)?-1:1))
static int vm_compare( SGS_CTX, sgs_Variable* a, sgs_Variable* b )
{
	const uint32_t ta = a->type, tb = b->type;
	
	/* both are INT */
	if( ta == SGS_VT_INT && tb == SGS_VT_INT ) return _SGS_SIGNDIFF( a->data.I, b->data.I );
	/* both are REAL */
	if( ta == SGS_VT_REAL && tb == SGS_VT_REAL ) return _SGS_SIGNDIFF( a->data.R, b->data.R );
	
	/* either is OBJECT */
	if( ta == SGS_VT_OBJECT || tb == SGS_VT_OBJECT )
	{
		int ret;
		sgs_Real out = 0;
		sgs_Variable lA = *a, lB = *b;
		VAR_ACQUIRE( &lA );
		VAR_ACQUIRE( &lB );
		
		ret = vm_compare_obj_meta( C, &out, a, b, a ) ||
			vm_compare_obj_meta( C, &out, a, b, b ) ||
			vm_compare_obj_iface( C, &out, a, b, a ) ||
			vm_compare_obj_iface( C, &out, a, b, b );
		
		VAR_RELEASE( &lA );
		VAR_RELEASE( &lB );
		
		if( ret )
			return _SGS_SIGNDIFF( out, 0 );
		
		/* fallback: check for equality */
		goto compare_nonarith;
	}
	
	/* either variable cannot participate in arithmetic ops */
	if( vm_is_nonarith_type( ta ) ||
		vm_is_nonarith_type( tb ) )
	{
compare_nonarith:
		if( ta != tb )
			return _SGS_SIGNDIFF( ta, tb );
		return _SGS_SIGNDIFF( a->data.P, b->data.P );
	}
	
	/* either is STRING */
	if( ta == SGS_VT_STRING || tb == SGS_VT_STRING )
	{
		/* other is NULL */
		if( ta == SGS_VT_NULL || tb == SGS_VT_NULL )
			return _SGS_SIGNDIFF( ta, tb );
		
		ptrdiff_t out;
		sgs_Variable A = *a, B = *b;
		fstk_push2( C, &A, &B );
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
		sgs_Real ra = var_getreal( a );
		sgs_Real rb = var_getreal( b );
		return _SGS_SIGNDIFF( ra, rb );
	}
}


static int vm_forprep( SGS_CTX, StkIdx outiter, sgs_Variable* obj )
{
	int ret = SGS_FALSE;
	sgs_VarObj* O = obj->data.O;
	
	VAR_RELEASE( stk_poff( C, outiter ) );
	
	if( obj->type != SGS_VT_OBJECT )
	{
		sgs_Msg( C, SGS_ERROR, "Variable of type '%s' "
			"doesn't have an iterator", TYPENAME( obj->type ) );
		return SGS_FALSE;
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
		}
	}
	
	if( ret == SGS_FALSE )
	{
		sgs_Msg( C, SGS_ERROR, "Object '%s' doesn't have an iterator", obj->data.O->iface->name );
		return SGS_FALSE;
	}
	return SGS_TRUE;
}

static SGSBOOL vm_fornext( SGS_CTX, StkIdx outkey, StkIdx outval, sgs_Variable* iter )
{
	StkIdx expargs = 0;
	int flags = 0, ret = SGS_ENOTSUP;
	sgs_VarObj* O = iter->data.O;
	_STACK_PREPARE;
	
	if( iter->type != SGS_VT_OBJECT )
	{
		sgs_Msg( C, SGS_ERROR, "iterator is not an object" );
		return SGS_FALSE;
	}
	
	if( O->iface == sgsstd_array_iter_iface )
	{
		/* = sgsstd_array_iter_getnext */
		sgsstd_array_iter_t* it = (sgsstd_array_iter_t*) O->data;
		sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) it->ref.data.O->data;
		if( it->size != hdr->size )
		{
			sgs_Msg( C, SGS_ERROR, "array changed size during iteration" );
			return SGS_FALSE;
		}
		else if( outkey < 0 && outval < 0 )
		{
			it->off++;
			return it->off < it->size;
		}
		else
		{
			if( outkey >= 0 )
				var_setint( C, stk_poff( C, outkey ), it->off );
			if( outval >= 0 )
				stk_setlvar( C, outval, hdr->data + it->off );
			return SGS_TRUE;
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
		if( flags == 0 )
			sgs_Msg( C, SGS_ERROR, "failed to advance iterator" );
		else
			sgs_Msg( C, SGS_ERROR, "failed to retrieve data from iterator" );
		return SGS_FALSE;
	}
	_STACK_UNPROTECT_SKIP( expargs );
	
	if( !flags )
		return ret > 0;
	else
	{
		if( outkey >= 0 ) stk_setlvar( C, outkey, stk_getpos( C, -2 + (outval<0) ) );
		if( outval >= 0 ) stk_setlvar( C, outval, stk_gettop( C ) );
		fstk_pop( C, expargs );
	}
	
	return SGS_TRUE;
}


int sgsstd_mm_getindex_router( SGS_CTX );
int sgsstd_mm_setindex_router( SGS_CTX );

static void vm_make_class( SGS_CTX, int outpos, sgs_Variable* name, sgs_Variable* inhname )
{
	int ret;
	sgs_Variable cls, inhcls;
	
	sgs_BreakIf( name->type != SGS_VT_STRING );
	
	sgs_PushStringLit( C, "__name" );
	fstk_push( C, name );
	
	sgs_PushStringLit( C, "__inherit" );
	if( inhname )
		fstk_push( C, inhname );
	else
		fstk_push_null( C );
	
	sgs_PushStringLit( C, "__getindex" );
	sgs_PushCFunc( C, sgsstd_mm_getindex_router );
	sgs_PushStringLit( C, "__setindex" );
	sgs_PushCFunc( C, sgsstd_mm_setindex_router );
	
	ret = sgsSTD_MakeDict( C, &cls, 8 );
	SGS_UNUSED( ret );
	sgs_BreakIf( ret != SGS_TRUE );
	
	sgs_RegSymbol( C, NULL, sgs_var_cstr( name ), cls );
	
	if( inhname )
	{
		sgs_BreakIf( inhname->type != SGS_VT_STRING );
		if( sgs_GetGlobal( C, *inhname, &inhcls ) && inhcls.type == SGS_VT_OBJECT )
		{
			sgs_ObjSetMetaObj( C, cls.data.O, inhcls.data.O );
			sgs_ObjSetMetaMethodEnable( cls.data.O, 1 );
			
			if( sgs_PushProperty( C, inhcls, "__inherited" ) )
			{
				fstk_push2( C, &inhcls, name );
				sgs_ThisCall( C, 1, 0 );
			}
			else sgs_Pop( C, 1 );
		}
		else
		{
			sgs_Msg( C, SGS_ERROR, "Class '%s' cannot inherit from '%s' - no such global object",
				sgs_var_cstr( name ), sgs_var_cstr( inhname ) );
		}
	}
	
	stk_setvar_leave( C, outpos, &cls );
}

static void vm_ctor( SGS_CTX, sgs_Variable* inst, sgs_Variable* ctorfunc, int argstart, int argend )
{
	int i;
	if( ctorfunc )
		fstk_push( C, ctorfunc );
	fstk_push( C, inst );
	stk_makespace( C, argend - argstart );
	for( i = argstart; i < argend; ++i )
	{
		sgs_Variable* ptr = stk_poff( C, i );
		VAR_ACQUIRE( ptr );
		*C->stack_top++ = *ptr;
	}
	sgs_ThisCall( C, argend - argstart, 0 );
}

static void vm_make_new( SGS_CTX, int outcls, int lastarg )
{
	sgs_Variable inst, clscopy = *stk_poff( C, outcls );
	if( clscopy.type == SGS_VT_FUNC || clscopy.type == SGS_VT_CFUNC )
	{
		sgsSTD_MakeDict( C, &inst, 0 );
		vm_ctor( C, &inst, &clscopy, outcls + 1, lastarg + 1 );
		stk_setvar_leave( C, outcls, &inst );
	}
	else if( clscopy.type == SGS_VT_OBJECT )
	{
		sgsSTD_MakeDict( C, &inst, 0 );
		sgs_ObjSetMetaObj( C, inst.data.O, clscopy.data.O );
		sgs_ObjSetMetaMethodEnable( inst.data.O, 1 );
		/* call the constructor */
		if( sgs_PushProperty( C, clscopy, "__construct" ) )
		{
			vm_ctor( C, &inst, NULL, outcls + 1, lastarg + 1 );
		}
		else sgs_Pop( C, 1 );
		
		stk_setvar_leave( C, outcls, &inst );
	}
	else
	{
		sgs_Msg( C, SGS_ERROR, "new: expected object" );
	}
}

static int vm_min_closure_count( sgs_Variable* vp )
{
	if( vp->type == SGS_VT_FUNC )
	{
		return vp->data.F->numclsr;
	}
	else
	{
		return 0;
	}
}


#define SGS_EXEC_PAUSED 0x1000
static int vm_exec( SGS_CTX );


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
static int vm_call( SGS_CTX, int args, int gotthis, int* outrvc, int can_reenter )
{
	sgs_Variable* pfunc = C->stack_top - args - gotthis - 1;
	ptrdiff_t stkcallbase, stkoff = SGS_STACK_PRESERVE( C, C->stack_off );
	int rvc = 0, ret = 1, allowed;
	
	gotthis = !!gotthis;
	stkcallbase = ( C->stack_top - C->stack_base ) - args - gotthis;
	
	if( pfunc->type == SGS_VT_OBJECT && pfunc->data.O->mm_enable )
	{
		sgs_Variable objfunc, fncopy = *pfunc;
		sgs_PushStringLit( C, "__call" );
		rvc = sgs_GetIndex( C, fncopy, *stk_gettop( C ), &objfunc, 0 );
		fstk_pop1( C );
		if( SGS_SUCCEEDED( rvc ) )
		{
			// set up metamethod call instead of object call
			if( !gotthis )
			{
				sgs_InsertVariable( C, -1 - args, sgs_MakeNull() );
				gotthis = 1;
			}
			sgs_InsertVariable( C, -2 - args, fncopy );
			args++;
			p_setvar_leave( C->stack_top - args - gotthis - 1, &objfunc );
		}
		pfunc = C->stack_top - args - gotthis - 1;
	}
	
	sgs_BreakIf( SGS_STACKFRAMESIZE < args + gotthis );
	allowed = sgsVM_PushStackFrame( C, pfunc );
	
	if( allowed )
	{
		sgs_StackFrame* sf = C->sf_last;
		/* WP (x4): stack size limit */
		sf->argbeg = (StkIdx) stkcallbase;
		sf->stkoff = (StkIdx) stkoff;
		/* WP: argument count limit */
		sf->argcount = (uint8_t) args;
		sf->flags = gotthis ? SGS_SF_METHOD : 0;
		
		switch( pfunc->type )
		{
		case SGS_VT_CFUNC: {
			C->stack_off = C->stack_top - args;
			SGS_PERFEVENT( ps_precall_end( (unsigned) -1, 1, FUNCTYPE_CFN ) );
			rvc = (*pfunc->data.C)( C );
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
		} break;
		case SGS_VT_FUNC: {
			sgs_iFunc* F = pfunc->data.F;
			int argend, stkargs, expargs = F->numargs + F->gotthis;
			
			/* if <this> was expected but wasn't properly passed, insert a NULL in its place */
			if( F->gotthis && !gotthis )
			{
				stk_insert_posn( C, args )->type = SGS_VT_NULL;
				sf->flags |= SGS_SF_METHOD;
				gotthis = SGS_TRUE;
			}
			C->stack_off = C->stack_top - args - F->gotthis;
			/* if <this> wasn't expected but was passed, ignore it */
			
			argend = (int) ( C->stack_top - C->stack_base );
			SGS_UNUSED( argend );
			
			/* fix argument stack */
			stkargs = args + F->gotthis;
			if( stkargs > expargs )
			{
				int first = F->numargs + gotthis;
				int all = args + gotthis;
				stk_transpose( C, first, all );
				C->stack_off += all - first;
				stk_push_nulls( C, F->numtmp );
			}
			else
				stk_push_nulls( C, F->numtmp + expargs - stkargs );
			
			{
#if SGS_DEBUG && SGS_DEBUG_VALIDATE && SGS_DEBUG_EXTRA
				/*
				printf("fcall begin=%d end=%d count=%d(%d) inthis=%s expthis=%s\n",
					stkcallbase, sf->argend, sf->argcount, args,
					SGS_HAS_FLAG( sf->flags, SGS_SF_METHOD ) ? "Y" : "N",
					SGS_HAS_FLAG( sf->flags, SGS_SF_HASTHIS ) ? "Y" : "N" );
				*/
				sgs_BreakIf( SGS_SF_ARG_COUNT(sf) != argend - stkcallbase );
#endif
				SGS_PERFEVENT( ps_precall_end( F->numargs, F->gotthis, FUNCTYPE_SGS ) );
				if( can_reenter )
				{
					sf->flags |= SGS_SF_REENTER;
					return -2;
				}
				
				rvc = vm_exec( C );
				if( rvc & SGS_EXEC_PAUSED )
				{
					rvc &= ~SGS_EXEC_PAUSED;
					if( outrvc )
						*outrvc = rvc;
					return 1;
				}
			}
		} break;
		case SGS_VT_OBJECT: {
			sgs_VarObj* O = pfunc->data.O;
			
			C->stack_off = C->stack_top - args;
			rvc = SGS_ENOTSUP;
			SGS_PERFEVENT( ps_precall_end( (unsigned) -1, 1, FUNCTYPE_OBJ ) );
			if( O->iface->call )
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
		} break;
		default: {
			/* try to find some name to add to the error */
			if( sf->nfname == NULL && sf->prev )
				sf->nfname = sf->prev->nfname;
			sgs_Msg( C, SGS_ERROR, "Variable of type '%s' "
				"cannot be called", TYPENAME( pfunc->type ) );
			ret = 0;
		} break;
		}
		
		C->state &= ~SGS_STATE_LASTFUNCABORT;
		if( ret && sf->flags & SGS_SF_ABORTED )
		{
			C->state |= SGS_STATE_LASTFUNCABORT;
		}
		vm_frame_pop( C );
	}
	else
	{
		C->state &= ~SGS_STATE_LASTFUNCABORT;
	}
	
	/* remove all stack items before the returned ones */
	fstk_clean( C, C->stack_base + stkcallbase - 1, C->stack_top - rvc );
	C->stack_off = SGS_STACK_RESTORE( C, stkoff );
	
	if( outrvc )
		*outrvc = rvc;
	C->num_last_returned = rvc;
	
	return ret;
}

static void vm_postcall( SGS_CTX, int rvc )
{
	sgs_StackFrame* sf = C->sf_last;
	sgs_StkIdx stkcallbase = sf->argbeg;
	sgs_StkIdx stkoff = sf->stkoff;
	
	vm_frame_pop( C );
	
	/* remove all stack items before the returned ones */
	fstk_clean( C, C->stack_base + stkcallbase - 1, C->stack_top - rvc );
	C->stack_off = SGS_STACK_RESTORE( C, stkoff );
	
	C->num_last_returned = rvc;
	if( C->sf_last )
	{
		sgs_instr_t I;
		int expect, args_from;
		sf = C->sf_last; /* current function change */
		
		if( *sf->iptr == SGS_SI_RETN )
		{
			/* next instr = return 0, this can mean one of two things:
			- call is the last instruction in the function
			- sgs_Abort was called
			... in both cases we do not care about the return or expected values
			*/
			fstk_pop( C, rvc );
		}
		else
		{
			I = *(sf->iptr-1);
			sgs_BreakIf( SGS_INSTR_GET_OP( I ) != SGS_SI_CALL );
			expect = SGS_INSTR_GET_A( I );
			args_from = SGS_INSTR_GET_B( I ) & 0xff;
			stk_resize_expected( C, expect, rvc );
			
			if( expect )
			{
				int i;
				for( i = expect - 1; i >= 0; --i )
					stk_setlvar( C, args_from + i, C->stack_top - expect + i );
				fstk_pop( C, expect );
			}
		}
	}
}


#if SGS_DEBUG && SGS_DEBUG_VALIDATE
static SGS_INLINE sgs_Variable* const_getvar( sgs_Variable* consts, int count, int off )
{
	sgs_BreakIf( off < 0 || off >= count );
	return consts + off;
}

static SGS_INLINE sgs_Closure* clsr_get( sgs_Closure** clsrlist, int count, int off )
{
	sgs_BreakIf( off < 0 || off >= count );
	return clsrlist[ off ];
}
#endif

/*
	Main VM execution loop
*/
static int vm_exec( SGS_CTX )
{
	sgs_StackFrame* SF;
	int32_t ret;
	
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
#  define RESVAR( v ) ( SGS_CONSTVAR(v) ? const_getvar( SF->cptr, SF->constcount, SGS_CONSTDEC(v) ) : stk_poff( C, (v) ) )
#  define CLSR( v ) clsr_get( SF->clsrlist, SF->clsrcount, (v) )
#else
#  define RESVAR( v ) ( SGS_CONSTVAR(v) ? ( SF->cptr + SGS_CONSTDEC(v) ) : stk_poff( C, (v) ) )
#  define CLSR( v ) (SF->clsrlist[v])
#endif
	
#define pp SF->iptr
#if SGS_DEBUG && SGS_DEBUG_VALIDATE
#  define pend SF->iend
#endif
#define instr SGS_INSTR_GET_OP(I)
#define argA SGS_INSTR_GET_A(I)
#define argB SGS_INSTR_GET_B(I)
#define argC SGS_INSTR_GET_C(I)
#define argE SGS_INSTR_GET_E(I)
	
restart_loop:
	SF = C->sf_last;
	ret = 0;
	
#if SGS_DEBUG && SGS_DEBUG_INSTR
	{
		const char *name, *file;
		sgs_StackFrameInfo( C, SF, &name, &file, NULL );
		printf( ">>>\n\t'%s' in %s @ %p [sz:%d]\n>>>\n", name, file, SF, (int) SGS_STACKFRAMESIZE );
	}
#endif
	
	for(;;)
	{
		if( C->hook_fn )
			C->hook_fn( C->hook_ctx, C, SGS_HOOK_STEP );
		
		const sgs_instr_t I = *SF->iptr++;
		
#if SGS_DEBUG
#  if SGS_DEBUG_INSTR
		printf( "*** [at 0x%04X] %s A=%d B=%d C=%d E=%d [sz:%d] ***\n",
			pp - 1 - SF->code, sgs_OpNames[ instr ],
			(int)(argA), (int)(argB), (int)(argC), (int)(argE),
			(int) SGS_STACKFRAMESIZE );
#  endif
#  if SGS_DEBUG_STATE
		sgsVM_StackDump( C );
#  endif
#endif
		SGS_UNUSED( sgs_ErrNames );
		SGS_UNUSED( sgs_OpNames );
		
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
			case SGS_INT_RESET_WAIT_TIMER: C->wait_timer = 0; break;
			}
			break;
		}

		case SGS_SI_RET1:
		{
#if SGS_DEBUG && SGS_DEBUG_STATE
			/* TODO restore memcheck */
			sgsVM_StackDump( C );
#endif
			if( SF->flags & SGS_SF_REENTER )
			{
				/* move argument, if expected, to prev_stack_frame->func
				(=location of the expected argument list)
				.. then pop stack frame */
				sgs_instr_t PCI;
				int i, expect;
				sgs_StackFrame* psf = SF->prev;
				sgs_Variable* pvar = RESVAR( argC );
				
				PCI = *(psf->iptr-1);
				sgs_BreakIf( SGS_INSTR_GET_OP( PCI ) != SGS_SI_CALL );
				expect = SGS_INSTR_GET_A( PCI );
				
				/* remove all stack items before the returned ones */
				C->stack_off = SGS_STACK_RESTORE( C, SF->stkoff ); /* ---- STACK CHANGE ---- */
				if( expect )
				{
					int argstart = SGS_INSTR_GET_B( PCI ) & 0xff;
					sgs_Variable* pdst = stk_poff( C, argstart );
					p_setvar( pdst, pvar );
					for( i = 1; i < expect; ++i )
					{
						VAR_RELEASE( &pdst[ i ] );
					}
				}
				stk_popto( C, SF->func );
				C->num_last_returned = 1;
				
				vm_frame_pop( C );
				
				goto restart_loop;
			}
			else
			{
				sgs_Variable var = *RESVAR( argC );
				stk_push( C, &var );
				C->state &= ~SGS_STATE_LASTFUNCPAUSE;
				return 1;
			}
		}
		case SGS_SI_RETN:
		{
			int rvc = argA;
#if SGS_DEBUG && SGS_DEBUG_STATE
			/* TODO restore memcheck */
			sgsVM_StackDump( C );
#endif
			if( SF->flags & SGS_SF_REENTER )
			{
				vm_postcall( C, rvc );
				goto restart_loop;
			}
			C->state &= ~SGS_STATE_LASTFUNCPAUSE;
			return rvc;
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
		SGS_PERFEVENT( ps_precall_begin(
			argC - (argB & 0xff) - (( argB & 0x100 ) != 0),
			( argB & 0x100 ) != 0, CALLSRC_SCRIPT ) );
		{
			/* argB & 0xff .. argC = func [this] [args] */
			int i, rvc = 0, expect = argA, args_from = argB & 0xff, args_to = argC;
			int gotthis = ( argB & 0x100 ) != 0;
			
			stk_makespace( C, args_to - args_from + 1 );
			for( i = args_from; i <= args_to; ++i )
			{
				*C->stack_top++ = C->stack_off[ i ];
				VAR_ACQUIRE( C->stack_off + i );
			}
			
			if( vm_call( C, args_to - args_from - gotthis, gotthis, &rvc, 1 ) == -2 )
			{
				goto restart_loop;
			}
			
			if( C->sf_last->flags & SGS_SF_PAUSED )
			{
				ret = rvc;
				goto paused;
			}
			
			stk_resize_expected( C, expect, rvc );
			
			if( expect )
			{
				for( i = expect - 1; i >= 0; --i )
					stk_setlvar( C, args_from + i, C->stack_top - expect + i );
				fstk_pop( C, expect );
			}
			
			break;
		}

		case SGS_SI_FORPREP: vm_forprep( C, argA, RESVAR( argB ) ); break;
		case SGS_SI_FORLOAD:
			vm_fornext( C,
				argB < 0x100 ? (int)argB : -1,
				argC < 0x100 ? (int)argC : -1,
				stk_poff( C, argA ) );
			break;
		case SGS_SI_FORJUMP:
		{
			int16_t off = argE;
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( vm_fornext( C, -1, -1, RESVAR( argC ) ) < 1 )
				pp += off;
			break;
		}
		
		case SGS_SI_NFORPREP:
		{
			int16_t off = argE;
			sgs_Variable* vars = stk_poff( C, argC & 0xff );
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( argC & 0x100 )
			{
				sgs_Real tmp;
				tmp = var_getreal( &vars[1] );
				var_setreal( C, &vars[1], tmp );
				tmp = var_getreal( &vars[2] );
				var_setreal( C, &vars[2], tmp );
				tmp = var_getreal( &vars[3] );
				var_setreal( C, &vars[3], tmp );
				if( vars[3].data.R >= 0 ? vars[2].data.R <= vars[1].data.R : vars[2].data.R >= vars[1].data.R )
					pp += off;
			}
			else
			{
				sgs_Int tmp;
				tmp = var_getint( &vars[1] );
				var_setint( C, &vars[1], tmp );
				tmp = var_getint( &vars[2] );
				var_setint( C, &vars[2], tmp );
				tmp = var_getint( &vars[3] );
				var_setint( C, &vars[3], tmp );
				if( vars[3].data.I >= 0 ? vars[2].data.I <= vars[1].data.I : vars[2].data.I >= vars[1].data.I )
					pp += off;
			}
			p_setvar( &vars[0], &vars[1] );
			break;
		}
		case SGS_SI_NFORJUMP:
		{
			int16_t off = argE;
			sgs_Variable* vars = stk_poff( C, argC & 0xff );
			sgs_BreakIf( pp+1 + off > pend || pp+1 + off < SF->code );
			if( argC & 0x100 )
			{
				vars[1].data.R += vars[3].data.R;
				if( vars[3].data.R >= 0 ? vars[2].data.R > vars[1].data.R : vars[2].data.R < vars[1].data.R )
					pp += off;
			}
			else
			{
				vars[1].data.I += vars[3].data.I;
				if( vars[3].data.I >= 0 ? vars[2].data.I > vars[1].data.I : vars[2].data.I < vars[1].data.I )
					pp += off;
			}
			p_setvar( &vars[0], &vars[1] );
			break;
		}
		
#define ARGS_2 sgs_Variable p2 = *RESVAR( argB );
#define ARGS_3 sgs_Variable p2 = *RESVAR( argB ), p3 = *RESVAR( argC );
#define SETOP sgs_Variable p1 = *stk_poff( C, argA );
#define GETOP sgs_Variable p1; p1.type = SGS_VT_NULL;
#define WRITEGET stk_setlvar_leave( C, argA, &p1 );
#define WRITEGETC stk_setlvar_leave( C, argC, &p1 );
		case SGS_SI_LOADCONST: { stk_setlvar( C, argC, SF->cptr + argE ); break; }
		case SGS_SI_GETVAR: { ARGS_2; GETOP; sgsSTD_GlobalGet( C, &p1, &p2 ); WRITEGET; break; }
		case SGS_SI_SETVAR: { ARGS_3; sgsSTD_GlobalSet( C, &p2, &p3 ); break; }
		case SGS_SI_GETPROP: { ARGS_3; GETOP; vm_getprop_safe( C, &p1, &p2, &p3, SGS_TRUE ); WRITEGET; break; }
		case SGS_SI_SETPROP: { ARGS_3; SETOP; vm_setprop( C, &p1, &p2, &p3, SGS_TRUE ); break; }
		case SGS_SI_GETINDEX: { ARGS_3; GETOP; vm_getprop_safe( C, &p1, &p2, &p3, SGS_FALSE ); WRITEGET; break; }
		case SGS_SI_SETINDEX: { ARGS_3; SETOP; vm_setprop( C, &p1, &p2, &p3, SGS_FALSE ); break; }
		case SGS_SI_ARRPUSH: {
			sgsstd_array_header_t* hdr = (sgsstd_array_header_t*) stk_poff( C, argA )->data.O->data;
			sgsstd_array_insert_p( C, hdr, hdr->size, RESVAR( argC ) ); break; }
		
		case SGS_SI_CLSRINFO: break;
		case SGS_SI_MAKECLSR: {
			sgs_Variable out;
			sgs_Closure** clsrlist;
			int i = 0, minclsr = vm_min_closure_count( RESVAR( argB ) );
			if( minclsr < argC )
				minclsr = argC;
			/* WP: range not affected by conversion */
			clsrlist = sgsSTD_MakeClosure( C, &out, RESVAR( argB ), (uint32_t) minclsr );
			stk_setvar_leave( C, argA, &out );
			if( argC )
			{
				for( i = 0; i < argC; ++i )
				{
					int varid;
					sgs_instr_t mdi = pp[ i / 3 ];
					switch( i % 3 )
					{
					case 0: varid = SGS_INSTR_GET_A( mdi ); break;
					case 1: varid = SGS_INSTR_GET_B( mdi ); break;
					case 2: varid = SGS_INSTR_GET_C( mdi ); break;
					}
					*clsrlist = CLSR( varid );
					(*clsrlist)->refcount++;
					clsrlist++;
				}
				pp += ( argC + 2 ) / 3;
			}
			for( ; i < minclsr; ++i )
			{
				*clsrlist = sgs_Alloc( sgs_Closure );
				(*clsrlist)->refcount = 1;
				(*clsrlist)->var.type = SGS_VT_NULL;
				clsrlist++;
			}
		} break;
		case SGS_SI_GETCLSR: stk_setlvar( C, argA, &CLSR( argB )->var ); break;
		case SGS_SI_SETCLSR: { sgs_Variable *p3 = RESVAR( argC ), *cv = &CLSR( argB )->var;
			VAR_RELEASE( cv ); *cv = *p3; VAR_ACQUIRE( RESVAR( argC ) ); } break;

		case SGS_SI_SET: { stk_setlvar( C, argA, RESVAR( argB ) ); break; }
		case SGS_SI_MCONCAT: { vm_op_concat_ex( C, argB ); stk_setlvar_leave( C, argA, stk_gettop( C ) ); stk_pop1nr( C ); break; }
		case SGS_SI_NEGATE: { ARGS_2; GETOP; vm_op_negate( C, &p1, &p2 ); WRITEGET; break; }
		case SGS_SI_BOOL_INV: { ARGS_2; vm_op_boolinv( C, (int16_t) argA, &p2 ); break; }
		case SGS_SI_INVERT: { ARGS_2; vm_op_invert( C, (int16_t) argA, &p2 ); break; }

		case SGS_SI_INC: { ARGS_2; GETOP; vm_op_incdec( C, &p1, &p2, +1 ); WRITEGET; break; }
		case SGS_SI_DEC: { ARGS_2; GETOP; vm_op_incdec( C, &p1, &p2, -1 ); WRITEGET; break; }
		case SGS_SI_ADD: { ARGS_3;
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ GETOP; var_setreal( C, &p1, p2.data.R + p3.data.R ); WRITEGET; break; }
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ GETOP; var_setint( C, &p1, p2.data.I + p3.data.I ); WRITEGET; break; }
			GETOP; vm_arith_op( C, &p1, &p2, &p3, ARITH_OP_ADD ); WRITEGET; break; }
		case SGS_SI_SUB: { ARGS_3;
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ GETOP; var_setreal( C, &p1, p2.data.R - p3.data.R ); WRITEGET; break; }
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ GETOP; var_setint( C, &p1, p2.data.I - p3.data.I ); WRITEGET; break; }
			GETOP; vm_arith_op( C, &p1, &p2, &p3, ARITH_OP_SUB ); WRITEGET; break; }
		case SGS_SI_MUL: { ARGS_3;
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ GETOP; var_setreal( C, &p1, p2.data.R * p3.data.R ); WRITEGET; break; }
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ GETOP; var_setint( C, &p1, p2.data.I * p3.data.I ); WRITEGET; break; }
			GETOP; vm_arith_op( C, &p1, &p2, &p3, ARITH_OP_MUL ); WRITEGET; break; }
		case SGS_SI_DIV: { ARGS_3; GETOP; vm_arith_op( C, &p1, &p2, &p3, ARITH_OP_DIV ); WRITEGET; break; }
		case SGS_SI_MOD: { ARGS_3; GETOP; vm_arith_op( C, &p1, &p2, &p3, ARITH_OP_MOD ); WRITEGET; break; }

		case SGS_SI_AND: { ARGS_3; vm_op_and( C, (int16_t) argA, &p2, &p3 ); break; }
		case SGS_SI_OR: { ARGS_3; vm_op_or( C, (int16_t) argA, &p2, &p3 ); break; }
		case SGS_SI_XOR: { ARGS_3; vm_op_xor( C, (int16_t) argA, &p2, &p3 ); break; }
		case SGS_SI_LSH: { ARGS_3; vm_op_lsh( C, (int16_t) argA, &p2, &p3 ); break; }
		case SGS_SI_RSH: { ARGS_3; vm_op_rsh( C, (int16_t) argA, &p2, &p3 ); break; }

#define STRICTLY_EQUAL( val ) if( p2.type != p3.type || ( p2.type == SGS_VT_OBJECT && \
								p2.data.O->iface != p3.data.O->iface ) ) { GETOP; var_setbool( C, &p1, val ); WRITEGET; break; }
#define VCOMPARE( op ) { int cr = vm_compare( C, &p2, &p3 ) op 0; GETOP; var_setbool( C, &p1, cr ); WRITEGET; }
		case SGS_SI_SEQ: { ARGS_3; STRICTLY_EQUAL( SGS_FALSE );
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I == p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R == p3.data.R ); break; }
			VCOMPARE( == ); break; }
		case SGS_SI_SNEQ: { ARGS_3; STRICTLY_EQUAL( SGS_TRUE );
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I != p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R != p3.data.R ); break; }
			VCOMPARE( != ); break; }
		case SGS_SI_EQ: { ARGS_3;
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I == p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R == p3.data.R ); break; }
			VCOMPARE( == ); break; }
		case SGS_SI_NEQ: { ARGS_3;
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I != p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R != p3.data.R ); break; }
			VCOMPARE( != ); break; }
		case SGS_SI_LT: { ARGS_3;
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I < p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R < p3.data.R ); break; }
			VCOMPARE( < ); break; }
		case SGS_SI_GTE: { ARGS_3;
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I >= p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R >= p3.data.R ); break; }
			VCOMPARE( >= ); break; }
		case SGS_SI_GT: { ARGS_3;
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I > p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R > p3.data.R ); break; }
			VCOMPARE( > ); break; }
		case SGS_SI_LTE: { ARGS_3;
			if( p2.type == SGS_VT_INT && p3.type == SGS_VT_INT ){ var_setbool( C, stk_poff( C, argA ), p2.data.I <= p3.data.I ); break; }
			if( p2.type == SGS_VT_REAL && p3.type == SGS_VT_REAL ){ var_setbool( C, stk_poff( C, argA ), p2.data.R <= p3.data.R ); break; }
			VCOMPARE( <= ); break; }
		case SGS_SI_RAWCMP: { ARGS_3; GETOP; var_setint( C, &p1, vm_compare( C, &p2, &p3 ) ); WRITEGET; break; }
			
		case SGS_SI_ARRAY: { GETOP; sgsSTD_MakeArray( C, &p1, -argE ); WRITEGETC; break; }
		case SGS_SI_DICT: { GETOP; sgsSTD_MakeDict( C, &p1, 0 ); WRITEGETC; break; }
		case SGS_SI_MAP: { GETOP; sgsSTD_MakeMap( C, &p1, 0 ); WRITEGETC; break; }
		case SGS_SI_CLASS: { vm_make_class( C, argA, RESVAR( argB ), argC != argA ? RESVAR( argC ) : NULL ); break; }
		case SGS_SI_NEW: { vm_make_new( C, argB, argC ); break; }
		case SGS_SI_RSYM: { ARGS_3; sgs_Variable symtbl = sgs_Registry( C, SGS_REG_SYM );
			sgs_SetIndex( C, symtbl, p2, p3, SGS_FALSE ); sgs_SetIndex( C, symtbl, p3, p2, SGS_FALSE ); break; }
			
		case SGS_SI_COTRT: if( var_getfin( C, RESVAR( argB ) ) ){ GETOP; var_setbool( C, &p1, 1 ); WRITEGET; } break;
		case SGS_SI_COTRF: if( !var_getfin( C, RESVAR( argB ) ) ){ GETOP; var_setbool( C, &p1, 0 ); WRITEGET; } break;
		case SGS_SI_COABORT:
			if( var_getbool( C, RESVAR( argC ) ) )
			{
				int16_t off = (int16_t) argE;
				const sgs_instr_t* p = pp + 1 + off, *pstop = pp;
				sgs_BreakIf( p > pstop || p < SF->code );
				while( p < pstop )
				{
					sgs_instr_t cI = *p;
					int op = SGS_INSTR_GET_OP( cI );
					if( op == SGS_SI_COTRT || op == SGS_SI_COTRF )
					{
						int cIargB = SGS_INSTR_GET_B( cI );
						sgs_Variable* var = RESVAR( cIargB );
						if( var->type == SGS_VT_THREAD )
							sgs_Abort( var->data.T );
					}
					p++;
				}
			}
			break;
		case SGS_SI_YLDJMP:
			if( var_getbool( C, RESVAR( argC ) ) == SGS_FALSE )
			{
				ret = 0;
				goto paused;
			}
			break;
#undef VCOMPARE
#undef STRICTLY_EQUAL
#undef ARGS_2
#undef ARGS_3
#undef RESVAR
#undef argA
#undef argB
#undef argC
#undef argE
#undef instr
#undef pend
#undef pp

		default:
			sgs_Msg( C, SGS_ERROR, "Illegal instruction executed: 0x%08X", I );
			break;
		}
	}
	
paused:
	C->sf_last->flags |= SGS_SF_PAUSED;
	C->state |= SGS_STATE_LASTFUNCPAUSE;
	if( C->hook_fn )
		C->hook_fn( C->hook_ctx, C, SGS_HOOK_PAUSE );
	return ret | SGS_EXEC_PAUSED;
}


/* INTERNAL INERFACE */

SGSBOOL sgs_ResumeStateRet( SGS_CTX, int args, int* outrvc )
{
	sgs_instr_t I;
	int op, rvc = 0;
	if( C->sf_last == NULL || ( C->sf_last->flags & SGS_SF_PAUSED ) == 0 )
		return SGS_FALSE; /* already running, may not return the expected data */
	if( C->sf_first->flags & SGS_SF_ABORTED )
	{
		while( C->sf_last )
			vm_frame_pop( C );
		C->state |= SGS_STATE_LASTFUNCABORT;
		if( outrvc )
			*outrvc = 0;
		return SGS_TRUE;
	}
	
	/* TODO validate state corruption */
	I = *(C->sf_last->iptr-1);
	op = SGS_INSTR_GET_OP( I );
	sgs_BreakIf( op != SGS_SI_CALL && op != SGS_SI_YLDJMP );
	if( op == SGS_SI_CALL )
	{
		int i, expect = SGS_INSTR_GET_A( I );
		int args_from = SGS_INSTR_GET_B( I ) & 0xff;
		stk_resize_expected( C, expect, args );
		
		if( expect )
		{
			for( i = expect - 1; i >= 0; --i )
				stk_setlvar( C, args_from + i, C->stack_top - expect + i );
			fstk_pop( C, expect );
		}
	}
	else if( op == SGS_SI_YLDJMP )
	{
		sgs_StackFrame* sf = C->sf_last;
		int16_t off = (int16_t) SGS_INSTR_GET_E( I );
		sf->iptr += off;
		sgs_BreakIf( sf->iptr+1 > sf->iend || sf->iptr+1 < sf->code );
	}
	
	C->sf_last->flags &= (uint8_t)~SGS_SF_PAUSED;
	
	if( C->hook_fn )
		C->hook_fn( C->hook_ctx, C, SGS_HOOK_CONT );
	
	rvc = vm_exec( C );
	if( ( rvc & SGS_EXEC_PAUSED ) == 0 )
		vm_postcall( C, rvc );
	if( outrvc )
		*outrvc = rvc & ~SGS_EXEC_PAUSED;
	
	return SGS_TRUE;
}

static size_t funct_size( const sgs_iFunc* f )
{
	size_t sz = f->size + sizeof( sgs_iStr ) * 2 + f->sfuncname->size + f->sfilename->size;
	const sgs_Variable* beg = SGS_ASSUME_ALIGNED_CONST( sgs_func_c_consts( f ), sgs_Variable );
	const sgs_Variable* end = SGS_ASSUME_ALIGNED_CONST( sgs_func_c_bytecode( f ), sgs_Variable );
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

void sgsVM_VarDump( SGS_CTX, const sgs_Variable* var )
{
	/* WP: variable size limit */
	sgs_ErrWritef( C, "%s", TYPENAME( var->type ) );
	switch( var->type )
	{
	case SGS_VT_NULL: break;
	case SGS_VT_BOOL: sgs_ErrWritef( C, " = %s", var->data.B ? "True" : "False" ); break;
	case SGS_VT_INT: sgs_ErrWritef( C, " = %" PRId64, var->data.I ); break;
	case SGS_VT_REAL: sgs_ErrWritef( C, " = %f", var->data.R ); break;
	case SGS_VT_STRING: sgs_ErrWritef( C, " [rc:%" PRId32"] = [%" PRId32 "]\"",
		var->data.S->refcount, var->data.S->size );
		sgs_WriteSafe( (sgs_ErrorOutputFunc) sgs_ErrWritef, C,
			sgs_var_cstr( var ), SGS_MIN( var->data.S->size, 16 ) );
		sgs_ErrWritef( C, var->data.S->size > 16 ? "...\"" : "\"" ); break;
	case SGS_VT_FUNC: sgs_ErrWritef( C, " [rc:%" PRId32"]", var->data.F->refcount ); break;
	case SGS_VT_CFUNC: sgs_ErrWritef( C, " = %p", (void*)(size_t) var->data.C ); break;
	case SGS_VT_OBJECT: sgs_ErrWritef( C, " [rc:%" PRId32"] = %p", var->data.O->refcount, var->data.O ); break;
	case SGS_VT_PTR: sgs_ErrWritef( C, " = %p", var->data.P ); break;
	case SGS_VT_THREAD: sgs_ErrWritef( C, " [rc:%" PRId32"] = %p", var->data.T->refcount, var->data.T ); break;
	}
}

void sgsVM_StackDump( SGS_CTX )
{
	ptrdiff_t i, stksz = C->stack_top - C->stack_base;
	/* WP: stack limit */
	sgs_ErrWritef( C, "STACK (size=%d, bytes=%d/%d)--\n", (int) stksz, (int)( stksz * (ptrdiff_t) STK_UNITSIZE ), (int)( C->stack_mem * STK_UNITSIZE ) );
	for( i = 0; i < stksz; ++i )
	{
		sgs_Variable* var = C->stack_base + i;
		if( var == C->stack_off )
			sgs_ErrWritef( C, "-- offset --\n" );
		sgs_ErrWritef( C, "  " ); sgsVM_VarDump( C, var ); sgs_ErrWritef( C, "\n" );
	}
	sgs_ErrWritef( C, "--\n" );
}



/* ---- The core interface ---- */

static void sgs_StackIdxError( SGS_CTX, sgs_StkIdx item )
{
	sgs_Msg( C, SGS_ERROR, "invalid stack index - %d (abs = %d, stack size = %d)",
		(int) item, (int) stk_absindex( C, item ), (int) SGS_STACKFRAMESIZE );
}

/*
	
	OBJECT CREATION
	
*/

void sgs_InitStringBuf( SGS_CTX, sgs_Variable* out, const char* str, sgs_SizeVal size )
{
	sgs_BreakIf( !str && size && "sgs_InitStringBuf: str = NULL" );
	sgsVM_VarCreateString( C, out, str, size );
}

void sgs_InitString( SGS_CTX, sgs_Variable* out, const char* str )
{
	size_t sz;
	sgs_BreakIf( !str && "sgs_InitString: str = NULL" );
	sz = SGS_STRINGLENGTHFUNC(str);
	sgs_BreakIf( sz > 0x7fffffff && "sgs_InitString: size exceeded" );
	/* WP: error detection */
	sgsVM_VarCreateString( C, out, str, (sgs_SizeVal) sz );
}

void sgs_InitObjectPtr( sgs_Variable* out, sgs_VarObj* obj )
{
	out->type = SGS_VT_OBJECT;
	out->data.O = obj;
	VAR_ACQUIRE( out );
}

void sgs_InitThreadPtr( sgs_Variable* out, sgs_Context* T )
{
	out->type = SGS_VT_THREAD;
	out->data.T = T;
	VAR_ACQUIRE( out );
}


static void copy_or_push( SGS_CTX, sgs_Variable* out, sgs_Variable* var )
{
	if( out )
		*out = *var;
	else
		fstk_push_leave( C, var );
}

SGSONE sgs_CreateObject( SGS_CTX, sgs_Variable* out, void* data, sgs_ObjInterface* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, data, iface, 0 );
	copy_or_push( C, out, &var );
	return 1;
}

void* sgs_CreateObjectIPA( SGS_CTX, sgs_Variable* out, uint32_t added, sgs_ObjInterface* iface )
{
	sgs_Variable var;
	var_create_obj( C, &var, NULL, iface, added );
	copy_or_push( C, out, &var );
	return var.data.O->data;
}

SGSONE sgs_CreateArray( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems )
{
	sgs_Variable var;
	var.type = SGS_VT_NULL;
	sgsSTD_MakeArray( C, &var, numitems );
	copy_or_push( C, out, &var );
	return 1;
}

SGSONE sgs_CreateDict( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems )
{
	sgs_Variable var;
	var.type = SGS_VT_NULL;
	sgsSTD_MakeDict( C, &var, numitems );
	copy_or_push( C, out, &var );
	return 1;
}

SGSONE sgs_CreateMap( SGS_CTX, sgs_Variable* out, sgs_SizeVal numitems )
{
	sgs_Variable var;
	var.type = SGS_VT_NULL;
	sgsSTD_MakeMap( C, &var, numitems );
	copy_or_push( C, out, &var );
	return 1;
}


/*

	STACK & SUB-ITEMS

*/

SGSONE sgs_PushNulls( SGS_CTX, int count )
{
	stk_push_nulls( C, count );
	return 1;
}

SGSONE sgs_PushBool( SGS_CTX, sgs_Bool value )
{
	sgs_Variable var;
	var.type = SGS_VT_BOOL;
	var.data.B = value ? 1 : 0;
	fstk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushInt( SGS_CTX, sgs_Int value )
{
	sgs_Variable var;
	var.type = SGS_VT_INT;
	var.data.I = value;
	fstk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushReal( SGS_CTX, sgs_Real value )
{
	sgs_Variable var;
	var.type = SGS_VT_REAL;
	var.data.R = value;
	fstk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushStringBuf( SGS_CTX, const char* str, sgs_SizeVal size )
{
	sgs_Variable var;
	sgs_BreakIf( !str && size && "sgs_PushStringBuf: str = NULL" );
	sgsVM_VarCreateString( C, &var, str, size );
	fstk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushString( SGS_CTX, const char* str )
{
	size_t sz;
	sgs_Variable var;
	sgs_BreakIf( !str && "sgs_PushString: str = NULL" );
	sz = SGS_STRINGLENGTHFUNC(str);
	sgs_BreakIf( sz > 0x7fffffff && "sgs_PushString: size exceeded" );
	/* WP: error detection */
	sgsVM_VarCreateString( C, &var, str, (sgs_SizeVal) sz );
	fstk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushCFunc( SGS_CTX, sgs_CFunc func )
{
	sgs_Variable var;
	var.type = SGS_VT_CFUNC;
	var.data.C = func;
	fstk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushPtr( SGS_CTX, void* ptr )
{
	sgs_Variable var;
	var.type = SGS_VT_PTR;
	var.data.P = ptr;
	fstk_push_leave( C, &var );
	return 1;
}

SGSONE sgs_PushObjectPtr( SGS_CTX, sgs_VarObj* obj )
{
	sgs_Variable var;
	var.type = SGS_VT_OBJECT;
	var.data.O = obj;
	fstk_push( C, &var ); /* a new reference must be created */
	return 1;
}

SGSONE sgs_PushThreadPtr( SGS_CTX, sgs_Context* T )
{
	sgs_Variable var;
	var.type = SGS_VT_THREAD;
	var.data.T = T;
	fstk_push( C, &var ); /* a new reference must be created */
	return 1;
}


SGSONE sgs_PushVariable( SGS_CTX, sgs_Variable var )
{
	fstk_push( C, &var );
	return 1;
}

void sgs_StoreVariable( SGS_CTX, sgs_Variable* out )
{
	*out = sgs_StackItem( C, -1 );
	stk_pop1nr( C );
}

void sgs_SetStackItem( SGS_CTX, StkIdx item, sgs_Variable val )
{
	if( sgs_IsValidIndex( C, item ) == SGS_FALSE )
	{
		sgs_StackIdxError( C, item );
		return;
	}
	stk_setvar( C, item, &val );
}

void sgs_InsertVariable( SGS_CTX, sgs_StkIdx pos, sgs_Variable val )
{
	if( pos > sgs_StackSize( C ) || pos < -sgs_StackSize( C ) - 1 )
	{
		sgs_Msg( C, SGS_ERROR, "sgs_InsertVariable: invalid index - %d (stack size = %d)",
			(int) pos, (int) SGS_STACKFRAMESIZE );
	}
	else
	{
		if( pos < 0 )
			pos = sgs_StackSize( C ) + 1 + pos;
		sgs_Variable* vp = stk_insert_pos( C, pos );
		*vp = val;
		VAR_ACQUIRE( vp );
	}
}


/* string generation */
char* sgs_PushStringAlloc( SGS_CTX, sgs_SizeVal size )
{
	sgs_Variable var;
	var_create_0str( C, &var, (uint32_t) size );
	fstk_push_leave( C, &var );
	return sgs_var_cstr( &var );
}

char* sgs_InitStringAlloc( SGS_CTX, sgs_Variable* var, sgs_SizeVal size )
{
	var_create_0str( C, var, (uint32_t) size );
	return sgs_var_cstr( var );
}

void sgs_FinalizeStringAlloc( SGS_CTX, sgs_StkIdx item )
{
	sgs_Variable var = sgs_StackItem( C, item );
	sgs_FinalizeStringAllocP( C, &var );
	*stk_getpos( C, item ) = var;
}

void sgs_FinalizeStringAllocP( SGS_CTX, sgs_Variable* var )
{
	if( var->type != SGS_VT_STRING )
		sgs_Msg( C, SGS_APIERR, "sgs_FinalizeStringAlloc: string required" );
	else
		var_finalize_str( C, var );
}



SGSBOOL sgs_GetIndex( SGS_CTX, sgs_Variable obj, sgs_Variable idx, sgs_Variable* out, int isprop )
{
	int ret;
	_EL_BACKUP;
	_EL_SETAPI(1);
	ret = vm_getprop( C, out, &obj, &idx, isprop );
	if( SGS_SUCCEEDED( ret ) )
		VM_GETPROP_RETPTR( ret, out );
	else
		var_initnull( out );
	_EL_RESET;
	return SGS_SUCCEEDED( ret );
}

SGSBOOL sgs_SetIndex( SGS_CTX, sgs_Variable obj, sgs_Variable idx, sgs_Variable val, int isprop )
{
	int ret;
	_EL_BACKUP;
	_EL_SETAPI(1);
	ret = vm_setprop( C, &obj, &idx, &val, isprop );
	_EL_RESET;
	return SGS_SUCCEEDED( ret );
}

SGSBOOL sgs_PushIndex( SGS_CTX, sgs_Variable obj, sgs_Variable idx, int isprop )
{
	int ret;
	sgs_Variable tmp;
	_EL_BACKUP;
	_EL_SETAPI(1);
	ret = vm_getprop( C, &tmp, &obj, &idx, isprop );
	if( SGS_SUCCEEDED( ret ) )
		VM_GETPROP_RETTOP( ret, &tmp );
	else
		fstk_push_null( C );
	_EL_RESET;
	return SGS_SUCCEEDED( ret );
}


SGSBOOL sgs_PushProperty( SGS_CTX, sgs_Variable obj, const char* name )
{
	int ret;
	sgs_PushString( C, name );
	ret = sgs_PushIndex( C, obj, *stk_gettop( C ), SGS_TRUE );
	stk_popskip( C, 1, 1 );
	return ret;
}

SGSBOOL sgs_SetProperty( SGS_CTX, sgs_Variable obj, const char* name, sgs_Variable val )
{
	int ret;
	sgs_PushString( C, name );
	ret = sgs_SetIndex( C, obj, *stk_gettop( C ), val, SGS_TRUE );
	fstk_pop1( C );
	return ret;
}

SGSBOOL sgs_PushNumIndex( SGS_CTX, sgs_Variable obj, sgs_Int idx )
{
	sgs_Variable ivar;
	ivar.type = SGS_VT_INT;
	ivar.data.I = idx;
	return sgs_PushIndex( C, obj, ivar, SGS_FALSE );
}

SGSBOOL sgs_SetNumIndex( SGS_CTX, sgs_Variable obj, sgs_Int idx, sgs_Variable val )
{
	sgs_Variable ivar;
	ivar.type = SGS_VT_INT;
	ivar.data.I = idx;
	return sgs_SetIndex( C, obj, ivar, val, SGS_FALSE );
}


SGSBOOL sgs_GetGlobal( SGS_CTX, sgs_Variable idx, sgs_Variable* out )
{
	int ret;
	_EL_BACKUP;
	out->type = SGS_VT_NULL;
	_EL_SETAPI(1);
	ret = sgsSTD_GlobalGet( C, out, &idx );
	_EL_RESET;
	return ret;
}

SGSBOOL sgs_SetGlobal( SGS_CTX, sgs_Variable idx, sgs_Variable val )
{
	int ret;
	sgs_ObjAcquire( C, C->_G );
	sgs_Variable vO; vO.type = SGS_VT_OBJECT; vO.data.O = C->_G;
	ret = sgs_SetIndex( C, vO, idx, val, 0 );
	sgs_ObjRelease( C, C->_G );
	return ret;
}

SGSBOOL sgs_PushGlobalByName( SGS_CTX, const char* name )
{
	int ret;
	sgs_Variable val;
	ret = sgs_GetGlobalByName( C, name, &val );
	fstk_push_leave( C, &val );
	return ret;
}

SGSBOOL sgs_GetGlobalByName( SGS_CTX, const char* name, sgs_Variable* out )
{
	int ret;
	sgs_Variable str;
	sgs_InitString( C, &str, name );
	ret = sgs_GetGlobal( C, str, out );
	sgs_Release( C, &str );
	return ret;
}

void sgs_SetGlobalByName( SGS_CTX, const char* name, sgs_Variable val )
{
	int ret;
	sgs_Variable str;
	sgs_InitString( C, &str, name );
	ret = sgs_SetGlobal( C, str, val );
	sgs_Release( C, &str );
	sgs_BreakIf( ret == SGS_FALSE );
	SGS_UNUSED( ret );
}


SGSONE sgs_CreatePropList( SGS_CTX, sgs_Variable* out, sgs_Variable obj )
{
	switch( obj.type )
	{
	case SGS_VT_STRING:
		sgs_PushStringLit( C, "length" );
		sgs_CreateArray( C, out, 1 );
		return 1;
		
	case SGS_VT_THREAD:
		sgs_PushStringLit( C, "was_aborted" );
		sgs_PushStringLit( C, "not_started" );
		sgs_PushStringLit( C, "running" );
		sgs_PushStringLit( C, "can_resume" );
		sgs_CreateArray( C, out, 4 );
		return 1;
		
	case SGS_VT_OBJECT:
		{
			int count = 0;
			const sgs_ObjProp* prop = obj.data.O->iface->proplist;
			if( prop )
			{
				while( prop->name )
				{
					if( ( prop->flags & SGS_OBJPROP_NOLIST ) == 0 )
					{
						sgs_PushStringBuf( C, prop->name, prop->nmlength );
						count++;
					}
					prop++;
				}
			}
			sgs_CreateArray( C, out, count );
		}
		return 1;
		
	default:
		sgs_CreateArray( C, out, 0 );
		return 1;
	}
}


sgs_Variable sgs_Registry( SGS_CTX, int subtype )
{
	sgs_Variable out;
	out.type = SGS_VT_OBJECT;
	switch( subtype )
	{
	case SGS_REG_ROOT:
		out.data.O = C->shared->_R;
		break;
	case SGS_REG_SYM:
		out.data.O = C->shared->_SYM;
		break;
	case SGS_REG_INC:
		out.data.O = C->shared->_INC;
		break;
	default:
		out.type = SGS_VT_NULL;
		sgs_Msg( C, SGS_APIERR, "sgs_Registry: invalid subtype (%d)", subtype );
		break;
	}
	return out;
}

void sgs_GetEnv( SGS_CTX, sgs_Variable* out )
{
	sgs_InitObjectPtr( out, C->_G );
}

void sgs_SetEnv( SGS_CTX, sgs_Variable var )
{
	if( var.type != SGS_VT_OBJECT ||
		( var.data.O->iface != sgsstd_dict_iface && var.data.O->iface != sgsstd_map_iface ) )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SetEnv: argument not dict/map" );
		return;
	}
	VAR_ACQUIRE( &var );
	sgs_ObjRelease( C, C->_G );
	C->_G = var.data.O;
}

void sgs_PushEnv( SGS_CTX )
{
	sgs_PushObjectPtr( C, C->_G );
}


/*
	o = offset (isprop=true) (sizeval)
	p = property (isprop=true) (null-terminated string)
	s = super-secret property (isprop=true) (sizeval, buffer)
	i = index (isprop=false) (sizeval)
	k = key (isprop=false) (null-terminated string)
	n = null-including key (isprop=false) (sizeval, buffer)
*/

static SGSBOOL sgs_parse_path_key( SGS_CTX, const char* fn, size_t at,
	va_list* pargs, char a, sgs_Variable* pkey, int* pisprop )
{
	sgs_SizeVal S = -1;
	char* P = NULL;
	
	if( a == 'o' )
	{
		*pisprop = 1;
		S = va_arg( *pargs, int );
	}
	else if( a == 'p' )
	{
		*pisprop = 1;
		P = va_arg( *pargs, char* );
		if( !P )
			goto nullptrerr;
	}
	else if( a == 's' )
	{
		*pisprop = 1;
		S = va_arg( *pargs, int );
		P = va_arg( *pargs, char* );
		if( !P )
			goto nullptrerr;
	}
	else if( a == 'i' )
	{
		*pisprop = 0;
		S = va_arg( *pargs, int );
	}
	else if( a == 'k' )
	{
		*pisprop = 0;
		P = va_arg( *pargs, char* );
		if( !P )
			goto nullptrerr;
	}
	else if( a == 'n' )
	{
		*pisprop = 0;
		S = va_arg( *pargs, int );
		P = va_arg( *pargs, char* );
		if( !P )
			goto nullptrerr;
	}
	else
	{
		sgs_Msg( C, SGS_APIERR, "%s: (pos. %d) unrecognized character '%c'", fn, (int) at, a );
		return SGS_FALSE;
	}
	
	if( P )
	{
		if( S >= 0 )
			sgs_InitStringBuf( C, pkey, P, S );
		else
			sgs_InitString( C, pkey, P );
	}
	else if( S >= 0 )
		*pkey = sgs_MakeInt( S );
	else
	{
		sgs_Msg( C, SGS_INTERR, "%s: (pos. %d) internal path parsing error", fn, (int) at );
		return SGS_FALSE;
	}
	return SGS_TRUE;
	
nullptrerr:
	sgs_Msg( C, SGS_APIERR, "%s: (pos. %d) [%c] = null string pointer passed", fn, (int) at, a );
	return SGS_FALSE;
}

static SGSBOOL sgs_PushPathBuf( SGS_CTX, const char* fn,
	sgs_Variable var, const char* path, size_t plen, va_list* pargs )
{
	int ret = SGS_SUCCESS;
	size_t i = 0;
	fstk_push( C, &var );
	while( i < plen )
	{
		sgs_Variable key;
		int prop = -1;
		char a = path[ i++ ];
		
		if( sgs_parse_path_key( C, fn, i, pargs, a, &key, &prop ) == SGS_FALSE )
			return SGS_FALSE;
		
		ret = sgs_PushIndex( C, *stk_gettop( C ), key, prop );
		VAR_RELEASE( &key );
		if( ret == SGS_FALSE )
			return ret;
		stk_popskip( C, 1, 1 );
	}
	return SGS_TRUE;
}

SGSBOOL sgs_PushPath( SGS_CTX, sgs_Variable var, const char* path, ... )
{
	int ret;
	StkIdx ssz = SGS_STACKFRAMESIZE;
	va_list args;
	va_start( args, path );
	ret = sgs_PushPathBuf( C, "sgs_PushPath", var, path, strlen(path), &args );
	if( ret )
	{
		stk_popskip( C, SGS_STACKFRAMESIZE - ssz - 1, 1 );
	}
	else
	{
		fstk_pop( C, SGS_STACKFRAMESIZE - ssz );
	}
	va_end( args );
	return ret;
}

SGSBOOL sgs_StorePath( SGS_CTX, sgs_Variable var, sgs_Variable val, const char* path, ... )
{
	int ret;
	size_t len = strlen( path );
	StkIdx ssz = SGS_STACKFRAMESIZE;
	va_list args;
	if( !*path )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_StorePath: expected non-empty path" );
		return SGS_FALSE;
	}
	va_start( args, path );
	ret = sgs_PushPathBuf( C, "sgs_StorePath", var, path, len - 1, &args );
	if( ret )
	{
		sgs_Variable key;
		int prop = -1;
		char a = path[ len - 1 ];
		
		if( ( ret = sgs_parse_path_key( C, "sgs_StorePath", len - 1, &args, a, &key, &prop ) ) == SGS_FALSE )
			goto fail;
		
		ret = sgs_SetIndex( C, *stk_gettop( C ), key, val, prop );
		VAR_RELEASE( &key );
		if( ret == SGS_FALSE )
			goto fail;
		ssz--;
	}
fail:
	va_end( args );
	fstk_pop( C, SGS_STACKFRAMESIZE - ssz );
	return ret;
}

/*
	argument unpacking:
	n - null (not sure why but I had a free letter here)
	b - boolean
	c,w,l,q,i,I - integers (int8,int16,int32,int64 x2,int)
	f,d,r - floats (reals) (float32,float64 x2)
	s,m - strings (string,string+size)
	p - function (callable, actually; p stands for "procedure";
		returns a SGSBOOL always, useful only for optional arguments)
	a,t,h,o,* - objects (array,dict,map,specific iface,any iface)
	& - pointer (void*)
	y - thread (sgs_Context*)
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

SGSBOOL sgs_LoadArgsExtVA( SGS_CTX, int from, const char* cmd, va_list* args )
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
				return sgs_Msg( C, SGS_APIERR, "sgs_LoadArgs*: cannot move argument pointer before 0" );
			}
			break;
		case '>': from++; break;
		case '@': method = 1; break;
		case '.':
			if( from < sgs_StackSize( C ) )
			{
				return sgs_Msg( C, SGS_WARNING, "function expects exactly %d arguments, %d given",
					from - method, sgs_StackSize( C ) - method );
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
		
		case 'c': case 'w': case 'l': case 'q': case 'i': case 'I':
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
						case 'I': va_arg( *args, int* ); break;
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
					else if( *cmd == 'I' && isig ){ imin = INT_MIN; imax = INT_MAX; }
					else if( *cmd == 'I' && !isig ){ imin = 0; imax = UINT_MAX; }
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
					case 'I': *va_arg( *args, int* ) = (int) i; break;
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
			
		case 'y':
			{
				if( opt && sgs_ItemType( C, from ) == SGS_VT_NULL )
				{
					if( !nowrite )
						*va_arg( *args, sgs_Context** ) = NULL;
					break;
				}
				
				if( sgs_ItemType( C, from ) != SGS_VT_THREAD )
				{
					argerrx( C, from, method, "thread", "" );
					return opt;
				}
				
				if( !nowrite )
				{
					*va_arg( *args, sgs_Context** ) = sgs_StackItem( C, from ).data.T;
				}
			}
			strict = 0; nowrite = 0; from++; break;
			
		case 'a': case 't': case 'h': case 'o': case '*':
			{
				sgs_ObjInterface* ifc = NULL;
				const char* ostr = "custom object";
				
				if( *cmd == 'a' ){ ifc = sgsstd_array_iface; ostr = "array"; }
				if( *cmd == 't' ){ ifc = sgsstd_dict_iface; ostr = "dict"; }
				if( *cmd == 'h' ){ ifc = sgsstd_map_iface; ostr = "map"; }
				if( *cmd == 'o' ) ifc = va_arg( *args, sgs_ObjInterface* );
				if( *cmd == '*' ) ostr = "object";
				
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
						case '*': va_arg( *args, sgs_ObjInterface** ); break;
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
					case '*': *va_arg( *args, sgs_ObjInterface** ) = O->iface; break;
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
					*va_arg( *args, sgs_Variable* ) = sgs_StackItem( C, from );
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
			return sgs_Msg( C, SGS_APIERR, "sgs_LoadArgs*: unrecognized character" );
			
		}
		if( opt && from >= sgs_StackSize( C ) && cmd[1] != '<' )
			break;
		cmd++;
	}
	return 1;
}

SGSBOOL sgs_LoadArgsExt( SGS_CTX, int from, const char* cmd, ... )
{
	SGSBOOL ret;
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
	ret = sgs_LoadArgsExtVA( C, 0, cmd, &args );
	va_end( args );
	return ret;
}

SGSBOOL sgs_ParseMethod( SGS_CTX, sgs_ObjInterface* iface, void** ptrout, const char* name )
{
	int method_call = sgs_Method( C );
	SGSFN( name );
	if( !sgs_IsObject( C, 0, iface ) )
	{
		sgs_ArgErrorExt( C, 0, method_call, iface->name, "" );
		return SGS_FALSE;
	}
	*ptrout = p_objdata( stk_poff( C, 0 ) );
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


void sgs_Pop( SGS_CTX, StkIdx count )
{
	if( SGS_STACKFRAMESIZE < count || count < 0 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_Pop: invalid count - %d (stack size = %d)",
			(int) count, (int) SGS_STACKFRAMESIZE );
		return;
	}
	fstk_pop( C, count );
}

void sgs_PopSkip( SGS_CTX, StkIdx count, StkIdx skip )
{
	if( SGS_STACKFRAMESIZE < count + skip || count < 0 || skip < 0 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_PopSkip: invalid counts - skip:%d, pop:%d (stack size = %d)",
			(int) skip, (int) count, (int) SGS_STACKFRAMESIZE );
		return;
	}
	stk_popskip( C, count, skip );
}

StkIdx sgs_StackSize( SGS_CTX )
{
	return SGS_STACKFRAMESIZE;
}

void sgs_SetStackSize( SGS_CTX, StkIdx size )
{
	if( size < 0 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SetStackSize: size (%d) cannot be negative", (int) size );
		return;
	}
	stk_setsize( C, size );
}

void sgs_SetDeltaSize( SGS_CTX, sgs_StkIdx diff )
{
	StkIdx tgtsize = SGS_STACKFRAMESIZE + diff;
	if( tgtsize < 0 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SetDeltaSize: resulting size (%d) "
			"cannot be negative (diff = %d)", (int) tgtsize, (int) diff );
		return;
	}
	stk_deltasize( C, diff );
}

SGSRESULT sgs_AdjustStack( SGS_CTX, int expected, int ret )
{
	if( SGS_SUCCEEDED( ret ) )
		stk_deltasize( C, expected - ret );
	return ret;
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

sgs_Variable sgs_OptStackItem( SGS_CTX, sgs_StkIdx item )
{
	if( sgs_IsValidIndex( C, item ) == 0 )
	{
		sgs_Variable var;
		var.type = SGS_VT_NULL;
		return var;
	}
	return *stk_getpos( C, item );
}

sgs_Variable sgs_StackItem( SGS_CTX, sgs_StkIdx item )
{
	if( sgs_IsValidIndex( C, item ) == 0 )
	{
		sgs_Variable var;
		sgs_StackIdxError( C, item );
		var.type = SGS_VT_NULL;
		return var;
	}
	return *stk_getpos( C, item );
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

void sgs_ArithOp( SGS_CTX, sgs_Variable* out, sgs_Variable* A, sgs_Variable* B, int op )
{
	if( op == SGS_EOP_NEGATE )
	{
		vm_op_negate( C, out, A ); /* throws an error */
		return;
	}
	if( op < SGS_EOP_ADD || op > SGS_EOP_MOD )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_ArithOp: invalid operation ID (%d) "
			"specified (add/sub/mul/div/mod/negate allowed)", op );
		VAR_RELEASE( out ); /* out = null */
		return;
	}
	vm_arith_op( C, out, A, B, op ); /* throws an error */
}

void sgs_IncDec( SGS_CTX, sgs_Variable* out, sgs_Variable* A, int inc )
{
	vm_op_incdec( C, out, A, inc ? 1 : -1 );
}


/*

	OPERATIONS

*/

int sgs_XFCall( SGS_CTX, int args, int gotthis )
{
	int rvc = 0;
	if( args < 0 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_XFCall: negative argument count (%d)", args );
		return 0;
	}
	if( SGS_STACKFRAMESIZE < args + ( gotthis ? 2 : 1 ) )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_XFCall: not enough items in stack (need: %d, got: %d)",
			args + ( gotthis ? 2 : 1 ), (int) SGS_STACKFRAMESIZE );
		return 0;
	}
	/* fix up closure */
	{
		sgs_Variable out, *vp = stk_ptop( C, - args - ( gotthis ? 2 : 1 ) );
		if( vp->type == SGS_VT_FUNC && vp->data.F->numclsr != 0 )
		{
			int i;
			sgs_Closure** clsrlist = sgsSTD_MakeClosure( C, &out, vp, vp->data.F->numclsr );
			for( i = 0; i < vp->data.F->numclsr; ++i )
			{
				sgs_Closure* nc = sgs_Alloc( sgs_Closure );
				nc->refcount = 1;
				nc->var.type = SGS_VT_NULL;
				*clsrlist++ = nc;
			}
			p_setvar_leave( vp, &out );
		}
	}
	SGS_PERFEVENT( ps_precall_begin( args, gotthis, CALLSRC_NATIVE ) );
	vm_call( C, args, gotthis, &rvc, 0 );
	return rvc;
}

SGSBOOL sgs_GlobalCall( SGS_CTX, const char* name, int args, int expect )
{
	sgs_Variable v_func;
	if( !sgs_GetGlobalByName( C, name, &v_func ) )
		return SGS_FALSE;
	sgs_InsertVariable( C, -args - 1, v_func );
	sgs_Release( C, &v_func );
	sgs_Call( C, args, expect );
	return SGS_TRUE;
}

void sgs_TypeOf( SGS_CTX, sgs_Variable var )
{
	const char* ty = "ERROR";
	
	switch( var.type )
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
			sgs_VarObj* O = var.data.O;
			if( O->mm_enable && _push_metamethod( C, O, "__typeof" ) )
			{
				sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
				sgs_PushObjectPtr( C, O );
				if( sgs_XThisCall( C, 0 ) > 0 && stk_gettop( C )->type == SGS_VT_STRING )
				{
					stk_downsize_keep( C, ssz, 1 );
					return;
				}
				stk_downsize( C, ssz );
			}
			ty = O->iface->name ? O->iface->name : "object";
		}
		break;
	case SGS_VT_PTR:    ty = "pointer"; break;
	case SGS_VT_THREAD: ty = "thread"; break;
	}
	
	sgs_PushString( C, ty );
}

void sgs_DumpVar( SGS_CTX, sgs_Variable var, int maxdepth )
{
	if( maxdepth <= 0 )
	{
		sgs_PushStringLit( C, "..." );
		return;
	}
	
	switch( var.type )
	{
	case SGS_VT_NULL: sgs_PushStringLit( C, "null" ); break;
	case SGS_VT_BOOL: sgs_PushString( C, var.data.B ? "bool (true)" : "bool (false)" ); break;
	case SGS_VT_INT: { char buf[ 32 ];
		sprintf( buf, "int (%" PRId64 ")", var.data.I );
		sgs_PushString( C, buf ); } break;
	case SGS_VT_REAL: { char buf[ 32 ];
		snprintf( buf, 31, "real (%g)", var.data.R );
		sgs_PushString( C, buf ); } break;
	case SGS_VT_STRING:
		{
			char buf[ 532 ];
			char* bptr = buf;
			char* bend = buf + 512;
			char* source = sgs_var_cstr( &var );
			uint32_t len = var.data.S->size;
			char* srcend = source + len;
			sprintf( buf, "string [%" PRId32"] \"", len );
			bptr += strlen( buf );
			while( source < srcend && bptr < bend )
			{
				if( *source == ' ' || sgs_isgraph( (int)*source ) )
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
			sgs_iFunc* F = var.data.F;
			
			const char* str1 = F->sfuncname->size ? "SGS function '" : "SGS function <anonymous>";
			const char* str2 = F->sfuncname->size ? "' defined at " : " defined at ";
			const char* str3 = "'";
			
			sgs_membuf_appbuf( &mb, C, str1, strlen(str1) );
			if( F->sfuncname->size )
				sgs_membuf_appbuf( &mb, C, sgs_str_cstr( F->sfuncname ), F->sfuncname->size );
			if( F->sfilename->size )
			{
				char lnbuf[ 32 ];
				sgs_membuf_appbuf( &mb, C, str2, strlen(str2) );
				sgs_membuf_appbuf( &mb, C, sgs_str_cstr( F->sfilename ), F->sfilename->size );
				sprintf( lnbuf, ":%d", (int) F->linenum );
				sgs_membuf_appbuf( &mb, C, lnbuf, strlen(lnbuf) );
			}
			else if( F->sfuncname->size )
				sgs_membuf_appbuf( &mb, C, str3, strlen(str3) );
			
			/* WP: various limits */
			sgs_PushStringBuf( C, mb.ptr, (sgs_SizeVal) mb.size );
			sgs_membuf_destroy( &mb, C );
		}
		break;
	case SGS_VT_CFUNC:
		{
			char buf[ 32 ];
			sprintf( buf, "C function (%p)", var.data.C );
			sgs_PushString( C, buf );
		}
		break;
	case SGS_VT_OBJECT:
		{
			char buf[ 256 ];
			int q = 0;
			_STACK_PREPARE;
			sgs_VarObj* obj = var.data.O;
			
			if( obj->iface->dump )
			{
				_STACK_PROTECT;
				q = SGS_SUCCEEDED( obj->iface->dump( C, obj, maxdepth - 1 ) );
				_STACK_UNPROTECT_SKIP( q );
			}
			
			if( !q )
			{
				snprintf( buf, 255, "object (%p) [%" PRId32"] %s", (void*) obj, obj->refcount,
					obj->iface->name ? obj->iface->name : "<unnamed>" );
				buf[ 255 ] = 0;
				sgs_PushString( C, buf );
			}
			else
				sgs_ToString( C, -1 );
		}
		break;
	case SGS_VT_PTR:
		{
			char buf[ 32 ];
			sprintf( buf, "pointer (%p)", var.data.P );
			sgs_PushString( C, buf );
		}
		break;
	case SGS_VT_THREAD:
		{
			char buf[ 32 ];
			sprintf( buf, "thread (%p)", var.data.T );
			sgs_PushString( C, buf );
		}
		break;
	default:
		{
			char buf[ 32 ];
			sprintf( buf, "<invalid-type> (%d)", (int) var.type );
			sgs_BreakIf( "Invalid variable type in sgs_DumpVar!" );
			sgs_PushString( C, buf );
		}
		break;
	}
}

static void sgsVM_GCExecute( SGS_SHCTX )
{
	sgs_Variable *vbeg, *vend;
	sgs_VarObj* p;
	
	S->redblue = !S->redblue;
	S->gcrun = SGS_TRUE;
	
	SGS_CTX = S->state_list;
	/* shared state */
	/* - interfaces */
	if( S->array_iface )
	{
		obj_gcmark( S, S->array_iface );
	}
	/* - registry */
	sgsSTD_RegistryGC( S );
	
	while( C )
	{
		/* -- MARK -- */
		/* STACK */
		vbeg = C->stack_base; vend = C->stack_top;
		while( vbeg < vend )
		{
			vm_gcmark( S, vbeg );
			vbeg++;
		}
		/* FRAMES */
		{
			sgs_StackFrame* sf = C->sf_first;
			while( sf )
			{
				if( sf->clsrref )
					obj_gcmark( S, sf->clsrref );
				sf = sf->next;
			}
		}
		
		/* GLOBALS */
		sgsSTD_GlobalGC( C );
		/* THREADS */
		sgsSTD_ThreadsGC( C );
		
		C = C->next;
	}
	
	/* -- SWEEP -- */
	C = S->state_list; // any context is good enough here
	C->refcount++;
	/* destruct objects */
	p = S->objs;
	while( p ){
		sgs_VarObj* pn = p->next;
		if( p->redblue != S->redblue )
			var_destruct_object( C, p );
		p = pn;
	}
	
	/* free variables */
	p = S->objs;
	while( p ){
		sgs_VarObj* pn = p->next;
		if( p->redblue != S->redblue )
			var_free_object( C, p );
		p = pn;
	}
	
	C->refcount--;
	S->gcrun = SGS_FALSE;
}

void sgs_GCExecute( SGS_CTX )
{
	SGS_SHCTX_USE;
	sgsVM_GCExecute( S );
}


const char* sgs_DebugDumpVarExt( SGS_CTX, sgs_Variable var, int maxdepth )
{
	if( maxdepth < 0 )
	{
		fstk_push( C, &var );
		return sgs_ToString( C, -1 );
	}
	else
	{
		sgs_DumpVar( C, var, maxdepth );
		return sgs_GetStringPtr( C, -1 );
	}
}


void sgs_PadString( SGS_CTX )
{
	const char* padding = "  ";
	const uint32_t padsize = 2;
	
	if( sgs_StackSize( C ) < 1 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_PadString: stack is empty" );
		return;
	}
	else
	{
		uint32_t i, allocsize;
		char* ostr, *ostre;
		const char* cstr;
		sgs_Variable* var = stk_getpos( C, -1 );
		if( var->type != SGS_VT_STRING )
		{
			sgs_Msg( C, SGS_APIERR, "sgs_PadString: need string at top of stack" );
			return;
		}
		cstr = sgs_var_cstr( var );
		for( i = 0; cstr[ i ]; )
			if( cstr[ i ] == '\n' ) i++; else cstr++;
		allocsize = var->data.S->size + i * padsize;
		if( allocsize > 0x7fffffff )
			allocsize = 0x7fffffff;
		
		/* WP: unimportant */
		sgs_PushStringAlloc( C, (sgs_SizeVal) allocsize );
		cstr = sgs_var_cstr( stk_getpos( C, -2 ) );
		ostr = sgs_var_cstr( stk_getpos( C, -1 ) );
		ostre = ostr + allocsize;
		while( *cstr && ostr < ostre )
		{
			*ostr++ = *cstr;
			if( *cstr == '\n' )
			{
				const char* ppd = padding;
				while( *ppd && ostr < ostre )
					*ostr++ = *ppd++;
			}
			cstr++;
		}
		sgs_PopSkip( C, 1, 1 );
		sgs_FinalizeStringAlloc( C, -1 );
	}
}

void sgs_ToPrintSafeString( SGS_CTX )
{
	sgs_MemBuf mb = sgs_membuf_create();
	char* buf = NULL;
	sgs_SizeVal size = 0, i;
	if( sgs_StackSize( C ) < 1 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_PadString: stack is empty" );
		return;
	}
	buf = sgs_ToStringBuf( C, -1, &size );
	for( i = 0; i < size; ++i )
	{
		if( sgs_isgraph( (int)buf[ i ] ) || buf[ i ] == ' ' )
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
		mb.size = 0x7fffffff;
	/* WP: unimportant */
	sgs_PushStringBuf( C, mb.ptr, (sgs_SizeVal) mb.size );
	sgs_membuf_destroy( &mb, C );
}

void sgs_StringConcat( SGS_CTX, StkIdx args )
{
	if( vm_op_concat_ex( C, args ) == SGS_FALSE )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_StringConcat: not enough items on stack (need: %d, have: %d)",
			(int) args, (int) SGS_STACKFRAMESIZE );
	}
}

void sgs_CloneItem( SGS_CTX, sgs_Variable var )
{
	/*
		strings are supposed to be immutable
		(even though C functions can accidentally
		or otherwise modify them with relative ease)
	*/
	if( var.type == SGS_VT_OBJECT )
	{
		int ret = SGS_ENOTFND;
		sgs_VarObj* O = var.data.O;
		if( O->mm_enable && _push_metamethod( C, O, "__clone" ) )
		{
			sgs_SizeVal ssz = SGS_STACKFRAMESIZE - 1;
			sgs_PushObjectPtr( C, O );
			if( sgs_XThisCall( C, 0 ) > 0 )
			{
				stk_downsize_keep( C, ssz, 1 );
				return;
			}
			stk_downsize( C, ssz );
		}
		if( O->iface->convert )
		{
			_STACK_PREPARE;
			_STACK_PROTECT;
			ret = O->iface->convert( C, O, SGS_CONVOP_CLONE );
			_STACK_UNPROTECT_SKIP( SGS_SUCCEEDED( ret ) && SGS_STACKFRAMESIZE >= 1 ? 1 : 0 );
		}
		if( SGS_FAILED( ret ) )
		{
			sgs_Msg( C, SGS_ERROR, "failed to clone variable" );
			fstk_push_null( C );
			return;
		}
	}
	else
	{
		/* even though functions are immutable, they're also impossible to modify,
			thus there is little need for showing an error when trying to convert one,
			especially if it's a part of some object to be cloned */
		fstk_push( C, &var );
	}
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
sgs_Int sgs_GetIntP( SGS_CTX, sgs_Variable* var ){ return var_getint( var ); }
sgs_Real sgs_GetRealP( SGS_CTX, sgs_Variable* var ){ return var_getreal( var ); }
void* sgs_GetPtrP( SGS_CTX, sgs_Variable* var ){ return var_getptr( var ); }

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
		sgs_TypeOf( C, *var );
		sgs_StoreVariable( C, var );
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

#define SGS_IS_SYSTEM_TYPE( ty ) ((ty) == SGS_VT_NULL || (ty) == SGS_VT_FUNC || \
	(ty) == SGS_VT_CFUNC || (ty) == SGS_VT_PTR || (ty) == SGS_VT_THREAD)

SGSBOOL sgs_ParseBoolP( SGS_CTX, sgs_Variable* var, sgs_Bool* out )
{
	uint32_t ty = var->type;
	if( SGS_IS_SYSTEM_TYPE( ty ) || ty == SGS_VT_STRING )
		return SGS_FALSE;
	if( out )
		*out = sgs_GetBoolP( C, var );
	return SGS_TRUE;
}

SGSBOOL sgs_ParseIntP( SGS_CTX, sgs_Variable* var, sgs_Int* out )
{
	sgs_Int i;
	if( SGS_IS_SYSTEM_TYPE( var->type ) )
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
	if( SGS_IS_SYSTEM_TYPE( var->type ) )
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
	if( SGS_IS_SYSTEM_TYPE( ty ) )
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


/* index versions */
sgs_Bool sgs_GetBool( SGS_CTX, StkIdx item )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return var_getbool( C, &var );
}

sgs_Int sgs_GetInt( SGS_CTX, StkIdx item )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return var_getint( &var );
}

sgs_Real sgs_GetReal( SGS_CTX, StkIdx item )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return var_getreal( &var );
}

void* sgs_GetPtr( SGS_CTX, StkIdx item )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return var_getptr( &var );
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
	item = stk_absindex( C, item );
	if( stk_getpos( C, item )->type == SGS_VT_OBJECT )
	{
		sgs_TypeOf( C, *stk_getpos( C, item ) );
		sgs_SetStackItem( C, item, *stk_gettop( C ) );
		sgs_Pop( C, 1 );
	}
	return sgs_ToStringBuf( C, item, outsize );
}


SGSBOOL sgs_IsObject( SGS_CTX, StkIdx item, sgs_ObjInterface* iface )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return var.type == SGS_VT_OBJECT && var.data.O->iface == iface;
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
	sgs_Variable var = sgs_OptStackItem( C, item );
	return sgs_ParseBoolP( C, &var, out );
}

SGSBOOL sgs_ParseInt( SGS_CTX, StkIdx item, sgs_Int* out )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return sgs_ParseIntP( C, &var, out );
}

SGSBOOL sgs_ParseReal( SGS_CTX, StkIdx item, sgs_Real* out )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return sgs_ParseRealP( C, &var, out );
}

SGSBOOL sgs_ParseString( SGS_CTX, StkIdx item, char** out, sgs_SizeVal* size )
{
	char* str;
	uint32_t ty;
	if( !sgs_IsValidIndex( C, item ) )
		return SGS_FALSE;
	ty = sgs_ItemType( C, item );
	if( SGS_IS_SYSTEM_TYPE( ty ) )
		return SGS_FALSE;
	str = sgs_ToStringBuf( C, item, size );
	if( out )
		*out = str;
	return str != NULL;
}

SGSBOOL sgs_ParseObjectPtr( SGS_CTX, sgs_StkIdx item,
	sgs_ObjInterface* iface, sgs_VarObj** out, int strict )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return sgs_ParseObjectPtrP( C, &var, iface, out, strict );
}

SGSBOOL sgs_ParsePtr( SGS_CTX, StkIdx item, void** out )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	return sgs_ParsePtrP( C, &var, out );
}


sgs_Bool sgs_GlobalBool( SGS_CTX, const char* name )
{
	sgs_Bool v;
	sgs_PushGlobalByName( C, name );
	v = var_getbool( C, stk_gettop( C ) );
	fstk_pop1( C );
	return v;
}

sgs_Int sgs_GlobalInt( SGS_CTX, const char* name )
{
	sgs_Int v;
	sgs_PushGlobalByName( C, name );
	v = var_getint( stk_gettop( C ) );
	fstk_pop1( C );
	return v;
}

sgs_Real sgs_GlobalReal( SGS_CTX, const char* name )
{
	sgs_Real v;
	sgs_PushGlobalByName( C, name );
	v = var_getreal( stk_gettop( C ) );
	fstk_pop1( C );
	return v;
}

char* sgs_GlobalStringBuf( SGS_CTX, const char* name, sgs_SizeVal* outsize )
{
	char* buf;
	sgs_PushGlobalByName( C, name );
	if( !sgs_ParseString( C, -1, &buf, outsize ) )
	{
		fstk_pop1( C );
		return NULL;
	}
	return buf;
}


SGSBOOL sgs_CreateIterator( SGS_CTX, sgs_Variable* out, sgs_Variable var )
{
	int ret;
	fstk_push_null( C );
	ret = vm_forprep( C, stk_absindex( C, -1 ), &var );
	if( out )
		sgs_StoreVariable( C, out );
	return ret;
}

SGSBOOL sgs_IterAdvance( SGS_CTX, sgs_Variable var )
{
	return vm_fornext( C, -1, -1, &var );
}

void sgs_IterPushData( SGS_CTX, sgs_Variable var, int key, int value )
{
	StkIdx idkey, idval;
	if( !key && !value )
		return;
	if( key )
	{
		idkey = stk_size( C );
		fstk_push_null( C );
	}
	else idkey = -1;
	if( value )
	{
		idval = stk_size( C );
		fstk_push_null( C );
	}
	else idval = -1;
	vm_fornext( C, idkey, idval, &var );
}

void sgs_IterGetData( SGS_CTX, sgs_Variable var, sgs_Variable* key, sgs_Variable* value )
{
	if( !key && !value )
		return;
	if( key ) fstk_push_null( C );
	if( value ) fstk_push_null( C );
	vm_fornext( C, key ? stk_topindex( C, value ? -2 : -1 ) : -1, value ? stk_topindex( C, -1 ) : -1, &var );
	if( value ) sgs_StoreVariable( C, value );
	if( key ) sgs_StoreVariable( C, key );
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

int sgs_ObjectArg( SGS_CTX )
{
	return C->object_arg;
}


void sgs_Acquire( SGS_CTX, sgs_Variable* var )
{
	SGS_UNUSED( C );
	VAR_ACQUIRE( var );
}

void sgs_Release( SGS_CTX, sgs_Variable* var )
{
	VAR_RELEASE( var );
}

void sgs_GCMark( SGS_CTX, sgs_Variable* var )
{
	SGS_SHCTX_USE;
	vm_gcmark( S, var );
}

void sgs_ObjAcquire( SGS_CTX, sgs_VarObj* obj )
{
	SGS_UNUSED( C );
	obj->refcount++;
}

void sgs_ObjRelease( SGS_CTX, sgs_VarObj* obj )
{
	if( --obj->refcount <= 0 && !C->shared->gcrun )
		sgsVM_VarDestroyObject( C, obj );
}

void sgs_ObjGCMark( SGS_CTX, sgs_VarObj* obj )
{
	SGS_SHCTX_USE;
	obj_gcmark( S, obj );
}

void sgs_ObjAssign( SGS_CTX, sgs_VarObj** dest, sgs_VarObj* src )
{
	if( src )
		sgs_ObjAcquire( C, src );
	if( *dest )
		sgs_ObjRelease( C, *dest );
	*dest = src;
}

void sgs_ObjCallDtor( SGS_CTX, sgs_VarObj* obj )
{
	var_destruct_object( C, obj );
}

void sgs_ObjSetMetaObj( SGS_CTX, sgs_VarObj* obj, sgs_VarObj* metaobj )
{
	sgs_VarObj* chk = metaobj;
	while( chk )
	{
		sgs_BreakIf( chk == obj && "sgs_ObjSetMetaObj: loop detected" );
		chk = chk->metaobj;
	}
	
	if( metaobj )
		sgs_ObjAcquire( C, metaobj );
	if( obj->metaobj )
		sgs_ObjRelease( C, obj->metaobj );
	obj->metaobj = metaobj;
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

void sgs_SetObjectDataP( sgs_Variable* var, void* data )
{
	_OBJPREP_P( ; );
	var->data.O->data = data;
}

void sgs_SetObjectIfaceP( sgs_Variable* var, sgs_ObjInterface* iface )
{
	_OBJPREP_P( ; );
	var->data.O->iface = iface;
}


#define _OBJPREP( ret ) \
	sgs_Variable var = sgs_OptStackItem( C, item ); \
	DBLCHK( var.type != SGS_VT_OBJECT, ret )

char* sgs_GetStringPtr( SGS_CTX, StkIdx item )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	DBLCHK( var.type != SGS_VT_STRING, NULL )
	return sgs_var_cstr( &var );
}

sgs_SizeVal sgs_GetStringSize( SGS_CTX, StkIdx item )
{
	sgs_Variable var = sgs_OptStackItem( C, item );
	DBLCHK( var.type != SGS_VT_STRING, 0 )
	/* WP: string limit */
	return (sgs_SizeVal) var.data.S->size;
}

sgs_VarObj* sgs_GetObjectStruct( SGS_CTX, StkIdx item )
{
	_OBJPREP( NULL );
	return var.data.O;
}

void* sgs_GetObjectData( SGS_CTX, StkIdx item )
{
	_OBJPREP( NULL );
	return var.data.O->data;
}

sgs_ObjInterface* sgs_GetObjectIface( SGS_CTX, StkIdx item )
{
	_OBJPREP( NULL );
	return var.data.O->iface;
}

void sgs_SetObjectData( SGS_CTX, StkIdx item, void* data )
{
	_OBJPREP( ; );
	var.data.O->data = data;
}

void sgs_SetObjectIface( SGS_CTX, StkIdx item, sgs_ObjInterface* iface )
{
	_OBJPREP( ; );
	var.data.O->iface = iface;
}

#undef DBLCHK


#undef StkIdx

