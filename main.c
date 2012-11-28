
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
#include "sgs_ctx.h"


const char* outfile = "tests-output.log";


double gettime()
{
	clock_t clk = clock();
	return (double)( clk ) / (double)( CLOCKS_PER_SEC );
}


char stdout_buf[ 20 ];
void setoutput( const char* file )
{
	if( file )
	{
		FILE* f = freopen( file, "a", stdout );
		if( !f )
		{
			printf( "Could not freopen\n" );
			exit( 1 );
		}
#if SGS_DEBUG
		setvbuf( f, NULL, _IONBF, 0 );
#endif
	}
	else
	{
		FILE* f =
#if __linux__
			freopen( stdout_buf, "w", stdout );
#else
			freopen( "CON", "w", stdout );
#endif
		if( !f )
		{
			printf( "Could not freopen\n" );
			exit( 1 );
		}
	}
}

char* get_file_contents( const char* file )
{
	int size;
	char* out = NULL;
	FILE* f = fopen( file, "rb" );
	if( !f ) return NULL;

	fseek( f, 0, SEEK_END );
	size = ftell( f );
	fseek( f, 0, SEEK_SET );

	out = (char*) malloc( size + 1 );

	{
		int rdb;
		rdb = fread( out, 1, size, f );
		if( size != rdb )
		{
			if( ferror( f ) )
			{
				fclose( f );
				free( out );
				return NULL;
			}
			assert( size == rdb );
		}
	}

	out[ size ] = 0;
	fclose( f );

	return out;
}

void check_context( sgs_Context* C )
{
	int all = 1;
	printf( "state checks:" );

	printf( " stack %s", C->stack_top != C->stack_base ? "ERROR" : "OK" );
	all &= C->stack_top == C->stack_base;

	printf( " => %s", all ? "OK" : "ERROR" );
	if( !all )
	{
		printf( "\n\n critical error in tests - aborting now\n\n" );
		exit( 1 );
	}
}

/* test statistics */
int tests_executed = 0;
int tests_successful = 0;

void count_tests()
{
	int testc = 0;
	DIR* d = opendir( "tests" );
	struct dirent* e;
	printf( "\n" );
	while( ( e = readdir( d ) ) != NULL )
	{
		const char* disp = "...";
		if( strcmp( e->d_name, "." ) == 0 || strcmp( e->d_name, ".." ) == 0 )
			continue;
		if( strncmp( e->d_name, "s_", 2 ) == 0 ) disp = "+++";
		if( strncmp( e->d_name, "f_", 2 ) == 0 ) disp = "---";
		printf( "%s\t%s\n", disp, e->d_name );
		testc++;
	}
	closedir( d );
	printf( "\n%8d tests found...\n\n", testc );
}

void exec_test( const char* fname, const char* nameonly, int disp )
{
	int retval;
	sgs_Context* C;
	double tm1, tm2;

	char* code = get_file_contents( fname );
	if( !code )
		return;
	C = sgs_CreateEngine();

	printf( "\t> running \"%s\" (%s)...\n", nameonly, disp == 0 ? "..." : ( disp >= 0 ? "+++" : "---" ) );
	setoutput( outfile );
	printf( "//\n/// O U T P U T  o f  %s\n//\n\n", nameonly );

	tm1 = gettime();
	retval = sgs_ExecString( C, code );
	tm2 = gettime();

	printf( "\n\n" );
	setoutput( NULL );

/*	if( disp )	*/
	{
		int has_errors = retval == SGS_SUCCESS ? 1 : -1;
		const char* sucfail = has_errors * disp >= 0 ? "| \tOK" : "| \tFAIL";
		if( disp == 0 && has_errors > 0 ) sucfail = "";
		printf( "\t\t\ttime: %f seconds %s\n", tm2 - tm1, sucfail );
	}

	printf( "\t\t\t    -\t" );
	check_context( C );
	printf( "\n" );
	sgs_DestroyEngine( C );
	printf( "\n\n" );
	free( code );
}

void exec_tests()
{
	DIR* d = opendir( "tests" );
	struct dirent* e;
	char namebuf[ 260 ];

	fclose( fopen( outfile, "w" ) );
	printf( "\n" );

	while( ( e = readdir( d ) ) != NULL )
	{
		int disp = 0;
		if( strcmp( e->d_name, "." ) == 0 || strcmp( e->d_name, ".." ) == 0 )
			continue;
		if( strncmp( e->d_name, "!!", 2 ) == 0 ) continue;
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
#if __linux__
	int saved_stdout = dup( 1 );
	sprintf( stdout_buf, "/dev/fd/%d", saved_stdout );
#endif
	printf( "//\n/// SGScript test framework\n//\n" );

	if( argc > 1 )
	{
		printf( "\n/// Executing test %s...\n", argv[ 1 ] );
		fclose( fopen( outfile, "w" ) );
		exec_test( argv[ 1 ], argv[ 1 ], 0 );
		return 0;
	}

	count_tests();
	printf( "\n/// Executing tests...\n" );
	exec_tests();
/*
	printf( "\n/// Finished!\nPress 'Enter' to continue..." );
	getchar();
*/
	return 0;
}
