
#include <stdio.h>
#include <assert.h>

#include "sgscript.h"
#include "sgs_idbg.h"


#define EPFX "SGSVM Error: "
#define EPRINT( x ) printf( EPFX x "\n" )

void readme()
{
	puts( "syntax: sgsvm [files|options]" );
	puts( "\t-h, --help: print this text" );
	puts( "\t-s, --separate: restart the engine between scripts" );
	puts( "\t-d, --debug: enable interactive debugging on errors" );
}

int main( int argc, char** argv )
{
	int i, sep = 0, idbg = 0;
	sgs_Context* C;
	sgs_IDbg D;

	printf( "SGSVM [SGScript v%s]\n", SGS_VERSION );

	if( argc < 2 )
	{
		EPRINT( "need to specify at least one file" );
		readme();
		return 1;
	}

	for( i = 1; i < argc; ++i )
	{
		if( strcmp( argv[ i ], "--separate" ) == 0 ||
			strcmp( argv[ i ], "-s" ) == 0 ){ sep = 1; argv[ i ] = 0; }
		else if( strcmp( argv[ i ], "--debug" ) == 0 ||
			strcmp( argv[ i ], "-d" ) == 0 ){ idbg = 1; argv[ i ] = 0; }
		else if( strcmp( argv[ i ], "--help" ) == 0 ||
			strcmp( argv[ i ], "-h" ) == 0 ){ readme(); return 0; }
	}

	C = sgs_CreateEngine();
	if( idbg ) sgs_InitIDbg( C, &D );

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
				if( idbg ) sgs_CloseIDbg( C, &D );
				sgs_DestroyEngine( C );
				C = sgs_CreateEngine();
				if( idbg ) sgs_InitIDbg( C, &D );
			}
		}
	}

	if( idbg ) sgs_CloseIDbg( C, &D );
	sgs_DestroyEngine( C );
	return 0;
}
