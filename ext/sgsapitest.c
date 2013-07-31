
#include <stdio.h>
#include <time.h>

#define SGS_INTERNAL
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
void atf_error( const char* msg )
{
	printf( "\nERROR\n%s\n", msg );
	atf_abort();
}
void atf_warning( const char* msg )
{
	printf( "\nWARNING\n%s\n", msg );
}

#define atf_defmsg( chk ) "test failed: \"" #chk "\""

#define atf_assert_( chk, msg ) do{ if( !(chk) ) atf_error( msg ); }while(0)
#define atf_assert( chk ) atf_assert_( chk, atf_defmsg( chk ) )

#define atf_check_( chk, msg ) do{ if( !(chk) ) atf_warning( msg ); }while(0)
#define atf_check( chk ) atf_check_( chk, atf_defmsg( chk ) )

void atf_assert_string_( const char* str1, const char* str2, const char* msg )
{
	const char* s1 = str1, *s2 = str2;
	if( str1 == NULL || str2 == NULL )
	{
		printf( "\nERROR\n%s (at least one of the strings is NULL)\n", msg );
		atf_abort();
	}

	do
	{
		if( *s1 != *s2 )
		{
			const char* spaces = "           ";
			printf( "\nERROR\n%s (mismatch at pos. %d)\n", msg, (int)(size_t)( s1-str1+1 ) );
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
	atf_assert_string_( str1, str2, atf_defmsg( str1 == str2 ) )


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
	atf_assert_( C, "could not create context (out of memory?)" );

	outfp = fopen( outfile, "a" );
	atf_assert_( outfp, "could not create output file" );

	errfp = fopen( outfile_errors, "a" );
	atf_assert_( errfp, "could not create error output file" );

	fprintf( outfp, "//\n/// O U T P U T  o f  %s\n//\n\n", testname );

	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, outfp );
	atf_assert( C->output_ctx == outfp );

	sgs_SetPrintFunc( C, SGSPRINTFN_DEFAULT, errfp );
	atf_assert( C->print_ctx == errfp );

	return C;
}
void destroy_context( SGS_CTX )
{
	sgs_DestroyEngine( C );
	fclose( outfp );
	fclose( errfp );
}

int sgs_dummy_func( SGS_CTX ){ return 0; }
void* sgs_dummy_iface[] = { SOP_END };


DEFINE_TEST( create_and_destroy )
{
	SGS_CTX = get_context();
	destroy_context( C );
}

DEFINE_TEST( stack_101 )
{
	sgs_Variable sgs_dummy_var;
	SGS_CTX = get_context();

	sgs_dummy_var.type = VTC_NULL;

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

	atf_assert( C->stack_off[0].type == VTC_NULL );
	atf_assert( C->stack_off[1].type == VTC_BOOL );
	atf_assert( C->stack_off[2].type == VTC_INT );
	atf_assert( C->stack_off[3].type == VTC_REAL );
	atf_assert( C->stack_off[4].type == VTC_STRING );
	atf_assert( C->stack_off[5].type == VTC_STRING );
	atf_assert( C->stack_off[6].type == VTC_CFUNC );
	atf_assert( C->stack_off[7].type == VTC_OBJECT );
	atf_assert( C->stack_off[8].type == VTC_NULL );

	atf_assert( C->stack_off[1].data.B == 1 );
	atf_assert( C->stack_off[2].data.I == 1337 );
	atf_assert( C->stack_off[3].data.R == 13.37 );
	atf_assert( C->stack_off[4].data.S->size == 7 );
	atf_assert_string( var_cstr( C->stack_off+4 ), "what is" );
	atf_assert( C->stack_off[5].data.S->size == 12 );
	atf_assert_string( var_cstr( C->stack_off+5 ), "what is this" );
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

	sgs_dummy_var.type = VTC_NULL;

	sgs_PushInt( C, 1 );
	sgs_PushInt( C, 2 );
	atf_assert( C->stack_top == C->stack_off + 2 );

	atf_assert( sgs_InsertVariable( C, 3, &sgs_dummy_var ) == SGS_EBOUNDS );
	atf_assert( sgs_InsertVariable( C, -4, &sgs_dummy_var ) == SGS_EBOUNDS );
	atf_assert( sgs_InsertVariable( C, -3, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_InsertVariable( C, 3, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_InsertVariable( C, 2, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 5 );

	atf_assert( C->stack_off[0].type == VTC_NULL );
	atf_assert( C->stack_off[1].type == VTC_INT );
	atf_assert( C->stack_off[2].type == VTC_NULL );
	atf_assert( C->stack_off[3].type == VTC_INT );
	atf_assert( C->stack_off[4].type == VTC_NULL );

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

	atf_assert( C->stack_off[1].type == VTC_INT );
	atf_assert( C->stack_off[1].data.I == 5 );
	atf_assert( sgs_Pop( C, 2 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 0 );

	destroy_context( C );
}

DEFINE_TEST( stack_propindex )
{
	SGS_CTX = get_context();

	atf_assert( sgs_PushProperty( C, "nope" ) == SGS_EINPROC );

	sgs_PushString( C, "key-one" );
	atf_assert( sgs_PushIndex( C, 0, 1 ) == SGS_EBOUNDS );
	atf_assert( sgs_PushIndex( C, 0, 0 ) == SGS_EINVAL );

	sgs_PushInt( C, 15 );
	atf_assert( sgs_PushIndex( C, -2, -1 ) == SGS_EBOUNDS );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	sgs_PushInt( C, 5 );
	atf_assert( sgs_PushIndex( C, -2, -1 ) == SGS_SUCCESS );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );

	sgs_PushString( C, "key-two" );
	sgs_PushNull( C );
	atf_assert( sgs_PushDict( C, 4 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );

	destroy_context( C );
}

DEFINE_TEST( globals_101 )
{
	SGS_CTX = get_context();

	atf_assert( sgs_PushGlobal( C, "array" ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	atf_assert( sgs_PushGlobal( C, "donut_remover" ) == SGS_ENOTFND );
	atf_assert_( sgs_StackSize( C ) == 1, "wrong stack size after failed PushGlobal" );

	atf_assert( sgs_PushArray( C, 1 ) == SGS_SUCCESS );
	atf_assert_( sgs_StackSize( C ) == 1, "wrong stack size after PushArray" );
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
	atf_assert( sgs_ItemType( C, -1 ) == SVT_OBJECT );
	
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

void output_strcat( void* ud, SGS_CTX, const void* ptr, sgs_SizeVal size )
{
	char* dst = (char*) ud;
	const char* src = (const char*) ptr;
	
	if( strlen( dst ) + size + 1 > 64 )
	{
		printf( "size: %d, whole string: %s%s\n", size, dst, src );
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
	sgs_PushGlobal( C, "print" );
	sgs_PushArray( C, 0 );
	sgs_PushDict( C, 0 );
	
	atf_assert( sgs_StackSize( C ) == 9 );
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
	atf_assert( sgs_ItemType( C, -1 ) == SVT_OBJECT );
	atf_assert( sgs_ItemTypeExt( C, -1 ) == VTC_ARRAY );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	/* properties and indices */
	atf_assert( sgs_PushPath( C, -1, "piso", "a", 1, 1, "d", 0 ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -1 ) == SVT_OBJECT );
	atf_assert( sgs_ItemTypeExt( C, -1 ) == VTC_DICT );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );
	
	/* storing data */
	sgs_PushInt( C, 42 );
	atf_assert( sgs_StorePath( C, -2, "pippp", "a", 1, "d", "e", "test" ) == SGS_SUCCESS );
	atf_assert( sgs_PushPath( C, -1, "pippp", "a", 1, "d", "e", "test" ) == SGS_SUCCESS );
	atf_assert( sgs_ItemType( C, -1 ) == SVT_INT );
	atf_assert( (C->stack_top-1)->data.I == 42 );
	atf_assert( sgs_Pop( C, 1 ) == SGS_SUCCESS );
	atf_assert( sgs_StackSize( C ) == 1 );

	destroy_context( C );
}


test_t all_tests[] =
{
	TST( create_and_destroy ),
	TST( stack_101 ),
	TST( stack_insert ),
	TST( stack_arraydict ),
	TST( stack_pushstore ),
	TST( stack_propindex ),
	TST( globals_101 ),
	TST( libraries ),
	TST( function_calls ),
	TST( complex_gc ),
	TST( commands ),
	TST( debugging ),
	TST( varpaths ),
};
int all_tests_count(){ return sizeof(all_tests)/sizeof(test_t); }

