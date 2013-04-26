
#include <stdio.h>
#include <time.h>
#ifdef _MSC_VER
#  include "msvc/dirent.c"
#else
#  include <dirent.h>
#endif
#include <assert.h>
#if __linux__
#  include <unistd.h>
#endif

#include "sgscript.h"
#include "sgs_int.h"


const char* outfile_internal = "tests-output-internal.log";
const char* outfile = "tests-output.log";
const char* outfile_errors = "tests-errors.log";


#ifdef WIN32
__declspec(dllexport)
#endif
int sgscript_main( SGS_CTX )
{
	sgs_PushInt( C, 1337 );
	sgs_StoreGlobal( C, "imported_var" );
	sgs_PushBool( C, 1 );
	return 1;
}


static void TF_printfn( void* ctx, SGS_CTX, int type, int line, const char* message )
{
	int ret = 0;
	const char* pfxs[] = { "[I:", "[W:", "[E:" };
	type = type / 100 - 1;
	if( type < 0 ) type = 0;
	if( type > 2 ) type = 2;
	ret |= sgs_PushGlobal( C, "ERRORS" );
	sgs_PushString( C, pfxs[ type ] );
	sgs_PushString( C, message );
	sgs_PushString( C, "]" );
	ret |= sgs_StringMultiConcat( C, 4 );
	ret |= sgs_StoreGlobal( C, "ERRORS" );
	sgs_PushInt( C, line );
	ret |= sgs_StoreGlobal( C, "ERRLINE" );
	sgs_BreakIf( ret != 0 );
}

static void prepengine( sgs_Context* C )
{
	int ret;
	const char* sgs_testapi =
	"global ERRORS = \"..uninitialized..\";\n"
	"global tests_failed = 0, tests_ran = 0;\n"
	"function test( result, name, onfail ){\n"
	"	global tests_failed, tests_ran;\n"
	"	tests_ran++;\n"
	"	if( result ){\n"
	"		print( \"OK   `\", name, \"`\\n\" );\n"
	"	}else{\n"
	"		tests_failed++;\n"
	"		print( \"FAIL `\", name, \"`\" );\n"
	"		if( onfail !== null )\n"
	"			print( \" - \", onfail );\n"
	"		print( \"\\n\" );\n"
	"		sys_errorstate();\n"
	"	}\n"
	"}\n"
	"function testEqual( what, expect, name, onfail ){\n"
	"	var failmsg = \"expected \\\"\" $ expect $ \"\\\", got \\\"\" $ what $ \"\\\"\";\n"
	"	if( onfail !== null ) failmsg $= \" (\" $ onfail $ \")\";\n"
	"	if( name === null ) name = \"...expecting \\\"\" $ expect $ \"\\\"\";\n"
	"	test( what === expect, name, failmsg );\n"
	"}\n"
	;

	ret = sgs_ExecString( C, sgs_testapi );
	UNUSED( ret );
	sgs_BreakIf( ret != SGS_SUCCESS );

	sgs_SetPrintFunc( C, TF_printfn, NULL );
}

/* test statistics */
int tests_executed = 0;
int tests_failed = 0;

int numallocs = 0;
static void* ext_memfunc( void* ud, void* ptr, size_t size )
{
	if( ptr ) numallocs--;
	if( size ) numallocs++;
	UNUSED( ud );
	return realloc( ptr, size );
}


#define ATTMKR "\n\n#### "
static void checkdestroy_context( sgs_Context* C )
{
	int all = 1;

	/* pre-destroy */
	all &= C->stack_top == C->stack_base;
	if( C->stack_top != C->stack_base ) printf( ATTMKR "stack left in bad state\n" );

	/* DESTROY */
	sgs_DestroyEngine( C );

	/* post-destroy */
	all &= numallocs == 0;
	if( numallocs ) printf( ATTMKR "memory leaks detected (%d)\n", numallocs );

	if( !all )
	{
		printf( "\n\n\tcritical error in tests - aborting now\n\n" );
		exit( 1 );
	}
}

static void exec_test( const char* fname, const char* nameonly, int disp )
{
	FILE* fp, *fpe;
	int retval;
	sgs_Context* C;
	double tm1, tm2;

	fpe = fopen( outfile_errors, "a" );
	numallocs = 0;
	C = sgs_CreateEngineExt( ext_memfunc, NULL );
	sgs_SetPrintFunc( C, SGSPRINTFN_DEFAULT, fpe );

	fprintf( fpe, ">>> test: %s\n", nameonly );
	printf( "> running %20s (%s)...\t", nameonly, disp == 0 ? "..." : ( disp >= 0 ? "+++" : "---" ) );

	if( strstr( nameonly, "TF" ) != NULL )
	{
		fp = fopen( outfile_internal, "w" );
		sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, fp );
		prepengine( C );
		fclose( fp );
	}

	fp = fopen( outfile, "a" );
	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, fp );
	fprintf( fp, "//\n/// O U T P U T  o f  %s\n//\n\n", nameonly );

	tm1 = sgs_GetTime();
	retval = sgs_ExecFile( C, fname );
	tm2 = sgs_GetTime();

	fprintf( fp, "\n\n" );
	fclose( fp );
	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, stdout );

/*	if( disp )	*/
	{
		int has_errors = retval == SGS_SUCCESS ? 1 : -1;
		const char* sucfail = has_errors * disp >= 0 ? "| OK" : "| FAIL";
		if( has_errors * disp < 0 )
			tests_failed++;
		if( disp == 0 && has_errors > 0 ) sucfail = "";
		printf( "time: %f seconds %s", tm2 - tm1, sucfail );
	}
	checkdestroy_context( C );
	fclose( fpe );
	printf( "\n" );
}

static void exec_tests()
{
	DIR* d = opendir( "tests" );
	struct dirent* e;
	char namebuf[ 260 ];

	fclose( fopen( outfile, "w" ) );
	fclose( fopen( outfile_errors, "w" ) );
	printf( "\n" );

	while( ( e = readdir( d ) ) != NULL )
	{
		int disp = 0;
		if( strcmp( e->d_name, "." ) == 0 || strcmp( e->d_name, ".." ) == 0 )
			continue;
		if( strncmp( e->d_name, "!_", 2 ) == 0 ) continue;
		if( strncmp( e->d_name, "s_", 2 ) == 0 ) disp = 1;
		if( strncmp( e->d_name, "f_", 2 ) == 0 ) disp = -1;

		tests_executed++;
		strcpy( namebuf, "tests/" );
		strcat( namebuf, e->d_name );

		exec_test( namebuf, e->d_name, disp );
	}
	closedir( d );
}

int
#ifdef _MSC_VER
__cdecl
#endif
main( int argc, char** argv )
{
	printf( "\n//\n/// SGScript test framework\n//\n" );

	if( argc > 1 )
	{
		printf( "\n/// Executing test %s...\n", argv[ 1 ] );
		fclose( fopen( outfile, "w" ) );
		fclose( fopen( outfile_errors, "w" ) );
		exec_test( argv[ 1 ], argv[ 1 ], 0 );
		return 0;
	}

	exec_tests();

	printf( "\n///\n/// Tests failed:  %d  / %d\n///\n", tests_failed, tests_executed );
	printf( "..note: some tests may fail in different ways,\nmight want to review the logs..\n\n" );
/*
	printf( "\n/// Finished!\nPress 'Enter' to continue..." );
	getchar();
*/
	return 0;
}
