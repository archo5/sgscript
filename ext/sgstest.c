
#include <stdio.h>
#include <time.h>

#define SGS_USE_FILESYSTEM

#include "sgscript.h"
#include "sgs_int.h"


const char* outfile = "tests-output.log";
const char* outfile_errors = "tests-errors.log";



static int sgrx_snprintf( char* buf, size_t len, const char* fmt, ... )
{
	if( len == 0 )
		return -1;
	va_list args;
	va_start( args, fmt );
	int ret = vsnprintf( buf, len, fmt, args );
	va_end( args );
	buf[ len - 1 ] = 0;
	return ret;
}



typedef struct testfile_
{
	char* nameonly;
	char* fullname;
	int sucfail;
	int loadtf;
	int sortkey;
}
testfile;


int tf_compare( const void* p1, const void* p2 )
{
	const testfile* f1 = (const testfile*) p1;
	const testfile* f2 = (const testfile*) p2;
	if( f1->sortkey != f2->sortkey )
		return f1->sortkey - f2->sortkey;
	return strcmp( f1->nameonly, f2->nameonly );
}

int calc_disp( const char* nameonly )
{
	int disp = 0;
	if( strncmp( nameonly, "s_", 2 ) == 0 ) disp = 1;
	if( strncmp( nameonly, "f_", 2 ) == 0 ) disp = -1;
	if( strstr( nameonly, "MT" ) != NULL ) disp = 1;
	if( strstr( nameonly, "TF" ) != NULL ) disp = 1;
	return disp;
}

int load_testfiles( const char* dir, testfile** files, size_t* count )
{
	DIR* d;
	struct dirent* e;
	struct stat sdata;
	char namebuf[ 260 ];
	testfile* TF;
	size_t TFM = 32, TFC = 0;
	d = opendir( dir );
	if( !d )
		return 0;
	TF = (testfile*) malloc( sizeof( testfile ) * TFM );

	while( ( e = readdir( d ) ) != NULL )
	{
		int disp = calc_disp( e->d_name );
		if( strcmp( e->d_name, "." ) == 0 || strcmp( e->d_name, ".." ) == 0 )
			continue;
		if( strncmp( e->d_name, "!_", 2 ) == 0 ) continue;
		if( strstr( e->d_name, ".sgs" ) != e->d_name + strlen( e->d_name ) - 4 )
			continue;
		
		sgrx_snprintf( namebuf, 260, "%s/%s", dir, e->d_name );
		stat( namebuf, &sdata );
		if( !( sdata.st_mode & S_IFREG ) )
			continue;
		
		if( TFC == TFM )
		{
			TFM *= 2;
			TF = (testfile*) realloc( TF, sizeof( testfile ) * TFM );
		}
		
		TF[ TFC ].nameonly = (char*) malloc( strlen( e->d_name ) + 1 );
		strcpy( TF[ TFC ].nameonly, e->d_name );
		TF[ TFC ].fullname = (char*) malloc( strlen( namebuf ) + 1 );
		strcpy( TF[ TFC ].fullname, namebuf );
		TF[ TFC ].sucfail = disp;
		TF[ TFC ].loadtf = strstr( e->d_name, "TF" ) != NULL;
		if( TF[ TFC ].loadtf )
			TF[ TFC ].sucfail = 1;
		TF[ TFC ].sortkey = ( disp != 1 ) * 2 + ( disp != -1 ) * 1 + TF[ TFC ].loadtf * 4;
		TFC++;
	}
	closedir( d );

	qsort( TF, TFC, sizeof( testfile ), tf_compare );

	*files = TF;
	*count = TFC;
	return 1;
}

void free_testfiles( testfile* files, size_t count )
{
	testfile* f = files, *fend = files + count;
	while( f < fend )
	{
		free( f->nameonly );
		free( f->fullname );
		f++;
	}
	free( files );
}




static void TF_printfn( void* ctx, SGS_CTX, int type, const char* message )
{
	const char* pfxs[] = { "[I:", "[W:", "[E:" };
	type = type / 100 - 1;
	if( type < 0 ) type = 0;
	if( type > 2 ) type = 2;
	sgs_PushGlobalByName( C, "ERRORS" );
	sgs_PushString( C, pfxs[ type ] );
	sgs_PushString( C, message );
	sgs_PushString( C, "]" );
	sgs_StringConcat( C, 4 );
	sgs_SetGlobalByName( C, "ERRORS", sgs_StackItem( C, -1 ) );
	sgs_Pop( C, 1 );
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
	"	}\n"
	"}\n";
	/* for ISO C90 support */
	const char* sgs_testapi2 =
	"function testEqual( what, expect, name, onfail ){\n"
	"	var failmsg = \"expected \\\"\" $ expect $ \"\\\", got \\\"\" $ what $ \"\\\"\";\n"
	"	if( onfail !== null ) failmsg $= \" (\" $ onfail $ \")\";\n"
	"	if( name === null ) name = \"...expecting \\\"\" $ expect $ \"\\\"\";\n"
	"	test( what === expect, name, failmsg );\n"
	"}\n"
	;

	ret = sgs_ExecString( C, sgs_testapi );
	SGS_UNUSED( ret );
	sgs_BreakIf( ret != SGS_SUCCESS );

	ret = sgs_ExecString( C, sgs_testapi2 );
	SGS_UNUSED( ret );
	sgs_BreakIf( ret != SGS_SUCCESS );

	sgs_SetMsgFunc( C, TF_printfn, NULL );
}

/* test statistics */
int tests_executed = 0;
int tests_failed = 0;

int numallocs = 0;
static void* ext_memfunc( void* ud, void* ptr, size_t size )
{
	if( ptr ) numallocs--;
	if( size ) numallocs++;
	else if( !ptr )
		return NULL;
	SGS_UNUSED( ud );
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
	if( numallocs > 0 ) printf( ATTMKR "memory leaks detected (numallocs = %d)\n", numallocs );
	if( numallocs < 0 ) printf( ATTMKR "repeated frees detected (numallocs = %d)\n", numallocs );

	if( !all )
	{
		printf( "\n\n\tcritical error in tests - aborting now\n\n" );
		exit( 1 );
	}
}

static void output_to_buffer( void* userdata, SGS_CTX, const void* ptr, size_t size )
{
	sgs_MemBuf* mb = (sgs_MemBuf*) userdata;
	sgs_membuf_appbuf( mb, C, ptr, size );
}

static void rec_printfn( void* ctx, SGS_CTX, int type, const char* message )
{
	const char pfxs[] = { 'I', 'W', 'E' };
	type = type / 100 - 1;
	if( type < 0 ) type = 0;
	if( type > 2 ) type = 2;
	sgs_ErrWritef( C, "[%c:%s]", pfxs[ type ], message );
}

static void exec_test( const char* fname, const char* nameonly )
{
	FILE* fp, *fpe;
	int retval, disp, is_MT, is_TF;
	sgs_Context* C;
	double tm1, tm2;
	
	disp = calc_disp( nameonly );
	is_MT = strstr( nameonly, "MT" ) != NULL;
	is_TF = strstr( nameonly, "TF" ) != NULL;
	fpe = fopen( outfile_errors, "a" );
	numallocs = 0;
	C = sgs_CreateEngineExt( ext_memfunc, NULL );
	sgs_SetErrOutputFunc( C, SGSOUTPUTFN_DEFAULT, fpe );
	sgs_SetMsgFunc( C, SGSMSGFN_DEFAULT_NOABORT, NULL );

	fprintf( fpe, "\n>>> test: %s\n", nameonly );
	printf( "> running %20s [%s]\t", nameonly, disp == 0 ? " " : ( disp >= 0 ? "+" : "-" ) );
	
	fp = fopen( outfile, "a" );
	setvbuf( fp, NULL, _IONBF, 0 );
	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, fp );
	fprintf( fp, "//\n/// O U T P U T  o f  %s\n//\n\n", nameonly );
	
	if( is_TF )
	{
		prepengine( C );
	}
	
	tm1 = sgs_GetTime();
	if( is_MT )
	{
		sgs_MemBuf outbuf = sgs_membuf_create(), errbuf = sgs_membuf_create();
		SGSRESULT lastexec = -1000;
		retval = SGS_SUCCESS;
		char* data, *data_alloc, testname[ 64 ] = "<unknown>";
		/* read the file */
		{
			size_t numread, readsize;
			FILE* tfh = fopen( fname, "r" );
			if( !tfh )
			{
				fprintf( stderr, "failed to open test file: '%s' - %s\n", fname, strerror( errno ) );
				exit( 1 );
			}
			fseek( tfh, 0, SEEK_END );
			readsize = (size_t) ftell( tfh );
			data_alloc = data = (char*) malloc( readsize + 1 );
			fseek( tfh, 0, SEEK_SET );
			numread = fread( data, 1, readsize, tfh );
			if( numread != readsize && ferror( tfh ) )
			{
				fprintf( stderr, "failed to read from test file: '%s' - %s\n", fname, strerror( errno ) );
				exit( 1 );
			}
			fclose( tfh );
			data[ numread ] = 0;
		}
		/* parse contents
			syntax: <ident [a-zA-Z0-9_]> <string `...`> ...
			every item is a command, some commands are tests, failures will impact the return value
		*/
		while( *data )
		{
			int esc = 0;
			char *ident_start, *ident_end, *value_start, *value_end, *decoded_value;
			
			/* skip spaces */
			while( *data == ' ' || *data == '\t' || *data == '\r' || *data == '\n' ) data++;
			
			/* parse identifier */
			ident_start = data;
			while( *data == '_' || *data == '/' || sgs_isalnum( *data ) ) data++;
			ident_end = data;
			if( ident_start == ident_end )
			{
				fprintf( stderr, "%s - expected identifier at '%16s...'\n", fname, data );
				retval = SGS_EINPROC;
				break;
			}
			
			/* skip spaces */
			while( *data == ' ' || *data == '\t' || *data == '\r' || *data == '\n' ) data++;
			
			/* parse value */
			if( *data != '`' )
			{
				fprintf( stderr, "%s - expected start of value (`) at '%16s...'\n", fname, data );
				retval = SGS_EINPROC;
				break;
			}
			value_start = ++data;
			while( *data )
			{
				if( *data == '`' && !esc )
					break;
				if( *data == '\\' && !esc )
					esc = 1;
				else esc = 0;
				data++;
			}
			if( *data != '`' )
			{
				fprintf( stderr, "%s - expected end of value (`) at '%16s...'\n", fname, data );
				retval = SGS_EINPROC;
				break;
			}
			value_end = data++;
			
			/* skip spaces */
			while( *data == ' ' || *data == '\t' || *data == '\r' || *data == '\n' ) data++;
			
			/* DECODE VALUE */
			decoded_value = (char*) malloc( (size_t)( value_end - value_start + 1 ) );
			{
				char* op = decoded_value;
				char* ip = value_start;
				while( ip < value_end )
				{
					if( *ip == '\\' && ( ip[1] == '\\' || ip[1] == '`' ) )
						*op = *++ip;
					else
						*op = *ip;
					op++;
					ip++;
				}
				*op = 0;
			}
			
			/* PROCESS ACTION */
			*ident_end = 0;
			
			if( ident_start[0] == '/' && ident_start[1] == '/' )
			{
				strncpy( testname, decoded_value, 64 );
				testname[ 63 ] = 0;
				printf( "." );
			}
			else if( strcmp( ident_start, "exec" ) == 0 )
			{
				/* to detect buffer overruns in compilation ..
				.. using memory checking tools, allocate a new buffer ..
				.. with the exact size of the data */
				size_t len = strlen( decoded_value );
				char* bc = (char*) malloc( len );
				memcpy( bc, decoded_value, len );
				lastexec = sgs_ExecBuffer( C, bc, len );
				free( bc );
			}
			else if( strcmp( ident_start, "result" ) == 0 )
			{
				const char* les = sgs_CodeString( SGS_CODE_ER, lastexec );
				if( strcmp( decoded_value, les ) != 0 )
				{
					printf( "[%s] ERROR in 'result': expected %s, got %s\n",
						testname, decoded_value, les );
					retval = SGS_EINPROC;
				}
			}
			else if( strcmp( ident_start, "rec_out" ) == 0 )
			{
				sgs_membuf_resize( &outbuf, C, 0 );
				sgs_membuf_appbuf( &outbuf, C, decoded_value, strlen( decoded_value ) );
				sgs_SetOutputFunc( C, output_to_buffer, &outbuf );
			}
			else if( strcmp( ident_start, "check_out" ) == 0 )
			{
				if( outbuf.size != strlen( decoded_value ) ||
					memcmp( outbuf.ptr, decoded_value, outbuf.size ) != 0 )
				{
					printf( "[%s] ERROR in 'check_out': expected '%s', got '%.*s'\n",
						testname, decoded_value, (int) outbuf.size, outbuf.ptr );
					retval = SGS_EINPROC;
				}
			}
			else if( strcmp( ident_start, "rec_err" ) == 0 )
			{
				sgs_membuf_resize( &errbuf, C, 0 );
				sgs_membuf_appbuf( &errbuf, C, decoded_value, strlen( decoded_value ) );
				sgs_SetErrOutputFunc( C, output_to_buffer, &errbuf );
				sgs_SetMsgFunc( C, rec_printfn, NULL );
			}
			else if( strcmp( ident_start, "check_err" ) == 0 )
			{
				if( errbuf.size != strlen( decoded_value ) ||
					memcmp( errbuf.ptr, decoded_value, errbuf.size ) != 0 )
				{
					printf( "[%s] ERROR in 'check_err': expected '%s', got '%.*s'\n",
						testname, decoded_value, (int) errbuf.size, errbuf.ptr );
					retval = SGS_EINPROC;
				}
			}
			else if( strcmp( ident_start, "rec" ) == 0 )
			{
				sgs_membuf_resize( &outbuf, C, 0 );
				sgs_membuf_appbuf( &outbuf, C, decoded_value, strlen( decoded_value ) );
				sgs_SetOutputFunc( C, output_to_buffer, &outbuf );
				sgs_membuf_resize( &errbuf, C, 0 );
				sgs_membuf_appbuf( &errbuf, C, decoded_value, strlen( decoded_value ) );
				sgs_SetErrOutputFunc( C, output_to_buffer, &errbuf );
				sgs_SetMsgFunc( C, rec_printfn, NULL );
			}
			else if( strcmp( ident_start, "reboot" ) == 0 )
			{
				sgs_membuf_destroy( &outbuf, C );
				sgs_membuf_destroy( &errbuf, C );
				outbuf = sgs_membuf_create();
				errbuf = sgs_membuf_create();
				
				checkdestroy_context( C );
				C = sgs_CreateEngineExt( ext_memfunc, NULL );
				sgs_SetErrOutputFunc( C, SGSOUTPUTFN_DEFAULT, fpe );
				sgs_SetMsgFunc( C, SGSMSGFN_DEFAULT_NOABORT, NULL );
				sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, fp );
			}
			
			free( decoded_value );
		}
		
		free( data_alloc );
		sgs_membuf_destroy( &outbuf, C );
		sgs_membuf_destroy( &errbuf, C );
	}
	else
	{
		retval = sgs_ExecFile( C, fname );
	}
	tm2 = sgs_GetTime();
	
	if( is_TF )
	{
		if( retval != SGS_SUCCESS && disp > 0 )
		{
			sgs_PushGlobalByName( C, "ERRORS" );
			puts( sgs_DebugPrintVar( C, sgs_StackItem( C, -1 ) ) );
			sgs_Pop( C, 2 );
		}
		
		if( sgs_PushGlobalByName( C, "tests_failed" ) )
		{
			if( sgs_GetInt( C, -1 ) )
				retval = SGS_EINPROC;
			sgs_Pop( C, 1 );
		}
	}

	fprintf( fp, "\n\n" );
	fclose( fp );
	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, stdout );

	tests_executed++;
/*	if( disp )	*/
	{
		int has_errors = retval == SGS_SUCCESS ? 1 : -1;
		const char* sucfail = has_errors * disp >= 0 ? " OK" : " FAIL";
		if( has_errors * disp < 0 )
			tests_failed++;
		if( disp == 0 && has_errors < 0 ) sucfail = " ~~";
		printf( "time: %f seconds |%s", tm2 - tm1, sucfail );
	}
	checkdestroy_context( C );
	fclose( fpe );
	printf( "\n" );
}

static void exec_tests( const char* dirname )
{
	int ret;
	size_t count;
	testfile* files, *f, *fend;
	fclose( fopen( outfile, "w" ) );
	fclose( fopen( outfile_errors, "w" ) );
	printf( "\n" );

	ret = load_testfiles( dirname, &files, &count );
	if( !ret )
	{
		printf( "\n\nfailed to load tests! aborting...\n\n" );
		exit( 1 );
	}

	f = files;
	fend = files + count;
	while( f < fend )
	{
		exec_test( f->fullname, f->nameonly );
		f++;
	}

	free_testfiles( files, count );
}

int
#ifdef _MSC_VER
__cdecl
#endif
main( int argc, char** argv )
{
	int i;
	char *testname = NULL, *dirname = "tests";
	setvbuf( stdout, NULL, _IONBF, 0 );
	printf( "\n//\n/// SGScript test framework\n//\n" );
	
	for( i = 1; i < argc; ++i )
	{
		if( ( !strcmp( argv[i], "--dir" ) || !strcmp( argv[i], "-d" ) ) && i + 1 < argc )
			dirname = argv[++i];
		else if( ( !strcmp( argv[i], "--test" ) || !strcmp( argv[i], "-t" ) ) && i + 1 < argc )
			testname = argv[++i];
	}
	printf("- test directory: %s\n", dirname );
	if( testname )
	{
		printf( "- one test: %s\n", testname );
	}

	if( testname )
	{
		printf( "\n/// Executing test %s...\n", testname );
		fclose( fopen( outfile, "w" ) );
		fclose( fopen( outfile_errors, "w" ) );
		exec_test( testname, testname );
		return 0;
	}

	exec_tests( dirname );

	printf( "\n///\n/// Tests failed:  %d  / %d\n///\n", tests_failed, tests_executed );
	printf( "..note: some tests may fail in different ways,\nmight want to review the logs..\n\n" );

	return 0;
}
