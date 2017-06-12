

#pragma once
#include "sgs_int.h"


const char* testname = "<none>";
static int verbose = 0;
static int serialize_unserialize_all_test = 0;
static int fsuat_not_this_test = 0;

/* API */

void atf_abort(){ abort(); }
void atf_error( int chk, const char* msg, int line )
{
	if( !chk )
	{
		printf( "\nERROR at line %d\ntest failed: %s\n", line, msg );
		atf_abort();
	}
	else if( verbose >= 2 )
	{
		printf( "\n[%d] %s\n", line, msg );
	}
}
void atf_warning( int chk, const char* msg, int line )
{
	if( !chk )
		printf( "\nWARNING at line %d\ntest failed: %s\n", line, msg );
}

#define atf_assert_( chk, msg, line ) atf_error( (chk) != 0, msg, line )
#define atf_assert( chk ) atf_assert_( chk, #chk, __LINE__ )

#define atf_check_( chk, msg, line ) atf_warning( (chk) != 0, msg, line )
#define atf_check( chk ) atf_check_( chk, #chk, __LINE__ )

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
	atf_assert_string_( str1, str2, "test failed: str1 == str2", __LINE__ )


/*************\
*             *
*  T E S T S  *
*             *
\*************/


void serialize_test( sgs_Context* C )
{
	sgs_Variable srlz;
	sgs_SerializeAll( C );
	sgs_GetStackItem( C, -1, &srlz );
	sgs_Pop( C, 1 );
	atf_assert( srlz.type == SGS_VT_STRING );
	
	if( serialize_unserialize_all_test >= 2 && !fsuat_not_this_test )
	{
		sgs_SizeVal i, sz, inc;
		for( i = 0, sz = sgs_GetStringSizeP( &srlz ), inc = sz / 5000 + 1; i <= sz; i += inc )
		{
			int unsrlz_result;
			sgs_Variable tmp;
			sgs_InitStringBuf( C, &tmp, sgs_GetStringPtrP( &srlz ), i );
			unsrlz_result = sgs_UnserializeAll( C, tmp );
			if( unsrlz_result != ( i == sz ) )
			{
				printf( "UNSERIALIZE - UNEXPECTED RESULT (%d) test %d/%d\n", unsrlz_result, i, sz );
			}
			atf_assert( unsrlz_result == ( i == sz ) );
			sgs_Release( C, &tmp );
		}
	}
	else
	{
		atf_assert( sgs_UnserializeAll( C, srlz ) );
	}
	sgs_Release( C, &srlz );
}

sgs_Context* currctx;
sgs_MemBuf outbuf;
sgs_MemBuf errbuf;
FILE* outfp;
FILE* errfp;
static void outfn_buffer( void* ud, SGS_CTX, const void* ptr, size_t size )
{
	sgs_MemBuf* mb = (sgs_MemBuf*) ud;
	sgs_membuf_appbuf( mb, C, ptr, size );
}
static void msgfn_buffer( void* ud, SGS_CTX, int type, const char* msg )
{
	const char* pfxs[] = { "[I:", "[W:", "[E:" };
	type = type / 100 - 1;
	if( type < 0 ) type = 0;
	if( type > 2 ) type = 2;
	
	sgs_WriteErrorInfo( C, SGS_ERRORINFO_FULL, (sgs_ErrorOutputFunc) sgs_ErrWritef, C, type, msg );
	
	sgs_MemBuf* mb = (sgs_MemBuf*) ud;
	sgs_membuf_appbuf( mb, C, pfxs[type], strlen(pfxs[type]) );
	sgs_membuf_appbuf( mb, C, msg, strlen(msg) );
	sgs_membuf_appbuf( mb, C, "]", 1 );
}
#define REDIR_NONE 0
#define REDIR_FILE 1
#define REDIR_BUF 2
sgs_Context* get_context_( int redir_out )
{
	SGS_CTX = sgs_CreateEngine();
	currctx = C;
	atf_assert_( C, "could not create context (out of memory?)", __LINE__ );
	
	outfp = NULL;
	if( redir_out == REDIR_FILE )
	{
		outfp = fopen( outfile, "a" );
		atf_assert_( outfp, "could not create output file", __LINE__ );
		setvbuf( outfp, NULL, _IONBF, 0 );
		
		sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, outfp );
		atf_assert( C->shared->output_ctx == outfp );
		
		fprintf( outfp, "//\n/// O U T P U T  o f  %s\n//\n\n", testname );
	}
	else if( redir_out == REDIR_BUF )
	{
		outbuf = sgs_membuf_create();
		sgs_SetOutputFunc( C, outfn_buffer, &outbuf );
		atf_assert( C->shared->output_ctx == &outbuf );
	}
	
	errfp = fopen( outfile_errors, "a" );
	atf_assert_( errfp, "could not create error output file", __LINE__ );
	setvbuf( errfp, NULL, _IONBF, 0 );
	
	sgs_SetErrOutputFunc( C, SGSOUTPUTFN_DEFAULT, errfp );
	atf_assert( C->shared->erroutput_ctx == errfp );
	
	errbuf = sgs_membuf_create();
	sgs_SetMsgFunc( C, msgfn_buffer, &errbuf );
	atf_assert( C->msg_ctx == &errbuf );
	
	return C;
}
sgs_Context* get_context(){ return get_context_( REDIR_FILE ); }
void pre_destroy_context( SGS_CTX )
{
	if( serialize_unserialize_all_test )
	{
		serialize_test( C );
	}
	
	if( C->shared->output_fn == outfn_buffer )
	{
		sgs_membuf_destroy( &outbuf, C );
	}
	sgs_membuf_destroy( &errbuf, C );
	currctx = NULL;
	
	sgs_SetOutputFunc( C, SGSOUTPUTFN_DEFAULT, stdout );
	sgs_SetErrOutputFunc( C, SGSOUTPUTFN_DEFAULT, stderr );
	
	if( outfp )
	{
		fclose( outfp );
		outfp = NULL;
	}
	fclose( errfp );
}
void destroy_context( SGS_CTX )
{
	pre_destroy_context( C );
	sgs_DestroyEngine( C );
}

void atf_clear_output()
{
	sgs_membuf_resize( &outbuf, currctx, 0 );
}
#define atf_check_output( expect ) atf_check_output_( expect, __LINE__ )
void atf_check_output_( const char* expect, int line )
{
	size_t len = strlen( expect );
	if( len != (size_t) outbuf.size || memcmp( outbuf.ptr, expect, len ) != 0 )
	{
		printf( "\nOUTPUT DATA MISMATCH (line %d)\nexpected [%d]: %s##end##\ngot [%d]: ",
			line, (int) len, expect, (int) outbuf.size );
		fwrite( outbuf.ptr, outbuf.size, 1, stdout );
		printf( "##end##\n" );
		atf_abort();
	}
}

void atf_clear_errors()
{
	sgs_membuf_resize( &errbuf, currctx, 0 );
}
#define atf_check_errors( expect ) atf_check_errors_( expect, __LINE__ )
void atf_check_errors_( const char* expect, int line )
{
	size_t len = strlen( expect );
	if( len != (size_t) errbuf.size || memcmp( errbuf.ptr, expect, len ) != 0 )
	{
		printf( "\nERROR MESSAGE MISMATCH (line %d)\nexpected [%d]: %s##end##\ngot [%d]: ",
			line, (int) len, expect, (int) outbuf.size );
		fwrite( errbuf.ptr, errbuf.size, 1, stdout );
		printf( "##end##\n" );
		atf_abort();
	}
}


