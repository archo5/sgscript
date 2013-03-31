
#include <stdio.h>
#include <assert.h>

#include "sgscript.h"


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

	setvbuf( stdout, NULL, _IOLBF, 128 );

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

			rv = sgs_ExecFile( C, argv[ i ] );

			if( rv != SGS_SUCCESS )
			{
				if( rv == SGS_ENOTFND ) printf( EPFX "file was not found: %s\n", argv[ i ] );
				else if( rv == SGS_EINPROC ) printf( EPFX "failed to load file: %s\n", argv[ i ] );
				else printf( EPFX "failed to run file: %s\n", argv[ i ] );
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
