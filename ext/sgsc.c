
#include <stdio.h>
#include <assert.h>

#include "sgscript.h"


#define EPFX "SGSC Error: "
#define EPRINT( x ) printf( EPFX x "\n" )

int action = 0;
const char* infile = NULL;
const char* outfile = NULL;
const char* outext = ".sgc";

void print_help()
{
	printf( "Available options:\n"
	        "  -h\t- print help info\n"
	        "  -c\t- compile a file\n"
	        "  -d\t- dump bytecode to STDOUT\n"
	        "  -o\t- set compiled output file name (optional)\n"
	        "note: either -c or -d must be specified\n"
	        "example usage:\n"
	        "> sgsc -c script.sgs -o compiled.sgc\n"
	        "> sgsc -d compiled.sgc\n"
	        "\n" );
}

int loadfile( const char* file, char** out, sgs_SizeVal* outsize )
{
	char* data;
	sgs_SizeVal len;
	FILE* f;

	f = fopen( file, "rb" );
	if( !f )
		return SGS_ENOTFND;
	fseek( f, 0, SEEK_END );
	len = ftell( f );
	fseek( f, 0, SEEK_SET );

	data = malloc( len );
	if( fread( data, 1, len, f ) != len )
	{
		fclose( f );
		free( data );
		return SGS_EINPROC;
	}
	fclose( f );

	*out = data;
	*outsize = len;
	return SGS_SUCCESS;
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
	if( !action )
	{
		EPRINT( "action (-c or -d) not specified" );
		print_help();
		return 1;
	}
	else if( !infile )
	{
		EPRINT( "no input file" );
		print_help();
		return 1;
	}

	/* do */
	{
		sgs_Context* C = sgs_CreateEngine();

		if( action == 1 )
		{
			int ret;
			FILE* f;
			char of[ 260 ];
			char* data = NULL, *data2 = NULL;
			sgs_SizeVal size, size2;
			ret = loadfile( infile, &data, &size );
			if( ret != SGS_SUCCESS )
			{
				printf( EPFX "failed to read the file, error %d\n", ret );
				return ret;
			}
			ret = sgs_Compile( C, data, size, &data2, &size2 );
			free( data );
			if( ret != SGS_SUCCESS )
			{
				printf( EPFX "failed to compile, error %d\n", ret );
				return ret;
			}
			if( outfile )
				strncpy( of, outfile, 260 );
			else
			{
				strncpy( of, infile, 260 );
				strcat( of, ".sgc" ); /* TODO fix potential buffer overflow */
			}
			f = fopen( of, "wb" );
			if( !f )
			{
				sgs_FreeCompileBuffer( data2 );
				printf( EPFX "failed to open output file for writing\n" );
				return errno;
			}
			fwrite( data2, 1, size2, f ); /* TODO handle partial writes */
			sgs_FreeCompileBuffer( data2 );
			fclose( f );

			printf( "successfully wrote bytecode to '%s'\n", of );
		}

		sgs_DestroyEngine( C );
	}

	return 0;
}
