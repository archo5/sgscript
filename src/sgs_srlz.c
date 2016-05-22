

#include "sgs_int.h"


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
	
	if( sgsBC_Func2Buf( C, &F, out ) == SGS_FALSE )
	{
		sgs_Msg( C, SGS_INTERR, "failed to serialize function: error in data" );
		return 0;
	}
	return 1;
}

static int _unserialize_function( SGS_CTX, const char* buf, size_t sz, sgs_iFunc** outfn )
{
	sgs_Variable strvar;
	sgs_iFunc* F;
	sgs_CompFunc* nf = NULL;
	const char* err;
	if( sgsBC_ValidateHeader( buf, sz ) < SGS_HEADER_SIZE )
	{
		sgs_Msg( C, SGS_WARNING, "failed to unserialize function: incomplete data" );
		return 0;
	}
	err = sgsBC_Buf2Func( C, "<anonymous>", buf, sz, &nf );
	if( err )
	{
		sgs_Msg( C, SGS_WARNING, "failed to unserialize function: %s", err );
		return 0;
	}
	
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
	sgs_InitStringBuf( C, &strvar, "", 0 );
	F->sfuncname = strvar.data.S;
	F->linenum = 0;
	VAR_ACQUIRE( &strvar );
	F->sfilename = strvar.data.S;
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


static SGSBOOL sgs__thread_serialize( SGS_CTX, sgs_Context* ctx, sgs_MemBuf* outbuf, sgs_MemBuf* argarray )
{
	sgs_MemBuf buf = sgs_membuf_create();
	sgs_StackFrame* sf;
	int32_t sfnum = 0;
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
	{
		sgs_Variable v_obj; v_obj.type = SGS_VT_OBJECT; v_obj.data.O = ctx->_G;
		sgs_Serialize( C, v_obj );
	}
	/* variables: stack */
	{
		sgs_Variable* p = ctx->stack_base;
		while( p != ctx->stack_top )
			sgs_Serialize( C, *p++ );
	}
	
	_WRITE32( 0x5C057A7E ); /* Serialized COroutine STATE */
	/* POD: main context */
	_WRITE32( ctx->minlev );
	_WRITE32( ctx->apilev );
	_WRITE32( ctx->last_errno );
	_WRITE32( ctx->state );
	_WRITE32( ctx->stack_top - ctx->stack_base );
	_WRITE32( ctx->stack_off - ctx->stack_base );
	_WRITE32( ctx->stack_mem );
	_WRITE32( ctx->clstk_top - ctx->clstk_base );
	_WRITE32( ctx->clstk_off - ctx->clstk_base );
	_WRITE32( ctx->clstk_mem );
	{
		sf = ctx->sf_first;
		while( sf )
		{
			sfnum++;
			sf = sf->next;
		}
		_WRITE32( sfnum );
	}
	_WRITE32( ctx->sf_count );
	_WRITE32( ctx->num_last_returned );
	/* closures */
	{
		sgs_Closure** p = ctx->clstk_base;
		while( p != ctx->clstk_top )
		{
			sgs_Closure** refp = ctx->clstk_base;
			while( refp != p )
			{
				if( *refp == *p )
					break;
				refp++;
			}
			if( refp != p )
			{
				// found reference
				sgs_Serialize( C, sgs_MakeNull() );
				_WRITE32( refp - ctx->clstk_base );
			}
			else
			{
				// make new
				sgs_Serialize( C, (*p)->var );
				_WRITE32( -1 );
			}
			p++;
		}
	}
	/* stack frames */
	sf = ctx->sf_first;
	while( sf )
	{
		sgs_Serialize( C, sf->func );
		
		/* 'code' will be taken from function */
		_WRITE32( sf->iptr - sf->code );
		_WRITE32( sf->iend - sf->code ); /* - for validation */
		_WRITE32( sf->lptr - sf->code );
		/* 'cptr' will be taken from function */
		/* 'nfname' is irrelevant for non-native functions */
		/* 'prev', 'next', 'cached' are system pointers */
		_WRITE32( sf->argbeg );
		_WRITE32( sf->argend );
		_WRITE32( sf->argsfrom );
		_WRITE32( sf->stkoff );
		_WRITE32( sf->clsoff );
		_WRITE32( sf->constcount ); /* - for validation */
		_WRITE32( sf->errsup );
		_WRITE8( sf->argcount );
		_WRITE8( sf->inexp );
		_WRITE8( sf->flags );
		sf = sf->next;
	}
	
	sgs_membuf_appchr( outbuf, C, 'T' );
	if( argarray )
	{
		uint32_t argcount = (uint32_t)( 1 /* _G */
			+ ( ctx->stack_top - ctx->stack_base ) /* stack */
			+ ( ctx->clstk_top - ctx->clstk_base ) /* closure stack */
			+ sfnum /* stack frame functions */ );
		sgs_membuf_appbuf( outbuf, C, &argcount, 4 );
		sgs_membuf_appbuf( outbuf, C, argarray->ptr + argarray->size - argcount * 4, argcount * 4 );
		sgs_membuf_erase( argarray, argarray->size - argcount * 4, argarray->size );
	}
	
	/* WP: string size */
	sgs_membuf_appbuf( outbuf, C, buf.ptr, buf.size );
	sgs_membuf_destroy( &buf, C );
	
#undef _WRITE32
#undef _WRITE8
	return 1;
}

static int sgs__thread_unserialize( SGS_CTX, sgs_Context** pT, char** pbuf, char* bufend )
{
	char *buf = *pbuf;
	sgs_Context* ctx = sgsCTX_ForkState( C, SGS_FALSE );
	
#define _READ32( x ) { if( buf + 4 > bufend ) goto fail; memcpy( &x, buf, 4 ); buf += 4; }
#define _READ8( x ) { if( buf + 1 > bufend ) goto fail; x = (uint8_t) *buf++; }
	
	{
		int32_t i, tag, sfnum, stacklen, stackoff, clstklen, clstkoff;
		
		_READ32( tag );
		if( tag != 0x5C057A7E )
			goto fail;
		
		/* POD: context */
		_READ32( ctx->minlev );
		_READ32( ctx->apilev );
		_READ32( ctx->last_errno );
		_READ32( ctx->state );
		_READ32( stacklen );
		_READ32( stackoff );
		_READ32( ctx->stack_mem );
		_READ32( clstklen );
		_READ32( clstkoff );
		_READ32( ctx->clstk_mem );
		_READ32( sfnum );
		_READ32( ctx->sf_count );
		_READ32( ctx->num_last_returned );
		
		/* variables: _G */
		ctx->_G = sgs_StackItem( C, 0 ).data.O;
		sgs_ObjAcquire( ctx, ctx->_G );
		
		/* variables: stack */
		sgs_BreakIf( ctx->stack_top != ctx->stack_base );
		for( i = 0; i < stacklen; ++i )
			sgs_PushVariable( ctx, sgs_StackItem( C, 1 + i ) );
		sgs_BreakIf( ctx->stack_top != ctx->stack_base + stacklen );
		if( stackoff > stacklen )
			goto fail;
		ctx->stack_off = ctx->stack_base + stackoff;
		
		/* variables: closure stack */
		sgs_BreakIf( ctx->clstk_top != ctx->clstk_base );
		for( i = 0; i < clstklen; ++i )
		{
			int32_t clref;
			/* POD: closures */
			_READ32( clref );
			if( clref >= 0 )
			{
				if( clref >= i )
					goto fail;
				// found reference
				sgs_ClPushItem( ctx, clref );
			}
			else
			{
				// make new
				sgs_ClPushVariable( ctx, sgs_StackItem( C, 1 + stacklen + i ) );
			}
		}
		sgs_BreakIf( ctx->clstk_top != ctx->clstk_base + clstklen );
		if( clstkoff > clstklen )
			goto fail;
		ctx->clstk_off = ctx->clstk_base + clstkoff;
		
		/* stack frames */
		for( i = 0; i < sfnum; ++i )
		{
			sgs_StackFrame* sf;
			int32_t iptrpos, iendpos, lptrpos, ccount;
			
			/* variables: stack frame functions */
			sgs_Variable v_func = sgs_StackItem( C, 1 + stacklen + clstklen + i );
			if( v_func.type != SGS_VT_FUNC )
				goto fail;
			if( !sgsVM_PushStackFrame( ctx, &v_func ) )
				goto fail;
			sf = ctx->sf_last;
			
			/* POD: stack frames */
			/* 'code' will be taken from function */
			_READ32( iptrpos );
			sf->iptr = sf->code + iptrpos;
			_READ32( iendpos ); /* - for validation */
			if( iendpos != sf->iend - sf->code )
				goto fail;
			_READ32( lptrpos );
			sf->lptr = sf->code + lptrpos;
			/* 'cptr' will be taken from function */
			/* 'nfname' is irrelevant for non-native functions */
			/* 'prev', 'next', 'cached' are system pointers */
			_READ32( sf->argbeg );
			_READ32( sf->argend );
			_READ32( sf->argsfrom );
			_READ32( sf->stkoff );
			_READ32( sf->clsoff );
			_READ32( ccount ); /* - for validation */
			if( ccount != sf->constcount )
				goto fail;
			_READ32( sf->errsup );
			_READ8( sf->argcount );
			_READ8( sf->inexp );
			_READ8( sf->flags );
		}
	}
	
#undef _READ32
#undef _READ8
	
	/* finalize */
	*pbuf = buf;
	sgs_BreakIf( ctx->refcount != 0 );
	*pT = ctx;
	return 1;
fail:
	sgsCTX_FreeState( ctx );
	return 0;
}


typedef struct sgs_serialize1_data
{
	int mode;
	sgs_MemBuf data;
}
sgs_serialize1_data;

void sgs_SerializeInt_V1( SGS_CTX, sgs_Variable var )
{
	int ret = SGS_TRUE;
	void* prev_serialize_state = C->serialize_state;
	sgs_serialize1_data SD = { 1, sgs_membuf_create() }, *pSD;
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
			goto fail;
		}
		sgs_Release( C, &sym );
	}
	
	if( var.type == SGS_VT_THREAD )
	{
		if( !sgs__thread_serialize( C, var.data.T, &pSD->data, NULL ) )
		{
			sgs_Msg( C, SGS_ERROR, "failed to serialize thread" );
			ret = SGS_FALSE;
			goto fail;
		}
	}
	else if( var.type == SGS_VT_OBJECT )
	{
		sgs_VarObj* O = var.data.O;
		_STACK_PREPARE;
		if( !O->iface->serialize )
		{
			sgs_Msg( C, SGS_ERROR, "cannot serialize object of type '%s'", O->iface->name );
			ret = SGS_FALSE;
			goto fail;
		}
		_STACK_PROTECT;
		ret = SGS_SUCCEEDED( O->iface->serialize( C, O ) );
		_STACK_UNPROTECT;
		if( ret == SGS_FALSE )
			goto fail;
	}
	else if( var.type == SGS_VT_CFUNC )
	{
		sgs_Msg( C, SGS_ERROR, "cannot serialize C functions" );
		ret = SGS_FALSE;
		goto fail;
	}
	else if( var.type == SGS_VT_PTR )
	{
		sgs_Msg( C, SGS_ERROR, "cannot serialize pointers" );
		ret = SGS_FALSE;
		goto fail;
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
					ret = SGS_FALSE;
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
			sgs_Msg( C, SGS_INTERR, "sgs_Serialize: unknown memory error" );
			ret = SGS_FALSE;
			goto fail;
		}
	}

fail:
	if( ep )
	{
		if( ret )
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

void sgs_SerializeObjectInt_V1( SGS_CTX, sgs_StkIdx args, const char* func, size_t fnsize )
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

#define sgs_unserr_incomp( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: incomplete data" )
#define sgs_unserr_error( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: error in data" )
#define sgs_unserr_objcall( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: could not create object from function" )
#define sgs_unserr_symfail( C ) sgs_Msg( C, SGS_WARNING, "failed to unserialize: could not map name to symbol" )

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
					sgs_iFunc* fn;
					if( str >= strend-3 )
						return sgs_unserr_incomp( C );
					SGS_AS_INT32( bcsz, str );
					str += 4;
					if( str > strend - bcsz )
						return sgs_unserr_incomp( C );
					/* WP: conversion does not affect values */
					if( !_unserialize_function( C, str, (size_t) bcsz, &fn ) )
						return SGS_FALSE; /* error already printed */
					tmp.type = SGS_VT_FUNC;
					tmp.data.F = fn;
					sgs_PushVariable( C, tmp );
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
			sgs_PushVariable( C, sym );
			sgs_Release( C, &sym );
		}
		else
		{
			return sgs_unserr_error( C );
		}
	}
	return SGS_TRUE;
}


typedef struct sgs_serialize2_data
{
	int mode;
	sgs_VHTable servartable;
	sgs_MemBuf argarray;
	sgs_VarObj* curObj;
	sgs_MemBuf data;
}
sgs_serialize2_data;

void sgs_SerializeInt_V2( SGS_CTX, sgs_Variable var )
{
	int ret = SGS_TRUE;
	void* prev_serialize_state = C->serialize_state;
	sgs_serialize2_data SD, *pSD;
	int ep = !C->serialize_state || *(int*)C->serialize_state != 2;
	
	if( ep )
	{
		SD.mode = 2;
		sgs_vht_init( &SD.servartable, C, 64, 64 );
		SD.argarray = sgs_membuf_create();
		SD.curObj = NULL;
		SD.data = sgs_membuf_create();
		C->serialize_state = &SD;
	}
	pSD = (sgs_serialize2_data*) C->serialize_state;
	
	if( var.type == SGS_VT_OBJECT || var.type == SGS_VT_CFUNC ||
		var.type == SGS_VT_FUNC || var.type == SGS_VT_THREAD )
	{
		sgs_StkIdx argidx;
		sgs_VHTVar* vv = sgs_vht_get( &pSD->servartable, &var );
		if( vv )
		{
			argidx = (sgs_StkIdx) ( vv - pSD->servartable.vars );
			sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
			goto fail;
		}
		else
		{
			sgs_Variable sym = sgs_MakeNull();
			if( sgs_GetSymbol( C, var, &sym ) && sym.type == SGS_VT_STRING )
			{
				sgs_SerializeInt_V2( C, sym );
				if( pSD->argarray.size < 4 )
				{
					/* error likely to be already printed */
					ret = SGS_FALSE;
					goto fail;
				}
				sgs_membuf_appchr( &pSD->data, C, 'S' );
				sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - 4, 4 );
				sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - sizeof(argidx), pSD->argarray.size );
				sgs_Release( C, &sym );
				
				// create variable resolve
				sgs_Variable idxvar;
				argidx = sgs_vht_size( &pSD->servartable );
				idxvar.type = SGS_VT_INT;
				idxvar.data.I = argidx;
				sgs_vht_set( &pSD->servartable, C, &var, &idxvar );
				sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
				goto fail;
			}
			sgs_Release( C, &sym );
		}
	}
	
	if( var.type == SGS_VT_THREAD )
	{
		if( !sgs__thread_serialize( C, var.data.T, &pSD->data, &pSD->argarray ) )
		{
			sgs_Msg( C, SGS_ERROR, "failed to serialize thread" );
			ret = SGS_FALSE;
			goto fail;
		}
	}
	else if( var.type == SGS_VT_OBJECT )
	{
		sgs_VarObj* O = var.data.O;
		sgs_VarObj* prevObj = pSD->curObj;
		_STACK_PREPARE;
		if( !O->iface->serialize )
		{
			sgs_Msg( C, SGS_WARNING, "Cannot serialize object of type '%s'", O->iface->name );
			ret = SGS_FALSE;
			goto fail;
		}
		pSD->curObj = O;
		_STACK_PROTECT;
		ret = SGS_SUCCEEDED( O->iface->serialize( C, O ) );
		_STACK_UNPROTECT;
		pSD->curObj = prevObj;
		if( ret == SGS_FALSE )
			goto fail;
	}
	else if( var.type == SGS_VT_CFUNC )
	{
		sgs_Msg( C, SGS_WARNING, "Cannot serialize C functions" );
		ret = SGS_FALSE;
		goto fail;
	}
	else if( var.type == SGS_VT_PTR )
	{
		sgs_Msg( C, SGS_WARNING, "Cannot serialize pointers" );
		ret = SGS_FALSE;
		goto fail;
	}
	else
	{
		sgs_StkIdx argidx;
		sgs_VHTVar* vv = sgs_vht_get( &pSD->servartable, &var );
		if( vv )
			argidx = (sgs_StkIdx) ( vv - pSD->servartable.vars );
		else
		{
			sgs_Variable idxvar;
			char pb[2];
			{
				pb[0] = 'P';
				/* WP: basetype limited to bits 0-7, sign interpretation does not matter */
				pb[1] = (char) var.type;
			}
			
			argidx = sgs_vht_size( &pSD->servartable );
			idxvar.type = SGS_VT_INT;
			idxvar.data.I = argidx;
			sgs_vht_set( &pSD->servartable, C, &var, &idxvar );
			
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
						ret = SGS_FALSE;
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
				sgs_Msg( C, SGS_ERROR, "sgs_Serialize: unknown memory error" );
				ret = SGS_FALSE;
				goto fail;
			}
		}
		sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
	}
	
fail:
	if( ep )
	{
		if( ret )
		{
			if( SD.data.size > 0x7fffffff )
			{
				sgs_Msg( C, SGS_ERROR, "serialized string too long" );
				ret = SGS_FALSE;
			}
			else
			{
				/* WP: added error condition */
				sgs_PushStringBuf( C, SD.data.ptr, (sgs_SizeVal) SD.data.size );
			}
		}
		if( ret == SGS_FALSE )
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
	sgs_VHTVar* vv;
	sgs_Variable V;
	sgs_StkIdx argidx;
	sgs_serialize2_data* pSD = (sgs_serialize2_data*) C->serialize_state;
	
	if( args < 0 || (size_t) args > pSD->argarray.size / sizeof(sgs_StkIdx) )
	{
		sgs_Msg( C, SGS_APIERR, "sgs_SerializeObject: specified "
			"more arguments than there are serialized items" );
		return;
	}
	/* WP: added error condition */
	argsize = sizeof(sgs_StkIdx) * (size_t) args;
	
	V.type = SGS_VT_OBJECT;
	V.data.O = pSD->curObj;
	vv = sgs_vht_get( &pSD->servartable, &V );
	if( vv )
		argidx = (sgs_StkIdx) ( vv - pSD->servartable.vars );
	else
	{
		sgs_Variable idxvar;
		char pb[6] = { 'C', 0, 0, 0, 0, 0 };
		{
			/* WP: they were pointless */
			pb[1] = (char)((args)&0xff);
			pb[2] = (char)((args>>8)&0xff);
			pb[3] = (char)((args>>16)&0xff);
			pb[4] = (char)((args>>24)&0xff);
		}
		
		/* WP: have error condition + sign interpretation doesn't matter */
		pb[ 5 ] = (char) fnsize;
		sgs_membuf_appbuf( &pSD->data, C, pb, 6 );
		sgs_membuf_appbuf( &pSD->data, C, pSD->argarray.ptr + pSD->argarray.size - argsize, argsize );
		sgs_membuf_appbuf( &pSD->data, C, func, fnsize );
		sgs_membuf_appchr( &pSD->data, C, '\0' );
		
		argidx = sgs_vht_size( &pSD->servartable );
		idxvar.type = SGS_VT_INT;
		idxvar.data.I = argidx;
		sgs_vht_set( &pSD->servartable, C, &V, &idxvar );
	}
	sgs_membuf_erase( &pSD->argarray, pSD->argarray.size - argsize, pSD->argarray.size );
	sgs_membuf_appbuf( &pSD->argarray, C, &argidx, sizeof(argidx) );
}

SGSBOOL sgs_UnserializeInt_V2( SGS_CTX, char* str, char* strend )
{
	sgs_Variable var;
	SGSBOOL res = SGS_TRUE;
	sgs_MemBuf mb = sgs_membuf_create();
	_STACK_PREPARE;
	
	_STACK_PROTECT;
	while( str < strend )
	{
		char c = *str++;
		if( c == 'P' )
		{
			if( str >= strend && sgs_unserr_incomp( C ) )
				goto fail;
			c = *str++;
			switch( c )
			{
			case SGS_VT_NULL: var.type = SGS_VT_NULL; break;
			case SGS_VT_BOOL:
				if( str >= strend && sgs_unserr_incomp( C ) )
					goto fail;
				var.type = SGS_VT_BOOL;
				var.data.B = *str++ != 0;
				break;
			case SGS_VT_INT:
				if( str >= strend-7 && sgs_unserr_incomp( C ) )
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
				if( str >= strend-7 && sgs_unserr_incomp( C ) )
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
					if( str >= strend-3 && sgs_unserr_incomp( C ) )
						goto fail;
					SGS_AS_INT32( strsz, str );
					str += 4;
					if( str > strend - strsz && sgs_unserr_incomp( C ) )
						goto fail;
					sgs_InitStringBuf( C, &var, str, strsz );
					str += strsz;
				}
				break;
			case SGS_VT_FUNC:
				{
					sgs_SizeVal bcsz;
					sgs_iFunc* fn;
					if( str >= strend-3 && sgs_unserr_incomp( C ) )
						goto fail;
					SGS_AS_INT32( bcsz, str );
					str += 4;
					if( str > strend - bcsz && sgs_unserr_incomp( C ) )
						goto fail;
					/* WP: conversion does not affect values */
					if( !_unserialize_function( C, str, (size_t) bcsz, &fn ) )
						goto fail; /* error already printed */
					var.type = SGS_VT_FUNC;
					var.data.F = fn;
					fn->refcount++;
					str += bcsz;
				}
				break;
			default:
				sgs_unserr_error( C );
				goto fail;
			}
		}
		else if( c == 'C' )
		{
			sgs_StkIdx subsz;
			int32_t i, pos, argc;
			int fnsz, ret;
			if( str >= strend-5 && sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( argc, str );
			str += 4;
			fnsz = *str++ + 1;
			for( i = 0; i < argc; ++i )
			{
				if( str >= strend-4 && sgs_unserr_incomp( C ) )
					goto fail;
				SGS_AS_INT32( pos, str );
				str += 4;
				if( pos < 0 || (size_t) pos >= mb.size / sizeof(sgs_Variable) )
				{
					sgs_unserr_error( C );
					goto fail;
				}
				sgs_PushVariable( C, ((sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( mb.ptr, 4 ))[ pos ] );
			}
			if( str > strend - fnsz && sgs_unserr_incomp( C ) )
				goto fail;
			subsz = sgs_StackSize( C ) - argc;
			ret = SGS_SUCCEEDED( sgs_GlobalCall( C, str, argc, 1 ) );
			if( ret == SGS_FALSE || sgs_StackSize( C ) - subsz < 1 )
			{
				sgs_unserr_objcall( C );
				res = ret;
				goto fail;
			}
			sgs_GetStackItem( C, -1, &var );
			sgs_SetStackSize( C, subsz );
			str += fnsz;
		}
		else if( c == 'T' )
		{
			int32_t i, pos, argc;
			sgs_Context* T = NULL;
			if( str >= strend-5 && sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( argc, str );
			str += 4;
			for( i = 0; i < argc; ++i )
			{
				if( str >= strend-4 && sgs_unserr_incomp( C ) )
					goto fail;
				SGS_AS_INT32( pos, str );
				str += 4;
				if( pos < 0 || (size_t) pos >= mb.size / sizeof(sgs_Variable) )
				{
					sgs_unserr_error( C );
					goto fail;
				}
				sgs_PushVariable( C, ((sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( mb.ptr, 4 ))[ pos ] );
			}
			if( !sgs__thread_unserialize( C, &T, &str, strend ) )
				return sgs_unserr_incomp( C );
			sgs_Pop( C, argc );
			sgs_InitThreadPtr( &var, T );
		}
		else if( c == 'S' )
		{
			int32_t pos;
			if( str >= strend-4 && sgs_unserr_incomp( C ) )
				goto fail;
			SGS_AS_INT32( pos, str );
			str += 4;
			if( !sgs_GetSymbol( C, ((sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( mb.ptr, 4 ))[ pos ], &var ) )
			{
				return sgs_unserr_symfail( C );
			}
		}
		else
		{
			sgs_unserr_error( C );
			goto fail;
		}
		sgs_membuf_appbuf( &mb, C, &var, sizeof(var) );
	}
	
	if( mb.size )
		sgs_PushVariable( C, *(sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( mb.ptr + mb.size - sizeof(sgs_Variable), 4 ) );
	res = mb.size != 0;
fail:
	_STACK_UNPROTECT_SKIP( res );
	{
		sgs_Variable* ptr = (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( mb.ptr, 4 );
		sgs_Variable* pend = (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( mb.ptr + mb.size, 4 );
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
			sprintf( tmp, "%g", var.data.R );
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
			ci = &((s3callinfo*) data->callinfo.ptr)[ call_info_index ];
			args = &((int32_t*) data->callargs.ptr)[ ci->arg_offset ];
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
		var = sgs_MakeNull();
	}
	else if( var.type == SGS_VT_FUNC || var.type == SGS_VT_CFUNC )
	{
		sgs_Msg( C, SGS_WARNING, "serialization mode 3 (SGSON text)"
			" does not support function serialization" );
		var = sgs_MakeNull();
	}
	else if( var.type == SGS_VT_PTR )
	{
		sgs_Msg( C, SGS_WARNING, "serialization mode 3 (SGSON text)"
			" does not support pointer serialization" );
		var = sgs_MakeNull();
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
			pSD->curObj = O;
			_STACK_PROTECT;
			ret = SGS_SUCCEEDED( O->iface->serialize( C, O ) );
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
			s3callinfo* ci = (s3callinfo*) SD.callinfo.ptr;
			s3callinfo* ciend = (s3callinfo*) ( SD.callinfo.ptr + SD.callinfo.size );
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

void sgs_SerializeObjectInt_V3( SGS_CTX, sgs_StkIdx args, const char* func, size_t fnsize )
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


static int sgs_UnserializeInt_V3( SGS_CTX, char* str, char* strend )
{
	int res;
	sgs_MemBuf stack = sgs_membuf_create();
	sgs_membuf_appchr( &stack, C, 0 );
	res = !sgson_parse( C, &stack, str, strend - str );
	sgs_membuf_destroy( &stack, C );
	return res;
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

void sgs_SerializeSGSONFmt( SGS_CTX, sgs_Variable var, const char* tab )
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
			
			/* find the function */
			sgs_GetGlobal( C, sgs_StackItem( C, i + 1 ), &func );
			
			/* call the function */
			sgs_Call( C, func, sgs_StackSize( C ) - ( i + 2 ), 1 );
			
			/* clean up */
			sgs_Release( C, &func );
			sgs_PopSkip( C, 2, 1 );
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
					SGSON_STK_PUSH( '(' );
					sgs_PushVariable( C, SGS_FSTKTOP ); /* marker for beginning of function */
					sgs_PushStringBuf( C, idstart, (sgs_SizeVal)( idend - idstart ) );
				}
				else /* map{..} */
				{
					SGSON_STK_PUSH( '{' );
					sgs_CreateMap( C, NULL, 0 );
				}
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
		if( ret == NULL )
			var->type = SGS_VT_FUNC;
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
	i16 linenum
	i16[instrcount] lineinfo
	i32 funcname_size
	byte[funcname_size] funcname
	varlist consts
	instr[instrcount] instrs
*/
static int bc_write_sgsfunc( sgs_iFunc* F, SGS_CTX, sgs_MemBuf* outbuf )
{
	uint32_t size = F->sfuncname->size;
	uint16_t cc, ic;
	uint8_t gntc[4] = { F->gotthis, F->numargs, F->numtmp, F->numclsr };
	
	/* WP: const/instruction limits */
	cc = (uint16_t)( F->instr_off / sizeof( sgs_Variable ) );
	ic = (uint16_t)( ( F->size - F->instr_off ) / sizeof( sgs_instr_t ) );

	sgs_membuf_appbuf( outbuf, C, &cc, sizeof( cc ) );
	sgs_membuf_appbuf( outbuf, C, &ic, sizeof( ic ) );
	sgs_membuf_appbuf( outbuf, C, gntc, 4 );
	sgs_membuf_appbuf( outbuf, C, &F->linenum, sizeof( sgs_LineNum ) );
	sgs_membuf_appbuf( outbuf, C, F->lineinfo, sizeof( uint16_t ) * ic );
	sgs_membuf_appbuf( outbuf, C, &size, sizeof( size ) );
	sgs_membuf_appbuf( outbuf, C, sgs_str_cstr( F->sfuncname ), F->sfuncname->size );

	if( !bc_write_varlist( sgs_func_consts( F ), C, cc, outbuf ) )
		return 0;

	sgs_membuf_appbuf( outbuf, C, sgs_func_bytecode( F ), sizeof( sgs_instr_t ) * ic );
	return 1;
}

static const char* bc_read_sgsfunc( decoder_t* D, sgs_Variable* var )
{
	sgs_Variable strvar;
	sgs_iFunc* F = NULL;
	uint32_t coff, ioff, size, fnsize;
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
	SGS_AS_INT16( F->linenum, D->buf + 8 );
	if( D->convend )
		F->linenum = (sgs_LineNum) esi16( F->linenum );
	F->lineinfo = sgs_Alloc_n( sgs_LineNum, ic );
	F->sfuncname = NULL;
	F->sfilename = NULL;
	D->buf += 10;
	
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

	var->data.F = F;
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
	i16 constcount
	i16 instrcount
	byte gotthis
	byte numargs
	byte numtmp
	byte numclsr
	-- -- -- -- -- 8 bytes in the previous section
	varlist consts
	i32[instrcount] instrs
	linenum[instrcount] lines
*/
int sgsBC_Func2Buf( SGS_CTX, sgs_CompFunc* func, sgs_MemBuf* outbuf )
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
		uint16_t cc, ic;
		uint8_t gntc[4] = { func->gotthis, func->numargs, func->numtmp, func->numclsr };
		
		/* max. count: 65535, max. variable size: 16 bytes */
		cc = (uint16_t) ( func->consts.size / sizeof( sgs_Variable ) );
		ic = (uint16_t) ( func->code.size / sizeof( sgs_instr_t ) );
		
		sgs_membuf_appbuf( outbuf, C, &cc, sizeof( cc ) );
		sgs_membuf_appbuf( outbuf, C, &ic, sizeof( ic ) );
		sgs_membuf_appbuf( outbuf, C, gntc, 4 );
		
		if( !bc_write_varlist( (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( func->consts.ptr, 4 ), C,
			cc, outbuf ) )
			return 0;
		
		sgs_membuf_appbuf( outbuf, C, func->code.ptr, sizeof( sgs_instr_t ) * ic );
		sgs_membuf_appbuf( outbuf, C, func->lnbuf.ptr, sizeof( sgs_LineNum ) * ic );
		
		{
			/* WP: bytecode size limit */
			uint32_t outbufsize = (uint32_t) ( outbuf->size - origobsize );
			memcpy( outbuf->ptr + origobsize + 10, &outbufsize, sizeof(uint32_t) );
		}
		
		return 1;
	}
}

const char* sgsBC_Buf2Func( SGS_CTX, const char* fn, const char* buf, size_t size, sgs_CompFunc** outfunc )
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
	{
		const char* ret = "data error";
		uint16_t cc, ic, cci;
		sgs_CompFunc* func = sgsBC_MakeCompFunc( C );
		SGS_AS_UINT16( cc, buf + 14 );
		SGS_AS_UINT16( ic, buf + 16 );
		SGS_AS_UINT8( func->gotthis, buf + 18 );
		SGS_AS_UINT8( func->numargs, buf + 19 );
		SGS_AS_UINT8( func->numtmp, buf + 20 );
		SGS_AS_UINT8( func->numclsr, buf + 21 );
		D->buf = buf + 22;
		
		if( D->convend )
		{
			/* WP: int promotion will not affect the result */
			cc = (uint16_t) esi16( cc );
			ic = (uint16_t) esi16( ic );
		}
		
		if( SGSNOMINDEC( cc + ic * sizeof( sgs_LineNum ) ) )
		{
			sgsBC_Free( C, func );
			return "data error (expected fn. data)";
		}
		
		sgs_membuf_resize( &func->consts, C, sizeof( sgs_Variable ) * cc );
		sgs_membuf_resize( &func->code, C, sizeof( sgs_instr_t ) * ic );
		sgs_membuf_resize( &func->lnbuf, C, sizeof( sgs_LineNum ) * ic );
		for( cci = 0; cci < cc; ++cci )
		{
			((sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( func->consts.ptr, 4 ))[ cci ].type = SGS_VT_NULL;
		}
		
		ret = bc_read_varlist( D, (sgs_Variable*) (void*) SGS_ASSUME_ALIGNED( func->consts.ptr, 4 ), cc );
		if( ret )
		{
			sgsBC_Free( C, func );
			return ret;
		}
		
		ret = "data error (expected fn. instructions)";
		if( SGSNOMINDEC( sizeof( sgs_instr_t ) * ic ) )
			goto free_fail;
		
		memcpy( func->code.ptr, D->buf, sizeof( sgs_instr_t ) * ic );
		if( D->convend )
			esi32_array( (sgs_instr_t*) (void*) SGS_ASSUME_ALIGNED( func->code.ptr, 4 ), ic );
		D->buf += sizeof( sgs_instr_t ) * ic;
		
		ret = "data error (expected fn. line numbers)";
		if( SGSNOMINDEC( sizeof( sgs_LineNum ) * ic ) )
			goto free_fail;
		
		memcpy( func->lnbuf.ptr, D->buf, sizeof( sgs_LineNum ) * ic );

		*outfunc = func;
		return NULL;
		
free_fail:
		sgsBC_Free( C, func );
		return ret;
	}
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


