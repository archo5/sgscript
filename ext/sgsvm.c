
#include <stdio.h>
#include <assert.h>

#include "sgscript.h"


static char* get_file_contents( const char* file )
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

#define EPFX "SGSVM Error: "
#define EPRINT( x ) printf( EPFX x "\n" )

int main( int argc, char** argv )
{
	int i, sep = 0;
	sgs_Context* C;
	
	if( argc < 2 )
	{
		EPRINT( "need to specify at least one file" );
		return 1;
	}
	
	for( i = 1; i < argc; ++i )
	{
		if( strcmp( argv[ i ], "--separate" ) == 0 ||
			strcmp( argv[ i ], "-s" ) == 0 ){ sep = 1; argv[ i ] = 0; }
	}
	
	C = sgs_CreateEngine();
	
	for( i = 1; i < argc; ++i )
	{
		if( argv[ i ] )
		{
			int rv;
			char* text = get_file_contents( argv[ i ] );
			if( !text )
			{
				printf( EPFX "failed to load file: %s\n", argv[ i ] );
				continue;
			}
			
			rv = sgs_ExecString( C, text );
			free( text );
			if( rv != SGS_SUCCESS )
			{
				printf( EPFX "failed to run file: %s\n", argv[ i ] );
				continue;
			}
			
			if( sep )
			{
				sgs_DestroyEngine( C );
				C = sgs_CreateEngine();
			}
		}
	}
	
	sgs_DestroyEngine( C );
	return 0;
}
