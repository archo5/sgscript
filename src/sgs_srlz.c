

#include "sgs_int.h"

#define SGS_DEBUG_SERIALIZE 0
#if SGS_DEBUG_SERIALIZE
#  define SRLZ_DEBUG( x ) x
#else
#  define SRLZ_DEBUG( x )
#endif


#define NUMBER_OUT_OF_RANGE( x, maxn ) (((unsigned)(x)) >= ((unsigned)(maxn)))


static int _serialize_function( SGS_CTX, sgs_iFunc* func, sgs_MemBuf* out )
{
	if( sgsBC_Func2Buf( C, func, out ) == SGS_FALSE )
	{
		sgs_Msg( C, SGS_INTERR, "failed to serialize function: error in data" );
		return 0;
	}
	return 1;
}

static int _unserialize_function( SGS_CTX, const char* buf, size_t sz, sgs_Variable* outfn )
{
	const char* err;
	if( sgsBC_ValidateHeader( buf, sz ) < SGS_HEADER_SIZE )
	{
		sgs_Msg( C, SGS_WARNING, "failed to unserialize function: incomplete data" );
		return 0;
	}
	err = sgsBC_Buf2Func( C, "<anonymous>", buf, sz, outfn );
	if( err )
		return sgs_Msg( C, SGS_WARNING, "failed to unserialize function: %s", err );
	return 1;
}


static SGSBOOL sgs__thread_serialize( SGS_CTX, sgs_Context* ctx, sgs_MemBuf* outbuf, sgs_MemBuf* argarray )
{
	sgs_MemBuf buf = sgs_membuf_create();
	sgs_StackFrame* sf;
	
	SRLZ_DEBUG( printf( "SRLZ thread_serialize\n" ) );
	
#define _WRITERL( x ) { sgs_membuf_appbuf( &buf, C, &x, 8 ); }
#define _WRITE32( x ) { int32_t _tmp = (int32_t)(x); sgs_membuf_appbuf( &buf, C, &_tmp, 4 ); }
#define _WRITE8( x ) { sgs_membuf_appchr( &buf, C, (char)(x) ); }
	
	/* failure condition: cannot serialize self */
	if( C == ctx )
		return 0;
	/* failure condition: C functions in stack frame */
	sf = ctx->sf_first;
	while( sf )
	{
		if( sf->iptr == NULL )
			return 0;
		sf = sf->next;
	}
	
	/* variables: _G */
	SRLZ_DEBUG( printf( "SRLZ thread _G\n" ) );
	{
		sgs_Variable v_obj; v_obj.type = SGS_VT_OBJECT; v_obj.data.O = ctx->_G;
		sgs_Serialize( C, v_obj );
	}
	/* variables: stack */
	SRLZ_DEBUG( printf( "SRLZ thread STACK\n" ) );
	{
		sgs_Variable* p = ctx->stack_base;
		sf = ctx->sf_first;
		while( p != ctx->stack_top )
		{
			/* if stack position equals frame function position and ..
			.. frame has a closure ref., serialize closure instead of its func. */
			int hasclsr = 0;
			if( sf && sf->func == p )
			{
				SRLZ_DEBUG( printf( "SRLZ thread found function at %d\n", (int)( sf->func - ctx->stack_base ) ) );
				if( sf->clsrref )
				{
					SRLZ_DEBUG( printf( "^ it has closure\n" ) );
					sgs_Variable vobj;
					vobj.type = SGS_VT_OBJECT;
					vobj.data.O = sf->clsrref;
					sgs_Serialize( C, vobj );
					hasclsr = 1;
				}
				/* next frames won't appear on stack before this position */
				sf = sf->next;
			}
			if( !hasclsr )
				sgs_Serialize( C, *p );
			p++;
		}
	}
	
	SRLZ_DEBUG( printf( "SRLZ thread CORE\n" ) );
	_WRITE32( 0x5C057A7E ); /* Serialized COroutine STATE */
	/* POD: main context */
	_WRITE32( ctx->minlev );
	_WRITE32( ctx->apilev );
	_WRITE32( ctx->last_errno );
	_WRITE32( ctx->state );
	_WRITERL( ctx->st_timeout );
	_WRITERL( ctx->wait_timer );
	_WRITERL( ctx->tm_accum );
	_WRITE32( ctx->stack_top - ctx->stack_base );
	_WRITE32( ctx->stack_off - ctx->stack_base );
	_WRITE32( ctx->stack_mem );
	_WRITE32( ctx->sf_count );
	_WRITE32( ctx->num_last_returned );
	/* stack frames */
	SRLZ_DEBUG( printf( "SRLZ thread FRAMES\n" ) );
	sf = ctx->sf_first;
	while( sf )
	{
		/* 'code' will be taken from function */
		if( sf->func->type == SGS_VT_FUNC )
		{
			sgs_iFunc* F = sf->func->data.F;
			_WRITE32( sf->func - ctx->stack_base );
			_WRITE32( sf->iptr - sgs_func_bytecode( F ) );
			_WRITE32( sgs_func_instr_count( F ) ); /* - for validation */
			_WRITE32( sgs_func_const_count( F ) ); /* - for validation */
		}
		else
		{
			SRLZ_DEBUG( printf( "SRLZ THREAD NOT VT_FUNC\n" ) );
			goto fail;
		}
		/* 'cptr' will be taken from function */
		/* 'nfname' is irrelevant for non-native functions */
		/* 'prev', 'next', 'cached' are system pointers */
		_WRITE32( sf->argbeg );
		_WRITE32( sf->stkoff );
		_WRITE32( sf->errsup );
		_WRITE8( sf->argcount );
		_WRITE8( sf->flags );
		sf = sf->next;
	}
	
	SRLZ_DEBUG( printf( "SRLZ emit thread\n" ) );
	sgs_membuf_appchr( outbuf, C, 'T' );
	if( argarray )
	{
		uint32_t argcount = (uint32_t)( 1 /* _G */
			+ ( ctx->stack_top - ctx->stack_base ) /* stack */ );
		sgs_membuf_appbuf( outbuf, C, &argcount, 4 );
		sgs_membuf_appbuf( outbuf, C, argarray->ptr + argarray->size - argcount * 4, argcount * 4 );
		sgs_membuf_erase( argarray, argarray->size - argcount * 4, argarray->size );
	}
	
	sgs_membuf_appbuf( outbuf, C, buf.ptr, buf.size );
	sgs_membuf_destroy( &buf, C );
	
#undef _WRITERL
#undef _WRITE32
#undef _WRITE8
	return 1;
fail:
	SRLZ_DEBUG( printf( "SRLZ serialize_thread ERROR\n" ) );
	sgs_membuf_destroy( &buf, C );
	return 0;
}

static int sgs__thread_unserialize( SGS_CTX, sgs_Context** pT, char** pbuf, char* bufend )
{
	char *buf = *pbuf;
	sgs_Context* ctx = sgsCTX_ForkState( C, SGS_FALSE );
	
	SRLZ_DEBUG( printf( "USRZ thread_unserialize\n" ) );
	
#define _READRL( x ) { if( buf + 8 > bufend ) goto fail; memcpy( &x, buf, 8 ); buf += 8; }
#define _READ32( x ) { if( buf + 4 > bufend ) goto fail; memcpy( &x, buf, 4 ); buf += 4; }
#define _READ8( x ) { if( buf + 1 > bufend ) goto fail; x = (uint8_t) *buf++; }
	
	{
		int32_t i, tag, stacklen, stackoff, sfnum;
		
		SRLZ_DEBUG( printf( "USRZ thread CORE\n" ) );
		_READ32( tag );
		if( tag != 0x5C057A7E )
		{
			SRLZ_DEBUG( printf( "USRZ THREAD NOT 5C057A7E ([s]erialized [co]routine [state])\n" ) );
			goto fail;
		}
		
		/* POD: context */
		_READ32( ctx->minlev );
		_READ32( ctx->apilev );
		_READ32( ctx->last_errno );
		_READ32( ctx->state );
		_READRL( ctx->st_timeout );
		_READRL( ctx->wait_timer );
		_READRL( ctx->tm_accum );
		_READ32( stacklen );
		_READ32( stackoff );
		_READ32( ctx->stack_mem );
		_READ32( sfnum );
		_READ32( ctx->num_last_returned );
		
		/* variables: _G */
		SRLZ_DEBUG( printf( "USRZ thread _G\n" ) );
		ctx->_G = sgs_StackItem( C, 0 ).data.O;
		sgs_ObjAcquire( ctx, ctx->_G );
		
		/* variables: stack */
		SRLZ_DEBUG( printf( "USRZ thread STACK\n" ) );
		sgs_BreakIf( ctx->stack_top != ctx->stack_base );
		for( i = 0; i < stacklen; ++i )
			sgs_PushVariable( ctx, sgs_StackItem( C, 1 + i ) );
		sgs_BreakIf( ctx->stack_top != ctx->stack_base + stacklen );
		if( stackoff > stacklen )
		{
			SRLZ_DEBUG( printf( "USRZ THREAD stackoff > stacklen\n" ) );
			goto fail;
		}
		ctx->stack_off = ctx->stack_base + stackoff;
		
		/* stack frames */
		SRLZ_DEBUG( printf( "USRZ thread FRAMES\n" ) );
		for( i = 0; i < sfnum; ++i )
		{
			sgs_StackFrame* sf;
			int32_t funcoff, iptrpos, iendpos, ccount;
			
			/* POD: stack frames */
			_READ32( funcoff );
			SRLZ_DEBUG( printf( "USRZ thread frame funcoff = %d\n", (int) funcoff ) );
			if( funcoff < 0 || funcoff >= stacklen )
			{
				SRLZ_DEBUG( printf( "USRZ THREAD funcoff out of bounds [0;stacklen)\n" ) );
				goto fail;
			}
			
			if( !sgsVM_PushStackFrame( ctx, ctx->stack_base + funcoff ) )
			{
				SRLZ_DEBUG( printf( "USRZ THREAD PushStackFrame failed\n" ) );
				goto fail;
			}
			sf = ctx->sf_last;
			
			/* 'code' will be taken from function */
			if( sf->func->type == SGS_VT_FUNC )
			{
				sgs_iFunc* F = sf->func->data.F;
				_READ32( iptrpos );
				_READ32( iendpos ); /* - for validation */
				if( iendpos != sgs_func_instr_count( F ) )
				{
					SRLZ_DEBUG( printf( "USRZ THREAD code size mismatch\n" ) );
					goto fail;
				}
				sf->iptr = sgs_func_bytecode( F ) + iptrpos;
				_READ32( ccount ); /* - for validation */
				if( ccount != sgs_func_const_count( F ) )
				{
					SRLZ_DEBUG( printf( "USRZ THREAD constant count mismatch\n" ) );
					goto fail;
				}
			}
			else
			{
				SRLZ_DEBUG( printf( "USRZ THREAD function NOT VT_FUNC (type=%d)\n", sf->func->type ) );
				SRLZ_DEBUG( if( sf->func->type == SGS_VT_OBJECT )
					printf( "^ iface = %s\n", sf->func->data.O->iface->name ) );
				goto fail;
			}
			
			/* 'cptr' will be taken from function */
			/* 'nfname' is irrelevant for non-native functions */
			/* 'prev', 'next', 'cached' are system pointers */
			_READ32( sf->argbeg );
			_READ32( sf->stkoff );
			_READ32( sf->errsup );
			_READ8( sf->argcount );
			_READ8( sf->flags );
		}
	}
	
#undef _READRL
#undef _READ32
#undef _READ8
	
	/* finalize */
	*pbuf = buf;
	sgs_BreakIf( ctx->refcount != 0 );
	*pT = ctx;
	SRLZ_DEBUG( printf( "USRZ thread_unserialize DONE\n" ) );
	return 1;
fail:
	SRLZ_DEBUG( printf( "USRZ thread_unserialize ERROR\n" ) );
	sgsCTX_FreeState( ctx );
	return 0;
}


typedef struct sgs_serialize1_data
{
	int mode;
	int ret;
	sgs_MemBuf data;
}
sgs_serialize1_data;

void sgs_SerializeInt_V1( SGS_CTX, sgs_Variable var )
{
	void* prev_serialize_state = C->serialize_state;
	sgs_serialize1_data SD = { 1, 1, sgs_membuf_create() }, *pSD;
	int ep = !C->serialize_state || *(int*)C->serialize_state != 1;
	
	if( ep )
	{
		C->serialize_state = &SD;
	}
	pSD = (sgs_serialize1_data*) C->serialize_state;
	
	if( var.type == SGS_VT_OBJECT || var.type == SGS_VT_CFUNC ||
		var.type == SGS_VT_FUNC || var.type == SGS_VT_THREAD )
	{
		sgs_Variable sym = sgs_MakeNull();
		if( sgs_GetSymbol( C, var, &sym ) && sym.type == SGS_VT_STRING )
		{
			sgs_SerializeInt_V1( C, sym );
			sgs_membuf_appchr( &pSD->data, C, 'S' );
			sgs_Release( C, &sym );
			goto end;
		}
		sgs_Release( C, &sym );
	}
	
	if( var.type == SGS_VT_OBJECT )
	{
		int parg = C->object_arg;
		sgs_VarObj* O = var.data.O;
		_STACK_PREPARE;
		if( !O->iface->serialize )
		{
			sgs_Msg( C, SGS_ERROR, "cannot serialize object of type '%s'", O->iface->name );
			pSD->ret = SGS_FALSE;
			goto end;
		}
		_STACK_PROTECT;
		C->object_arg = 1;
		pSD->ret = SGS_SUCCEEDED( O->iface->serialize( C, O ) );
		C->object_arg = parg;
		_STACK_UNPROTECT;
		goto end;
	}
	else if( var.type == SGS_VT_CFUNC )
	{
		sgs_Msg( C, SGS_ERROR, "serialization mode 1 does not support C function serialization" );
		pSD->ret = SGS_FALSE;
		goto end;
	}
	else if( var.type == SGS_VT_PTR )
	{
		sgs_Msg( C, SGS_ERROR, "serialization mode 1 does not support pointer serialization" );
		pSD->ret = SGS_FALSE;
		goto end;
	}
	else if( var.type == SGS_VT_THREAD )
	{
		sgs_Msg( C, SGS_ERROR, "serialization mode 1 does not support thread serialization" );
		pSD->ret = SGS_FALSE;
		goto end;
	}
	else
	{
		char pb[2];
		{
			pb[0] = 'P';
			/* WP: basetype limited to bits 0-7, sign interpretation does not matter */
			pb[1] = (char) var.type;
		}
		sgs_membuf_appbuf( &pSD->data, C, pb, 2 );
		switch( var.type )
		{
		case SGS_VT_NULL: break;
		/* WP: var.data.B uses only bit 0 */
		case SGS_VT_BOOL: { char b = (char) var.data.B; sgs_membuf_appbuf( &pSD->data, C, &b, 1 ); } break;
		case SGS_VT_INT: sgs_membuf_appbuf( &pSD->data, C, &var.data.I, sizeof( sgs_Int ) ); break;
		case SGS_VT_REAL: sgs_membuf_appbuf( &pSD->data, C, &var.data.R, sizeof( sgs_Real ) ); break;
		case SGS_VT_STRING:
			sgs_membuf_appbuf( &pSD->data, C, &var.data.S->size, 4 );
			sgs_membuf_appbuf( &pSD->data, C, sgs_var_cstr( &var ), var.data.S->size );
			break;
		case SGS_VT_FUNC:
			{
				size_t szbefore = pSD->data.size;
				if( !_serialize_function( C, var.data.F, &pSD->data ) )
				{
					sgs_Msg( C, SGS_INTERR, "sgs_Serialize: failed to serialize function "
						"(ptr = %p, name = %s, file = %s)", var.data.F,
						sgs_str_cstr( var.data.F->sfuncname ), sgs_str_cstr( var.data.F->sfilename ) );
					pSD->ret = SGS_FALSE;
					goto end;
				}
				else
				{
					uint32_t szdiff = (uint32_t) ( pSD->data.size - szbefore );
					sgs_membuf_insbuf( &pSD->data, C, szbefore, &szdiff, sizeof(szdiff) );
				}
			}
			break;
		default:
			sgs_Msg( C, SGS_INTERR, "sgs_Serialize: unknown memory error" );
			pSD->ret = SGS_FALSE;
			goto end;
		}
	}

end:
	if( ep )
	{
		if( SD.ret )
		{
			if( SD.data.size > 0x7fffffff )
			{
				sgs_Msg( C, SGS_ERROR, "serialized string too long" );
				sgs_PushNull( C );
			}
			else
			{
				/* WP: added error condition */
				sgs_PushStringBuf( C, SD.data.ptr, (sgs_SizeVal) SD.data.size );
			}
		}
		else
		{
			sgs_PushNull( C );
		}
		sgs_membuf_destroy( &SD.data, C );
		C->serialize_state = prev_serialize_state;
	}
}

static void sgs_SerializeObjectInt_V1( SGS_CTX, sgs_StkIdx args, const char* func, size_t fnsize )
{
	sgs_serialize1_data* pSD = (sgs_serialize1_data*) C->serialize_state;
	char pb[7] = { 'C', 0, 0, 0, 0, 0, 0 };
	{
		/* WP: they were pointless */
		pb[1] = (char)((args)&0xff);
		pb[2] = (char)((args>>8)&0xff);
		pb[3] = (char)((args>>16)&0xff);
		pb[4] = (char)((args>>24)&0xff);
		/* WP: have error condition + sign interpretation doesn't matter */
		pb[5] = (char) fnsize;
	}
	
	sgs_membuf_appbuf( &pSD->data, C, pb, 6 );
	sgs_membuf_appbuf( &pSD->data, C, func, fnsize );
	sgs_membuf_appbuf( &pSD->data, C, pb + 6, 1 );
}

static void sgs_SerializeObjIndexInt_V1( SGS_CTX, int isprop )
{
	sgs_serialize1_data* pSD = (sgs_serialize1_data*) C->serialize_state;
	sgs_membuf_appchr( &pSD->data, C, isprop ? '.' : '[' );
}

#define sgs_unserr_incomp( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: incomplete data [L%d]", __LINE__ )
#define sgs_unserr_error( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: error in data [L%d]", __LINE__ )
#define sgs_unserr_objcall( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: could not create object from function [L%d]", __LINE__ )
#define sgs_unserr_symfail( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: could not map name to symbol [L%d]", __LINE__ )

SGSBOOL sgs_UnserializeInt_V1( SGS_CTX, char* str, char* strend )
{
	while( str < strend )
	{
		char c = *str++;
		if( c == 'P' )
		{
			if( str >= strend )
				return sgs_unserr_incomp( C );
			c = *str++;
			switch( c )
			{
			case SGS_VT_NULL: sgs_PushNull( C ); break;
			case SGS_VT_BOOL:
				if( str >= strend )
					return sgs_unserr_incomp( C );
				sgs_PushBool( C, *str++ );
				break;
			case SGS_VT_INT:
				if( str >= strend-7 )
					return sgs_unserr_incomp( C );
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
					return sgs_unserr_incomp( C );
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
						return sgs_unserr_incomp( C );
					SGS_AS_INT32( strsz, str );
					str += 4;
					if( str > strend - strsz )
						return sgs_unserr_incomp( C );
					sgs_PushStringBuf( C, str, strsz );
					str += strsz;
				}
				break;
			case SGS_VT_FUNC:
				{
					sgs_Variable tmp;
					sgs_SizeVal bcsz;
					if( str >= strend-3 )
						return sgs_unserr_incomp( C );
					SGS_AS_INT32( bcsz, str );
					str += 4;
					if( str > strend - bcsz )
						return sgs_unserr_incomp( C );
					/* WP: conversion does not affect values */
					if( !_unserialize_function( C, str, (size_t) bcsz, &tmp ) )
						return SGS_FALSE; /* error already printed */
					fstk_push_leave( C, &tmp );
					str += bcsz;
				}
				break;
			default:
				return sgs_unserr_error( C );
			}
		}
		else if( c == 'C' )
		{
			int32_t argc;
			int fnsz, ret;
			if( str >= strend-4 )
				return sgs_unserr_incomp( C );
			SGS_AS_INT32( argc, str );
			str += 4;
			fnsz = *str++ + 1;
			if( str > strend - fnsz )
				return sgs_unserr_incomp( C );
			ret = sgs_GlobalCall( C, str, argc, 1 );
			if( SGS_FAILED( ret ) )
				return sgs_unserr_objcall( C );
			str += fnsz;
		}
		else if( c == 'T' )
		{
			sgs_Context* T = NULL;
			if( !sgs__thread_unserialize( C, &T, &str, strend ) )
				return sgs_unserr_incomp( C );
			sgs_PushThreadPtr( C, T );
		}
		else if( c == 'S' )
		{
			sgs_Variable sym;
			if( !sgs_GetSymbol( C, sgs_StackItem( C, -1 ), &sym ) )
			{
				return sgs_unserr_symfail( C );
			}
			sgs_Pop( C, 1 );
			fstk_push_leave( C, &sym );
		}
		else if( c == '.' || c == '[' )
		{
			sgs_SetIndex( C, sgs_StackItem( C, -3 ), sgs_StackItem( C, -2 ),
				sgs_StackItem( C, -1 ), c == '.' );
			sgs_Pop( C, 2 );
		}
		else
		{
			return sgs_unserr_error( C );
		}
	}
	return SGS_TRUE;
}


/*
	mode 2 commands:
	P - base type (null/bool/int/real/string/func)
	O - object
	C - single closure
	Q - closure object
	f - closure function
	< - closure variable
	T - thread
	p - set thread parent
	S - symbol
	. - set property
	[ - set index
	R - return value
*/

typedef struct sgs_serialize2_data
{
	int mode;
	int ret;
	sgs_VHTable servartable;
	sgs_MemBuf argarray;
	sgs_VarObj* curObj;
	sgs_MemBuf data;
	int32_t metaObjArg;
}
sgs_serialize2_data;

#define SGS_VTSPC_CLOSURE 0xf0

static void srlz_mode2_addvar( SGS_CTX, sgs_serialize2_data* pSD, sgs_Variable* pvar )
{
	sgs_Variable idxvar;
	uint32_t argidx = (uint32_t) sgs_vht_size( &pSD->servartable );
	idxvar.type = SGS_VT_INT;
	idxvar.data.I = argidx;
	sgs_vht_set( &pSD->servartable, C, pvar, &idxvar );
	sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
}

void sgs_SerializeInt_V2( SGS_CTX, sgs_Variable var )
{
	void* prev_serialize_state = C->serialize_state;
	sgs_serialize2_data SD, *pSD;
	int ep = !C->serialize_state || *(int*)C->serialize_state != 2;
	
	if( ep )
	{
		SRLZ_DEBUG( printf( "SRLZ == mode 2 START ==\n") );
		
		SD.mode = 2;
		SD.ret = 1;
		sgs_vht_init( &SD.servartable, C, 64, 64 );
		SD.argarray = sgs_membuf_create();
		SD.curObj = NULL;
		SD.data = sgs_membuf_create();
		C->serialize_state = &SD;
	}
	pSD = (sgs_serialize2_data*) C->serialize_state;
	
	SRLZ_DEBUG( printf( "SRLZ var type=%d\n", (int) var.type ) );
	SRLZ_DEBUG( if( var.type == SGS_VT_OBJECT ){
		printf( "- object type: %s\n", var.data.O->iface->name ); } );
	
	/* if variable was repeated, push the index of it on argument stack */
	{
		sgs_VHTVar* vv = sgs_vht_get( &pSD->servartable, &var );
		if( vv )
		{
			uint32_t argidx = (uint32_t) ( vv - pSD->servartable.vars );
			
			SRLZ_DEBUG( printf( "SRLZ repeated variable\n" ) );
			
			sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
			goto end;
		}
	}
	
	if( var.type == SGS_VT_OBJECT || var.type == SGS_VT_CFUNC ||
		var.type == SGS_VT_FUNC || var.type == SGS_VT_THREAD )
	{
		sgs_Variable sym = sgs_MakeNull();
		if( sgs_GetSymbol( C, var, &sym ) && sym.type == SGS_VT_STRING )
		{
			SRLZ_DEBUG( printf( "SRLZ new symbol\n" ) );
			
			sgs_SerializeInt_V2( C, sym );
			if( pSD->argarray.size < 4 )
			{
				/* error likely to be already printed */
				pSD->ret = SGS_FALSE;
				goto end;
			}
			
			SRLZ_DEBUG( printf( "SRLZ emit symbol\n" ) );
			
			sgs_membuf_appchr( &pSD->data, C, 'S' );
			sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 4, 4 );
			sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - 4, pSD->argarray.size );
			sgs_Release( C, &sym );
			
			goto addvar;
		}
		sgs_Release( C, &sym );
	}
	
	if( var.type == SGS_VTSPC_CLOSURE )
	{
		sgs_Closure* cl = (sgs_Closure*) var.data.P;
		
		SRLZ_DEBUG( printf( "SRLZ new closure\n" ) );
		
		sgs_SerializeInt_V2( C, cl->var );
		if( pSD->argarray.size < 4 )
		{
			/* error likely to be already printed */
			pSD->ret = SGS_FALSE;
			goto end;
		}
		
		SRLZ_DEBUG( printf( "SRLZ emit closure\n" ) );
		
		sgs_membuf_appchr( &pSD->data, C, 'C' );
		sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 4, 4 );
		sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - 4, pSD->argarray.size );
	}
	else if( var.type == SGS_VT_THREAD )
	{
		int32_t parent_th_idx = 0;
		sgs_Context* parent;
		
		SRLZ_DEBUG( printf( "SRLZ new thread (%p)\n", var.data.T ) );
		
		parent = var.data.T->parent;
		if( parent && parent != sgs_RootContext( C ) )
		{
			sgs_Variable pvT;
			pvT.type = SGS_VT_THREAD;
			pvT.data.T = parent;
			
			SRLZ_DEBUG( printf( "SRLZ thread has non-root parent (%p)\n", parent ) );
			
			sgs_VHTVar* vv = sgs_vht_get( &pSD->servartable, &pvT );
			if( vv )
			{
				parent_th_idx = (int32_t) ( vv - pSD->servartable.vars );
			}
			else
			{
				sgs_Msg( C, SGS_ERROR, "thread has an unserialized non-root parent" );
				pSD->ret = SGS_FALSE;
				goto end;
			}
		}
		
		if( !sgs__thread_serialize( C, var.data.T, &pSD->data, &pSD->argarray ) )
		{
			sgs_Msg( C, SGS_ERROR, "failed to serialize thread" );
			pSD->ret = SGS_FALSE;
			goto end;
		}
		
		srlz_mode2_addvar( C, pSD, &var );
		if( parent )
		{
			SRLZ_DEBUG( printf( "SRLZ new thread parent (%p)\n", parent ) );
			
			sgs_membuf_appchr( &pSD->data, C, 'p' );
			sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 4, 4 );
			if( parent == sgs_RootContext( C ) )
			{
				sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 4, 4 );
			}
			else
			{
				sgs_membuf_appbuf( &pSD->data, C, &parent_th_idx, sizeof(parent_th_idx) );
			}
		}
		
		/* subthreads */
		{
			size_t argarrsize = pSD->argarray.size;
			sgs_Context* subT = var.data.T->subthreads;
			while( subT )
			{
				sgs_Variable svT;
				svT.type = SGS_VT_THREAD;
				svT.data.T = subT;
				sgs_SerializeInt_V2( C, svT );
				subT = subT->st_next;
			}
			sgs_membuf_erase( &pSD->argarray, argarrsize, pSD->argarray.size );
		}
		goto end; /* variable added already, must skip that step */
	}
	else if( var.type == SGS_VT_OBJECT )
	{
		int parg = C->object_arg;
		int32_t mo_arg = -1, prev_mo_arg = pSD->metaObjArg;
		sgs_VarObj* O = var.data.O, *MO = sgs_ObjGetMetaObj( var.data.O );
		sgs_VarObj* prevObj = pSD->curObj;
		_STACK_PREPARE;
		
		if( O->iface == sgsstd_closure_iface )
		{
			/* serialize closure (data=sgs_Variable func,sgs_clsrcount_t n,sgs_Closure*[n]) */
			char* ptr;
			sgs_clsrcount_t i, count;
			sgs_Closure** clsrlist;
			
			SRLZ_DEBUG( printf( "SRLZ new obj/closure\n" ) );
			
			ptr = (char*) O->data;
			count = *SGS_ASSUME_ALIGNED( ptr + sizeof(sgs_Variable), sgs_clsrcount_t );
			clsrlist = SGS_ASSUME_ALIGNED( ptr + sizeof(sgs_Variable) + sizeof(sgs_clsrcount_t), sgs_Closure* );
			
			/* write closure */
			{
				int32_t args = (int32_t) count;
				char pb[5] = { 'Q', 0 };
				{
					/* WP: they were pointless */
					pb[1] = (char)((args)&0xff);
					pb[2] = (char)((args>>8)&0xff);
					pb[3] = (char)((args>>16)&0xff);
					pb[4] = (char)((args>>24)&0xff);
				}
				
				SRLZ_DEBUG( printf( "SRLZ emit obj/closure (%d args)\n", (int) args ) );
				
				sgs_membuf_appbuf( &pSD->data, C, pb, 5 );
			}
			srlz_mode2_addvar( C, pSD, &var );
			
			SRLZ_DEBUG( printf( "SRLZ obj/closure > func\n" ) );
			sgs_SerializeInt_V2( C, *(sgs_Variable*) O->data );
			
			SRLZ_DEBUG( printf( "SRLZ emit obj/closure [f]unc\n" ) );
			sgs_membuf_appchr( &pSD->data, C, 'f' );
			sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 8, 8 );
			sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - 4, pSD->argarray.size );
			
			for( i = 0; i < count; ++i )
			{
				uint32_t idx = (uint32_t) i;
				sgs_Variable tmp;
				
				SRLZ_DEBUG( printf( "SRLZ obj/closure > closure %d\n", (int) i ) );
				
				tmp.type = SGS_VTSPC_CLOSURE;
				tmp.data.P = clsrlist[ i ];
				sgs_SerializeInt_V2( C, tmp );
				
				SRLZ_DEBUG( printf( "SRLZ emit obj/closure [<] argument variable\n" ) );
				
				sgs_membuf_appchr( &pSD->data, C, '<' );
				sgs_membuf_appbuf( &pSD->data, C, &idx, sizeof(idx) );
				sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 8, 8 );
				sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - 4, pSD->argarray.size );
			}
			
			goto end;
		}
		else
		{
			SRLZ_DEBUG( printf( "SRLZ new object (%p/%p/%s)\n", O->data, O->iface, O->iface->name ) );
			
			if( MO )
			{
				size_t origsize = pSD->argarray.size;
				(void) origsize;
				
				SRLZ_DEBUG( printf( "SRLZ obj metaobj\n" ) );
				
				sgs_SerializeInt_V2( C, sgs_MakeObjPtrNoRef( MO ) );
				/* save position of target object */
				SGS_AS_INT32( mo_arg, pSD->argarray.ptr + pSD->argarray.size - 4 );
				/* remove the meta-object from arguments */
				sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - 4, pSD->argarray.size );
				/* check if argument array size was not changed */
				sgs_BreakIf( origsize != pSD->argarray.size );
			}
			
			if( !O->iface->serialize )
			{
				SRLZ_DEBUG( printf( "- no callback\n" ) );
				
				sgs_Msg( C, SGS_WARNING, "Cannot serialize object of type '%s'", O->iface->name );
				pSD->ret = SGS_FALSE;
				goto end;
			}
			pSD->curObj = O;
			_STACK_PROTECT;
			C->object_arg = 2;
			pSD->metaObjArg = mo_arg;
			pSD->ret = SGS_SUCCEEDED( O->iface->serialize( C, O ) );
			pSD->metaObjArg = prev_mo_arg;
			C->object_arg = parg;
			_STACK_UNPROTECT;
			pSD->curObj = prevObj;
			/* variable was added in SerializeObject */
			goto end;
		}
	}
	else if( var.type == SGS_VT_CFUNC )
	{
		SRLZ_DEBUG( printf( "SRLZ cfunc BAD\n" ) );
		sgs_Msg( C, SGS_WARNING, "Cannot serialize C functions" );
		pSD->ret = SGS_FALSE;
		goto end;
	}
	else if( var.type == SGS_VT_PTR )
	{
		SRLZ_DEBUG( printf( "SRLZ ptr BAD\n" ) );
		sgs_Msg( C, SGS_WARNING, "Cannot serialize pointers" );
		pSD->ret = SGS_FALSE;
		goto end;
	}
	else
	{
		char pb[2];
		{
			pb[0] = 'P';
			/* WP: basetype limited to bits 0-7, sign interpretation does not matter */
			pb[1] = (char) var.type;
		}
		
		SRLZ_DEBUG( printf( "SRLZ new/emit basic type (%d)\n", (int) var.type ) );
		
		sgs_membuf_appbuf( &pSD->data, C, pb, 2 );
		switch( var.type )
		{
		case SGS_VT_NULL: break;
		/* WP: var.data.B uses only bit 0 */
		case SGS_VT_BOOL: { char b = (char) var.data.B; sgs_membuf_appbuf( &pSD->data, C, &b, 1 ); } break;
		case SGS_VT_INT: sgs_membuf_appbuf( &pSD->data, C, &var.data.I, sizeof( sgs_Int ) ); break;
		case SGS_VT_REAL: sgs_membuf_appbuf( &pSD->data, C, &var.data.R, sizeof( sgs_Real ) ); break;
		case SGS_VT_STRING:
			sgs_membuf_appbuf( &pSD->data, C, &var.data.S->size, 4 );
			sgs_membuf_appbuf( &pSD->data, C, sgs_var_cstr( &var ), var.data.S->size );
			break;
		case SGS_VT_FUNC:
			{
				size_t szbefore = pSD->data.size;
				if( !_serialize_function( C, var.data.F, &pSD->data ) )
				{
					pSD->ret = SGS_FALSE;
					goto end;
				}
				else
				{
					uint32_t szdiff = (uint32_t) ( pSD->data.size - szbefore );
					sgs_membuf_insbuf( &pSD->data, C, szbefore, &szdiff, sizeof(szdiff) );
				}
			}
			break;
		default:
			sgs_Msg( C, SGS_ERROR, "sgs_Serialize: unknown memory error" );
			pSD->ret = SGS_FALSE;
			goto end;
		}
	}
	
addvar:
	srlz_mode2_addvar( C, pSD, &var );
end:
	if( ep )
	{
		SRLZ_DEBUG( printf( "SRLZ -- mode 2 AT END --\n" ) );
		
		if( SD.argarray.size == 0 )
			SD.ret = 0;
		else
		{
			SRLZ_DEBUG( printf( "SRLZ emit return\n" ) );
			
			sgs_membuf_appchr( &SD.data, C, 'R' );
			sgs_membuf_appbuf( &SD.data, C, SD.argarray.ptr + SD.argarray.size - 4, 4 );
		}
		if( SD.ret )
		{
			if( SD.data.size > 0x7fffffff )
			{
				sgs_Msg( C, SGS_ERROR, "serialized string too long" );
				SD.ret = SGS_FALSE;
			}
			else
			{
				/* WP: added error condition */
				sgs_PushStringBuf( C, SD.data.ptr, (sgs_SizeVal) SD.data.size );
			}
		}
		if( SD.ret == SGS_FALSE )
		{
			sgs_PushNull( C );
		}
		sgs_vht_free( &SD.servartable, C );
		sgs_membuf_destroy( &SD.argarray, C );
		sgs_membuf_destroy( &SD.data, C );
		C->serialize_state = prev_serialize_state;
	}
}

void sgs_SerializeObjectInt_V2( SGS_CTX, sgs_StkIdx args, const char* func, size_t fnsize )
{
	size_t argsize;
	sgs_serialize2_data* pSD = (sgs_serialize2_data*) C->serialize_state;
	
	if( args < 0 || (size_t) args > pSD->argarray.size / sizeof(sgs_StkIdx) )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObject: specified "
			"more arguments than there are serialized items" );
		return;
	}
	
	{
		char pb[11] = { 'O', 0 };
		{
			/* WP: they were pointless */
			pb[1] = (char)((args)&0xff);
			pb[2] = (char)((args>>8)&0xff);
			pb[3] = (char)((args>>16)&0xff);
			pb[4] = (char)((args>>24)&0xff);
			/* WP: have error condition + sign interpretation doesn't matter */
			pb[5] = (char) fnsize;
			pb[6] = (char) sgs_ObjGetMetaMethodEnable( pSD->curObj );
			memcpy( &pb[7], &pSD->metaObjArg, 4 );
		}
		
		sgs_membuf_appbuf( &pSD->data, C, pb, 11 );
	}
	
	/* write args */
	argsize = sizeof(sgs_StkIdx) * (size_t) args; /* WP: added error condition */
	sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - argsize, argsize );
	/* pop written args */
	sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - argsize, pSD->argarray.size );
	/* write function name */
	sgs_membuf_appbuf( &pSD->data, C, func, fnsize );
	sgs_membuf_appchr( &pSD->data, C, '\0' );
	/* register the object variable */
	{
		sgs_Variable objvar;
		objvar.type = SGS_VT_OBJECT;
		objvar.data.O = pSD->curObj;
		srlz_mode2_addvar( C, pSD, &objvar );
	}
}

static void sgs_SerializeObjIndexInt_V2( SGS_CTX, int isprop )
{
	sgs_serialize2_data* pSD = (sgs_serialize2_data*) C->serialize_state;
	
	if( 3 > pSD->argarray.size / sizeof(sgs_StkIdx) )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObjIndex: less than 3 serialized items found" );
		return;
	}
	/* expecting 3 arguments: object, key, value */
	sgs_membuf_appchr( &pSD->data, C, isprop ? '.' : '[' );
	sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 4 * 3, 4 * 3 );
	/* remove only 2 of them */
	sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - 4 * 2, pSD->argarray.size );
}

SGSBOOL sgs_UnserializeInt_V2( SGS_CTX, char* str, char* strend )
{
	int32_t retpos = -1;
	sgs_Variable var;
	SGSBOOL res = SGS_FALSE;
	sgs_MemBuf mb = sgs_membuf_create();
	_STACK_PREPARE;
	
	SRLZ_DEBUG( printf( "USRZ --- mode 2 START ---\n" ) );
	
	_STACK_PROTECT;
	while( str < strend )
	{
		char c = *str++;
		if( c == 'P' )
		{
			SRLZ_DEBUG( printf( "USRZ found [P]lain variable\n" ) );
			
			if( str >= strend && !sgs_unserr_incomp( C ) )
				goto fail;
			c = *str++;
			SRLZ_DEBUG( printf( "- type %d\n", c ) );
			switch( c )
			{
			case SGS_VT_NULL: var.type = SGS_VT_NULL; break;
			case SGS_VT_BOOL:
				if( str >= strend && !sgs_unserr_incomp( C ) )
					goto fail;
				var.type = SGS_VT_BOOL;
				var.data.B = *str++ != 0;
				break;
			case SGS_VT_INT:
				if( str >= strend-7 && !sgs_unserr_incomp( C ) )
					goto fail;
				else
				{
					sgs_Int val;
					SGS_AS_INTEGER( val, str );
					var.type = SGS_VT_INT;
					var.data.I = val;
				}
				str += 8;
				break;
			case SGS_VT_REAL:
				if( str >= strend-7 && !sgs_unserr_incomp( C ) )
					goto fail;
				else
				{
					sgs_Real val;
					SGS_AS_REAL( val, str );
					var.type = SGS_VT_REAL;
					var.data.R = val;
				}
				str += 8;
				break;
			case SGS_VT_STRING:
				{
					sgs_SizeVal strsz;
					if( str >= strend-3 && !sgs_unserr_incomp( C ) )
						goto fail;
					SGS_AS_INT32( strsz, str );
					str += 4;
					if( NUMBER_OUT_OF_RANGE( strsz, strend - str ) && !sgs_unserr_incomp( C ) )
						goto fail;
					sgs_InitStringBuf( C, &var, str, strsz );
					str += strsz;
				}
				break;
			case SGS_VT_FUNC:
				{
					sgs_SizeVal bcsz;
					if( str >= strend-3 && !sgs_unserr_incomp( C ) )
						goto fail;
					SGS_AS_INT32( bcsz, str );
					str += 4;
					if( str > strend - bcsz && !sgs_unserr_incomp( C ) )
						goto fail;
					/* WP: conversion does not affect values */
					if( !_unserialize_function( C, str, (size_t) bcsz, &var ) )
						goto fail; /* error already printed */
					str += bcsz;
				}
				break;
			default:
				sgs_unserr_error( C );
				goto fail;
			}
		}
		else if( c == 'O' )
		{
			sgs_StkIdx subsz;
			int32_t i, pos, argc, mo_arg;
			int fnsz, mm_enable, ret;
			
			SRLZ_DEBUG( printf( "USRZ found [O]bject\n" ) );
			
			if( str > strend-5 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( argc, str );
			SRLZ_DEBUG( printf( "- %d args\n", argc ) );
			str += 4;
			fnsz = *str++ + 1;
			mm_enable = *str++ != 0;
			SGS_AS_INT32( mo_arg, str );
			str += 4;
			for( i = 0; i < argc; ++i )
			{
				if( str > strend-4 && !sgs_unserr_incomp( C ) )
					goto fail;
				SGS_AS_INT32( pos, str );
				str += 4;
				if( pos < 0 || (size_t) pos >= mb.size / sizeof(sgs_Variable) )
				{
					sgs_unserr_error( C );
					goto fail;
				}
				sgs_PushVariable( C, (SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ pos ] );
			}
			if( str > strend - fnsz && !sgs_unserr_incomp( C ) )
				goto fail;
			
			SRLZ_DEBUG( printf( "- args=%d mmenable=%s func=%s\n", argc, mm_enable ? "Y" : "n", str ) );
			
			subsz = sgs_StackSize( C ) - argc;
			ret = SGS_SUCCEEDED( sgs_GlobalCall( C, str, argc, 1 ) );
			if( ret == SGS_FALSE || sgs_StackSize( C ) - subsz < 1 || sgs_ItemType( C, -1 ) != SGS_VT_OBJECT )
			{
				sgs_unserr_objcall( C );
				goto fail;
			}
			sgs_GetStackItem( C, -1, &var );
			sgs_ObjSetMetaMethodEnable( var.data.O, mm_enable );
			if( mo_arg < 0 )
				sgs_ObjSetMetaObj( C, var.data.O, NULL );
			else /* if( c == '3' ) */
			{
				sgs_Variable* mov;
				/* add meta-object */
				if( mo_arg < 0 || mo_arg >= (int32_t) ( mb.size / sizeof( sgs_Variable ) ) )
				{
					sgs_unserr_error( C );
					goto fail;
				}
				mov = &(SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ mo_arg ];
				if( mov->type != SGS_VT_OBJECT && !sgs_unserr_error( C ) )
					goto fail;
				if( mov->data.O == var.data.O && !sgs_unserr_error( C ) )
					goto fail;
				sgs_ObjSetMetaObj( C, var.data.O, mov->data.O );
			}
			sgs_SetStackSize( C, subsz );
			str += fnsz;
		}
		else if( c == 'C' )
		{
			/* single closure (sgs_Closure* in P) */
			sgs_Closure* clsr;
			int32_t pos;
			
			SRLZ_DEBUG( printf( "USRZ found single [C]losure\n" ) );
			
			if( str > strend-4 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( pos, str );
			
			SRLZ_DEBUG( printf( "- var=%d\n", (int) pos ) );
			
			str += 4;
			clsr = sgs_Alloc( sgs_Closure );
			clsr->refcount = 0;
			clsr->var = (SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ pos ];
			sgs_Acquire( C, &clsr->var );
			var.type = SGS_VTSPC_CLOSURE;
			var.data.P = clsr;
		}
		else if( c == 'Q' )
		{
			/* closure object */
			int32_t argc;
			sgs_Variable funcvar;
			
			SRLZ_DEBUG( printf( "USRZ found [Q] (closure object)\n" ) );
			
			if( str > strend-4 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( argc, str );
			str += 4;
			
			SRLZ_DEBUG( printf( "- %d args\n", argc ) );
			
			/* create the closure object */
			funcvar.type = SGS_VT_NULL;
			sgsSTD_MakeClosure( C, &var, &funcvar, (size_t) argc );
		}
		else if( c == 'f' )
		{
			sgs_Variable *cvp, *fvp;
			int32_t pos, func;
			
			SRLZ_DEBUG( printf( "USRZ found [f] (closure function)\n" ) );
			
			if( str > strend-8 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( pos, str ); str += 4;
			SGS_AS_INT32( func, str ); str += 4;
			
			SRLZ_DEBUG( printf( "- closure=%d func=%d\n", pos, func ) );
			
			if( pos < 0 || (size_t) pos >= mb.size / sizeof(sgs_Variable) ||
				func < 0 || (size_t) func >= mb.size / sizeof(sgs_Variable) )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			
			cvp = &(SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ pos ];
			fvp = &(SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ func ];
			if( cvp->type != SGS_VT_OBJECT || cvp->data.O->iface != sgsstd_closure_iface )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			*(sgs_Variable*)cvp->data.O->data = *fvp;
			VAR_ACQUIRE( fvp );
			continue;
		}
		else if( c == '<' )
		{
			sgs_Variable *cvp, *avp;
			int32_t which, cvpos, avpos;
			
			SRLZ_DEBUG( printf( "USRZ found [<] (closure variable)\n" ) );
			
			if( str > strend-12 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( which, str ); str += 4;
			SGS_AS_INT32( cvpos, str ); str += 4;
			SGS_AS_INT32( avpos, str ); str += 4;
			
			SRLZ_DEBUG( printf( "- closure=%d arg=%d avpos=%d\n", cvpos, which, avpos ) );
			
			if( cvpos < 0 || (size_t) cvpos >= mb.size / sizeof(sgs_Variable) ||
				avpos < 0 || (size_t) avpos >= mb.size / sizeof(sgs_Variable) )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			
			cvp = &(SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ cvpos ];
			avp = &(SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ avpos ];
			if( cvp->type != SGS_VT_OBJECT || cvp->data.O->iface != sgsstd_closure_iface ||
				avp->type != SGS_VTSPC_CLOSURE )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			
			{
				char* ptr = (char*) cvp->data.O->data;
				sgs_clsrcount_t count = *SGS_ASSUME_ALIGNED( ptr + sizeof(sgs_Variable), sgs_clsrcount_t );
				sgs_Closure** clsrlist = SGS_ASSUME_ALIGNED( ptr + sizeof(sgs_Variable) + sizeof(sgs_clsrcount_t), sgs_Closure* );
				if( which < 0 || (sgs_clsrcount_t) which >= count )
				{
					sgs_unserr_error( C );
					goto fail;
				}
				clsrlist[ which ] = (sgs_Closure*) avp->data.P;
				clsrlist[ which ]->refcount++;
			}
			continue;
		}
		else if( c == 'T' )
		{
			int32_t i, pos, argc;
			sgs_Context* T = NULL;
			
			SRLZ_DEBUG( printf( "USRZ found [T]hread\n" ) );
			
			if( str > strend-5 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( argc, str );
			SRLZ_DEBUG( printf( "- %d args\n", argc ) );
			str += 4;
			for( i = 0; i < argc; ++i )
			{
				if( str > strend-4 && !sgs_unserr_incomp( C ) )
					goto fail;
				SGS_AS_INT32( pos, str );
				
				SRLZ_DEBUG( printf( "- arg %d = %d\n", (int) i, (int) pos ) );
				
				str += 4;
				if( pos < 0 || (size_t) pos >= mb.size / sizeof(sgs_Variable) )
				{
					sgs_unserr_error( C );
					goto fail;
				}
				sgs_PushVariable( C, (SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ pos ] );
			}
			if( !sgs__thread_unserialize( C, &T, &str, strend ) && !sgs_unserr_incomp( C ) )
				goto fail;
			sgs_Pop( C, argc );
			sgs_InitThreadPtr( &var, T );
		}
		else if( c == 'p' )
		{
			sgs_Context *T, *P;
			int32_t thread, parent;
			
			sgs_Variable* varlist = SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable );
			
			SRLZ_DEBUG( printf( "USRZ found [p]arent of a thread\n" ) );
			
			if( str > strend-8 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( thread, str );
			SGS_AS_INT32( parent, str+4 );
			str += 8;
			
			SRLZ_DEBUG( printf( "- thread=%d parent=%d\n", (int) thread, (int) parent ) );
			
			if( thread < 0 || thread >= (int32_t) ( mb.size / sizeof( sgs_Variable ) ) ||
				parent < 0 || parent >= (int32_t) ( mb.size / sizeof( sgs_Variable ) ) ||
				varlist[ thread ].type != SGS_VT_THREAD ||
				varlist[ parent ].type != SGS_VT_THREAD )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			
			T = varlist[ thread ].data.T;
			P = thread == parent ? sgs_RootContext( C ) : varlist[ parent ].data.T;
			T->parent = P;
			T->st_next = P->subthreads;
			P->subthreads = T;
			T->refcount++;
			continue;
		}
		else if( c == 'S' )
		{
			int32_t pos;
			
			SRLZ_DEBUG( printf( "USRZ found [S]ymbol\n" ) );
			
			if( str > strend-4 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( pos, str );
			str += 4;
			
			SRLZ_DEBUG( printf( "- var=%d\n", (int) pos ) );
			
			if( pos < 0 || pos >= (int32_t) ( mb.size / sizeof( sgs_Variable ) ) )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			if( !sgs_GetSymbol( C, (SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ pos ], &var ) )
			{
				sgs_unserr_symfail( C );
				goto fail;
			}
		}
		else if( c == '.' || c == '[' )
		{
			int32_t pobj, pkey, pval, pend = (int32_t) ( mb.size / sizeof( sgs_Variable ) );
			sgs_Variable* varlist = SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable );
			
			SRLZ_DEBUG( printf( "USRZ found [%c] (post-%s)\n", c, c == '.' ? "properties" : "indices" ) );
			
			if( str > strend-12 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( pobj, str );
			SGS_AS_INT32( pkey, str + 4 );
			SGS_AS_INT32( pval, str + 8 );
			str += 12;
			if( pobj < 0 || pobj >= pend ||
				pkey < 0 || pkey >= pend ||
				pval < 0 || pval >= pend )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			sgs_SetIndex( C, varlist[ pobj ], varlist[ pkey ], varlist[ pval ], c == '.' );
			continue;
		}
		else if( c == 'R' )
		{
			SRLZ_DEBUG( printf( "USRZ found [R]eturn value\n" ) );
			
			if( str > strend-4 && !sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( retpos, str );
			str += 4;
			
			SRLZ_DEBUG( printf( "- var=%d\n", (int) retpos ) );
			
			if( retpos < 0 || retpos >= (int32_t) ( mb.size / sizeof( sgs_Variable ) ) )
			{
				sgs_unserr_error( C );
				goto fail;
			}
			continue;
		}
		else
		{
			sgs_unserr_error( C );
			goto fail;
		}
		SRLZ_DEBUG( printf( "^ new var %d (type=%d)\n", (int)( mb.size / sizeof(var) ), var.type ) );
		SRLZ_DEBUG( if(var.type == SGS_VT_OBJECT) printf( "^ iface = %s\n", var.data.O->iface->name ) );
		sgs_membuf_appbuf( &mb, C, &var, sizeof(var) );
	}
	
	SRLZ_DEBUG( printf( "USRZ === mode 2 END ===\n" ) );
	if( mb.size )
	{
		if( retpos >= 0 )
			sgs_PushVariable( C, (SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable ))[ retpos ] );
		else
			sgs_PushVariable( C, *SGS_ASSUME_ALIGNED( mb.ptr + mb.size - sizeof(sgs_Variable), sgs_Variable ) );
	}
	res = mb.size != 0;
fail:
	_STACK_UNPROTECT_SKIP( res );
	{
		sgs_Variable* ptr = SGS_ASSUME_ALIGNED( mb.ptr, sgs_Variable );
		sgs_Variable* pend = SGS_ASSUME_ALIGNED( mb.ptr + mb.size, sgs_Variable );
		while( ptr < pend )
		{
			sgs_Release( C, ptr++ );
		}
	}
	sgs_membuf_destroy( &mb, C );
	return res;
}


/* Serialization mode 3 - text
	- [...] - array
	- {...} - dict
	- map{...} - map
	- <ident>(...) - custom object
*/
typedef struct s3callinfo
{
	sgs_Variable func_name; /* string */
	int32_t arg_offset;
	int32_t arg_count;
}
s3callinfo;

typedef struct sgs_serialize3_data
{
	int mode;
	/* serialized variable set, also maps objects to call info, offset at value.data.I */
	sgs_VHTable servartable;
	/* s3callinfo[], contains function call info */
	sgs_MemBuf callinfo;
	/* int32_t[], contains function call arguments, first after offset: int count */
	sgs_MemBuf callargs;
	/* sgs_Variable[], contains arguments for sgs_SerializeObject */
	sgs_MemBuf argarray;
	/* the current serialized object */
	sgs_VarObj* curObj;
	/* output byte array */
	sgs_MemBuf data;
}
sgs_serialize3_data;

#define sgson_tab( buf, C, depth, tab, tablen ) if( tab ){ \
	int _i = depth; \
	sgs_membuf_appchr( buf, C, '\n' ); \
	while( _i-- > 0 ) sgs_membuf_appbuf( buf, C, tab, (size_t) tablen ); }

#define sgs_tohex( c ) ("0123456789ABCDEF"[ (c) & 0x0f ])

#define sgs_isid( c ) ( sgs_isalnum( c ) || (c) == '_' )

static int sgson_encode_var( SGS_CTX, sgs_serialize3_data* data,
	int depth, const char* tab, sgs_SizeVal tablen )
{
	sgs_MemBuf* buf = &data->data;
	sgs_Variable var = sgs_StackItem( C, -1 );
	switch( var.type )
	{
	case SGS_VT_NULL:
		sgs_membuf_appbuf( buf, C, "null", 4 );
		return 1;
	case SGS_VT_BOOL:
		sgs_membuf_appbuf( buf, C, var.data.B ? "true" : "false", var.data.B ? 4 : 5 );
		return 1;
	case SGS_VT_INT:
		{
			char tmp[ 64 ];
			sprintf( tmp, "%" PRId64, var.data.I );
			sgs_membuf_appbuf( buf, C, tmp, strlen( tmp ) );
			return 1;
		}
	case SGS_VT_REAL:
		{
			char tmp[ 64 ];
			snprintf( tmp, 63, "%g", var.data.R );
			sgs_membuf_appbuf( buf, C, tmp, strlen( tmp ) );
			return 1;
		}
	case SGS_VT_STRING:
		{
			char* str = sgs_GetStringPtr( C, -1 );
			char* frm = str, *end = str + sgs_GetStringSize( C, -1 );
			sgs_membuf_appchr( buf, C, '"' );
			{
				while( str < end )
				{
					if( *str == '"' || *str == '\\' )
					{
						char pp[2];
						{
							pp[0] = '\\';
							pp[1] = *str;
						}
						if( str != frm )
							sgs_membuf_appbuf( buf, C, frm, (size_t) ( str - frm ) );
						sgs_membuf_appbuf( buf, C, pp, 2 );
						frm = str + 1;
					}
					else if( *str < 0x20 || *str == 0x7f )
					{
						size_t len = 2;
						char pp[4];
						pp[0] = '\\';
						if( *str == '\n' ){ pp[1] = 'n'; }
						else if( *str == '\r' ){ pp[1] = 'r'; }
						else if( *str == '\t' ){ pp[1] = 't'; }
						else
						{
							pp[1] = 'x';
							pp[2] = sgs_tohex( *str >> 4 );
							pp[3] = sgs_tohex( *str );
							len = 4;
						}
						if( str != frm )
							sgs_membuf_appbuf( buf, C, frm, (size_t) ( str - frm ) );
						sgs_membuf_appbuf( buf, C, pp, len );
						frm = str + 1;
					}
					str++;
				}
				if( str != frm )
					sgs_membuf_appbuf( buf, C, frm, (size_t) ( str - frm ) );
			}
			sgs_membuf_appchr( buf, C, '"' );
			return 1;
		}
	case SGS_VT_FUNC:
	case SGS_VT_CFUNC:
	case SGS_VT_PTR:
	case SGS_VT_THREAD:
	case SGS_VT_OBJECT:
		/* if one of these exists, it has call info */
		{
			int32_t call_info_index, *args, i;
			s3callinfo* ci;
			const char* fn_name;
			size_t fn_size;
			
			sgs_VHTVar* vv = sgs_vht_get( &data->servartable, &var );
			sgs_BreakIf( vv == NULL );
			
			if( vv->val.type != SGS_VT_INT )
			{
				/* error probably already printed */
				sgs_membuf_appbuf( buf, C, "null", 4 );
				return 1;
			}
			
			call_info_index = (int32_t) vv->val.data.I;
			ci = &(SGS_ASSUME_ALIGNED( data->callinfo.ptr, s3callinfo ))[ call_info_index ];
			args = &(SGS_ASSUME_ALIGNED( data->callargs.ptr, int32_t ))[ ci->arg_offset ];
			fn_name = sgs_var_cstr( &ci->func_name );
			fn_size = (size_t) ci->func_name.data.S->size;
			
			if( fn_size == 5 && !strcmp( fn_name, "array" ) )
			{
				sgs_membuf_appchr( buf, C, '[' );
				depth++;
				for( i = 0; i < ci->arg_count; ++i )
				{
					if( i )
						sgs_membuf_appchr( buf, C, ',' );
					sgson_tab( buf, C, depth, tab, tablen );
					
					sgs_PushVariable( C, data->servartable.vars[ args[ i ] ].key );
					if( !sgson_encode_var( C, data, depth, tab, tablen ) )
						return 0;
					sgs_Pop( C, 1 );
				}
				depth--;
				if( ci->arg_count )
					sgson_tab( buf, C, depth, tab, tablen );
				sgs_membuf_appchr( buf, C, ']' );
			}
			else if( ( fn_size == 4 && !strcmp( fn_name, "dict" ) )
				|| ( fn_size == 3 && !strcmp( fn_name, "map" ) ) )
			{
				if( fn_size == 3 ) /* if map */
					sgs_membuf_appbuf( buf, C, fn_name, fn_size );
				sgs_membuf_appchr( buf, C, '{' );
				depth++;
				for( i = 0; i < ci->arg_count; i += 2 )
				{
					if( i )
						sgs_membuf_appchr( buf, C, ',' );
					sgson_tab( buf, C, depth, tab, tablen );
					
					/* key */
					{
						sgs_Variable key = data->servartable.vars[ args[ i ] ].key;
						int wrotekey = 0;
						/* small identifier optimization */
						if( key.type == SGS_VT_STRING )
						{
							char* str = sgs_GetStringPtrP( &key );
							char* end = str + sgs_GetStringSizeP( &key );
							if( end - str <= 32 && end - str > 0 && ( *str == '_' || sgs_isalpha( *str ) ) )
							{
								char* cc = str + 1;
								while( cc < end )
								{
									if( !sgs_isalnum( *cc ) && *cc != '_' )
										break;
									cc++;
								}
								if( cc == end )
								{
									/* only small identifiers */
									sgs_membuf_appbuf( buf, C, str, (size_t)( end - str ) );
									wrotekey = 1;
								}
							}
						}
						
						if( wrotekey == 0 )
						{
							if( key.type != SGS_VT_STRING )
								sgs_membuf_appchr( buf, C, '[' );
							
							sgs_PushVariable( C, key );
							if( !sgson_encode_var( C, data, depth, tab, tablen ) )
								return 0;
							sgs_Pop( C, 1 );
							
							if( key.type != SGS_VT_STRING )
								sgs_membuf_appchr( buf, C, ']' );
						}
					}
					
					/* = */
					if( tab )
						sgs_membuf_appbuf( buf, C, " = ", 3 );
					else
						sgs_membuf_appchr( buf, C, '=' );
					
					/* value */
					sgs_PushVariable( C, data->servartable.vars[ args[ i + 1 ] ].key );
					if( !sgson_encode_var( C, data, depth, tab, tablen ) )
						return 0;
					sgs_Pop( C, 1 );
				}
				depth--;
				if( ci->arg_count )
					sgson_tab( buf, C, depth, tab, tablen );
				sgs_membuf_appchr( buf, C, '}' );
			}
			else /* function call */
			{
				sgs_membuf_appbuf( buf, C, fn_name, fn_size );
				sgs_membuf_appchr( buf, C, '(' );
				for( i = 0; i < ci->arg_count; ++i )
				{
					if( i )
						sgs_membuf_appbuf( buf, C, ", ", 2 );
					
					sgs_PushVariable( C, data->servartable.vars[ args[ i ] ].key );
					if( !sgson_encode_var( C, data, depth, tab, tablen ) )
						return 0;
					sgs_Pop( C, 1 );
				}
				sgs_membuf_appchr( buf, C, ')' );
			}
			return 1;
		}
	}
	return 0;
}

void sgs_SerializeInt_V3( SGS_CTX, sgs_Variable var, const char* tab, sgs_SizeVal tablen )
{
	int ret = SGS_TRUE;
	void* prev_serialize_state = C->serialize_state;
	sgs_serialize3_data SD, *pSD;
	int ep = !C->serialize_state || *(int*)C->serialize_state != 3;
	
	if( ep )
	{
		SD.mode = 3;
		sgs_vht_init( &SD.servartable, C, 64, 64 );
		SD.callinfo = sgs_membuf_create();
		SD.callargs = sgs_membuf_create();
		SD.argarray = sgs_membuf_create();
		SD.data = sgs_membuf_create();
		SD.curObj = NULL;
		C->serialize_state = &SD;
	}
	pSD = (sgs_serialize3_data*) C->serialize_state;
	
	/* SYMBOLS */
	if( var.type == SGS_VT_OBJECT || var.type == SGS_VT_CFUNC ||
		var.type == SGS_VT_FUNC || var.type == SGS_VT_THREAD || var.type == SGS_VT_PTR )
	{
		int32_t argidx;
		sgs_VHTVar* vv = sgs_vht_get( &pSD->servartable, &var );
		if( vv )
		{
			argidx = (int32_t) ( vv - pSD->servartable.vars );
			sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
			goto fail;
		}
		else
		{
			sgs_Variable sym = sgs_MakeNull();
			if( sgs_GetSymbol( C, var, &sym ) && sym.type == SGS_VT_STRING )
			{
				int32_t call_info_offset = (int32_t) ( pSD->callinfo.size / sizeof(s3callinfo) );
				int32_t call_args_offset = (int32_t) ( pSD->callargs.size / 4 );
				s3callinfo ci = { sgs_MakeNull(), call_args_offset, 1 };
				sgs_InitString( C, &ci.func_name, "sym_get" );
				
				/* this is expected to succeed (type=string) */
				sgs_SerializeInt_V3( C, sym, tab, tablen );
				sgs_Release( C, &sym );
				
				/* append sym_get call argument */
				sgs_membuf_appbuf( &pSD->callargs, C, pSD->argarray.ptr + pSD->argarray.size - 4, 4 );
				/* pop argument */
				sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - sizeof(argidx), pSD->argarray.size );
				/* append sym_get call info */
				sgs_membuf_appbuf( &pSD->callinfo, C, &ci, sizeof(ci) );
				
				/* create variable resolve */
				sgs_Variable idxvar;
				argidx = sgs_vht_size( &pSD->servartable );
				idxvar.type = SGS_VT_INT;
				idxvar.data.I = call_info_offset;
				sgs_vht_set( &pSD->servartable, C, &var, &idxvar );
				
				/* push new argument */
				sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
				goto fail;
			}
			sgs_Release( C, &sym );
		}
	}
	
	/* SPECIAL TYPES */
	if( var.type == SGS_VT_THREAD )
	{
		sgs_Msg( C, SGS_WARNING, "serialization mode 3 (SGSON text)"
			" does not support thread serialization" );
		ret = 0;
		goto fail;
	}
	else if( var.type == SGS_VT_FUNC || var.type == SGS_VT_CFUNC )
	{
		sgs_Msg( C, SGS_WARNING, "serialization mode 3 (SGSON text)"
			" does not support function serialization" );
		ret = 0;
		goto fail;
	}
	else if( var.type == SGS_VT_PTR )
	{
		sgs_Msg( C, SGS_WARNING, "serialization mode 3 (SGSON text)"
			" does not support pointer serialization" );
		ret = 0;
		goto fail;
	}
	
	/* OBJECTS */
	if( var.type == SGS_VT_OBJECT )
	{
		sgs_VarObj* O = var.data.O;
		sgs_VarObj* prevObj = pSD->curObj;
		_STACK_PREPARE;
		if( !O->iface->serialize )
		{
			sgs_Msg( C, SGS_WARNING, "cannot serialize object of type '%s'", O->iface->name );
			var = sgs_MakeNull();
		}
		else
		{
			int parg = C->object_arg;
			pSD->curObj = O;
			_STACK_PROTECT;
			C->object_arg = 3;
			ret = SGS_SUCCEEDED( O->iface->serialize( C, O ) );
			C->object_arg = parg;
			_STACK_UNPROTECT;
			pSD->curObj = prevObj;
			if( ret == SGS_FALSE )
			{
				sgs_Msg( C, SGS_ERROR, "failed to serialize object of type '%s'", O->iface->name );
				goto fail;
			}
		}
	}
	else /* null, bool, int, real, string */
	{
		int32_t argidx;
		sgs_VHTVar* vv = sgs_vht_get( &pSD->servartable, &var );
		if( vv )
			argidx = (int32_t) ( vv - pSD->servartable.vars );
		else
		{
			sgs_Variable val = sgs_MakeNull();
			argidx = sgs_vht_size( &pSD->servartable );
			sgs_vht_set( &pSD->servartable, C, &var, &val );
		}
		sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
	}
	
fail:
	if( ep )
	{
		/* serialize the variable tree */
		if( ret )
		{
			sgs_PushVariable( C, var );
			ret = sgson_encode_var( C, &SD, 0, tab, tablen );
			if( SD.data.size > 0x7fffffff )
			{
				ret = 0;
				sgs_Msg( C, SGS_WARNING, "generated more string data than allowed to store" );
			}
			sgs_Pop( C, 1 );
			if( ret )
				sgs_PushStringBuf( C, SD.data.ptr, (sgs_SizeVal) SD.data.size );
		}
		if( !ret )
			sgs_PushNull( C );
		
		/* free the serialization state */
		sgs_vht_free( &SD.servartable, C );
		sgs_membuf_destroy( &SD.argarray, C );
		{
			s3callinfo* ci = SGS_ASSUME_ALIGNED( SD.callinfo.ptr, s3callinfo );
			s3callinfo* ciend = SGS_ASSUME_ALIGNED( SD.callinfo.ptr + SD.callinfo.size, s3callinfo );
			while( ci < ciend )
			{
				sgs_Release( C, &ci->func_name );
				ci++;
			}
		}
		sgs_membuf_destroy( &SD.callinfo, C );
		sgs_membuf_destroy( &SD.callargs, C );
		sgs_membuf_destroy( &SD.data, C );
		C->serialize_state = prev_serialize_state;
	}
}

static void sgs_SerializeObjectInt_V3( SGS_CTX, sgs_StkIdx args, const char* func, size_t fnsize )
{
	size_t argsize;
	sgs_VHTVar* vv;
	sgs_Variable V;
	int32_t argidx;
	sgs_serialize3_data* pSD = (sgs_serialize3_data*) C->serialize_state;
	
	V.type = SGS_VT_OBJECT;
	V.data.O = pSD->curObj;
	if( args < 0 || (size_t) args > pSD->argarray.size / sizeof(int32_t) )
	{
		sgs_Variable idxvar = sgs_MakeNull();
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObject: specified "
			"more arguments than there are serialized items" );
		sgs_vht_set( &pSD->servartable, C, &V, &idxvar );
		return;
	}
	/* WP: added error condition */
	argsize = sizeof(int32_t) * (size_t) args;
	
	vv = sgs_vht_get( &pSD->servartable, &V );
	if( vv )
		argidx = (int32_t) ( vv - pSD->servartable.vars );
	else
	{
		int32_t call_info_offset = (int32_t) ( pSD->callinfo.size / sizeof(s3callinfo) );
		int32_t call_args_offset = (int32_t) ( pSD->callargs.size / 4 );
		s3callinfo ci = { sgs_MakeNull(), call_args_offset, args };
		sgs_InitStringBuf( C, &ci.func_name, func, (sgs_SizeVal) fnsize );
		
		/* append call arguments */
		sgs_membuf_appbuf( &pSD->callargs, C, pSD->argarray.ptr + pSD->argarray.size - argsize, argsize );
		/* append call info */
		sgs_membuf_appbuf( &pSD->callinfo, C, &ci, sizeof(ci) );
		
		/* create variable resolve */
		sgs_Variable idxvar;
		argidx = sgs_vht_size( &pSD->servartable );
		idxvar.type = SGS_VT_INT;
		idxvar.data.I = call_info_offset;
		sgs_vht_set( &pSD->servartable, C, &V, &idxvar );
	}
	/* pop arguments */
	sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - argsize, pSD->argarray.size );
	/* append object as new argument */
	sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
}

static int sgs_UnserializeInt_V3( SGS_CTX, char* str, char* strend )
{
	int res;
	sgs_MemBuf stack = sgs_membuf_create();
	sgs_membuf_appchr( &stack, C, 0 );
	res = !sgson_parse( C, &stack, str, (sgs_SizeVal) ( strend - str ) );
	sgs_membuf_destroy( &stack, C );
	return res;
}


void sgs_SerializeExt( SGS_CTX, sgs_Variable var, int mode )
{
	if( mode == SGS_SERIALIZE_DEFAULT )
		mode = C->serialize_state ? *(int*) C->serialize_state : 2;
	
	if( mode == 3 )
		sgs_SerializeInt_V3( C, var, NULL, 0 );
	else if( mode == 2 )
		sgs_SerializeInt_V2( C, var );
	else if( mode == 1 )
		sgs_SerializeInt_V1( C, var );
	else
	{
		sgs_PushNull( C );
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeExt: bad mode (%d)", mode );
	}
}

void sgs_SerializeObject( SGS_CTX, sgs_StkIdx args, const char* func )
{
	int mode;
	size_t fnsize = strlen( func );
	if( !C->serialize_state )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObject: called outside the serialization process" );
		return;
	}
	if( fnsize >= 255 )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObject: function name length exceeds 255" );
		return;
	}
	mode = *(int*) C->serialize_state;
	
	if( mode == 3 )
		sgs_SerializeObjectInt_V3( C, args, func, fnsize );
	else if( mode == 2 )
		sgs_SerializeObjectInt_V2( C, args, func, fnsize );
	else if( mode == 1 )
		sgs_SerializeObjectInt_V1( C, args, func, fnsize );
	else
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObjectExt: bad mode (%d)", mode );
	}
}

void sgs_SerializeObjIndex( SGS_CTX, sgs_Variable key, sgs_Variable val, int isprop )
{
	int mode;
	if( !C->serialize_state )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObject: called outside the serialization process" );
		return;
	}
	mode = *(int*) C->serialize_state;
	
	if( mode == 1 || mode == 2 )
	{
		sgs_Serialize( C, key );
		sgs_Serialize( C, val );
	}
	
	if( mode == 3 )
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObjIndex: mode 3 is not supported" );
	else if( mode == 2 )
		sgs_SerializeObjIndexInt_V2( C, isprop );
	else if( mode == 1 )
		sgs_SerializeObjIndexInt_V1( C, isprop );
	else
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObjectExt: bad mode (%d)", mode );
	}
}


SGSBOOL sgs_UnserializeExt( SGS_CTX, sgs_Variable var, int mode )
{
	SGSRESULT res = 0;
	char* str = NULL, *strend;
	sgs_SizeVal size = 0;
	_STACK_PREPARE;
	sgs_PushVariable( C, var );
	if( !sgs_ParseString( C, -1, &str, &size ) || !size )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_Unserialize: variable does not resolve to a non-empty string" );
		sgs_Pop( C, 1 );
		sgs_PushNull( C );
		return 0;
	}
	
	strend = str + size;
	
	if( mode == SGS_SERIALIZE_DEFAULT )
		mode = C->serialize_state ? *(int*) C->serialize_state : 2;
	
	_STACK_PROTECT;
	if( mode == 3 )
		res = sgs_UnserializeInt_V3( C, str, strend );
	else if( mode == 2 )
		res = sgs_UnserializeInt_V2( C, str, strend );
	else if( mode == 1 )
		res = sgs_UnserializeInt_V1( C, str, strend );
	else
	{
		sgs_Msg( C, SGS_APIERR, "sgs_UnserializeExt: bad mode (%d)", mode );
	}
	_STACK_UNPROTECT_SKIP( res );
	return res;
}



/* SGS object notation [SGSON] */

void sgs_SerializeSGSON( SGS_CTX, sgs_Variable var, const char* tab )
{
	sgs_SizeVal tablen = tab ? (sgs_SizeVal) SGS_STRINGLENGTHFUNC( tab ) : 0;
	sgs_SerializeInt_V3( C, var, tab, tablen );
}


static void sgson_skipws( const char** p, const char* end )
{
	const char* pos = *p;
	while( pos < end )
	{
		if( *pos != ' ' && *pos != '\t' &&
			*pos != '\n' && *pos != '\r' )
			break;
		pos++;
	}
	*p = pos;
}

#define SGSON_STK_TOP stack->ptr[ stack->size - 1 ]
#define SGSON_STK_POP sgs_membuf_resize( stack, C, stack->size - 1 )
#define SGSON_STK_PUSH( what ) sgs_membuf_appchr( stack, C, what )

const char* sgson_parse( SGS_CTX, sgs_MemBuf* stack, const char* buf, sgs_SizeVal size )
{
	int stk = sgs_StackSize( C );
	const char* pos = buf, *end = buf + size;
	for(;;)
	{
		int push = 0;
		sgson_skipws( &pos, end );
		if( pos >= end )
			break;

		if( SGSON_STK_TOP == '{' &&
			*pos != '"' &&
			*pos != '\'' &&
			*pos != '_' &&
			*pos != '[' &&
			!sgs_isalpha( *pos ) &&
			*pos != '}' )
			return pos;

		if( SGSON_STK_TOP == 0 && sgs_StackSize( C ) > stk )
			return pos;

		if( *pos == '{' )
		{
			SGSON_STK_PUSH( '{' );
			sgs_CreateDict( C, NULL, 0 );
		}
		else if( *pos == '}' )
		{
			if( SGSON_STK_TOP != '{' )
				return pos;
			SGSON_STK_POP;
			push = 1;
		}
		else if( *pos == '[' )
		{
			if( SGSON_STK_TOP == '{' )
			{
				/* non-string key marker (for maps) */
				SGSON_STK_TOP = 'M';
			}
			else
			{
				SGSON_STK_PUSH( '[' );
				sgs_CreateArray( C, NULL, 0 );
			}
		}
		else if( *pos == ']' )
		{
			if( SGSON_STK_TOP == 'M' )
			{
				SGSON_STK_TOP = '=';
				pos++;
				sgson_skipws( &pos, end );
				if( *pos != '=' )
					return pos;
			}
			else
			{
				if( SGSON_STK_TOP != '[' )
					return pos;
				SGSON_STK_POP;
				push = 1;
			}
		}
		else if( *pos == ')' )
		{
			sgs_Variable func;
			if( SGSON_STK_TOP != '(' )
				return pos;
			SGSON_STK_POP;
			push = 1;
			
			/* search for marker */
			sgs_SizeVal i = sgs_StackSize( C ) - 1;
			while( i >= 0 && sgs_StackItem( C, i ).type != 255 )
				i--;
			sgs_BreakIf( i < 0 );
			
			/* replace function name with global function */
			sgs_GetGlobal( C, sgs_StackItem( C, i + 1 ), &func );
			sgs_SetStackItem( C, i + 1, func );
			VAR_RELEASE( &func );
			
			/* call the function */
			sgs_Call( C, sgs_StackSize( C ) - ( i + 2 ), 1 );
			
			/* clean up - pop marker, skip returned value */
			sgs_PopSkip( C, 1, 1 );
		}
		else if( *pos == '"' || *pos == '\'' )
		{
			char sc = *pos;
			const char* beg = ++pos;
			sgs_MemBuf str = sgs_membuf_create();
			while( pos < end && *pos != sc )
			{
				uint8_t cc = (uint8_t) *pos;
				if( cc <= 0x1f || cc == 0x7f )
				{
					sgs_membuf_destroy( &str, C );
					return pos;
				}
				if( *pos == '\\' )
				{
					pos++;
					switch( *pos )
					{
					case '"':
					case '\'':
					case '\\':
						sgs_membuf_appchr( &str, C, *pos );
						break;
					case 'n': sgs_membuf_appchr( &str, C, '\n' ); break;
					case 'r': sgs_membuf_appchr( &str, C, '\r' ); break;
					case 't': sgs_membuf_appchr( &str, C, '\t' ); break;
					case 'x':
						{
							uint8_t hex[ 2 ];
							uint8_t chr;
							pos++;
							if( !sgs_hexchar( pos[0] ) ){ goto strfail; }
							if( !sgs_hexchar( pos[1] ) ){ pos++; goto strfail; }
							hex[ 0 ] = (uint8_t) ( sgs_gethex( pos[0] ) );
							hex[ 1 ] = (uint8_t) ( sgs_gethex( pos[1] ) );
							pos++;
							chr = (uint8_t) ( ( hex[0] << 4 ) | hex[1] );
							sgs_membuf_appchr( &str, C, (char) chr );
						}
						break;
					default:
					strfail:
						sgs_membuf_destroy( &str, C );
						return pos;
					}
				}
				else
					sgs_membuf_appchr( &str, C, *pos );
				pos++;
			}
			if( pos >= end || str.size > 0x7fffffff )
			{
				sgs_membuf_destroy( &str, C );
				return beg;
			}
			sgs_PushStringBuf( C, str.ptr, (sgs_SizeVal) str.size );
			sgs_membuf_destroy( &str, C );
			if( SGSON_STK_TOP == '{' )
			{
				SGSON_STK_TOP = '=';
				pos++;
				sgson_skipws( &pos, end );
				if( *pos != '=' )
					return pos;
			}
			else
			{
				push = 1;
			}
		}
		else if( sgs_decchar( *pos ) || *pos == '-' )
		{
			sgs_Int outi;
			sgs_Real outf;
			int type = sgs_util_strtonum( &pos, end, &outi, &outf );
			if( type == 1 )
			{
				sgs_PushInt( C, outi );
				push = 1;
			}
			else if( type == 2 )
			{
				sgs_PushReal( C, outf );
				push = 1;
			}
			else
				return pos;
			pos--;
		}
		else if( *pos == '_' || sgs_isalpha( *pos ) )
		{
			if( end - pos >= 4 &&
				( pos[4] == '\0' || sgs_isoneof( pos[4], ")}]=, \n\r\t" ) ) &&
				memcmp( pos, "null", 4 ) == 0 )
			{
				if( SGSON_STK_TOP == '{' )
					return pos; /* no keywords as keys outside [..] */
				sgs_PushNull( C );
				pos += 4 - 1;
				push = 1;
			}
			else if( end - pos >= 4 &&
				( pos[4] == '\0' || sgs_isoneof( pos[4], ")}]=, \n\r\t" ) ) &&
				memcmp( pos, "true", 4 ) == 0 )
			{
				if( SGSON_STK_TOP == '{' )
					return pos; /* no keywords as keys outside [..] */
				sgs_PushBool( C, SGS_TRUE );
				pos += 4 - 1;
				push = 1;
			}
			else if( end - pos >= 5 &&
				( pos[5] == '\0' || sgs_isoneof( pos[5], ")}]=, \n\r\t" ) ) &&
				memcmp( pos, "false", 5 ) == 0 )
			{
				if( SGSON_STK_TOP == '{' )
					return pos; /* no keywords as keys outside [..] */
				sgs_PushBool( C, SGS_FALSE );
				pos += 5 - 1;
				push = 1;
			}
			else if( SGSON_STK_TOP == '{' ) /* identifiers as keys */
			{
				const char* idend = pos;
				while( sgs_isid( *idend ) )
					idend++;
				if( idend - pos > 255 )
				{
					return pos;
				}
				
				sgs_PushStringBuf( C, pos, (sgs_SizeVal)( idend - pos ) );
				pos = idend;
				SGSON_STK_TOP = '=';
				sgson_skipws( &pos, end );
				if( *pos != '=' )
					return pos;
			}
			else /* identifiers as function names */
			{
				const char* idstart = pos;
				const char* idend = pos;
				while( sgs_isid( *idend ) )
					idend++;
				if( idend - idstart > 255 )
					return pos; /* func. name size exceeds 255 chars */
				
				pos = idend;
				sgson_skipws( &pos, end );
				if( pos[0] != '(' && pos[0] != '{' )
					return pos; /* no opening char. */
				
				if( pos[0] == '(' )
				{
					sgs_Variable marker = { 255 };
					SGSON_STK_PUSH( '(' );
					sgs_PushVariable( C, marker ); /* marker for beginning of function */
					sgs_PushStringBuf( C, idstart, (sgs_SizeVal)( idend - idstart ) );
				}
				else if( idend - idstart == 3 && memcmp( idstart, "map", 3 ) == 0 )
				{
					SGSON_STK_PUSH( '{' );
					sgs_CreateMap( C, NULL, 0 );
				}
				else return pos;
			}
		}
		else
			return pos;

		if( push )
		{
			if( SGSON_STK_TOP == '[' || SGSON_STK_TOP == '=' || SGSON_STK_TOP == '(' )
			{
				int revchr = SGSON_STK_TOP == '[' ? ']' : '}';
				if( SGSON_STK_TOP == '(' )
					revchr = ')';
				
				pos++;
				sgson_skipws( &pos, end );
				if( pos >= end )
					break;
				if( *pos != ',' && *pos != revchr ) return pos;
				if( *pos != ',' )
					pos--;
			}
			if( SGSON_STK_TOP == '[' )
			{
				sgs_ArrayPush( C, sgs_StackItem( C, -2 ), 1 );
			}
			if( SGSON_STK_TOP == '=' )
			{
				sgs_SetIndex( C, sgs_StackItem( C, -3 ), sgs_StackItem( C, -2 ), sgs_StackItem( C, -1 ), SGS_FALSE );
				sgs_Pop( C, 2 );
				SGSON_STK_TOP = '{';
			}
		}
		pos++;
	}
/*	printf( "%d, %.*s, %d\n", stack->size, stack->size, stack->ptr, sgs_StackSize(C)-stk ); */
	return sgs_StackSize( C ) > stk && stack->size == 1 ? NULL : buf;
}

void sgs_UnserializeSGSONExt( SGS_CTX, const char* str, size_t size )
{
	const char* ret = NULL;
	sgs_MemBuf stack = sgs_membuf_create();
	sgs_SizeVal stksize = sgs_StackSize( C );
	sgs_membuf_appchr( &stack, C, 0 );
	ret = sgson_parse( C, &stack, str, (sgs_SizeVal) size );
	sgs_membuf_destroy( &stack, C );
	if( ret )
	{
		sgs_PushNull( C );
		sgs_Msg( C, SGS_ERROR, "failed to parse SGSON (position %d, %.8s...", ret - str, ret );
	}
	sgs_PopSkip( C, sgs_StackSize( C ) - stksize - 1, 1 );
}



/* bytecode serialization */

#define SGSNOMINDEC( cnt ) (D->end - D->buf < (ptrdiff_t)(cnt))

#define esi16( x ) ( (((x)&0xff)<<8) | (((x)>>8)&0xff) )

#define esi32( x ) (\
	(((x)&0xff)<<24) | (((x)&0xff00)<<8) |\
	(((x)>>8)&0xff00) | (((x)>>24)&0xff) )

#define bswap( a, b ) { uint8_t tmp_ = (a); (a) = (b); (b) = tmp_; }
#define bflip16( x ) bswap( (x)[0], (x)[1] );
#define bflip32( x ) bswap( (x)[0], (x)[3] ); bswap( (x)[1], (x)[2] );

typedef struct decoder_s
{
	SGS_CTX;
	const char* buf, *start, *end;
	char convend;
	const char* filename;
	size_t filename_len;
}
decoder_t;

static void esi16_array( uint16_t* data, unsigned cnt )
{
	unsigned i;
	for( i = 0; i < cnt; ++i )
	{
		data[ i ] = (uint16_t) esi16( data[ i ] );
	}
}

static void esi32_array( uint32_t* data, unsigned cnt )
{
	unsigned i;
	for( i = 0; i < cnt; ++i )
	{
		data[ i ] = (uint32_t) esi32( data[ i ] );
	}
}


/*
	i32 size
	byte[size] data
*/
static void bc_write_sgsstring( sgs_iStr* S, SGS_CTX, sgs_MemBuf* outbuf )
{
	sgs_membuf_appbuf( outbuf, C, &S->size, sizeof( int32_t ) );
	sgs_membuf_appbuf( outbuf, C, sgs_str_cstr( S ), S->size );
}

static const char* bc_read_sgsstring( decoder_t* D, sgs_Variable* var )
{
	const char* buf = D->buf;
	int32_t len;
	
	if( SGSNOMINDEC( 4 ) )
		return "data error (expected string length)";
	
	SGS_AS_INT32( len, buf );
	if( D->convend )
		len = esi32( len );
	buf += 4;
	
	if( SGSNOMINDEC( len ) )
		return "data error (expected string bytes)";
	
	sgsVM_VarCreateString( D->C, var, buf, len );
	D->buf = buf + len;
	
	return NULL;
}


/*
	byte type
	--if type = NULL:
	--if type = BOOL:
	byte value
	--if type = INT:
	integer value
	--if type = REAL:
	real value
	--if type = STRING:
	stringdata data
	--if type = FUNC:
	funcdata data
*/
static int bc_write_sgsfunc( sgs_iFunc* F, SGS_CTX, sgs_MemBuf* outbuf );
static int bc_write_var( sgs_Variable* var, SGS_CTX, sgs_MemBuf* outbuf )
{
	uint8_t vt = (uint8_t) var->type;
	/* WP: don't care about the sign when serializing bitfield */
	sgs_membuf_appchr( outbuf, C, (char) vt );
	switch( vt )
	{
	case SGS_VT_NULL: break;
	/* WP: var->data.B can only store 0/1 */
	case SGS_VT_BOOL: sgs_membuf_appchr( outbuf, C, (char) var->data.B ); break;
	case SGS_VT_INT: sgs_membuf_appbuf( outbuf, C, &var->data.I, sizeof( sgs_Int ) ); break;
	case SGS_VT_PTR: { sgs_Int val = (intptr_t) var->data.P;
		sgs_membuf_appbuf( outbuf, C, &val, sizeof( val ) ); break; }
	case SGS_VT_REAL: sgs_membuf_appbuf( outbuf, C, &var->data.R, sizeof( sgs_Real ) ); break;
	case SGS_VT_STRING: bc_write_sgsstring( var->data.S, C, outbuf ); break;
	case SGS_VT_FUNC: if( !bc_write_sgsfunc( var->data.F, C, outbuf ) ) return 0; break;
	default:
		return 0;
	}
	return 1;
}

static const char* bc_read_sgsfunc( decoder_t* D, sgs_Variable* var );
static const char* bc_read_var( decoder_t* D, sgs_Variable* var )
{
	const char* ret = NULL;
	uint8_t vt;
	
	if( SGSNOMINDEC( 1 ) )
		return "data error (expected type)";
	
	vt = (uint8_t) *D->buf++;
	var->type = SGS_VT_NULL;
	switch( vt )
	{
	case SGS_VT_NULL: var->type = vt; break;
	case SGS_VT_BOOL:
		if( SGSNOMINDEC( 1 ) )
			return "data error (expected value)";
		
		var->type = vt;
		var->data.B = *D->buf++ ? 1 : 0;
		break;
		
	case SGS_VT_INT:
		if( SGSNOMINDEC( sizeof( sgs_Int ) ) )
			return "data error (expected value)";
		
		var->type = vt;
		SGS_AS_INTEGER( var->data.I, D->buf );
		D->buf += sizeof( sgs_Int );
		break;
		
	case SGS_VT_PTR:
		if( SGSNOMINDEC( sizeof( sgs_Int ) ) )
			return "data error (expected value)";
		
		var->type = vt;
		SGS_AS_INTEGER( var->data.I, D->buf );
		var->data.P = (void*)(intptr_t) var->data.I;
		D->buf += sizeof( sgs_Int );
		break;
		
	case SGS_VT_REAL:
		if( SGSNOMINDEC( sizeof( sgs_Real ) ) )
			return "data error (expected value)";
		
		var->type = vt;
		SGS_AS_REAL( var->data.R, D->buf );
		D->buf += sizeof( sgs_Real );
		break;
		
	case SGS_VT_STRING:
		ret = bc_read_sgsstring( D, var );
		if( ret == NULL )
			var->type = SGS_VT_STRING;
		break;
		
	case SGS_VT_FUNC:
		ret = bc_read_sgsfunc( D, var );
		break;
		
	default:
		return "invalid variable type found";
	}
	return ret;
}


/*
	var[cnt] varlist
*/
static int bc_write_varlist( sgs_Variable* vlist, SGS_CTX, int cnt, sgs_MemBuf* outbuf )
{
	int i;
	for( i = 0; i < cnt; ++i )
	{
		if( !bc_write_var( vlist + i, C, outbuf ) )
			return 0;
	}
	return 1;
}

static const char* bc_read_varlist( decoder_t* D, sgs_Variable* vlist, int cnt )
{
	int i;
	for( i = 0; i < cnt; ++i )
	{
		const char* ret = bc_read_var( D, vlist + i );
		if( ret )
		{
			cnt = i;
			for( i = 0; i < cnt; ++i )
				sgs_Release( D->C, vlist + i );
			return ret;
		}
	}
	return NULL;
}


/*
	i16 constcount
	i16 instrcount
	byte gotthis
	byte numargs
	byte numtmp
	byte numclsr
	byte inclsr
	i16 linenum
	i16[instrcount] lineinfo
	i32 funcname_size
	byte[funcname_size] funcname
	varlist consts
	instr[instrcount] instrs
*/
static int bc_write_sgsfunc( sgs_iFunc* F, SGS_CTX, sgs_MemBuf* outbuf )
{
	uint32_t varinfosize = 0, size = F->sfuncname->size;
	uint16_t cc, ic;
	uint8_t gntc[5] = { F->gotthis, F->numargs, F->numtmp, F->numclsr, F->inclsr };
	
	/* WP: const/instruction limits */
	cc = (uint16_t) sgs_func_const_count( F );
	ic = (uint16_t) sgs_func_instr_count( F );

	sgs_membuf_appbuf( outbuf, C, &cc, sizeof( cc ) );
	sgs_membuf_appbuf( outbuf, C, &ic, sizeof( ic ) );
	sgs_membuf_appbuf( outbuf, C, gntc, 5 );
	sgs_membuf_appbuf( outbuf, C, &F->linenum, sizeof( sgs_LineNum ) );
	sgs_membuf_appbuf( outbuf, C, sgs_func_lineinfo( F ), sizeof( sgs_LineNum ) * ic );
	sgs_membuf_appbuf( outbuf, C, &size, sizeof( size ) );
	sgs_membuf_appbuf( outbuf, C, sgs_str_cstr( F->sfuncname ), F->sfuncname->size );

	if( !bc_write_varlist( sgs_func_consts( F ), C, cc, outbuf ) )
		return 0;

	sgs_membuf_appbuf( outbuf, C, sgs_func_bytecode( F ), sizeof( sgs_instr_t ) * ic );
	
	if( F->dbg_varinfo )
	{
		memcpy( &varinfosize, F->dbg_varinfo, sizeof(varinfosize) );
		sgs_membuf_appbuf( outbuf, C, F->dbg_varinfo, varinfosize );
	}
	else
	{
		sgs_membuf_appbuf( outbuf, C, &varinfosize, sizeof(varinfosize) );
	}
	return 1;
}

static const char* bc_read_sgsfunc( decoder_t* D, sgs_Variable* var )
{
	sgs_Variable strvar;
	sgs_iFunc* F = NULL;
	uint32_t coff, ioff, size, fnsize, varinfosize;
	uint16_t cc, ic;
	const char* ret = "data error (expected fn. data)";
	SGS_CTX = D->C;
	
	if( SGSNOMINDEC( 10 ) )
		goto fail;
	
	SGS_AS_UINT16( cc, D->buf );
	SGS_AS_UINT16( ic, D->buf + 2 );
	
	if( D->convend )
	{
		/* WP: int promotion will not affect the result */
		cc = (uint16_t) esi16( cc );
		ic = (uint16_t) esi16( ic );
	}
	
	/* basic tests to avoid allocating too much memory */
	if( SGSNOMINDEC( 10 + (ptrdiff_t) ( cc + ic * sizeof(sgs_LineNum) ) ) )
		goto fail;
	
	/* WP: const/instruction limits */
	ioff = (uint32_t) sizeof( sgs_Variable ) * cc;
	coff = (uint32_t) sizeof( sgs_instr_t ) * ic;
	size = ioff + coff;
	
	F = sgs_Alloc_a( sgs_iFunc, size );
	F->refcount = 1;
	F->size = size;
	F->instr_off = ioff;
	SGS_AS_UINT8( F->gotthis, D->buf + 4 );
	SGS_AS_UINT8( F->numargs, D->buf + 5 );
	SGS_AS_UINT8( F->numtmp, D->buf + 6 );
	SGS_AS_UINT8( F->numclsr, D->buf + 7 );
	SGS_AS_UINT8( F->inclsr, D->buf + 8 );
	SGS_AS_INT16( F->linenum, D->buf + 9 );
	if( D->convend )
		F->linenum = (sgs_LineNum) esi16( F->linenum );
	F->lineinfo = sgs_Alloc_n( sgs_LineNum, ic );
	F->sfuncname = NULL;
	F->sfilename = NULL;
	F->dbg_varinfo = NULL;
	D->buf += 11;
	
	ret = "data error (expected fn. line numbers)";
	if( SGSNOMINDEC( sizeof( sgs_LineNum ) * ic ) )
		goto fail;
	
	memcpy( F->lineinfo, D->buf, sizeof( sgs_LineNum ) * ic );
	D->buf += sizeof( sgs_LineNum ) * ic;
	if( D->convend )
		esi16_array( (uint16_t*) F->lineinfo, ic );
	
	ret = "data error (expected fn. name)";
	if( SGSNOMINDEC( 4 ) )
		goto fail;
	SGS_AS_UINT32( fnsize, D->buf ); D->buf += 4;
	if( D->convend )
		fnsize = (uint32_t) esi32( fnsize );
	if( SGSNOMINDEC( fnsize ) )
		goto fail;
	/* WP: string limit */
	memcpy( sgs_InitStringAlloc( C, &strvar, (sgs_SizeVal) fnsize ), D->buf, fnsize );
	sgs_FinalizeStringAllocP( C, &strvar );
	F->sfuncname = strvar.data.S;
	D->buf += fnsize;
	
	/* WP: string limit */
	sgs_InitStringBuf( C, &strvar, D->filename, (sgs_SizeVal) D->filename_len );
	F->sfilename = strvar.data.S;
	
	/* the main data */
	ret = bc_read_varlist( D, sgs_func_consts( F ), cc );
	if( ret )
		goto fail;
	
	ret = "data error (expected fn. instructions)";
	if( SGSNOMINDEC( coff ) )
		goto fail;
	memcpy( sgs_func_bytecode( F ), D->buf, coff );
	if( D->convend )
		esi32_array( sgs_func_bytecode( F ), coff / sizeof( sgs_instr_t ) );
	D->buf += coff;
	
	ret = "data error (expected fn. debug variable info)";
	if( SGSNOMINDEC( 4 ) )
		goto fail;
	SGS_AS_UINT32( varinfosize, D->buf );
	if( D->convend )
		varinfosize = esi32( varinfosize );
	if( SGSNOMINDEC( varinfosize ) )
		goto fail;
	if( varinfosize != 0 )
	{
		F->dbg_varinfo = sgs_Alloc_a( char, varinfosize );
		memcpy( F->dbg_varinfo, D->buf, varinfosize );
		D->buf += varinfosize;
		if( D->convend )
		{
			char *v, *vend;
			v = F->dbg_varinfo + 4;
			vend = F->dbg_varinfo + varinfosize;
			while( v < vend )
			{
				bflip32( v ); v += 4;
				bflip32( v ); v += 4;
				bflip16( v ); v += 2;
				v += v[0] + 1;
			}
		}
	}
	else D->buf += 4;

	var->data.F = F;
	var->type = SGS_VT_FUNC;
	return NULL;

fail:
	if( F )
	{
		/* everything is allocated together, between error jumps */
		sgs_Dealloc( F->lineinfo );
		strvar.type = SGS_VT_STRING;
		if( F->sfuncname )
		{
			strvar.data.S = F->sfuncname;
			sgs_Release( C, &strvar );
		}
		if( F->sfilename )
		{
			strvar.data.S = F->sfilename;
			sgs_Release( C, &strvar );
		}
		sgs_Dealloc( F );
	}
	return ret;
}


/*
	-- header -- 14 bytes --
	seq "SGS\0"
	byte version_major
	byte version_minor
	byte version_incr
	byte integer_size
	byte real_size
	byte flags
	u32 filesize
	-- header end --
	function main
*/
int sgsBC_Func2Buf( SGS_CTX, sgs_iFunc* func, sgs_MemBuf* outbuf )
{
	size_t origobsize = outbuf->size;
	char header_bytes[ 14 ] =
	{
		'S', 'G', 'S', 0,
		SGS_VERSION_MAJOR,
		SGS_VERSION_MINOR,
		SGS_VERSION_INCR,
		sizeof( sgs_Int ),
		sizeof( sgs_Real ),
		0,
		0, 0, 0, 0
	};
	header_bytes[ 9 ] = ( O32_HOST_ORDER == O32_LITTLE_ENDIAN ) ? SGSBC_FLAG_LITTLE_ENDIAN : 0;
	sgs_membuf_reserve( outbuf, C, origobsize + 1000 );
	sgs_membuf_appbuf( outbuf, C, header_bytes, 14 );
	
	{
		uint32_t sz;
		int ret = bc_write_sgsfunc( func, C, outbuf );
		if( !ret )
			return 0;
		sz = (uint32_t)( outbuf->size - origobsize );
		memcpy( outbuf->ptr + origobsize + 10, &sz, sizeof(sz) );
		return 1;
	}
}

const char* sgsBC_Buf2Func( SGS_CTX, const char* fn, const char* buf, size_t size, sgs_Variable* outfunc )
{
	char flags;
	uint32_t sz;
	
	if( size < 22 )
		return "data error (expected fn. header)";
	
	flags = buf[ 9 ];
	SGS_AS_UINT32( sz, buf + 10 );
	
	decoder_t Dstorage, *D;
	D = &Dstorage; /* macro compatibility */
	{
		D->C = C;
		D->buf = NULL;
		D->start = buf;
		D->end = buf + size;
		D->convend = ( O32_HOST_ORDER == O32_LITTLE_ENDIAN ) !=
			( ( flags & SGSBC_FLAG_LITTLE_ENDIAN ) != 0 );
		D->filename = fn;
		D->filename_len = strlen( fn );
	}
	
	if( D->convend )
		sz = esi32( sz );
	if( (size_t) sz != size )
		return "data error (fn. data size mismatch)";
	
	D->buf = buf + 14;
	return bc_read_sgsfunc( D, outfunc );
}

int sgsBC_ValidateHeader( const char* buf, size_t size )
{
	int i;
	char validate_bytes[ 9 ] =
	{
		'S', 'G', 'S', 0,
		SGS_VERSION_MAJOR,
		SGS_VERSION_MINOR,
		SGS_VERSION_INCR,
		sizeof( sgs_Int ),
		sizeof( sgs_Real )
	};

	if( size < SGS_MIN_BC_SIZE )
		return -1;
	for( i = 0; i < 9; ++i )
	{
		if( buf[ i ] != validate_bytes[ i ] )
			return i;
	}
	return SGS_HEADER_SIZE;
}


