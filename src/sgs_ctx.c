
#include <stdarg.h>
#include <ctype.h>

#define SGS_INTERNAL

#include "sgs_int.h"


#if SGS_DEBUG && SGS_DEBUG_FLOW
#  ifdef _MSC_VER
#    define DBGINFO( ... ) sgs_Printf( C, SGS_INFO, -1, __VA_ARGS__ )
#  else
#    define DBGINFO( wat... ) sgs_Printf( C, SGS_INFO, -1, wat )
#  endif
#else
#  ifdef _MSC_VER
#    define DBGINFO( ... )
#  else
#    define DBGINFO( wat... )
#  endif
#endif


static const char* g_varnames[] = { "null", "bool", "int", "real", "string", "func", "cfunc", "obj" };


static int default_array_func( SGS_CTX )
{
	sgs_Printf( C, SGS_ERROR, -1, "'array' creating function is undefined" );
	return 0;
}
static int default_dict_func( SGS_CTX )
{
	sgs_Printf( C, SGS_ERROR, -1, "'dict' creating function is undefined" );
	return 0;
}


static void default_outputfn( void* userdata, SGS_CTX, const void* ptr, sgs_SizeVal size )
{
	fwrite( ptr, 1, size, (FILE*) userdata );
}

static void default_printfn( void* ctx, SGS_CTX, int type, int line, const char* msg )
{
	const char* errpfxs[ 3 ] = { "Info", "Warning", "Error" };
	type = type / 100 - 1;
	if( type < 0 ) type = 0;
	if( type > 2 ) type = 2;
	sgs_StackFrame* p = sgs_GetFramePtr( C, FALSE );
	UNUSED( ctx );
	while( p != NULL )
	{
		const char* file, *name;
		int ln;
		if( !p->next )
			break;
		sgs_StackFrameInfo( C, p, &name, &file, &ln );
		fprintf( ctx, "- \"%s\" in %s, line %d\n", name, file, ln );
		p = p->next;
	}
	if( line > 0 )
		fprintf( ctx, "%s [line %d]: %s\n", errpfxs[ type ], line, msg );
	else
		fprintf( ctx, "%s: %s\n", errpfxs[ type ], msg );
}


static void ctx_init( SGS_CTX )
{
	C->version = ( ( ( SGS_VERSION_MAJOR << SGS_VERSION_OFFSET ) |
		SGS_VERSION_MINOR ) << SGS_VERSION_OFFSET | SGS_VERSION_INCR );
	C->output_fn = default_outputfn;
	C->output_ctx = stdout;
	C->print_fn = default_printfn;
	C->print_ctx = stderr;
	C->minlev = SGS_INFO;

	C->state = 0;
	C->fctx = NULL;
	C->filename = NULL;

	C->stack_mem = 32;
	C->stack_base = sgs_Alloc_n( sgs_Variable, C->stack_mem );
	C->stack_off = C->stack_base;
	C->stack_top = C->stack_base;

	C->call_args = 0;
	C->call_expect = 0;
	C->call_this = FALSE;

	C->sf_first = NULL;
	C->sf_last = NULL;

	ht_init( &C->data, C, 4 );

	C->objs = NULL;
	C->objcount = 0;
	C->redblue = 0;
	C->gclist = NULL;
	C->gclist_size = 0;

	C->array_func = default_array_func;
	C->dict_func = default_dict_func;
}

sgs_Context* sgs_CreateEngineExt( sgs_MemFunc memfunc, void* mfuserdata )
{
	SGS_CTX = memfunc( mfuserdata, NULL, sizeof( sgs_Context ) );
	C->memsize = sizeof( sgs_Context );
	C->memfunc = memfunc;
	C->mfuserdata = mfuserdata;
	ctx_init( C );
	sgsVM_RegStdLibs( C );
	return C;
}

void sgs_DestroyEngine( SGS_CTX )
{
	HTPair *p, *pend;
	C->print_fn = NULL;
	C->print_ctx = NULL;

	/* clear the stack */
	while( C->stack_base != C->stack_top )
	{
		C->stack_top--;
		sgs_Release( C, C->stack_top );
	}

	/* clear the globals' table */
	p = C->data.pairs;
	pend = C->data.pairs + C->data.size;
	while( p < pend )
	{
		if( p->str && p->ptr )
		{
			sgs_Release( C, (sgs_VarPtr) p->ptr );
			sgs_Dealloc( p->ptr );
		}
		p++;
	}

	/* unsetting keys one by one might reallocate the table more often than it's necessary */
	ht_free( &C->data, C );
	ht_init( &C->data, C, 4 );

	sgs_GCExecute( C );

	sgs_Dealloc( C->stack_base );
	ht_free( &C->data, C );

	C->memfunc( C->mfuserdata, C, 0 );
}


void sgs_SetOutputFunc( SGS_CTX, sgs_OutputFunc func, void* ctx )
{
	if( func == SGSOUTPUTFN_DEFAULT )
		func = default_outputfn;
	C->output_fn = func;
	C->output_ctx = ctx;
}

void sgs_Write( SGS_CTX, const void* ptr, sgs_SizeVal size )
{
	C->output_fn( C->output_ctx, C, ptr, size );
}

void sgs_Writef( SGS_CTX, const char* what, ... )
{
	char buf[ SGS_OUTPUT_STACKBUF_SIZE ];
	MemBuf info = membuf_create();
	int cnt;
	va_list args;
	char* ptr = buf;

	va_start( args, what );
	cnt = SGS_VSPRINTF_LEN( what, args );
	va_end( args );

	if( cnt >= SGS_OUTPUT_STACKBUF_SIZE )
	{
		membuf_resize( &info, C, cnt + 1 );
		ptr = info.ptr;
	}

	va_start( args, what );
	vsprintf( ptr, what, args );
	va_end( args );
	ptr[ cnt ] = 0;

	sgs_WriteStr( C, ptr );

	membuf_destroy( &info, C );
}


void sgs_SetPrintFunc( SGS_CTX, sgs_PrintFunc func, void* ctx )
{
	if( func == SGSPRINTFN_DEFAULT )
		func = default_printfn;
	C->print_fn = func;
	C->print_ctx = ctx;
}

void sgs_Printf( SGS_CTX, int type, int line, const char* what, ... )
{
	char buf[ SGS_OUTPUT_STACKBUF_SIZE ];
	MemBuf info = membuf_create();
	int cnt;
	va_list args;
	char* ptr = buf;

	va_start( args, what );
	cnt = SGS_VSPRINTF_LEN( what, args );
	va_end( args );

	if( cnt >= SGS_OUTPUT_STACKBUF_SIZE )
	{
		membuf_resize( &info, C, cnt + 1 );
		ptr = info.ptr;
	}

	va_start( args, what );
	vsprintf( ptr, what, args );
	va_end( args );
	ptr[ cnt ] = 0;

	if( line < 0 && C->sf_last )
	{
		sgs_StackFrameInfo( C, C->sf_last, NULL, NULL, &line );
	}

	C->print_fn( C->print_ctx, C, type, line, ptr );

	membuf_destroy( &info, C );
}

void* sgs_Memory( SGS_CTX, void* ptr, size_t size )
{
	void* p;
	sgs_BreakIf( size < 0 );
	if( size )
	{
		size += 16;
		C->memsize += size;
	}
	if( ptr )
	{
		ptr = ((char*)ptr) - 16;
		C->memsize -= AS_UINT32( ptr );
	}
	p = C->memfunc( C->mfuserdata, ptr, size );
	if( p )
	{
		AS_UINT32( p ) = size;
		p = ((char*)p) + 16;
	}
	return p;
}


static int ctx_decode( SGS_CTX, const char* buf, int32_t size, sgs_CompFunc** out )
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
			sgs_Printf( C, SGS_ERROR, -1, "Failed to read bytecode file (%s)", ret );
			return -1;
		}
	}

	*out = func;
	return 1;
}

static int ctx_compile( SGS_CTX, const char* buf, int32_t size, sgs_CompFunc** out )
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
#if SGS_PROFILE_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
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

static SGSRESULT ctx_execute( SGS_CTX, const char* buf, int32_t size, int clean, int* rvc )
{
	int returned, rr, oo;
	sgs_CompFunc* func;

	oo = C->stack_off - C->stack_base;

	if( !( rr = ctx_decode( C, buf, size, &func ) ) &&
		!ctx_compile( C, buf, size, &func ) )
		return SGS_ECOMP;

	if( rr < 0 )
		return SGS_EINVAL;

	DBGINFO( "...executing the generated function" );
	C->stack_off = C->stack_top;
	C->gclist = (sgs_VarPtr) func->consts.ptr;
	C->gclist_size = func->consts.size / sizeof( sgs_Variable );
	returned = sgsVM_ExecFn( C, func->code.ptr, func->code.size,
		func->consts.ptr, func->consts.size, clean, (uint16_t*) func->lnbuf.ptr );
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

SGSRESULT sgs_ExecBuffer( SGS_CTX, const char* buf, int32_t size )
{
	DBGINFO( "sgs_ExecBuffer called!" );
	return ctx_execute( C, buf, size, TRUE, NULL );
}

SGSRESULT sgs_EvalBuffer( SGS_CTX, const char* buf, int size, int* rvc )
{
	DBGINFO( "sgs_EvalBuffer called!" );
	return ctx_execute( C, buf, size, FALSE, rvc );
}

SGSRESULT sgs_EvalFile( SGS_CTX, const char* file, int* rvc )
{
	int ret;
	long len;
	FILE* f;
	char* data;
	const char* ofn;
	DBGINFO( "sgs_EvalFile called!" );

	f = fopen( file, "rb" );
	if( !f )
		return SGS_ENOTFND;
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );

	data = sgs_Alloc_n( char, len );
	if( fread( data, 1, len, f ) != len )
	{
		fclose( f );
		sgs_Dealloc( data );
		return SGS_EINPROC;
	}
	fclose( f );

	ofn = C->filename;
	C->filename = file;
	ret = ctx_execute( C, data, len, rvc ? FALSE : TRUE, rvc );
	C->filename = ofn;

	sgs_Dealloc( data );
	return ret;
}

SGSRESULT sgs_Compile( SGS_CTX, const char* buf, sgs_SizeVal size, char** outbuf, sgs_SizeVal* outsize )
{
	MemBuf mb;
	sgs_CompFunc* func;

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


static void _recfndump( const char* constptr, sgs_SizeVal constsize,
	const char* codeptr, sgs_SizeVal codesize )
{
	sgs_Variable* var = (sgs_Variable*) constptr;
	sgs_Variable* vend = (sgs_Variable*) ( constptr + constsize );
	while( var < vend )
	{
		if( var->type == SVT_FUNC )
		{
			_recfndump( (const char*) func_consts( var->data.F ), var->data.F->instr_off,
				(const char*) func_bytecode( var->data.F ), var->data.F->size - var->data.F->instr_off );
		}
		var++;
	}
	sgsBC_DumpEx( constptr, constsize, codeptr, codesize );
}

SGSRESULT sgs_DumpCompiled( SGS_CTX, const char* buf, sgs_SizeVal size )
{
	int rr;
	sgs_CompFunc* func;

	if( !( rr = ctx_decode( C, buf, size, &func ) ) &&
		!ctx_compile( C, buf, size, &func ) )
		return SGS_ECOMP;

	if( rr < 0 )
		return SGS_EINVAL;

	_recfndump( func->consts.ptr, func->consts.size, func->code.ptr, func->code.size );

	sgsBC_Free( C, func );
	return SGS_SUCCESS;
}



/* g_varnames ^^ */
static const char* g_ifitems[] =
{
	"end", "destruct", "clone", "gettype", "getprop", "setprop",
	"getindex", "setindex", "tobool", "toint", "toreal", "tostring",
	"dump", "gcmark", "getiter", "nextkey", "call", "compare",
	"add", "sub", "mul", "div", "mod", "negate", "flags"
};

static void ctx_print_safe( SGS_CTX, const char* str, sgs_SizeVal size )
{
	const char* strend = str + size;
	while( str < strend )
	{
		if( *str == ' ' || isgraph( *str ) )
			sgs_Write( C, str, 1 );
		else
		{
			static const char* hexdigs = "0123456789ABCDEF";
			char buf[ 4 ] = { '\\', 'x', hexdigs[ (*str & 0xf0) >> 4 ], hexdigs[ *str & 0xf ] };
			sgs_Write( C, buf, 4 );
		}
		str++;
	}
}

static void dumpobj( SGS_CTX, sgs_VarObj* p )
{
	char buf[ 320 ];
	void** ci = p->iface;
	buf[0] = 0;
	while( *ci )
	{
		int osi = ((int)*ci) == SOP_FLAGS ? ARRAY_SIZE( g_ifitems ) - 1 : ((int)*ci);
		if( *buf )
			strcat( buf, "," );
		strcat( buf, g_ifitems[ osi ] );
		ci += 2;
	}
	sgs_Writef( C, "OBJECT %p refcount=%d data=%p iface=%p (%s) prev=%p next=%p redblue=%s destroying=%s",
		p, p->refcount, p->data, p->iface, buf, p->prev, p->next, p->redblue ? "R" : "B", p->destroying ? "T" : "F" );
}

static void dumpvar( SGS_CTX, sgs_Variable* var )
{
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
		sgs_Writef( C, " [rc:%d] '%s'[%d]%s", var->data.F->refcount,
			var->data.F->funcname.ptr ? var->data.F->funcname.ptr : "<anonymous>",
			(int) var->data.F->numargs, var->data.F->gotthis ? " (method)" : "" );
		break;
	case SVT_CFUNC: sgs_Writef( C, " = %p", var->data.C ); break;
	case SVT_OBJECT: sgs_Writef( C, " = " ); dumpobj( C, var->data.O ); break;
	}
}

SGSMIXED sgs_Stat( SGS_CTX, int type )
{
	switch( type )
	{
	case SGS_STAT_VERSION: return C->version;
	case SGS_STAT_VARCOUNT: return C->objcount;
	case SGS_STAT_MEMSIZE: return C->memsize;
	case SGS_STAT_DUMP_STACK:
		{
			sgs_Variable* p = C->stack_base;
			sgs_WriteStr( C, "VARIABLE ---- STACK ---- BASE ----\n" );
			while( p < C->stack_top )
			{
				if( p == C->stack_off )
				{
					sgs_WriteStr( C, "VARIABLE ---- STACK ---- OFFSET ----\n" );
				}
				sgs_WriteStr( C, "VARIABLE " );
				dumpvar( C, (sgs_Variable*) p );
				sgs_WriteStr( C, "\n" );
				p++;
			}
			sgs_WriteStr( C, "VARIABLE ---- STACK ---- TOP ----\n" );
		}
		return SGS_SUCCESS;
	case SGS_STAT_DUMP_GLOBALS:
		{
			HTPair* p = C->data.pairs;
			HTPair* pend = C->data.pairs + C->data.size;
			sgs_WriteStr( C, "GLOBAL ---- LIST ---- START ----\n" );
			while( p < pend )
			{
				if( p->str )
				{
					sgs_WriteStr( C, "GLOBAL '" );
					ctx_print_safe( C, p->str, p->size );
					sgs_WriteStr( C, "' = " );
					dumpvar( C, (sgs_Variable*) p->ptr );
					sgs_WriteStr( C, "\n" );
				}
				p++;
			}
			sgs_WriteStr( C, "GLOBAL ---- LIST ---- END ----\n" );
		}
		return SGS_SUCCESS;
	case SGS_STAT_DUMP_OBJECTS:
		{
			object_t* p = C->objs;
			sgs_WriteStr( C, "OBJECT ---- LIST ---- START ----\n" );
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
			sgs_WriteStr( C, "FRAME ---- LIST ---- START ----\n" );
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
	default:
		return SGS_EINVAL;
	}
}

int32_t sgs_Cntl( SGS_CTX, int what, int32_t val )
{
	int32_t x;
	switch( what )
	{
	case SGS_CNTL_STATE: x = C->state; C->state = val; return x;
	case SGS_CNTL_GET_STATE: return C->state;
	case SGS_CNTL_MINLEV: x = C->minlev; C->minlev = val; return x;
	case SGS_CNTL_GET_MINLEV: return C->minlev;
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
		L = frame->lntable[ frame->iptr - frame->code ];
		F = C->filename;
	}
	else if( frame->func->type == SVT_FUNC )
	{
		N = "<anonymous function>";
		if( frame->func->data.F->funcname.size )
			N = frame->func->data.F->funcname.ptr;
		L = frame->func->data.F->lineinfo[ frame->iptr - frame->code ];
		if( frame->func->data.F->filename.size )
			F = frame->func->data.F->filename.ptr;
	}
	else if( frame->func->type == SVT_CFUNC )
	{
		N = "<C function>";
		F = "<C code>";
	}
	else if( frame->func->type == SVT_OBJECT )
	{
		N = "<object>";
	}
	if( name ) *name = N;
	if( file ) *file = F;
	if( line ) *line = L;
}

sgs_StackFrame* sgs_GetFramePtr( SGS_CTX, int end )
{
	return end ? C->sf_last : C->sf_first;
}

