
#include <stdarg.h>

#include "sgs_ctx.h"
#include "sgs_tok.h"
#include "sgs_fnt.h"
#include "sgs_bcg.h"
#include "sgs_proc.h"


#if SGS_DEBUG && SGS_DEBUG_FLOW
 #ifdef _MSC_VER
  #define DBGINFO( ... ) sgs_Printf( C, SGS_INFO, -1, __VA_ARGS__ )
 #else
  #define DBGINFO( wat... ) sgs_Printf( C, SGS_INFO, -1, wat )
 #endif
#else
 #ifdef _MSC_VER
  #define DBGINFO( ... )
 #else
  #define DBGINFO( wat... )
 #endif
#endif


static void default_printfn( void* ctx, SGS_CTX, int type, int line, const char* msg )
{
	const char* errpfxs[ 3 ] = { "Info", "Warning", "Error" };
	sgs_StackFrame* p = sgs_GetFramePtr( C, FALSE );
	UNUSED( ctx );
	while( p != NULL )
	{
		char* file, *name;
		int ln;
		if( !p->next )
			break;
		sgs_StackFrameInfo( C, p, &name, &file, &ln );
		fprintf( stderr, "- \"%s\" in %s, line %d\n", name, file, ln );
		p = p->next;
	}
	if( line > 0 )
		fprintf( stderr, "%s [line %d]: %s\n", errpfxs[ type ], line, msg );
	else
		fprintf( stderr, "%s: %s\n", errpfxs[ type ], msg );
}


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


static void ctx_init( SGS_CTX )
{
	C->print_fn = &default_printfn;
	C->print_ctx = NULL;

	C->state = 0;

	C->stack_mem = 32;
	C->stack_base = sgs_Alloc_n( sgs_Variable, C->stack_mem );
	C->stack_off = C->stack_base;
	C->stack_top = C->stack_base;

	C->call_args = 0;
	C->call_expect = 0;
	C->call_this = FALSE;

	C->sf_first = NULL;
	C->sf_last = NULL;

	ht_init( &C->data, 4 );

	C->objs = NULL;
	C->objcount = 0;
	C->redblue = 0;
	C->gclist = NULL;
	C->gclist_size = 0;

	C->array_func = default_array_func;
	C->dict_func = default_dict_func;
}

sgs_Context* sgs_CreateEngine()
{
	SGS_CTX = sgs_Alloc( sgs_Context );
	ctx_init( C );
	sgsVM_RegStdLibs( C );
	return C;
}

void sgs_DestroyEngine( SGS_CTX )
{
	HTPair *p, *pend;
	C->print_fn = NULL;
	C->print_ctx = NULL;

	while( C->stack_base != C->stack_top )
	{
		C->stack_top--;
		sgs_Release( C, C->stack_top );
	}
	sgs_Free( C->stack_base );

	p = C->data.pairs;
	pend = C->data.pairs + C->data.size;
	while( p < pend )
	{
		if( p->str && p->ptr )
		{
			sgs_Release( C, (sgs_VarPtr) p->ptr );
			sgs_Free( (sgs_VarPtr) p->ptr );
		}
		p++;
	}
	ht_free( &C->data );

	while( C->objs )
		sgsVM_VarDestroyObject( C, C->objs );

	sgs_Free( C );

#if SGS_DEBUG && SGS_DEBUG_MEMORY && SGS_DEBUG_CHECK_LEAKS
	sgs_DumpMemoryInfo();
#endif
}


static int ctx_execute( SGS_CTX, const char* buf, int32_t size, int clean, int* rvc )
{
	int returned;
	LNTable lnT;
	TokenList tlist = NULL;
	FTNode* ftree = NULL;
	sgs_CompFunc* func = NULL;

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
	sgsFT_Destroy( ftree ); ftree = NULL;
	sgsT_Free( tlist ); tlist = NULL;
	DBGINFO( "...generating line number table" );
	lht_init_all( &lnT, (uint16_t*) func->lnbuf.ptr, func->lnbuf.size / sizeof( uint16_t ) );
#if SGS_PROFILE_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	sgsBC_Dump( func );
#endif

	DBGINFO( "...executing the generated function" );
	C->gclist = (sgs_VarPtr) func->consts.ptr;
	C->gclist_size = func->consts.size / sizeof( sgs_Variable );
	returned = sgsVM_ExecFn( C, func->code.ptr, func->code.size, func->consts.ptr, func->consts.size, clean, &lnT );
	lht_free( &lnT );
	if( rvc )
		*rvc = returned;
	C->gclist = NULL;
	C->gclist_size = 0;

	DBGINFO( "...cleaning up bytecode/constants" );
	sgsBC_Free( C, func ); func = NULL;

	DBGINFO( "...finished!" );
	return SGS_SUCCESS;

error:
	if( func )	sgsBC_Free( C, func );
	if( ftree ) sgsFT_Destroy( ftree );
	if( tlist ) sgsT_Free( tlist );

	return SGS_ECOMP;
}

int sgs_ExecBuffer( SGS_CTX, const char* buf, int32_t size )
{
	DBGINFO( "sgs_ExecBuffer called!" );
	return ctx_execute( C, buf, size, TRUE, NULL );
}

int sgs_EvalBuffer( SGS_CTX, const char* buf, int size, int* rvc )
{
	DBGINFO( "sgs_EvalBuffer called!" );
	return ctx_execute( C, buf, size, FALSE, rvc );
}


int sgs_Stat( SGS_CTX, int type )
{
	switch( type )
	{
	case SGS_STAT_VARCOUNT: return C->objcount;
	default:
		return 0;
	}
}

void sgs_StackFrameInfo( SGS_CTX, sgs_StackFrame* frame, char** name, char** file, int* line )
{
	int L = 0;
	char* N = "<non-callable type>", *F = "<buffer>";

	UNUSED( C );
	if( !frame->func )
	{
		N = "<main>";
		L = lht_get( frame->lntable, frame->iptr - frame->code );
	}
	else if( frame->func->type == SVT_FUNC )
	{
		N = "<anonymous function>";
		if( frame->func->data.F->funcname.size )
			N = frame->func->data.F->funcname.ptr;
		L = lht_get( &frame->func->data.F->lineinfo, frame->iptr - frame->code );
	}
	else if( frame->func->type == SVT_CFUNC )
	{
		N = "<C function>";
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


void sgs_SetPrintFunc( SGS_CTX, sgs_PrintFunc func, void* ctx )
{
	C->print_fn = func;
	C->print_ctx = ctx;
}

void sgs_Printf( SGS_CTX, int type, int line, const char* what, ... )
{
	StrBuf info;
	int cnt;
	va_list args;

	if( !C->print_fn )
		return;

	info = strbuf_create();

	va_start( args, what );
#ifdef _MSC_VER
	cnt = _vscprintf( what, args );
#else
	cnt = vsnprintf( NULL, 0, what, args );
#endif
	va_end( args );

	strbuf_resize( &info, cnt );

	va_start( args, what );
	vsprintf( info.ptr, what, args );
	va_end( args );

	if( line < 0 && C->sf_last )
	{
		sgs_StackFrameInfo( C, C->sf_last, NULL, NULL, &line );
	}

	C->print_fn( C->print_ctx, C, type, line, info.ptr );

	strbuf_destroy( &info );
}

