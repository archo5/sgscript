
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#define SGS_INTERNAL
#define SGS_REALLY_INTERNAL
#define SGS_INTERNAL_STRINGTABLES

#include "sgs_int.h"



#if SGS_DEBUG && SGS_DEBUG_FLOW
#  define DBGINFO( text ) sgs_Msg( C, SGS_INFO, text )
#else
#  define DBGINFO( text )
#endif


static const char* g_varnames[] = { "null", "bool", "int", "real", "string", "func", "cfunc", "obj", "ptr" };


static void ctx_init( SGS_CTX )
{
	C->version = SGS_VERSION_INT;
	C->apiversion = SGS_API_VERSION;
	C->output_fn = sgs_StdOutputFunc;
	C->output_ctx = stdout;
	C->minlev = SGS_INFO;
	C->apilev = SGS_ERROR;
	C->msg_fn = sgs_StdMsgFunc;
	C->msg_ctx = stderr;
	C->last_errno = 0;
	C->hook_fn = NULL;
	C->hook_ctx = NULL;
	
	C->state = 0;
	C->fctx = NULL;
	C->filename = NULL;
	vht_init( &C->stringtable, C, 256, 256 );
	
	C->stack_mem = 32;
	C->stack_base = sgs_Alloc_n( sgs_Variable, C->stack_mem );
	C->stack_off = C->stack_base;
	C->stack_top = C->stack_base;
	
	C->clstk_mem = 32;
	C->clstk_base = sgs_Alloc_n( sgs_Closure*, C->clstk_mem );
	C->clstk_off = C->clstk_base;
	C->clstk_top = C->clstk_base;
	
	C->sf_first = NULL;
	C->sf_last = NULL;
	C->sf_cached = NULL;
	C->sf_count = 0;
	
	C->objs = NULL;
	C->objcount = 0;
	C->redblue = 0;
	C->gclist = NULL;
	C->gclist_size = 0;
	C->gcrun = FALSE;
#if SGS_OBJPOOL_SIZE > 0
	C->objpool_data = sgs_Alloc_n( sgs_ObjPoolItem, SGS_OBJPOOL_SIZE );
#else
	C->objpool_data = NULL;
#endif
	C->objpool_size = 0;
	
	vht_init( &C->typetable, C, 32, 32 );
	
#ifdef SGS_JIT
	sgsJIT_Init( C );
#endif
}

sgs_Context* sgs_CreateEngineExt( sgs_MemFunc memfunc, void* mfuserdata )
{
	SGS_CTX = (sgs_Context*) memfunc( mfuserdata, NULL, sizeof( sgs_Context ) );
	C->memsize = sizeof( sgs_Context );
	C->numallocs = 1;
	C->numblocks = 1;
	C->numfrees = 0;
	C->memfunc = memfunc;
	C->mfuserdata = mfuserdata;
	ctx_init( C );
	sgsSTD_GlobalInit( C );
	sgsSTD_PostInit( C );
	return C;
}


void sgs_DestroyEngine( SGS_CTX )
{
	sgs_StackFrame* sf = C->sf_cached, *sfn;

	/* clear the stack */
	while( C->stack_base != C->stack_top )
	{
		C->stack_top--;
		sgs_Release( C, C->stack_top );
	}
	while( C->clstk_base != C->clstk_top )
	{
		C->clstk_top--;
		if( --(*C->clstk_top)->refcount < 1 )
		{
			sgs_Release( C, &(*C->clstk_top)->var );
			sgs_Dealloc( *C->clstk_top );
		}
	}
	
	sgsSTD_GlobalFree( C );
	
	sgs_GCExecute( C );
	sgs_BreakIf( C->objs || C->objcount );
	
	sgs_Dealloc( C->stack_base );
	
	sgs_Dealloc( C->clstk_base );
	
	vht_free( &C->typetable, C );
	
	{
		VHTVar* p = C->stringtable.vars;
		VHTVar* pend = p + C->stringtable.size;
		while( p < pend )
		{
			string_t* st = p->key.data.S;
			st->refcount--;
			sgs_BreakIf( st->refcount > 0 );
			sgs_BreakIf( st->refcount < 0 );
			sgs_Dealloc( st );
			p++;
		}
	}
	vht_free( &C->stringtable, C );
	
	/* free the call stack */
	while( sf )
	{
		sfn = sf->cached;
		sgs_Dealloc( sf );
		sf = sfn;
	}
	C->sf_cached = NULL;
	
#ifdef SGS_JIT
	sgsJIT_Destroy( C );
#endif
	
#if SGS_OBJPOOL_SIZE > 0
	{
		int32_t i;
		for( i = 0; i < C->objpool_size; ++i )
			sgs_Dealloc( C->objpool_data[ i ].obj );
		sgs_Dealloc( C->objpool_data );
	}
#endif

#ifdef SGS_DEBUG_LEAKS
	sgs_BreakIf( C->memsize > sizeof( sgs_Context ) );
	sgs_BreakIf( C->memsize < sizeof( sgs_Context ) );
#endif
	
	C->msg_fn = NULL;
	C->msg_ctx = NULL;
	
	C->memfunc( C->mfuserdata, C, 0 );
}


const char* sgs_CodeString( int type, int val )
{
	if( type == SGS_CODE_ER )
	{
		if( val < SGS_EINPROC || val > SGS_SUCCESS )
			return NULL;
		return sgs_ErrNames[  -  val ];
	}
	else if( type == SGS_CODE_VT )
	{
		if( val < 0 || val >= SGS_VT__COUNT )
			return NULL;
		return sgs_VarNames[ val ];
	}
	else if( type == SGS_CODE_OP )
	{
		if( val < 0 || val >= SGS_SI_COUNT )
			return NULL;
		return sgs_OpNames[ val ];
	}
	else if( type == SGS_CODE_OI )
	{
		if( val < 0 || val >= (int) ARRAY_SIZE( sgs_IfaceNames ) )
			return NULL;
		return sgs_IfaceNames[ val ];
	}
	else
		return NULL;
}


void sgs_GetOutputFunc( SGS_CTX, sgs_OutputFunc* outf, void** outc )
{
	*outf = C->output_fn;
	*outc = C->output_ctx;
}

void sgs_SetOutputFunc( SGS_CTX, sgs_OutputFunc func, void* ctx )
{
	sgs_BreakIf( func == NULL );
	if( func == SGSOUTPUTFN_DEFAULT )
		func = sgs_StdOutputFunc;
	if( !ctx && func == sgs_StdOutputFunc )
		ctx = stdout;
	C->output_fn = func;
	C->output_ctx = ctx;
}

void sgs_Write( SGS_CTX, const void* ptr, size_t size )
{
	C->output_fn( C->output_ctx, C, ptr, size );
}

SGSBOOL sgs_Writef( SGS_CTX, const char* what, ... )
{
	char buf[ SGS_OUTPUT_STACKBUF_SIZE ];
	MemBuf info = membuf_create();
	int cnt;
	va_list args;
	char* ptr = buf;
	
	va_start( args, what );
	cnt = SGS_VSPRINTF_LEN( what, args );
	va_end( args );
	
	if( cnt < 0 )
		return FALSE;
	
	if( cnt >= SGS_OUTPUT_STACKBUF_SIZE )
	{
		/* WP: false alarm */
		membuf_resize( &info, C, (size_t) cnt + 1 );
		ptr = info.ptr;
	}
	
	va_start( args, what );
	vsprintf( ptr, what, args );
	va_end( args );
	ptr[ cnt ] = 0;
	
	sgs_WriteStr( C, ptr );
	
	membuf_destroy( &info, C );
	return TRUE;
}


void sgs_GetMsgFunc( SGS_CTX, sgs_MsgFunc* outf, void** outc )
{
	*outf = C->msg_fn;
	*outc = C->msg_ctx;
}

void sgs_SetMsgFunc( SGS_CTX, sgs_MsgFunc func, void* ctx )
{
	sgs_BreakIf( func == NULL );
	if( func == SGSMSGFN_DEFAULT )
		func = sgs_StdMsgFunc;
	else if( func == SGSMSGFN_DEFAULT_NOABORT )
		func = sgs_StdMsgFunc_NoAbort;
	if( !ctx && ( func == sgs_StdMsgFunc || func == sgs_StdMsgFunc_NoAbort ) )
		ctx = stderr;
	C->msg_fn = func;
	C->msg_ctx = ctx;
}

int sgs_Msg( SGS_CTX, int type, const char* what, ... )
{
	char buf[ SGS_OUTPUT_STACKBUF_SIZE ];
	MemBuf info = membuf_create();
	int off = 0, cnt = 0, slen = 0;
	va_list args;
	char* ptr = buf;

	if( type < C->minlev )
		return 0;

	va_start( args, what );
	cnt = SGS_VSPRINTF_LEN( what, args );
	va_end( args );
	
	sgs_BreakIf( cnt < 0 );
	if( cnt < 0 )
	{
		C->msg_fn( C->msg_ctx, C, SGS_ERROR, "sgs_Msg ERROR: failed to print the message" );
		return 0;
	}
	
	if( C->sf_last && C->sf_last->nfname )
	{
		/* WP: it is expected that native functions ..
		.. will not set names of more than 2GB length */
		slen = (int) strlen( C->sf_last->nfname );
		off = slen + SGS_MSG_EXTRABYTES;
		cnt += off;
	}

	if( cnt >= SGS_OUTPUT_STACKBUF_SIZE )
	{
		/* WP: cnt is never negative */
		membuf_resize( &info, C, (size_t) cnt + 1 );
		ptr = info.ptr;
	}
	
	if( C->sf_last && C->sf_last->nfname )
	{
		/* WP: slen is generated from size_t, thus is never negative */
		memcpy( ptr, C->sf_last->nfname, (size_t) slen );
		memcpy( ptr + slen, SGS_MSG_EXTRASTRING, SGS_MSG_EXTRABYTES );
	}

	va_start( args, what );
	vsprintf( ptr + off, what, args );
	va_end( args );
	ptr[ cnt ] = 0;

	C->msg_fn( C->msg_ctx, C, type, ptr );

	membuf_destroy( &info, C );
	
	return 0;
}


void sgs_WriteErrorInfo( SGS_CTX, int flags, sgs_ErrorOutputFunc func, void* ctx, int type, const char* msg )
{
	if( flags & SGS_ERRORINFO_STACK )
	{
		sgs_StackFrame* p = sgs_GetFramePtr( C, FALSE );
		UNUSED( ctx );
		while( p != NULL )
		{
			const char* file, *name;
			int ln;
			if( !p->next && !p->code )
				break;
			sgs_StackFrameInfo( C, p, &name, &file, &ln );
			if( ln )
				func( ctx, "- \"%s\" in %s, line %d\n", name, file, ln );
			else
				func( ctx, "- \"%s\" in %s\n", name, file );
			p = p->next;
		}
	}
	if( flags & SGS_ERRORINFO_ERROR )
	{
		static const char* errpfxs[ 3 ] = { "Info", "Warning", "Error" };
		type = type / 100 - 1;
		if( type < 0 ) type = 0;
		if( type > 2 ) type = 2;
		func( ctx, "%s: %s\n", errpfxs[ type ], msg );
	}
}

static void serialize_output_func( void* ud, SGS_CTX, const void* ptr, size_t datasize )
{
	MemBuf* B = (MemBuf*) ud;
	membuf_appbuf( B, C, ptr, datasize );
}

void sgs_PushErrorInfo( SGS_CTX, int flags, int type, const char* msg )
{
	sgs_OutputFunc oldfn = C->output_fn;
	void* oldctx = C->output_ctx;
	
	MemBuf B = membuf_create();
	C->output_fn = serialize_output_func;
	C->output_ctx = &B;
	
	sgs_WriteErrorInfo( C, flags, (sgs_ErrorOutputFunc) sgs_Writef, C, type, msg );
	
	/* WP: hopefully the error messages will not be more than 2GB long */
	sgs_PushStringBuf( C, B.ptr, (sgs_SizeVal) B.size );
	
	membuf_destroy( &B, C );
	C->output_fn = oldfn;
	C->output_ctx = oldctx;
}


SGSBOOL sgs_GetHookFunc( SGS_CTX, sgs_HookFunc* outf, void** outc )
{
	if( C->hook_fn )
	{
		*outf = C->hook_fn;
		*outc = C->hook_ctx;
		return 1;
	}
	return 0;
}

void sgs_SetHookFunc( SGS_CTX, sgs_HookFunc func, void* ctx )
{
	C->hook_fn = func;
	C->hook_ctx = ctx;
}


void* sgs_Memory( SGS_CTX, void* ptr, size_t size )
{
	void* p;
	if( size )
	{
		size += 16;
		C->memsize += size;
		C->numallocs++;
		C->numblocks++;
	}
	if( ptr )
	{
		ptr = ((char*)ptr) - 16;
		C->memsize -= *(size_t*)ptr;
		C->numfrees++;
		C->numblocks--;
	}
	p = C->memfunc( C->mfuserdata, ptr, size );
	if( p )
	{
		memcpy( p, &size, sizeof(size_t) );
		p = ((char*)p) + 16;
	}
	return p;
}


static int ctx_decode( SGS_CTX, const char* buf, size_t size, sgs_CompFunc** out )
{
	sgs_CompFunc* func = NULL;

	if( sgsBC_ValidateHeader( buf, size ) < SGS_HEADER_SIZE )
	{
		/* invalid / unsupported / unrecognized file */
		return 0;
	}

	{
		const char* ret;
		ret = sgsBC_Buf2Func( C, C->filename ? C->filename : "", buf, size, &func );
		if( ret )
		{
			/* just invalid, error! */
			sgs_Msg( C, SGS_ERROR, "Failed to read bytecode file (%s)", ret );
			return -1;
		}
	}

	*out = func;
	return 1;
}

static int ctx_compile( SGS_CTX, const char* buf, size_t size, sgs_CompFunc** out )
{
	sgs_CompFunc* func = NULL;
	TokenList tlist = NULL;
	FTNode* ftree = NULL;

	C->state = 0;

	DBGINFO( "...running the tokenizer" );
	tlist = sgsT_Gen( C,  buf, size );
	if( !tlist || C->state & SGS_HAS_ERRORS )
		goto error;
#if SGS_DEBUG && SGS_DEBUG_DATA
	sgsT_DumpList( tlist, NULL );
#endif

	DBGINFO( "...running the function tree builder" );
	ftree = sgsFT_Compile( C, tlist );
	if( !ftree || C->state & SGS_HAS_ERRORS )
		goto error;
#if SGS_DEBUG && SGS_DEBUG_DATA
	sgsFT_Dump( ftree );
#endif

	DBGINFO( "...generating the opcode" );
	func = sgsBC_Generate( C, ftree );
	if( !func || C->state & SGS_HAS_ERRORS )
		goto error;
	DBGINFO( "...cleaning up tokens/function tree" );
	sgsFT_Destroy( C, ftree ); ftree = NULL;
	sgsT_Free( C, tlist ); tlist = NULL;
#if SGS_DUMP_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	sgsBC_Dump( func );
#endif

	*out = func;
	return 1;

error:
	if( func )	sgsBC_Free( C, func );
	if( ftree ) sgsFT_Destroy( C, ftree );
	if( tlist ) sgsT_Free( C, tlist );

	return 0;
}

static SGSRESULT ctx_execute( SGS_CTX, const char* buf, size_t size, int clean, int* rvc )
{
	int returned, rr;
	ptrdiff_t oo;
	sgs_CompFunc* func;

	oo = C->stack_off - C->stack_base;

	if( !( rr = ctx_decode( C, buf, size, &func ) ) &&
		!ctx_compile( C, buf, size, &func ) )
		return SGS_ECOMP;

	if( rr < 0 )
		return SGS_EINVAL;

	DBGINFO( "...executing the generated function" );
	C->stack_off = C->stack_top;
	C->gclist = (sgs_Variable*) (void*) ASSUME_ALIGNED( func->consts.ptr, 4 );
	/* WP: const limit */
	C->gclist_size = (uint16_t) func->consts.size / sizeof( sgs_Variable );
	returned = sgsVM_ExecFn( C, func->numtmp, func->code.ptr, func->code.size,
		func->consts.ptr, func->consts.size, clean, (uint16_t*) (void*) ASSUME_ALIGNED( func->lnbuf.ptr, 4 ) );
	if( rvc )
		*rvc = returned;
	C->gclist = NULL;
	C->gclist_size = 0;
	C->stack_off = C->stack_base + oo;

	DBGINFO( "...cleaning up bytecode/constants" );
	sgsBC_Free( C, func );

	DBGINFO( "...finished!" );
	return SGS_SUCCESS;
}

SGSRESULT sgs_EvalBuffer( SGS_CTX, const char* buf, size_t size, int* rvc )
{
	DBGINFO( "sgs_EvalBuffer called!" );
	
	if( size > 0x7fffffff )
		return SGS_EINVAL;
	
	return ctx_execute( C, buf, size, !rvc, rvc );
}

SGSRESULT sgs_EvalFile( SGS_CTX, const char* file, int* rvc )
{
	int ret;
	long len;
	size_t ulen;
	FILE* f;
	char* data;
	const char* ofn;
	char magic[4];
	DBGINFO( "sgs_EvalFile called!" );
	
	f = fopen( file, "rb" );
	if( !f )
	{
		sgs_Errno( C, 0 );
		return SGS_ENOTFND;
	}
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	if( len < 0 )
	{
		sgs_Errno( C, 0 );
		return SGS_EINPROC;
	}
	fseek( f, 0, SEEK_SET );
	
	if( len > 4 )
	{
		if( fread( magic, 1, 4, f ) != 4 )
		{
			sgs_Errno( C, 0 );
			fclose( f );
			return SGS_EINPROC;
		}
		if( ( magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F' ) // ELF binary
		 || ( magic[0] == 'M' && magic[1] == 'Z' ) ) // DOS binary / DOS stub for PE binary
		{
			sgs_Errno( C, 1 );
			fclose( f );
			return SGS_EINVAL;
		}
		fseek( f, 0, SEEK_SET );
	}
	
	/* WP: len is always in the range of size_t */
	ulen = (size_t) len;
	
	data = sgs_Alloc_n( char, ulen );
	if( fread( data, 1, ulen, f ) != ulen )
	{
		sgs_Errno( C, 0 );
		fclose( f );
		sgs_Dealloc( data );
		return SGS_EINPROC;
	}
	fclose( f );
	
	ofn = C->filename;
	C->filename = file;
	ret = ctx_execute( C, data, ulen, rvc ? FALSE : TRUE, rvc );
	C->filename = ofn;
	
	sgs_Dealloc( data );
	return ret;
}

SGSRESULT sgs_Compile( SGS_CTX, const char* buf, size_t size, char** outbuf, size_t* outsize )
{
	MemBuf mb;
	sgs_CompFunc* func;
	
	if( size > 0x7fffffff )
		return SGS_EINVAL;
	
	if( !ctx_compile( C, buf, size, &func ) )
		return SGS_ECOMP;
	
	mb = membuf_create();
	if( !sgsBC_Func2Buf( C, func, &mb ) )
	{
		membuf_destroy( &mb, C );
		return SGS_EINPROC;
	}
	
	*outbuf = mb.ptr;
	*outsize = mb.size;
	
	sgsBC_Free( C, func );
	
	return SGS_SUCCESS;
}


static void _recfndump( const char* constptr, size_t constsize,
	const char* codeptr, size_t codesize, int gt, int args, int tmp, int clsr )
{
	const sgs_Variable* var = (const sgs_Variable*) (const void*) ASSUME_ALIGNED( constptr, 4 );
	const sgs_Variable* vend = (const sgs_Variable*) (const void*) ASSUME_ALIGNED( constptr + constsize, 4 );
	while( var < vend )
	{
		if( var->type == SGS_VT_FUNC )
		{
			_recfndump( (const char*) func_consts( var->data.F ), var->data.F->instr_off,
				(const char*) func_bytecode( var->data.F ), var->data.F->size - var->data.F->instr_off,
				var->data.F->gotthis, var->data.F->numargs, var->data.F->numtmp, var->data.F->numclsr );
		}
		var++;
	}
	printf( "\nFUNC: type=%s args=%d tmp=%d closures=%d\n", gt ? "method" : "function", args, tmp, clsr );
	sgsBC_DumpEx( constptr, constsize, codeptr, codesize );
}

SGSRESULT sgs_DumpCompiled( SGS_CTX, const char* buf, size_t size )
{
	int rr;
	sgs_CompFunc* func;
	
	if( size > 0x7fffffff )
		return SGS_EINVAL;
	
	if( !( rr = ctx_decode( C, buf, size, &func ) ) &&
		!ctx_compile( C, buf, size, &func ) )
		return SGS_ECOMP;
	
	if( rr < 0 )
		return SGS_EINVAL;
	
	_recfndump( func->consts.ptr, func->consts.size, func->code.ptr, func->code.size,
		func->gotthis, func->numargs, func->numtmp, func->numclsr );
	
	sgsBC_Free( C, func );
	return SGS_SUCCESS;
}


SGSRESULT sgs_Abort( SGS_CTX )
{
	sgs_StackFrame* sf = C->sf_last;
	if( sf && !sf->iptr )
		sf = sf->prev; /* should be able to use this inside a C function */
	if( !sf || !sf->iptr )
		return SGS_ENOTFND;
	while( sf && sf->iptr )
	{
		sf->iptr = sf->iend;
		sf = sf->prev;
	}
	return SGS_SUCCESS;
}



static void ctx_print_safe( SGS_CTX, const char* str, size_t size )
{
	const char* strend = str + size;
	while( str < strend )
	{
		if( *str == ' ' || isgraph( *str ) )
			sgs_Write( C, str, 1 );
		else
		{
			static const char* hexdigs = "0123456789ABCDEF";
			char buf[ 4 ] = { '\\', 'x', 0, 0 };
			{
				buf[2] = hexdigs[ (*str & 0xf0) >> 4 ];
				buf[3] = hexdigs[ *str & 0xf ];
			}
			sgs_Write( C, buf, 4 );
		}
		str++;
	}
}

static void dumpobj( SGS_CTX, sgs_VarObj* p )
{
	sgs_Writef( C, "OBJECT %p refcount=%d data=%p iface=%p prev=%p next=%p redblue=%s",
		p, p->refcount, p->data, p->iface, p->prev, p->next, p->redblue ? "R" : "B" );
}

/* g_varnames ^^ */

static void dumpvar( SGS_CTX, sgs_Variable* var )
{
	if( var->type > SVT_PTR )
	{
		sgs_Writef( C, "INVALID TYPE %d\n", (int) var->type );
		return;
	}
	/* WP: var->type base type info uses bits 1-8 */
	sgs_Writef( C, "%s (size:%d)", g_varnames[ var->type ], sgsVM_VarSize( var ) );
	switch( var->type )
	{
	case SVT_NULL: break;
	case SVT_BOOL: sgs_Writef( C, " = %s", var->data.B ? "true" : "false" ); break;
	case SVT_INT: sgs_Writef( C, " = %" PRId64, var->data.I ); break;
	case SVT_REAL: sgs_Writef( C, " = %f", var->data.R ); break;
	case SVT_STRING:
		sgs_Writef( C, " [rc:%d] = \"", var->data.S->refcount );
		ctx_print_safe( C, var_cstr( var ), MIN( var->data.S->size, 16 ) );
		sgs_Writef( C, var->data.S->size > 16 ? "...\"" : "\"" );
		break;
	case SVT_FUNC:
		sgs_Writef( C, " [rc:%d] '%s'[%d]%s tmp=%d clsr=%d", var->data.F->refcount,
			var->data.F->funcname.ptr ? var->data.F->funcname.ptr : "<anonymous>",
			(int) var->data.F->numargs, var->data.F->gotthis ? " (method)" : "",
			(int) var->data.F->numtmp, (int) var->data.F->numclsr );
		break;
	case SVT_CFUNC: sgs_Writef( C, " = %p", var->data.C ); break;
	case SVT_OBJECT: sgs_Writef( C, " = " ); dumpobj( C, var->data.O ); break;
	case SVT_PTR: sgs_Writef( C, " = %p", var->data.P ); break;
	}
}

ptrdiff_t sgs_Stat( SGS_CTX, int type )
{
	switch( type )
	{
	/* WP: not important */
	case SGS_STAT_VERSION: return (ptrdiff_t) C->version;
	case SGS_STAT_APIVERSION: return (ptrdiff_t) C->apiversion;
	case SGS_STAT_OBJCOUNT: return (ptrdiff_t) C->objcount;
	case SGS_STAT_MEMSIZE: return (ptrdiff_t) C->memsize;
	case SGS_STAT_NUMALLOCS: return (ptrdiff_t) C->numallocs;
	case SGS_STAT_NUMFREES: return (ptrdiff_t) C->numfrees;
	case SGS_STAT_NUMBLOCKS: return (ptrdiff_t) C->numblocks;
	case SGS_STAT_DUMP_STACK:
		{
			sgs_Variable* p = C->stack_base;
			sgs_WriteStr( C, "\nVARIABLE -- ---- STACK ---- BASE ----\n" );
			while( p < C->stack_top )
			{
				if( p == C->stack_off )
				{
					sgs_WriteStr( C, "VARIABLE -- ---- STACK ---- OFFSET ----\n" );
				}
				sgs_Writef( C, "VARIABLE %02d ", p - C->stack_base );
				dumpvar( C, p );
				sgs_WriteStr( C, "\n" );
				p++;
			}
			sgs_WriteStr( C, "VARIABLE -- ---- STACK ---- TOP ----\n" );
		}
		return SGS_SUCCESS;
	case SGS_STAT_DUMP_GLOBALS:
		{
			VHTVar *p, *pend;
			sgsSTD_GlobalIter( C, &p, &pend );
			sgs_WriteStr( C, "\nGLOBAL ---- LIST ---- START ----\n" );
			while( p < pend )
			{
				sgs_iStr* str = p->key.data.S;
				sgs_WriteStr( C, "GLOBAL '" );
				ctx_print_safe( C, str_cstr( str ), str->size );
				sgs_WriteStr( C, "' = " );
				dumpvar( C, &p->val );
				sgs_WriteStr( C, "\n" );
				p++;
			}
			sgs_WriteStr( C, "GLOBAL ---- LIST ---- END ----\n" );
		}
		return SGS_SUCCESS;
	case SGS_STAT_DUMP_OBJECTS:
		{
			object_t* p = C->objs;
			sgs_WriteStr( C, "\nOBJECT ---- LIST ---- START ----\n" );
			while( p )
			{
				dumpobj( C, p );
				sgs_WriteStr( C, "\n" );
				p = p->next;
			}
			sgs_WriteStr( C, "OBJECT ---- LIST ---- END ----\n" );
		}
		return SGS_SUCCESS;
	case SGS_STAT_DUMP_FRAMES:
		{
			sgs_StackFrame* p = sgs_GetFramePtr( C, FALSE );
			sgs_WriteStr( C, "\nFRAME ---- LIST ---- START ----\n" );
			while( p != NULL )
			{
				const char* file, *name;
				int ln;
				sgs_StackFrameInfo( C, p, &name, &file, &ln );
				sgs_Writef( C, "FRAME \"%s\" in %s, line %d\n", name, file, ln );
				p = p->next;
			}
			sgs_WriteStr( C, "FRAME ---- LIST ---- END ----\n" );
		}
		return SGS_SUCCESS;
	case SGS_STAT_DUMP_STATS:
		{
			sgs_WriteStr( C, "\nSTATS ---- ---- ----\n" );
			sgs_Writef( C, "# allocs: %d\n", C->numallocs );
			sgs_Writef( C, "# frees: %d\n", C->numfrees );
			sgs_Writef( C, "# mem blocks: %d\n", C->numblocks );
			sgs_Writef( C, "# mem bytes: %d\n", C->memsize );
			sgs_Writef( C, "# objects: %d\n", C->objcount );
			sgs_Writef( C, "GC state: %s\n", C->redblue ? "red" : "blue" );
			sgs_WriteStr( C, "---- ---- ---- -----\n" );
		}
		return SGS_SUCCESS;
	case SGS_STAT_XDUMP_STACK:
		{
			ptrdiff_t i = 0, ssz = C->stack_top - C->stack_base;
			sgs_WriteStr( C, "\nVARIABLE -- ---- STACK ---- BASE ----\n" );
			while( i < ssz )
			{
				sgs_Variable* p = C->stack_base + i;
				if( p == C->stack_off )
				{
					sgs_WriteStr( C, "VARIABLE -- ---- STACK ---- OFFSET ----\n" );
				}
				sgs_Writef( C, "VARIABLE %02d ", (int) i );
				dumpvar( C, p );
				sgs_WriteStr( C, "\n" );
				sgs_PushVariable( C, p );
				if( SGS_SUCCEEDED( sgs_DumpVar( C, 6 ) ) )
				{
					sgs_WriteStr( C, sgs_ToString( C, -1 ) );
					sgs_Pop( C, 1 );
					sgs_WriteStr( C, "\n" );
				}
				i++;
			}
			sgs_WriteStr( C, "VARIABLE -- ---- STACK ---- TOP ----\n" );
		}
		return SGS_SUCCESS;
	default:
		return SGS_EINVAL;
	}
}

int32_t sgs_Cntl( SGS_CTX, int what, int32_t val )
{
	int32_t x;
	switch( what )
	{
	/* WP: last bit of C->state is not used */
	case SGS_CNTL_STATE: x = (int32_t) C->state; C->state = (uint32_t) val; return x;
	case SGS_CNTL_GET_STATE: return (int32_t) C->state;
	case SGS_CNTL_MINLEV: x = C->minlev; C->minlev = val; return x;
	case SGS_CNTL_GET_MINLEV: return C->minlev;
	case SGS_CNTL_APILEV: x = C->apilev; C->apilev = val; return x;
	case SGS_CNTL_GET_APILEV: return C->apilev;
	case SGS_CNTL_ERRNO: x = C->last_errno; C->last_errno = val ? 0 : errno; return x;
	case SGS_CNTL_SET_ERRNO: x = C->last_errno; C->last_errno = val; return x;
	case SGS_CNTL_GET_ERRNO: return C->last_errno;
	}
	return 0;
}

void sgs_StackFrameInfo( SGS_CTX, sgs_StackFrame* frame, const char** name, const char** file, int* line )
{
	int L = 0;
	const char* N = "<non-callable type>";
	const char* F = "<buffer>";

	UNUSED( C );
	if( !frame->func )
	{
		N = "<main>";
		L = frame->lntable[ frame->lptr - frame->code ];
		if( frame->filename )
			F = frame->filename;
	}
	else if( frame->func->type == SGS_VT_FUNC )
	{
		N = "<anonymous function>";
		if( frame->func->data.F->funcname.size )
			N = frame->func->data.F->funcname.ptr;
		L = !frame->func->data.F->lineinfo ? 1 :
			frame->func->data.F->lineinfo[ frame->lptr - frame->code ];
		if( frame->func->data.F->filename.size )
			F = frame->func->data.F->filename.ptr;
		else if( frame->filename )
			F = frame->filename;
	}
	else if( frame->func->type == SGS_VT_CFUNC )
	{
		N = frame->nfname ? frame->nfname : "[C function]";
		F = "[C code]";
	}
	else if( frame->func->type == SGS_VT_OBJECT )
	{
		N = "<object>";
		F = "[C code]";
	}
	if( name ) *name = N;
	if( file ) *file = F;
	if( line ) *line = L;
}

sgs_StackFrame* sgs_GetFramePtr( SGS_CTX, int end )
{
	return end ? C->sf_last : C->sf_first;
}


SGSRESULT sgs_RegisterType( SGS_CTX, const char* name, sgs_ObjInterface* iface )
{
	size_t len;
	VHTVar* p;
	if( !iface )
		return SGS_EINVAL;
	len = strlen( name );
	if( len > 0x7fffffff )
		return SGS_EINVAL;
	/* WP: error condition */
	p = vht_get_str( &C->typetable, name, (uint32_t) len, sgs_HashFunc( name, len ) );
	if( p )
		return SGS_EINPROC;
	{
		sgs_Variable tmp;
		tmp.type = SVT_PTR;
		tmp.data.P = iface;
		sgs_PushStringBuf( C, name, (sgs_SizeVal) len );
		vht_set( &C->typetable, C, C->stack_top-1, &tmp );
		sgs_Pop( C, 1 );
	}
	return SGS_SUCCESS;
}

SGSRESULT sgs_UnregisterType( SGS_CTX, const char* name )
{
	size_t len = strlen( name );
	if( len > 0x7fffffff )
		return SGS_EINVAL;
	/* WP: error condition */
	VHTVar* p = vht_get_str( &C->typetable, name, (uint32_t) len, sgs_HashFunc( name, len ) );
	if( !p )
		return SGS_ENOTFND;
	vht_unset( &C->typetable, C, &p->key );
	return SGS_SUCCESS;
}

sgs_ObjInterface* sgs_FindType( SGS_CTX, const char* name )
{
	size_t len = strlen( name );
	if( len > 0x7fffffff )
		return NULL;
	/* WP: error condition */
	VHTVar* p = vht_get_str( &C->typetable, name, (uint32_t) len, sgs_HashFunc( name, len ) );
	if( p )
		return (sgs_ObjInterface*) p->val.data.P;
	return NULL;
}


void sgs_FuncName( SGS_CTX, const char* fnliteral )
{
	if( C->sf_last )
		C->sf_last->nfname = fnliteral;
}

int sgs_HasFuncName( SGS_CTX )
{
	return !!C->sf_last && !!C->sf_last->nfname;
}


void sgs_PushStringBuf32( SGS_CTX, sgs_String32* S, const char* str, size_t len )
{
	sgs_BreakIf( len > 31 );
	S->data.refcount = 1;
	/* WP: range of len: 0-31 */
	S->data.size = (uint32_t) len;
	S->data.hash = sgs_HashFunc( str, len );
	S->data.isconst = 0;
	memcpy( S->buf, str, len );
	S->buf[ len ] = 0;
	
	{
		sgs_Variable v;
		v.type = SGS_VT_STRING;
		v.data.S = &S->data;
		sgs_PushVariable( C, &v );
	}
}

void sgs_CheckString32( sgs_String32* S )
{
	sgs_BreakIf( !sgs_IsFreedString32( S ) );
}

SGSBOOL sgs_IsFreedString32( sgs_String32* S )
{
	return S->data.refcount == 1;
}

