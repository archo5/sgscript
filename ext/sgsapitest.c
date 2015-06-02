
#include <stdio.h>
#include <time.h>

#define SGS_USE_FILESYSTEM

#include "sgs_int.h"


const char* outfile = "apitests-output.log";
const char* outfile_errors = "apitests-errors.log";



#define DEFINE_TEST( name ) static void at_##name()

typedef void (*testfunc) ();
typedef struct _test_t { testfunc fn; const char* nm; } test_t;
#define TST( name ) { at_##name, #name }


extern test_t all_tests[];
const char* testname;


int all_tests_count();
void exec_tests()
{
	int i, count = all_tests_count();
	fclose( fopen( outfile, "w" ) );
	fclose( fopen( outfile_errors, "w" ) );
	
	for( i = 0; i < count; ++i )
	{
		testname = all_tests[ i ].nm;
		printf( "- [%02d] %-20s ...", i+1, all_tests[ i ].nm );
		all_tests[ i ].fn();
		printf( " OK\n" );
	}
}

int
#ifdef _MSC_VER
__cdecl
#endif
main( int argc, char** argv )
{
	printf( "\n//\n/// SGScript [API] test framework\n//\n" );

	exec_tests();

	return 0;
}


/* API */

void atf_abort(){ abort(); }
void atf_error( const char* msg, int line )
{
	printf( "\nERROR at line %d\n%s\n", line, msg );
	atf_abort();
}
void atf_warning( const char* msg, int line )
{
	printf( "\nWARNING at line %d\n%s\n", line, msg );
}

#define atf_defmsg( chk ) "test failed: \"" #chk "\""

#define atf_assert_( chk, msg, line ) do{ if( !(chk) ) atf_error( msg, line ); }while(0)
#define atf_assert( chk ) atf_assert_( chk, atf_defmsg( chk ), __LINE__ )

#define atf_check_( chk, msg, line ) do{ if( !(chk) ) atf_warning( msg, line ); }while(0)
#define atf_check( chk ) atf_check_( chk, atf_defmsg( chk ), __LINE__ )

void atf_assert_string_( const char* str1, const char* str2, const char* msg, int line )
{
	const char* s1 = str1, *s2 = str2;
	if( str1 == NULL || str2 == NULL )
	{
		printf( "\nERROR at line %d\n%s (at least one of the strings is NULL)\n", line, msg );
		atf_abort();
	}
	
	do
	{
		if( *s1 != *s2 )
		{
			const char* spaces = "           ";
			printf( "\nERROR at line %d\n%s (mismatch at pos. %d)\n", line, msg, (int)(size_t)( s1-str1+1 ) );
			printf( "...%-20s...\n", s1 - 10 > str1 ? s1 : str1 );
			printf( "...%-20s...\n", s2 - 10 > str2 ? s2 : str2 );
			printf( "   %.*s^\n", (int)(size_t)( s1 - str1 > 10 ? 10 : s1 - str1 ), spaces );
			atf_abort();
		}
		s1++;
		s2++;
	}
	while( *s1 && *s2 );
}
#define atf_assert_string( str1, str2 ) \
	atf_assert_string_( str1, str2, atf_defmsg( str1 == str2 ), __LINE__ )


/*************\
*             *
*  T E S T S  *
*             *
\*************/


FILE* outfp;
FILE* errfp;
sgs_Context* get_context()
{
	SGS_CTX = sgs_CreateEngine();
	atf_assert_( C, "could not create context (out of memory?)", __LINE__ );
	
	outfp = fopen( outfile, "a" );
	atf_assert_( outfp, "could not create output file", __LINE__ );
	setvbuf( outfp, NULL, _IONBF, 0 );
	
	errfp = fopen( outfile_errors, "a" );
	atf_assert_( errfp, "could not create error output file", __LINE__ );
	setvbuf( errfp, NULL, _IONBF, 0 );
	
	fprintf( outfp, "//\n/// O U T P U T  o f  %s\n//\n\n", testname );
	
	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, outfp );
	atf_assert( C->output_ctx == outfp );
	
	sgs_SetErrOutputFunc( C, SGSOUTPUTFN_DEFAULT, errfp );
	atf_assert( C->erroutput_ctx == errfp );
	
	return C;
}
void destroy_context( SGS_CTX )
{
	sgs_DestroyEngine( C );
	fclose( outfp );
	fclose( errfp );
}

int sgs_dummy_func( SGS_CTX ){ return 0; }
sgs_ObjInterface sgs_dummy_iface[1] =
{{
	"dummy",
	NULL, NULL,
	NULL, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL
}};


DEFINE_TEST( create_and_destroy )
{
	SGS_CTX = get_context();
	SGS_SHCTX_USE;
	sgs_Writef( C, "New context memory usage: %d B (%.2f kB)\n", (int) S->memsize, (double) S->memsize / 1024.0 );
	destroy_context( C );
}

DEFINE_TEST( array_mem )
{
	SGS_CTX = get_context();
	SGS_SHCTX_USE;
	sgs_PushArray( C, 0 );
	sgs_Writef( C, "context[array] memory usage: %d B (%.2f kB)\n", (int) S->memsize, (double) S->memsize / 1024.0 );
	destroy_context( C );
}

DEFINE_TEST( stack_101 )
{
	sgs_Variable sgs_dummy_var;
	SGS_CTX = get_context();
	
	sgs_dummy_var.type = SGS_VT_NULL;
	
	atf_assert( C->stack_base == C->stack_off );
	atf_assert( C->stack_top == C->stack_off );
	
	sgs_PushNull( C );
	sgs_PushBool( C, 1 );
	sgs_PushInt( C, 1337 );
	sgs_PushReal( C, 13.37 );
	sgs_PushStringBuf( C, "what is this", 7 );
	sgs_PushString( C, "what is this" );
	sgs_PushCFunction( C, sgs_dummy_func );
	sgs_PushObject( C, NULL, sgs_dummy_iface );
	sgs_PushVariable( C, &sgs_dummy_var );
	
	atf_assert( C->stack_base == C->stack_off );
	atf_assert( sgs_StackSize( C ) == 9 );
	atf_assert( C->stack_top == C->stack_off + 9 );
	
	atf_assert( C->stack_off[0].type == SGS_VT_NULL );
	atf_assert( C->stack_off[1].type == SGS_VT_BOOL );
	atf_assert( C->stack_off[2].type == SGS_VT_INT );
	atf_assert( C->stack_off[3].type == SGS_VT_REAL );
	atf_assert( C->stack_off[4].type == SGS_VT_STRING );
	atf_assert( C->stack_off[5].type == SGS_VT_STRING );
	atf_assert( C->stack_off[6].type == SGS_VT_CFUNC );
	atf_assert( C->stack_off[7].type == SGS_VT_OBJECT );
	atf_assert( C->stack_off[8].type == SGS_VT_NULL );
	
	atf_assert( C->stack_off[1].data.B == 1 );
	atf_assert( C->stack_off[2].data.I == 1337 );
	atf_assert( C->stack_off[3].data.R == 13.37 );
	atf_assert( C->stack_off[4].data.S->size == 7 );
	atf_assert_string( sgs_var_cstr( C->stack_off+4 ), "what is" );
	atf_assert( C->stack_off[5].data.S->size == 12 );
	atf_assert_string( sgs_var_cstr( C->stack_off+5 ), "what is this" );
	atf_assert( C->stack_off[6].data.C == sgs_dummy_func );
	atf_assert( C->stack_off[7].data.O->data == NULL );
	atf_assert( C->stack_off[7].data.O->iface == sgs_dummy_iface );
	
	atf_assert( sgs_Pop( C, 10 ) == SGS_EINVAL );
	atf_assert( sgs_StackSize( C ) == 9 );
	atf_assert( C->stack_top == C->stack_off + 9 );
	
	atf_assert( sgs_Pop( C, 9 ) == SGS_SUCCESS );
	atf_assert( C->stack_off == C->stack_top );
	atf_assert( C->stack_base == C->stack_off );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	destroy_context( C );
}

DEFINE_TEST( stack_insert )
{
	sgs_Variable sgs_dummy_var;
	SGS_CTX = get_context();
	
	sgs_dummy_var.type = SGS_VT_NULL;
	
	sgs_PushInt( C, 1 );
	sgs_PushInt( C, 2 );
	atf_assert( C->stack_top == C->stack_off + 2 );
	
	atf_assert( sgs_InsertVariable( C, 3, &sgs_dummy_var ) == SGS_EBOUNDS );
	atf_assert( sgs_InsertVariable( C, -4, &sgs_dummy_var ) == SGS_EBOUNDS );
	atf_assert( sgs_InsertVariable( C, -3, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_InsertVariable( C, 3, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_InsertVariable( C, 2, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 5 );
	
	atf_assert( C->stack_off[0].type == SGS_VT_NULL );
	atf_assert( C->stack_off[1].type == SGS_VT_INT );
	atf_assert( C->stack_off[2].type == SGS_VT_NULL );
	atf_assert( C->stack_off[3].type == SGS_VT_INT );
	atf_assert( C->stack_off[4].type == SGS_VT_NULL );
	
	atf_assert( sgs_Pop( C, 5 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	destroy_context( C );
}

DEFINE_TEST( stack_arraydict )
{
	SGS_CTX = get_context();
	
	sgs_PushNull( C );
	sgs_PushString( C, "key-one" );
	sgs_PushInt( C, 5 );
	sgs_PushString( C, "key-two" );
	
	atf_assert( sgs_PushArray( C, 5 ) == SGS_EINVAL );
	atf_assert( sgs_PushArray( C, 0 ) == SGS_SUCCESS );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	
	sgs_PushNull( C );
	sgs_PushString( C, "key-one" );
	sgs_PushInt( C, 5 );
	sgs_PushString( C, "key-two" );
	
	atf_assert( sgs_PushArray( C, 4 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 5 );
	
	atf_assert( sgs_PushDict( C, 6 ) == SGS_EINVAL );
	atf_assert( sgs_PushDict( C, 5 ) == SGS_EINVAL );
	atf_assert( sgs_PushDict( C, 0 ) == SGS_SUCCESS );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_PushDict( C, 4 ) == SGS_SUCCESS );
	
	atf_assert( sgs_StackSize( C ) == 2 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	/* TODO: test "not a string key" in dict creation */
	
	destroy_context( C );
}

DEFINE_TEST( stack_pushstore )
{
	SGS_CTX = get_context();
	
	sgs_PushNull( C );
	sgs_PushInt( C, 5 );
	atf_assert( sgs_PushItem( C, 2 ) == SGS_EBOUNDS );
	atf_assert( sgs_PushItem( C, -3 ) == SGS_EBOUNDS );
	atf_assert( sgs_PushItem( C, -2 ) == SGS_SUCCESS );
	atf_assert( sgs_PushItem( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 4 );
	
	atf_assert( sgs_StoreItem( C, -5 ) == SGS_EBOUNDS );
	atf_assert( sgs_StoreItem( C, 4 ) == SGS_EBOUNDS );
	atf_assert( sgs_StoreItem( C, -3 ) == SGS_SUCCESS );
	atf_assert( sgs_StoreItem( C, 0 ) == SGS_SUCCESS );
	
	atf_assert( C->stack_off[1].type == SGS_VT_INT );
	atf_assert( C->stack_off[1].data.I == 5 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	destroy_context( C );
}

DEFINE_TEST( stack_propindex )
{
	SGS_CTX = get_context();
	
	atf_assert( sgs_PushProperty( C, -1, "nope" ) == SGS_EBOUNDS );
	
	sgs_PushString( C, "key-one" );
	atf_assert( sgs_PushIndexII( C, 0, 1, 0 ) == SGS_EBOUNDS );
	atf_assert( sgs_PushIndexII( C, 0, 0, 0 ) == SGS_EINVAL );
	
	sgs_PushInt( C, 15 );
	atf_assert( sgs_PushIndexII( C, -2, -1, 0 ) == SGS_EBOUNDS );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	sgs_PushInt( C, 5 );
	atf_assert( sgs_PushIndexII( C, -2, -1, 0 ) == SGS_SUCCESS );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	
	sgs_PushString( C, "key-two" );
	sgs_PushNull( C );
	atf_assert( sgs_PushDict( C, 4 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	destroy_context( C );
}

static sgs_RegIntConst nidxints[] =
{
	{ "test", 42 },
	{ NULL, 0 },
};
DEFINE_TEST( stack_negidx )
{
	SGS_CTX = get_context();
	
	atf_assert( sgs_PushDict( C, 0 ) == SGS_SUCCESS );
	
	atf_assert( sgs_StoreIntConsts( C, -1, nidxints, -1 ) == SGS_SUCCESS );
	atf_assert( sgs_PushProperty( C, -1, "test" ) == SGS_SUCCESS );
	atf_assert( sgs_GetInt( C, -1 ) == 42 );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	destroy_context( C );
}

DEFINE_TEST( globals_101 )
{
	SGS_CTX = get_context();
	
	atf_assert( sgs_PushGlobal( C, "array" ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	atf_assert( sgs_PushGlobal( C, "donut_remover" ) == SGS_ENOTFND );
	atf_assert_( sgs_StackSize( C ) == 1, "wrong stack size after failed PushGlobal", __LINE__ );
	
	atf_assert( sgs_PushArray( C, 1 ) == SGS_SUCCESS );
	atf_assert_( sgs_StackSize( C ) == 1, "wrong stack size after PushArray", __LINE__ );
	atf_assert( sgs_StoreGlobal( C, "yarra" ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	destroy_context( C );
}

DEFINE_TEST( libraries )
{
	SGS_CTX = get_context();
	
	atf_assert( sgs_LoadLib_Fmt( C ) == SGS_SUCCESS );
	atf_assert( sgs_LoadLib_IO( C ) == SGS_SUCCESS );
	atf_assert( sgs_LoadLib_Math( C ) == SGS_SUCCESS );
	atf_assert( sgs_LoadLib_OS( C ) == SGS_SUCCESS );
	atf_assert( sgs_LoadLib_String( C ) == SGS_SUCCESS );
	
	atf_assert( sgs_PushGlobal( C, "fmt_text" ) == SGS_SUCCESS );
	atf_assert( sgs_PushGlobal( C, "io_file" ) == SGS_SUCCESS );
	atf_assert( sgs_PushGlobal( C, "pow" ) == SGS_SUCCESS );
	atf_assert( sgs_PushGlobal( C, "os_date_string" ) == SGS_SUCCESS );
	atf_assert( sgs_PushGlobal( C, "string_replace" ) == SGS_SUCCESS );
	
	destroy_context( C );
}

DEFINE_TEST( function_calls )
{
	SGS_CTX = get_context();
	
	atf_assert( sgs_PushGlobal( C, "array" ) == SGS_SUCCESS );
	atf_assert( sgs_Call( C, 5, 1 ) == SGS_EINVAL );
	atf_assert( sgs_Call( C, 1, 0 ) == SGS_EINVAL );
	atf_assert( sgs_Call( C, 0, 0 ) == SGS_SUCCESS );
	atf_assert( sgs_PushGlobal( C, "array" ) == SGS_SUCCESS );
	atf_assert( sgs_Call( C, 0, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_OBJECT );
	
	atf_assert( sgs_PushGlobal( C, "array" ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 2 );
	atf_assert( sgs_ThisCall( C, 1, 0 ) == SGS_EINVAL );
	atf_assert( sgs_ThisCall( C, 0, 0 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	destroy_context( C );
}

DEFINE_TEST( complex_gc )
{
	SGS_CTX = get_context();
	
	const char* str =
	"o = { a = [ { b = {}, c = 5 }, { d = { e = {} }, f = [] } ], g = [] };"
	"o.a[0].parent = o; o.a[1].d.parent = o; o.a[1].d.e.parent = o.a[1].d;"
	"o.a[1].f.push(o); o.g.push( o.a[1].f ); o.a.push( o.a );";
	
	atf_assert( sgs_ExecString( C, str ) == SGS_SUCCESS );
	atf_assert( sgs_GCExecute( C ) == SGS_SUCCESS );
	
	destroy_context( C );
}

void output_strcat( void* ud, SGS_CTX, const void* ptr, size_t size )
{
	char* dst = (char*) ud;
	const char* src = (const char*) ptr;
	
	if( strlen( dst ) + size + 1 > 64 )
	{
		/* WP: good enough for these tests */
		printf( "size: %d, whole string: %s%s\n", (int) size, dst, src );
		fflush( stdout );
	}
	atf_assert( strlen( dst ) + size + 1 <= 64 );
	
	strcat( dst, src );
}
DEFINE_TEST( commands )
{
	char pbuf[ 64 ] = {0};
	
	SGS_CTX = get_context();
	
	const char* str =
	"print('1');print('2','3');print();"
	"print '4'; print '5', '6'; print;";
	
	sgs_SetOutputFunc( C, output_strcat, pbuf );
	atf_assert( sgs_ExecString( C, str ) == SGS_SUCCESS );
	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, outfp );
	
	atf_assert_string( pbuf, "123456" );
	
	destroy_context( C );
}

DEFINE_TEST( debugging )
{
	int rvc;
	SGS_CTX = get_context();
	
	sgs_PushNull( C );
	sgs_PushBool( C, 1 );
	sgs_PushInt( C, 1337 );
	sgs_PushReal( C, 3.14 );
	sgs_PushString( C, "wat" );
	sgs_EvalString( C, "return function(){};", &rvc );
	atf_assert( sgs_PushGlobal( C, "print" ) == SGS_SUCCESS );
	sgs_PushArray( C, 0 );
	sgs_PushDict( C, 0 );
	
	atf_assert( sgs_StackSize( C ) == 9 );
	sgs_Stat( C, SGS_STAT_DUMP_STACK );
	sgs_Stat( C, SGS_STAT_DUMP_GLOBALS );
	sgs_Stat( C, SGS_STAT_DUMP_OBJECTS );
	sgs_Stat( C, SGS_STAT_DUMP_FRAMES );
	
	destroy_context( C );
}

DEFINE_TEST( varpaths )
{
	SGS_CTX = get_context();
	
	const char* str =
	"global o = { a = [ { b = {}, c = 5 }, { d = { e = {} }, f = [] } ], g = [] };"
	"o.a[0].parent = o; o.a[1].d.parent = o; o.a[1].d.e.parent = o.a[1].d;"
	"o.a[1].f.push(o); o.g.push( o.a[1].f ); o.a.push( o.a );";
	
	atf_assert( sgs_ExecString( C, str ) == SGS_SUCCESS );
	atf_assert( sgs_PushGlobal( C, "o" ) == SGS_SUCCESS );
	
	/* basic property retrieval */
	atf_assert( sgs_PushPath( C, -1, "p", "g" ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_OBJECT );
	atf_assert( sgs_IsObject( C, -1, sgsstd_array_iface ) );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	/* properties and indices */
	atf_assert( sgs_PushPath( C, -1, "piso", "a", 1, 1, "d", 0 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_OBJECT );
	atf_assert( sgs_IsObject( C, -1, sgsstd_dict_iface ) );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	/* storing data */
	sgs_PushInt( C, 42 );
	atf_assert( sgs_StorePath( C, -2, "pippp", "a", 1, "d", "e", "test" ) == SGS_SUCCESS );
	atf_assert( sgs_PushPath( C, -1, "pippp", "a", 1, "d", "e", "test" ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_INT );
	atf_assert( (C->stack_top-1)->data.I == 42 );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	destroy_context( C );
}

DEFINE_TEST( iterators )
{
	SGS_CTX = get_context();
	
	/* ARRAY ITERATOR */
	sgs_PushBool( C, 1 );
	sgs_PushInt( C, 42 );
	sgs_PushString( C, "wat" );
	
	atf_assert( sgs_StackSize( C ) == 3 );
	atf_assert( sgs_PushArray( C, 3 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	/* test iteration */
	atf_assert( sgs_PushIterator( C, 0 ) == SGS_SUCCESS );
	atf_assert( (C->stack_top-1)->type == SGS_VT_OBJECT );
	atf_assert( (C->stack_top-1)->data.O->iface == sgsstd_array_iter_iface );
	for(;;)
	{
		int ret = sgs_IterAdvance( C, -1 );
		atf_assert( SGS_SUCCEEDED( ret ) );
		if( !ret )
			break;
	}
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	
	/* test keys & values */
	atf_assert( sgs_PushIterator( C, 0 ) == SGS_SUCCESS );
	atf_assert( (C->stack_top-1)->type == SGS_VT_OBJECT );
	atf_assert( (C->stack_top-1)->data.O->iface == sgsstd_array_iter_iface );
	/* - values 1 */
	atf_assert( sgs_IterAdvance( C, -1 ) == 1 );
	atf_assert( sgs_IterPushData( C, -1, 1, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -2 ) == SGS_VT_INT );
	atf_assert( sgs_GetInt( C, -2 ) == 0 );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_BOOL );
	atf_assert( sgs_GetBool( C, -1 ) == 1 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	/* - values 2 */
	atf_assert( sgs_IterAdvance( C, -1 ) == 1 );
	atf_assert( sgs_IterPushData( C, -1, 1, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -2 ) == SGS_VT_INT );
	atf_assert( sgs_GetInt( C, -2 ) == 1 );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_INT );
	atf_assert( sgs_GetInt( C, -1 ) == 42 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	/* - values 3 */
	atf_assert( sgs_IterAdvance( C, -1 ) == 1 );
	atf_assert( sgs_IterPushData( C, -1, 1, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -2 ) == SGS_VT_INT );
	atf_assert( sgs_GetInt( C, -2 ) == 2 );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_STRING );
	atf_assert( sgs_GetStringSize( C, -1 ) == 3 );
	atf_assert( strcmp( sgs_GetStringPtr( C, -1 ), "wat" ) == 0 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	/* - end */
	atf_assert( sgs_IterAdvance( C, -1 ) == 0 );
	
	/* clean up */
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	/* DICT ITERATOR */
	sgs_PushString( C, "1st" );
	sgs_PushBool( C, 1 );
	sgs_PushString( C, "2nd" );
	sgs_PushInt( C, 42 );
	sgs_PushString( C, "3rd" );
	sgs_PushString( C, "wat" );
	
	atf_assert( sgs_StackSize( C ) == 6 );
	atf_assert( sgs_PushDict( C, 6 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	/* test iteration */
	atf_assert( sgs_PushIterator( C, 0 ) == SGS_SUCCESS );
	atf_assert( (C->stack_top-1)->type == SGS_VT_OBJECT );
	atf_assert( (C->stack_top-1)->data.O->iface == sgsstd_dict_iter_iface );
	for(;;)
	{
		int ret = sgs_IterAdvance( C, -1 );
		atf_assert( SGS_SUCCEEDED( ret ) );
		if( !ret )
			break;
	}
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	
	/* test keys & values */
	atf_assert( sgs_PushIterator( C, 0 ) == SGS_SUCCESS );
	atf_assert( (C->stack_top-1)->type == SGS_VT_OBJECT );
	atf_assert( (C->stack_top-1)->data.O->iface == sgsstd_dict_iter_iface );
	/* - values 1 */
	atf_assert( sgs_IterAdvance( C, -1 ) == 1 );
	atf_assert( sgs_IterPushData( C, -1, 1, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -2 ) == SGS_VT_STRING );
	atf_assert( sgs_GetStringSize( C, -2 ) == 3 );
	atf_assert( strcmp( sgs_GetStringPtr( C, -2 ), "1st" ) == 0 );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_BOOL );
	atf_assert( sgs_GetBool( C, -1 ) == 1 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	/* - values 2 */
	atf_assert( sgs_IterAdvance( C, -1 ) == 1 );
	atf_assert( sgs_IterPushData( C, -1, 1, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -2 ) == SGS_VT_STRING );
	atf_assert( sgs_GetStringSize( C, -2 ) == 3 );
	atf_assert( strcmp( sgs_GetStringPtr( C, -2 ), "2nd" ) == 0 );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_INT );
	atf_assert( sgs_GetInt( C, -1 ) == 42 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	/* - values 3 */
	atf_assert( sgs_IterAdvance( C, -1 ) == 1 );
	atf_assert( sgs_IterPushData( C, -1, 1, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -2 ) == SGS_VT_STRING );
	atf_assert( sgs_GetStringSize( C, -2 ) == 3 );
	atf_assert( strcmp( sgs_GetStringPtr( C, -2 ), "3rd" ) == 0 );
	atf_assert( sgs_ItemType( C, -1 ) == SGS_VT_STRING );
	atf_assert( sgs_GetStringSize( C, -1 ) == 3 );
	atf_assert( strcmp( sgs_GetStringPtr( C, -1 ), "wat" ) == 0 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	/* - end */
	atf_assert( sgs_IterAdvance( C, -1 ) == 0 );
	
	/* clean up */
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );
	
	destroy_context( C );
}


int nom_method_A( SGS_CTX )
{
	SGSFN( "nom::method_A" );
	sgs_PushInt( C, 5 );
	if( sgs_Method( C ) )
	{
		sgs_PushItem( C, -1 );
		sgs_StoreProperty( C, 0, "a" );
	}
	return 1;
}

int nom_method_B( SGS_CTX )
{
	SGSFN( "nom::method_B" );
	sgs_PushInt( C, 7 );
	if( sgs_Method( C ) )
	{
		sgs_PushItem( C, -1 );
		sgs_StoreProperty( C, 0, "b" );
	}
	return 1;
}

int nom_iface( SGS_CTX );
int nom_ctor( SGS_CTX )
{
	SGSFN( "nom::nom" );
	sgs_PushInterface( C, nom_iface );
	sgs_PushDict( C, 0 );
	sgs_ObjSetMetaObj( C, sgs_GetObjectStruct( C, -1 ), sgs_GetObjectStruct( C, -2 ) );
	return 1;
}

static sgs_RegFuncConst nom_funcs[] =
{
	{ "method_A", nom_method_A },
	{ "method_B", nom_method_B },
	{ "__call", nom_ctor },
	{ NULL, NULL },
};

int nom_iface( SGS_CTX )
{
	SGSFN( "nom_iface" );
	atf_assert( sgs_PushDict( C, 0 ) == SGS_SUCCESS );
	atf_assert( sgs_StoreFuncConsts( C, -1, nom_funcs, -1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	sgs_ObjSetMetaMethodEnable( sgs_GetObjectStruct( C, -1 ), 1 );
	return 1;
}

DEFINE_TEST( native_obj_meta )
{
	SGS_CTX = get_context();
	
	/* register as ctor/iface */
	sgs_PushInterface( C, nom_iface );
	sgs_StoreGlobal( C, "nom" );
	
	/* try creating and running the object */
	atf_assert( sgs_ExecString( C, ""
		"a = nom();\n"
		"printvar(a.method_A());\n"
		"printvar(a.method_B());\n"
		"function nom.method_C(){ this.c = 9; return 9; }\n"
		"printvar(a.method_C());\n"
		"printvar(a);\n"
	"" ) == SGS_SUCCESS );
	
	destroy_context( C );
}

DEFINE_TEST( fork_state )
{
	SGS_CTX, *CFF, *CFP;
	
	/* --- basic usage --- */
	C = get_context();
	
	/* fork the state */
	CFF = sgs_ForkState( C, 1 );
	CFP = sgs_ForkState( C, 0 );
	
	/* check state count */
	atf_assert( sgs_Stat( C, SGS_STAT_STATECOUNT ) == 3 );
	atf_assert( sgs_Stat( CFF, SGS_STAT_STATECOUNT ) == 3 );
	atf_assert( sgs_Stat( CFP, SGS_STAT_STATECOUNT ) == 3 );
	
	/* release in the creating order */
	sgs_FreeState( C );
	sgs_FreeState( CFF );
	atf_assert( sgs_Stat( CFP, SGS_STAT_STATECOUNT ) == 1 );
	sgs_FreeState( CFP );
	
	/* --- try running something on both --- */
	C = get_context();
	CFF = sgs_ForkState( C, 1 );
	CFP = sgs_ForkState( C, 0 );
	const char* str =
	"global o = { a = [ { b = {}, c = 5 }, { d = { e = {} }, f = [] } ], g = [] };"
	"o.a[0].parent = o; o.a[1].d.parent = o; o.a[1].d.e.parent = o.a[1].d;"
	"o.a[1].f.push(o); o.g.push( o.a[1].f ); o.a.push( o.a );";
	atf_assert( sgs_ExecString( C, str ) == SGS_SUCCESS );
	atf_assert( sgs_ExecString( CFF, str ) == SGS_SUCCESS );
	atf_assert( sgs_ExecString( CFP, str ) == SGS_SUCCESS );
	sgs_FreeState( C );
	sgs_FreeState( CFF );
	sgs_FreeState( CFP );
}

DEFINE_TEST( yield_resume )
{
	SGS_CTX = get_context();
	
	atf_assert( sgs_ExecString( C, ""
		"global m0 = println('one') || true;\n"
		"yield();\n"
		"global m1 = println('two') || true;\n"
	"" ) == SGS_SUCCESS );
	
	// check if paused
	atf_assert( sgs_Cntl( C, SGS_CNTL_GET_STATE, 0 ) & SGS_STATE_PAUSED );
	atf_assert( sgs_GlobalBool( C, "m0" ) == SGS_TRUE );
	atf_assert( sgs_GlobalBool( C, "m1" ) == SGS_FALSE );
	
	// resume
	atf_assert( sgs_ResumeState( C ) == SGS_TRUE );
	// check if done
	atf_assert( ( sgs_Cntl( C, SGS_CNTL_GET_STATE, 0 ) & SGS_STATE_PAUSED ) == 0 );
	atf_assert( sgs_GlobalBool( C, "m1" ) == SGS_TRUE );
	
	destroy_context( C );
}

int sm_tick_id = 0;
int sm_resume_id = 0;
static int sm_wait( SGS_CTX )
{
	SGSFN( "sm_wait" );
	sm_resume_id = sm_tick_id + (int) sgs_GetInt( C, 0 );
	fprintf( outfp, "paused on tick %d until %d\n", sm_tick_id, sm_resume_id );
	atf_assert( sgs_PauseState( C ) );
	return 1;
}
DEFINE_TEST( state_machine_core )
{
	SGS_CTX = get_context();
	
	sgs_RegFuncConst fns[] = {{ "wait", sm_wait }};
	atf_assert( sgs_RegFuncConsts( C, fns, 1 ) == SGS_SUCCESS );
	
	sm_tick_id = 0;
	sm_resume_id = 0;
	int rvc = 0;
	atf_assert( sgs_EvalString( C, ""
		"println('a');\n"
		"wait(15);\n"
		"println('b');\n"
		"wait(10);\n"
		"println('c');\n"
		"(function test(){\n"
			"wait(15);\n"
			"println('d');\n"
			"wait(10);\n"
			"println('e');\n"
		"})();\n"
	"", &rvc ) == SGS_SUCCESS );
	atf_assert( rvc == 1 );
	fprintf( outfp, "[EvalString value-returned %d]\n", (int) sgs_GetInt( C, -1 ) );
	sgs_Pop( C, 1 );
	
	while( sgs_Cntl( C, SGS_CNTL_GET_PAUSED, 0 ) )
	{
		while( sm_tick_id < sm_resume_id )
			sm_tick_id++;
		fprintf( outfp, "resuming on tick %d\n", sm_tick_id );
		atf_assert( sgs_ResumeStateExp( C, 1 ) == SGS_TRUE );
		fprintf( outfp, "[ResumeStateExp value-returned %d]\n", (int) sgs_GetInt( C, -1 ) );
		sgs_Pop( C, 1 );
	}
	
	destroy_context( C );
}


test_t all_tests[] =
{
	TST( create_and_destroy ),
	TST( array_mem ),
	TST( stack_101 ),
	TST( stack_insert ),
	TST( stack_arraydict ),
	TST( stack_pushstore ),
	TST( stack_propindex ),
	TST( stack_negidx ),
	TST( globals_101 ),
	TST( libraries ),
	TST( function_calls ),
	TST( complex_gc ),
	TST( commands ),
	TST( debugging ),
	TST( varpaths ),
	TST( iterators ),
	TST( native_obj_meta ),
	TST( fork_state ),
	TST( yield_resume ),
	TST( state_machine_core ),
};
int all_tests_count(){ return sizeof(all_tests)/sizeof(test_t); }

