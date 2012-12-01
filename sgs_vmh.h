

#ifndef SGS_VMH_H_INCLUDED
#define SGS_VMH_H_INCLUDED

#include "sgs_ctx.h"
#include "sgs_proc.h"


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

static sgs_Variable* make_var( SGS_CTX, int type )
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

static SGS_INLINE char* var_cstr( sgs_Variable* var )
{
	sgs_BreakIf( var->type != SVT_STRING );
	return var->data.S.ptr;// ? val->data.S.ptr : val->data.B;
}

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

static sgs_Variable* var_create_str( SGS_CTX, const char* str, int32_t len )
{
	sgs_Variable* var;
	sgs_BreakIf( !str );

	if( len < 0 )
		len = strlen( str );

	var = var_create_0str( C, len );
	memcpy( var_cstr( var ), str, len );
	return var;
}



#endif /* SGS_VMH_H_INCLUDED */
