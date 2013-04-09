
#include <stdio.h>
#include <assert.h>

#include "sgscript.h"


#define EPFX "SGSC Error: "
#define EPRINT( x ) printf( EPFX x "\n" )

int action = 0;
const char* infile = NULL;
const char* outfile = NULL;
const char* outext = NULL;

void print_help()
{
	printf( "Available options:\n"
	        "-h\t- print help info\n"
	        "-c\t- compile a file\n"
	        "-d\t- dump bytecode\n"
	        "-o\t- set output file (optional)\n"
	        "note: either -c or -d must be specified\n"
	        "\n" );
}

int main( int argc, char** argv )
{
	int i;

	printf( "SGSC [SGScript v%s]\n", SGS_VERSION );

	/* parse */
	for( i = 1; i < argc; ++i )
	{
		if( 0 == strcmp( argv[ i ], "-h" ) )
		{
			print_help();
			return 0;
		}
		else if( 0 == strcmp( argv[ i ], "-c" ) )
			action = 1;
		else if( 0 == strcmp( argv[ i ], "-d" ) )
			action = 2;
		else if( 0 == strcmp( argv[ i ], "-o" ) )
		{
			i++;
			if( i >= argc )
			{
				EPRINT( "expected file name after -o" );
				return 1;
			}
			outfile = argv[ i ];
		}
		else if( argv[ i ][ 0 ] == '-' )
		{
			EPRINT( "unrecognized option" );
			print_help();
			return 1;
		}
		else if( infile )
		{
			EPRINT( "can only process one file at a time" );
			return 1;
		}
		else
			infile = argv[ i ];
	}

	/* validate */

	/* do */
	{
		sgs_Context* C = sgs_CreateContext();

		if( action == 1 )
		{
		}

		sgs_DestroyContext( C );
	}

	return 0;
}
