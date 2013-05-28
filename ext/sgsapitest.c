
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
			printf( "\nERROR\n%s (mismatch at pos. %d)\n", msg, s1-str1+1 );
			printf( "...%-20s...\n", s1 - 10 > str1 ? s1 : str1 );
			printf( "...%-20s...\n", s2 - 10 > str2 ? s2 : str2 );
			printf( "   %.*s^\n", s1 - str1 > 10 ? 10 : s1 - str1, spaces );
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

	sgs_dummy_var.type = SVT_NULL;

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
	atf_assert( C->stack_top == C->stack_off + 9 );

	atf_assert( C->stack_off[0].type == SVT_NULL );
	atf_assert( C->stack_off[1].type == SVT_BOOL );
	atf_assert( C->stack_off[2].type == SVT_INT );
	atf_assert( C->stack_off[3].type == SVT_REAL );
	atf_assert( C->stack_off[4].type == SVT_STRING );
	atf_assert( C->stack_off[5].type == SVT_STRING );
	atf_assert( C->stack_off[6].type == SVT_CFUNC );
	atf_assert( C->stack_off[7].type == SVT_OBJECT );
	atf_assert( C->stack_off[8].type == SVT_NULL );

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
	atf_assert( C->stack_top == C->stack_off + 9 );

	atf_assert( sgs_Pop( C, 9 ) == SGS_SUCCESS );
	atf_assert( C->stack_off == C->stack_top );
	atf_assert( C->stack_base == C->stack_off );

	destroy_context( C );
}

DEFINE_TEST( stack_insert )
{
	sgs_Variable sgs_dummy_var;
	SGS_CTX = get_context();

	sgs_dummy_var.type = SVT_NULL;

	sgs_PushInt( C, 1 );
	sgs_PushInt( C, 2 );
	atf_assert( C->stack_top == C->stack_off + 2 );

	atf_assert( sgs_InsertVariable( C, 3, &sgs_dummy_var ) == SGS_EBOUNDS );
	atf_assert( sgs_InsertVariable( C, -4, &sgs_dummy_var ) == SGS_EBOUNDS );
	atf_assert( sgs_InsertVariable( C, -3, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_InsertVariable( C, 3, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( sgs_InsertVariable( C, 2, &sgs_dummy_var ) == SGS_SUCCESS );
	atf_assert( C->stack_top == C->stack_off + 5 );

	atf_assert( C->stack_off[0].type == SVT_NULL );
	atf_assert( C->stack_off[1].type == SVT_INT );
	atf_assert( C->stack_off[2].type == SVT_NULL );
	atf_assert( C->stack_off[3].type == SVT_INT );
	atf_assert( C->stack_off[4].type == SVT_NULL );

	sgs_Pop( C, 5 );
	atf_assert( C->stack_off == C->stack_top );

	destroy_context( C );
}


test_t all_tests[] =
{
	TST( create_and_destroy ),
	TST( stack_101 ),
	TST( stack_insert ),
};
int all_tests_count(){ return sizeof(all_tests)/sizeof(test_t); }

