
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


static void default_printfn( void* ctx, int type, int line, const char* msg )
{
	const char* errpfxs[ 3 ] = { "Info", "Warning", "Error" };
	UNUSED( ctx );
	if( line >= 0 )
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
	C->has_this = FALSE;

	ht_init( &C->data, 4 );

	C->vars = NULL;
	C->varcount = 0;
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
			sgs_Free( (sgs_VarPtr) p->ptr );
		p++;
	}
	ht_free( &C->data );
#ifdef GC
	while( C->vars )
		sgsVM_VarDestroy( C, C->vars );
#endif

	sgs_Free( C );

#if SGS_DEBUG && SGS_DEBUG_MEMORY && SGS_DEBUG_CHECK_LEAKS
	sgs_DumpMemoryInfo();
#endif
}


int sgs_ExecBuffer( SGS_CTX, const char* buf, int32_t size )
{
	TokenList tlist = NULL;
	FTNode* ftree = NULL;
	sgs_CompFunc* func = NULL;

	DBGINFO( "sgs_ExecBuffer called!" );

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
#if SGS_PROFILE_BYTECODE || ( SGS_DEBUG && SGS_DEBUG_DATA )
	sgsBC_Dump( func );
#endif

	DBGINFO( "...executing the generated function" );
	C->gclist = (sgs_VarPtr) func->consts.ptr;
	C->gclist_size = func->consts.size / sizeof( sgs_Variable );
	sgsVM_ExecFn( C, func->code.ptr, func->code.size, func->consts.ptr, func->consts.size );
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

int sgs_Stat( SGS_CTX, int type )
{
	switch( type )
	{
	case SGS_STAT_VARCOUNT: return C->varcount;
	default:
		return 0;
	}
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

	C->print_fn( C->print_ctx, type, line, info.ptr );

	strbuf_destroy( &info );
}

