

/* for the constants... */
#define _USE_MATH_DEFINES
#undef __STRICT_ANSI__
#include <math.h>

#define SGS_INTERNAL
#define SGS_USE_FILESYSTEM

#include "sgs_int.h"

#define FLAG( a, b ) (((a)&(b))!=0)
#define STDLIB_WARN( warn ) { sgs_Printf( C, SGS_WARNING, -1, warn ); return 0; }



/* path helper functions */

static int32_t findlastof( const char* str, int32_t len, const char* of )
{
	const char* ptr = str + len;
	while( ptr-- > str )
	{
		const char* pof = of;
		while( *pof )
		{
			if( *pof == *ptr )
				return ptr - str;
			pof++;
		}
	}
	return -1;
}

static int path_replast( SGS_CTX, int from, int with )
{
	sgs_SizeVal size, pos;
	char* buf = sgs_ToStringBuf( C, from, &size );
	if( !buf || ( pos = findlastof( buf, size, "/\\" ) ) < 0 )
	{
		return !sgs_PushItem( C, with );
	}
	sgs_PushStringBuf( C, buf, pos + 1 );
	return !( sgs_PushItem( C, with ) || sgs_StringConcat( C ) );
}



/* libraries - I / O */

#define FILE_READ 1
#define FILE_WRITE 2

#define FST_UNKNOWN 0
#define FST_FILE 1
#define FST_DIR 2

static int sgsstd_io_setcwd( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "io_setcwd() - unexpected arguments; function expects 1 argument: string" )

	ret = chdir( str );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_getcwd( SGS_CTX )
{
	if( sgs_StackSize( C ) != 0 )
		STDLIB_WARN( "io_getcwd() - unexpected arguments; function expects 0 arguments" )

	{
		/* XPC WARNING: getcwd( NULL, 0 ) relies on undefined behavior */
		char* cwd = getcwd( NULL, 0 );
		if( cwd )
		{
			sgs_PushString( C, cwd );
			free( cwd );
			return 1;
		}
		else
			return 0;
	}
}

static int sgsstd_io_rename( SGS_CTX )
{
	int ret;
	char* path, *nnm;
	sgs_SizeVal psz, nnmsz;

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &path, &psz ) ||
		!sgs_ParseString( C, 1, &nnm, &nnmsz ) )
		STDLIB_WARN( "io_rename() - unexpected arguments; function expects 2 arguments: string, string" )

	if( !path_replast( C, 0, 1 ) ||
		!sgs_ParseString( C, -1, &nnm, &nnmsz ) )
	{
		sgs_PushBool( C, FALSE );
		return 1;
	}

	ret = rename( path, nnm );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_file_exists( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "io_file_exists() - unexpected arguments; function expects 1 argument: string" )

	{
		FILE* fp = fopen( str, "rb" );
		sgs_PushBool( C, !!fp );
		if( fp ) fclose( fp );
		return 1;
	}
}

static int sgsstd_io_dir_exists( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "io_dir_exists() - unexpected arguments; function expects 1 argument: string" )

	{
		DIR* dp = opendir( str );
		sgs_PushBool( C, !!dp );
		if( dp ) closedir( dp );
		return 1;
	}
}

static int sgsstd_io_stat( SGS_CTX )
{
	char* str;
	sgs_SizeVal size;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "io_stat() - unexpected arguments; function expects 1 argument: string" )

	{
		struct stat data;
		if( stat( str, &data ) != 0 )
			return 0;

		/* --- */
		sgs_PushString( C, "atime" );
		sgs_PushInt( C, data.st_atime );
		sgs_PushString( C, "ctime" );
		sgs_PushInt( C, data.st_ctime );
		sgs_PushString( C, "mtime" );
		sgs_PushInt( C, data.st_mtime );
		sgs_PushString( C, "type" );
		if( data.st_mode & S_IFDIR )
			sgs_PushInt( C, FST_DIR );
		else if( data.st_mode & S_IFREG )
			sgs_PushInt( C, FST_FILE );
		else
			sgs_PushInt( C, FST_UNKNOWN );
		sgs_PushString( C, "size" );
		sgs_PushInt( C, data.st_size );
		return !sgs_GlobalCall( C, "dict", 10, 1 );
	}
}

static int sgsstd_io_dir_create( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "io_dir_create() - unexpected arguments; function expects 1 argument: string" )

	ret = mkdir( str
#ifndef WIN32
		,0777
#endif
	);
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_dir_delete( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "io_dir_delete() - unexpected arguments; function expects 1 argument: string" )

	ret = rmdir( str );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_file_delete( SGS_CTX )
{
	int ret;
	char* str;
	sgs_SizeVal size;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "io_delete() - unexpected arguments; function expects 1 argument: string" )

	ret = remove( str );
	sgs_PushBool( C, ret == 0 );
	return 1;
}

static int sgsstd_io_file_write( SGS_CTX )
{
	char* path, *data;
	sgs_SizeVal psz, dsz;

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &path, &psz ) ||
		!sgs_ParseString( C, 1, &data, &dsz ) )
		STDLIB_WARN( "io_file_write() - unexpected arguments; function expects 2 arguments: string, string" )

	{
		sgs_SizeVal wsz;
		FILE* fp = fopen( path, "wb" );
		if( !fp )
			STDLIB_WARN( "io_file_write() - failed to create file" )
		wsz = fwrite( data, 1, dsz, fp );
		fclose( fp );
		if( wsz < dsz )
			STDLIB_WARN( "io_file_write() - failed to write to file" )
	}

	sgs_PushBool( C, TRUE );
	return 1;
}

static int sgsstd_io_file_read( SGS_CTX )
{
	char* path;
	sgs_SizeVal psz;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_ParseString( C, 0, &path, &psz ) )
		STDLIB_WARN( "io_file_read() - unexpected arguments; function expects 1 argument: string" )

	{
		sgs_SizeVal len, rd;
		FILE* fp = fopen( path, "rb" );
		if( !fp )
			STDLIB_WARN( "io_file_read() - failed to open file" )
		fseek( fp, 0, SEEK_END );
		len = ftell( fp );
		fseek( fp, 0, SEEK_SET );

		sgs_PushStringBuf( C, NULL, len );
		rd = fread( sgs_GetStringPtr( C, -1 ), 1, len, fp );
		fclose( fp );
		if( rd < len )
			STDLIB_WARN( "io_file_read() - failed to read file" )

		return 1;
	}
}


static const sgs_RegRealConst i_rconsts[] =
{
	{ "FILE_READ", FILE_READ },
	{ "FILE_WRITE", FILE_WRITE },

	{ "FST_UNKNOWN", FST_UNKNOWN },
	{ "FST_FILE", FST_FILE },
	{ "FST_DIR", FST_DIR },
};

#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst i_fconsts[] =
{
	FN( io_getcwd ), FN( io_setcwd ),
	FN( io_rename ),
	FN( io_file_exists ), FN( io_dir_exists ), FN( io_stat ),
	FN( io_dir_create ), FN( io_dir_delete ),
	FN( io_file_delete ),
	FN( io_file_write ), FN( io_file_read ),
};

SGSRESULT sgs_LoadLib_IO( SGS_CTX )
{
	int ret;
	ret = sgs_RegRealConsts( C, i_rconsts, ARRAY_SIZE( i_rconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, i_fconsts, ARRAY_SIZE( i_fconsts ) );
	return ret;
}



/* libraries -  M A T H  */

#define SGS_PI 3.14159265358979323846
#define SGS_E 2.7182818284590452354

static sgs_Real myround( sgs_Real x )
{
	return floor( x + 0.5 ) - 0.5;
}

static int sgsstd_pow( SGS_CTX )
{
	sgs_Real b, e;
	if( sgs_StackSize( C ) != 2 || !sgs_ParseReal( C, 0, &b ) || !sgs_ParseReal( C, 1, &e ) )
		STDLIB_WARN( "pow() - unexpected arguments; function expects 2 arguments: real, real" )
	if( ( b < 0 && e != (sgs_Real) (sgs_Integer) e )
		|| ( b == 0 && e < 0 ) )
		STDLIB_WARN( "pow() - mathematical error" )
	sgs_PushReal( C, pow( b, e ) );
	return 1;
}

static int sgsstd_sqrt( SGS_CTX )
{
	sgs_Real arg0;
	if( sgs_StackSize( C ) != 1 || !sgs_ParseReal( C, 0, &arg0 ) )
		STDLIB_WARN( "sqrt() - unexpected arguments; function expects 1 argument: real" )
	if( arg0 < 0 )
		STDLIB_WARN( "sqrt() - mathematical error" )
	sgs_PushReal( C, sqrtf( arg0 ) );
	return 1;
}

static int sgsstd_log( SGS_CTX )
{
	sgs_Real x, b;
	if( sgs_StackSize( C ) != 2 || !sgs_ParseReal( C, 0, &x ) || !sgs_ParseReal( C, 1, &b ) )
		STDLIB_WARN( "log() - unexpected arguments; function expects 2 arguments: real, real" )
	if( x < 0 )
		STDLIB_WARN( "log() - mathematical error" )
	sgs_PushReal( C, log( x ) / log( b ) );
	return 1;
}

#define MATHFUNC_CN( name, orig ) \
static int sgsstd_##name( SGS_CTX ) { \
	sgs_Real arg0; \
	if( sgs_StackSize( C ) != 1 || !sgs_ParseReal( C, 0, &arg0 ) ) \
		STDLIB_WARN( #name "() - unexpected arguments; function expects 1 argument: real" ) \
	sgs_PushReal( C, orig( arg0 ) ); \
	return 1; }
#define MATHFUNC( name ) MATHFUNC_CN( name, name )

#define MATHFUNC2_CN( name, orig ) \
static int sgsstd_##name( SGS_CTX ) { \
	sgs_Real arg0, arg1; \
	if( sgs_StackSize( C ) != 2 || !sgs_ParseReal( C, 0, &arg0 ) || !sgs_ParseReal( C, 1, &arg1 ) ) \
		STDLIB_WARN( #name "() - unexpected arguments; function expects 2 arguments: real, real" ) \
	sgs_PushReal( C, orig( arg0, arg1 ) ); \
	return 1; }
#define MATHFUNC2( name ) MATHFUNC2_CN( name, name )

MATHFUNC_CN( abs, fabs )
MATHFUNC( floor )
MATHFUNC( ceil )
MATHFUNC_CN( round, myround )

MATHFUNC( sin )
MATHFUNC( cos )
MATHFUNC( tan )
MATHFUNC( asin )
MATHFUNC( acos )
MATHFUNC( atan )
MATHFUNC2( atan2 )


static const sgs_RegRealConst m_rconsts[] =
{
	{ "M_PI", SGS_PI },
	{ "M_E", SGS_E },
};

#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst m_fconsts[] =
{
	FN( abs ), FN( floor ), FN( ceil ), FN( round ),
	FN( pow ), FN( sqrt ), FN( log ),
	FN( sin ), FN( cos ), FN( tan ),
	FN( asin ), FN( acos ), FN( atan ), FN( atan2 ),
};

SGSRESULT sgs_LoadLib_Math( SGS_CTX )
{
	int ret;
	ret = sgs_RegRealConsts( C, m_rconsts, ARRAY_SIZE( m_rconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, m_fconsts, ARRAY_SIZE( m_fconsts ) );
	return ret;
}



#if 0
/* libraries -  N A T I V E  */

static int sgsstd_pointerI_tobool( SGS_CTX, sgs_VarObj* data )
{
	return sgs_PushBool( C, data->data != NULL );
}

static int sgsstd_pointerI_toint( SGS_CTX, sgs_VarObj* data )
{
	return sgs_PushInt( C, (sgs_Integer) (size_t) data->data );
}

static int sgsstd_pointerI_tostring( SGS_CTX, sgs_VarObj* data )
{
	char buf[ 32 ];
	sprintf( buf, "pointer (%p)", data->data );
	return sgs_PushString( C, buf );
}

static int sgsstd_pointerI_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	return sgs_PushString( C, "native_pointer" );
}

void* sgsstd_pointer_iface[] =
{
	SOP_TOBOOL, sgsstd_pointerI_tobool,
	SOP_TOINT, sgsstd_pointerI_toint,
	SOP_TOSTRING, sgsstd_pointerI_tostring,
	SOP_GETTYPE, sgsstd_pointerI_gettype,
	SOP_END
};

static int sgsstd_native_pointer( SGS_CTX )
{
	void* val = 0;

	if( sgs_StackSize( C ) > 1 )
		STDLIB_WARN( "native_pointer() - unexpected arguments; function expects 0-1 arguments: [int]" )

	if( sgs_StackSize( C ) == 1 )
		val = (void*) (size_t) sgs_ToInt( C, 0 );

	return sgs_PushObject( C, val, sgsstd_pointer_iface ) == SGS_SUCCESS ? 1 : 0;
}


#define NTYPE_VOID   0
#define NTYPE_CHAR   1
#define NTYPE_UCHAR  2
#define NTYPE_SHORT  3
#define NTYPE_USHORT 4
#define NTYPE_INT    5
#define NTYPE_UINT   6
#define NTYPE_LONG   7
#define NTYPE_ULONG  8
#define NTYPE_LLONG  9
#define NTYPE_ULLONG 10
#define NTYPE_FLOAT  11
#define NTYPE_DOUBLE 12
#define NTYPE_LDBL   13
#define NTYPE_BOOL   14
#define NTYPE_SIZE_T 15
#define NTYPE_PTRDIFF_T 16
#define NTYPE_V8     17
#define NTYPE_V16    18
#define NTYPE_V32    19
#define NTYPE_V64    20
#define NTYPE_POINTER 21

#define NCALL_CDECL   1
#define NCALL_STDCALL 2

typedef
struct sgsstd_nfunc_hdr_s
{
	sgs_Variable fname;
	void* funcptr;
	int ret;
	int* args;
	int argc;
}
sgsstd_nfunc_hdr;

#define SGSNFUNC_HDR sgsstd_nfunc_hdr* hdr = (sgsstd_nfunc_hdr*) data->data

static int sgsstd_nfuncI_destruct( SGS_CTX, sgs_VarObj* data )
{
	SGSNFUNC_HDR;
	sgs_Release( C, &hdr->fname );
	if( hdr->args )
		sgs_Free( hdr->args );
	sgs_Free( hdr );
	return SGS_SUCCESS;
}

static int sgsstd_nfuncI_call( SGS_CTX, sgs_VarObj* data )
{
	return SGS_SUCCESS;
}

static int sgsstd_nfuncI_tostring( SGS_CTX, sgs_VarObj* data )
{
	SGSNFUNC_HDR;
	return
		sgs_PushString( C, "native function (" ) || 
		sgs_PushVariable( C, &hdr->fname ) ||
		sgs_PushString( C, ")" ) ||
		sgs_StringMultiConcat( C, 3 );
}

static int sgsstd_nfuncI_gettype( SGS_CTX, sgs_VarObj* data )
{
	UNUSED( data );
	return sgs_PushString( C, "native_function" );
}

void* sgsstd_nfunc_iface[] =
{
	SOP_DESTRUCT, sgsstd_nfuncI_destruct,
	SOP_CALL, sgsstd_nfuncI_call,
	SOP_TOSTRING, sgsstd_nfuncI_tostring,
	SOP_GETTYPE, sgsstd_nfuncI_gettype,
	SOP_END
};


static int sgsstd_native_import_symbol( SGS_CTX )
{
	int i, argc = sgs_StackSize( C ) - 4;
	char* fnstr, *pnstr;
	sgs_SizeVal fnsize, pnsize;
	sgs_Integer cty = 0, rty = 0;
	sgsstd_nfunc_hdr proto, *nfunc;

	if( argc < 0 ||
		!sgs_ParseString( C, 0, &fnstr, &fnsize ) ||
		!sgs_ParseString( C, 1, &pnstr, &pnsize ) ||
		!sgs_ParseInt( C, 2, &cty ) ||
		!sgs_ParseInt( C, 3, &rty ) )
		STDLIB_WARN( "native_import_symbol() - unexpected arguments; function expects 4+ arguments: string, string, int, int, [int]+" )

	if( cty < NCALL_CDECL || cty > NCALL_STDCALL )
		STDLIB_WARN( "native_import_symbol() - invalid call type" )

	if( rty < NTYPE_VOID || rty > NTYPE_POINTER )
		STDLIB_WARN( "native_import_symbol() - invalid return type" )

	if( argc )
		proto.args = sgs_Alloc_n( int, argc );
	else
		proto.args = NULL;
	for( i = 0; i < argc; ++i )
	{
		sgs_Integer aty = 0;
		if( !sgs_ParseInt( C, i + 4, &aty ) || aty < NTYPE_VOID || aty > NTYPE_POINTER )
		{
			char ebuf[ 32 ];
			sprintf( ebuf, "native_import_symbol() - invalid argument %d type", i + 1 );
			sgs_Free( proto.args );
			STDLIB_WARN( ebuf )
		}
		proto.args[ i ] = aty;
	}

	i = sgs_GetProcAddress( fnstr, pnstr, &proto.funcptr );
	if( i != 0 )
	{
		sgs_Free( proto.args );

		if( i == SGS_XPC_NOFILE ) STDLIB_WARN( "native_import_symbol() - file not found" )
		else if( i == SGS_XPC_NOPROC ) STDLIB_WARN( "native_import_symbol() - procedure not found" )
		else if( i == SGS_XPC_NOTSUP ) STDLIB_WARN( "native_import_symbol() - feature is not supported on this platform" )
		else STDLIB_WARN( "native_import_symbol() - unknown error occured" )
	}

	proto.argc = argc;
	proto.ret = rty;
	proto.fname = *sgs_StackItem( C, 1 );
	sgs_Acquire( C, &proto.fname );

	nfunc = sgs_Alloc( sgsstd_nfunc_hdr );
	memcpy( nfunc, &proto, sizeof( proto ) );
	return sgs_PushObject( C, nfunc, sgsstd_nfunc_iface ) == SGS_SUCCESS ? 1 : 0;
}

static const sgs_RegIntConst n_iconsts[] =
{
#define DEFTYPE( name ) { "NTYPE_" #name, NTYPE_##name }
	DEFTYPE( VOID ), DEFTYPE( CHAR ), DEFTYPE( UCHAR ), DEFTYPE( SHORT ), DEFTYPE( USHORT ),
	DEFTYPE( INT ), DEFTYPE( UINT ), DEFTYPE( LONG ), DEFTYPE( ULONG ), DEFTYPE( LLONG ),
	DEFTYPE( ULLONG ), DEFTYPE( FLOAT ), DEFTYPE( DOUBLE ), DEFTYPE( LDBL ), DEFTYPE( BOOL ),
	DEFTYPE( SIZE_T ), DEFTYPE( PTRDIFF_T ), DEFTYPE( V8 ), DEFTYPE( V16 ), DEFTYPE( V32 ),
	DEFTYPE( V64 ), DEFTYPE( POINTER ),
#undef DEFTYPE
	{ "eNCALL_CDECL", NCALL_CDECL },
	{ "eNCALL_STDCALL", NCALL_STDCALL },
};

static const sgs_RegFuncConst n_fconsts[] =
{
	FN( native_pointer ), FN( native_import_symbol ),
};

SGSRESULT sgs_LoadLib_Native( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, n_iconsts, ARRAY_SIZE( n_iconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	sgs_RegFuncConsts( C, n_fconsts, ARRAY_SIZE( n_fconsts ) );
	return ret;
}
#endif



/* libraries -  S T R I N G  */

#define sgsNO_REV_INDEX 1
#define sgsSTRICT_RANGES 2
#define sgsLEFT 1
#define sgsRIGHT 2

static SGS_INLINE int32_t idx2off( int32_t size, int32_t i )
{
	if( -i > size || i >= size ) return -1;
	return i < 0 ? size + i : i;
}

static int sgsstd_string_cut( SGS_CTX )
{
	int argc;
	char* str;
	sgs_SizeVal size;
	sgs_Integer flags = 0, i1, i2;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 4 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		( i2 = size - 1 ) < 0 || /* comparison should always fail */
		!sgs_ParseInt( C, 1, &i1 ) ||
		( argc >= 3 && !sgs_ParseInt( C, 2, &i2 ) ) ||
		( argc >= 4 && !sgs_ParseInt( C, 3, &flags ) ) )
		STDLIB_WARN( "string_cut() - unexpected arguments; function expects 2-4 arguments: string, int, [int], [int]" );

	if( FLAG( flags, sgsNO_REV_INDEX ) && ( i1 < 0 || i2 < 0 ) )
		STDLIB_WARN( "string_cut() - detected negative indices" );

	i1 = i1 < 0 ? size + i1 : i1;
	i2 = i2 < 0 ? size + i2 : i2;
	if( FLAG( flags, sgsSTRICT_RANGES ) && ( i1 > i2 || i1 < 0 || i2 < 0 || i1 >= size || i2 >= size ) )
		STDLIB_WARN( "string_cut() - invalid character range" );

	if( i1 > i2 || i1 >= size || i2 < 0 )
		sgs_PushStringBuf( C, "", 0 );
	else
	{
		i1 = MAX( 0, MIN( i1, size - 1 ) );
		i2 = MAX( 0, MIN( i2, size - 1 ) );
		sgs_PushStringBuf( C, str + i1, i2 - i1 + 1 );
	}
	return 1;
}

static int sgsstd_string_part( SGS_CTX )
{
	int argc;
	char* str;
	sgs_SizeVal size;
	sgs_Integer flags = 0, i1, i2;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 4 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseInt( C, 1, &i1 ) ||
		( i2 = size - i1 ) < 0 || /* comparison should always fail */
		( argc >= 3 && !sgs_ParseInt( C, 2, &i2 ) ) ||
		( argc >= 4 && !sgs_ParseInt( C, 3, &flags ) ) )
		STDLIB_WARN( "string_part() - unexpected arguments; function expects 2-4 arguments: string, int, [int], [int]" );

	if( FLAG( flags, sgsNO_REV_INDEX ) && ( i1 < 0 || i2 < 0 ) )
		STDLIB_WARN( "string_part() - detected negative indices" );

	i1 = i1 < 0 ? size + i1 : i1;
	i2 = i2 < 0 ? size + i2 : i2;
	if( FLAG( flags, sgsSTRICT_RANGES ) && ( i1 < 0 || i1 + i2 < 0 || i2 < 0 || i1 >= size || i1 + i2 > size ) )
		STDLIB_WARN( "string_part() - invalid character range" );

	if( i2 <= 0 || i1 >= size || i1 + i2 < 0 )
		sgs_PushStringBuf( C, "", 0 );
	else
	{
		i2 += i1 - 1;
		i1 = MAX( 0, MIN( i1, size - 1 ) );
		i2 = MAX( 0, MIN( i2, size - 1 ) );
		sgs_PushStringBuf( C, str + i1, i2 - i1 + 1 );
	}
	return 1;
}

static int sgsstd_string_reverse( SGS_CTX )
{
	int argc;
	char* str, *sout;
	sgs_SizeVal size, i;

	argc = sgs_StackSize( C );
	if( argc != 1 || !sgs_ParseString( C, 0, &str, &size ) )
		STDLIB_WARN( "string_reverse() - unexpected arguments; function expects 1 argument: string" );

	sgs_PushStringBuf( C, NULL, size );
	sout = sgs_GetStringPtr( C, -1 );

	for( i = 0; i < size; ++i )
		sout[ size - i - 1 ] = str[ i ];

	return 1;
}

static int sgsstd_string_pad( SGS_CTX )
{
	int argc;
	char* str, *pad = " ", *sout;
	sgs_SizeVal size, padsize = 1;
	sgs_Integer tgtsize, flags = sgsRIGHT, lpad = 0, i;

	argc = sgs_StackSize( C );
	if( ( argc < 2 || argc > 4 ) ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseInt( C, 1, &tgtsize ) ||
		( argc >= 3 && !sgs_ParseString( C, 2, &pad, &padsize ) ) ||
		( argc >= 4 && !sgs_ParseInt( C, 3, &flags ) ) )
		STDLIB_WARN( "string_pad() - unexpected arguments; function expects 2-4 arguments: string, int, [string], [int]" );

	if( tgtsize <= size || !FLAG( flags, sgsLEFT | sgsRIGHT ) )
	{
		sgs_PushItem( C, 0 );
		return 1;
	}

	sgs_PushStringBuf( C, NULL, tgtsize );
	sout = sgs_GetStringPtr( C, -1 );
	if( FLAG( flags, sgsLEFT ) )
	{
		if( FLAG( flags, sgsRIGHT ) )
		{
			sgs_Integer pp = tgtsize - size;
			lpad = pp / 2 + pp % 2;
		}
		else
			lpad = tgtsize - size;
	}

	memcpy( sout + lpad, str, size );
	for( i = 0; i < lpad; ++i )
		sout[ i ] = pad[ i % padsize ];
	size += lpad;
	while( size < tgtsize )
	{
		sout[ size ] = pad[ size % padsize ];
		size++;
	}

	return 1;
}

static int sgsstd_string_repeat( SGS_CTX )
{
	int argc;
	char* str, *sout;
	sgs_SizeVal size;
	sgs_Integer count;

	argc = sgs_StackSize( C );
	if( argc != 2 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseInt( C, 1, &count ) || count < 0 )
		STDLIB_WARN( "string_repeat() - unexpected arguments; function expects 2 arguments: string, int (>= 0)" );

	sgs_PushStringBuf( C, NULL, count * size );
	sout = sgs_GetStringPtr( C, -1 );
	while( count-- )
	{
		memcpy( sout, str, size );
		sout += size;
	}
	return 1;
}

static int sgsstd_string_count( SGS_CTX )
{
	int argc, overlap = FALSE;
	char* str, *sub, *strend;
	sgs_SizeVal size, subsize, ret = 0;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseString( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !sgs_ParseBool( C, 2, &overlap ) ) )
		STDLIB_WARN( "string_count() - unexpected arguments; function expects 2-3 arguments: string, string (length > 0), [bool]" );

	strend = str + size - subsize;
	while( str <= strend )
	{
		if( strncmp( str, sub, subsize ) == 0 )
		{
			ret++;
			str += overlap ? 1 : subsize;
		}
		else
			str++;
	}

	sgs_PushInt( C, ret );
	return 1;
}

static int sgsstd_string_find( SGS_CTX )
{
	int argc;
	char* str, *sub, *strend, *ostr;
	sgs_SizeVal size, subsize; 
	sgs_Integer from = 0;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseString( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !sgs_ParseInt( C, 2, &from ) ) )
		STDLIB_WARN( "string_find() - unexpected arguments; function expects 2-3 arguments: string, string (length > 0), [int]" );

	strend = str + size - subsize;
	ostr = str;
	str += from >= 0 ? from : MAX( 0, size + from );
	while( str <= strend )
	{
		if( strncmp( str, sub, subsize ) == 0 )
		{
			sgs_PushInt( C, str - ostr );
			return 1;
		}
		str++;
	}

	return 0;
}

static int sgsstd_string_find_rev( SGS_CTX )
{
	int argc;
	char* str, *sub, *ostr;
	sgs_SizeVal size, subsize;
	sgs_Integer from = -1;

	argc = sgs_StackSize( C );
	if( argc < 2 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		!sgs_ParseString( C, 1, &sub, &subsize ) || subsize <= 0 ||
		( argc == 3 && !sgs_ParseInt( C, 2, &from ) ) )
		STDLIB_WARN( "string_find_rev() - unexpected arguments; function expects 2-3 arguments: string, string (length > 0), [int]" );

	ostr = str;
	str += from >= 0 ? MIN( from, size - subsize ) : MIN( size - subsize, size + from );
	while( str >= ostr )
	{
		if( strncmp( str, sub, subsize ) == 0 )
		{
			sgs_PushInt( C, str - ostr );
			return 1;
		}
		str--;
	}

	return 0;
}

static int _stringrep_ss( SGS_CTX, char* str, int32_t size, char* sub, int32_t subsize, char* rep, int32_t repsize )
{
	/* the algorithm:
		- find matches, count them, predict size of output string
		- readjust matches to fit the process of replacing
		- rewrite string with replaced matches
	*/
#define NUMSM 32 /* statically-stored matches */
	int32_t sma[ NUMSM ];
	int32_t* matches = sma;
	int matchcount = 0, matchcap = NUMSM, curmatch;
#undef NUMSM

	char* strend = str + size - subsize;
	char* ptr = str, *i, *o;
	int32_t outlen;

	/* subsize = 0 handled by parent */

	while( ptr <= strend )
	{
		if( strncmp( ptr, sub, subsize ) == 0 )
		{
			if( matchcount == matchcap )
			{
				matchcap *= 4;
				int32_t* nm = sgs_Alloc_n( int32_t, matchcap );
				memcpy( nm, matches, sizeof( int32_t ) * matchcount );
				if( matches != sma )
					sgs_Dealloc( matches );
				matches = nm;
			}
			matches[ matchcount++ ] = ptr - str;

			ptr += subsize;
		}
		else
			ptr++;
	}

	outlen = size + ( repsize - subsize ) * matchcount;
	sgs_PushStringBuf( C, NULL, outlen );

	i = str;
	o = sgs_GetStringPtr( C, -1 );
	strend = str + size;
	curmatch = 0;
	while( i < strend && curmatch < matchcount )
	{
		char* mp = str + matches[ curmatch++ ];
		int len = mp - i;
		if( len )
			memcpy( o, i, len );
		i += len;
		o += len;

		memcpy( o, rep, repsize );
		i += subsize;
		o += repsize;
	}
	if( i < strend )
	{
		memcpy( o, i, strend - i );
	}

	if( matches != sma )
		sgs_Dealloc( matches );

	return 1;
}
static int _stringrep_as( SGS_CTX, char* str, int32_t size, sgs_Variable* subarr, char* rep, int32_t repsize )
{
	char* substr;
	sgs_SizeVal subsize;
	sgs_Variable var;
	int32_t i, arrsize = sgs_ArraySize( C, subarr );
	if( arrsize < 0 )
		goto fail;

	for( i = 0; i < arrsize; ++i )
	{
		if( !sgs_ArrayGet( C, subarr, i, &var ) )   goto fail;
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
		if( !sgs_ParseString( C, -1, &substr, &subsize ) )
			goto fail;

		if( !_stringrep_ss( C, str, size, substr, subsize, rep, repsize ) )
			goto fail;

		if( sgs_PopSkip( C, i > 0 ? 2 : 1, 1 ) != SGS_SUCCESS )
			goto fail;

		str = sgs_GetStringPtr( C, -1 );
		size = sgs_GetStringSize( C, -1 );
	}

	return 1;

fail:
	return 0;
}
static int _stringrep_aa( SGS_CTX, char* str, int32_t size, sgs_Variable* subarr, sgs_Variable* reparr )
{
	char* substr, *repstr;
	sgs_SizeVal subsize, repsize;
	sgs_Variable var;
	int32_t i, arrsize = sgs_ArraySize( C, subarr ),
		reparrsize = sgs_ArraySize( C, reparr );
	if( arrsize < 0 || reparrsize < 0 )
		goto fail;

	for( i = 0; i < arrsize; ++i )
	{
		if( !sgs_ArrayGet( C, subarr, i, &var ) )   goto fail;
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
		if( !sgs_ParseString( C, -1, &substr, &subsize ) )
			goto fail;

		if( !sgs_ArrayGet( C, reparr, i % reparrsize, &var ) )   goto fail;
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
		if( !sgs_ParseString( C, -1, &repstr, &repsize ) )
			goto fail;

		if( !_stringrep_ss( C, str, size, substr, subsize, repstr, repsize ) )
			goto fail;

		if( sgs_PopSkip( C, i > 0 ? 3 : 2, 1 ) != SGS_SUCCESS )
			goto fail;

		str = sgs_GetStringPtr( C, -1 );
		size = sgs_GetStringSize( C, -1 );
	}

	return 1;

fail:
	return 0;
}
static int sgsstd_string_replace( SGS_CTX )
{
	int argc, isarr1, isarr2, ret;
	char* str, *sub, *rep;
	sgs_Variable var1, var2;
	sgs_SizeVal size, subsize, repsize;

	argc = sgs_StackSize( C );
	if( argc != 3 )
		goto invargs;

	sgs_GetStackItem( C, 1, &var1 );
	sgs_GetStackItem( C, 2, &var2 );
	isarr1 = sgs_IsArray( C, &var1 );
	isarr2 = sgs_IsArray( C, &var2 );

	if( !sgs_ParseString( C, 0, &str, &size ) )
		goto invargs;

	if( isarr1 && isarr2 )
	{
		return _stringrep_aa( C, str, size, &var1, &var2 );
	}

	if( isarr2 )
		goto invargs;

	ret = sgs_ParseString( C, 2, &rep, &repsize );
	if( isarr1 && ret )
	{
		return _stringrep_as( C, str, size, &var1, rep, repsize );
	}

	if( sgs_ParseString( C, 1, &sub, &subsize ) && ret )
	{
		if( subsize == 0 )
		{
			sgs_PushVariable( C, &var1 );
			return 1;
		}
		return _stringrep_ss( C, str, size, sub, subsize, rep, repsize );
	}

invargs:
	STDLIB_WARN( "string_replace() - unexpected arguments; function expects 3 arguments: string, ((string, string) | (array, string) | (array, array))" );
}


static SGS_INLINE int stdlib_isoneof( char c, char* from, int fsize )
{
	char* fend = from + fsize;
	while( from < fend )
	{
		if( c == *from )
			return TRUE;
		from++;
	}
	return FALSE;
}

static int sgsstd_string_trim( SGS_CTX )
{
	int argc;
	char* str, *strend, *list = " \t\r\n";
	sgs_SizeVal size, listsize = 4;
	sgs_Integer flags = sgsLEFT | sgsRIGHT;

	argc = sgs_StackSize( C );
	if( argc < 1 || argc > 3 ||
		!sgs_ParseString( C, 0, &str, &size ) ||
		( argc >= 2 && !sgs_ParseString( C, 1, &list, &listsize ) ) ||
		( argc >= 3 && !sgs_ParseInt( C, 2, &flags ) ) )
		STDLIB_WARN( "string_trim() - unexpected arguments; function expects 1-3 arguments: string, [string], [int]" );

	if( !FLAG( flags, sgsLEFT | sgsRIGHT ) )
	{
		sgs_PushItem( C, 0 );
		return 1;
	}

	strend = str + size;
	if( flags & sgsLEFT )
	{
		while( str < strend && stdlib_isoneof( *str, list, listsize ) )
			str++;
	}
	if( flags & sgsRIGHT )
	{
		while( str < strend && stdlib_isoneof( *(strend-1), list, listsize ) )
			strend--;
	}

	sgs_PushStringBuf( C, str, strend - str );
	return 1;
}

static int sgsstd_string_implode( SGS_CTX )
{
	sgs_Variable arr;
	sgs_SizeVal i, asize;

	if( sgs_StackSize( C ) != 2 ||
		!sgs_GetStackItem( C, 0, &arr ) ||
		!sgs_IsArray( C, &arr ) ||
		!sgs_ParseString( C, 1, NULL, NULL ) )
		STDLIB_WARN( "string_implode() - unexpected arguments; function expects 2 arguments: array, string" )

	asize = sgs_ArraySize( C, &arr );
	if( !asize )
	{
		sgs_PushString( C, "" );
		return 1;
	}
	for( i = 0; i < asize; ++i )
	{
		sgs_Variable var;
		if( i )
			sgs_PushItem( C, 1 );
		if( !sgs_ArrayGet( C, &arr, i, &var ) )
			STDLIB_WARN( "string_implode() - failed to read from array" )
		sgs_PushVariable( C, &var );
		sgs_Release( C, &var );
	}
	sgs_StringMultiConcat( C, i * 2 - 1 );
	return 1;
}

static char* _findpos( char* a, sgs_SizeVal asize, char* b, sgs_SizeVal bsize )
{
	char* aend = a + asize - bsize;
	char* pend = b + bsize;
	while( a <= aend )
	{
		char* x = a, *p = b;
		while( p < pend )
			if( *x++ != *p++ )
				goto notthis;
		return a;
notthis:
		a++;
	}
	return NULL;
}

static int sgsstd_string_explode( SGS_CTX )
{
	char* a, *b, *p, *pp;
	sgs_SizeVal asize, bsize;

	if( sgs_StackSize( C ) != 2 ||
		!sgs_ParseString( C, 0, &a, &asize ) ||
		!sgs_ParseString( C, 1, &b, &bsize ) )
		STDLIB_WARN( "string_explode() - unexpected arguments; function expects 2 arguments: string, string" )

	if( !bsize )
	{
		p = a + asize;
		while( a < p )
			sgs_PushStringBuf( C, a++, 1 );
	}
	else
	{
		pp = a;
		p = _findpos( a, asize, b, bsize );

		while( p )
		{
			sgs_PushStringBuf( C, pp, p - pp );
			pp = p + bsize;
			p = _findpos( pp, asize - ( pp - a ), b, bsize );
		}

		sgs_PushStringBuf( C, pp, a + asize - pp );
	}

	sgs_PushCFunction( C, C->array_func );
	return sgs_Call( C, sgs_StackSize( C ) - 3, 1 ) == SGS_SUCCESS;
}

static int sgsstd_string_charcode( SGS_CTX )
{
	char* a;
	sgs_SizeVal asize, argc = sgs_StackSize( C );
	sgs_Integer off = 0;

	if( argc < 1 || argc > 2 ||
		!sgs_ParseString( C, 0, &a, &asize ) ||
		( argc == 2 && !sgs_ParseInt( C, 1, &off ) ) )
		STDLIB_WARN( "string_charcode() - unexpected arguments; function expects 1-2 arguments: string[, int]" )

	if( off < 0 )
		off += asize;

	if( off < 0 || off >= (sgs_Integer) asize )
		STDLIB_WARN( "string_charcode() - index out of bounds" )

	sgs_PushInt( C, a[ off ] );
	return 1;
}

static int sgsstd_string_frombytes( SGS_CTX )
{
	char* buf;
	int hasone = 0;
	sgs_SizeVal size, i = 0;
	sgs_Variable arr;
	sgs_Integer onecode;

	if( sgs_StackSize( C ) != 1 ||
		!sgs_GetStackItem( C, 0, &arr ) ||
		( !sgs_IsArray( C, &arr ) && !( hasone = sgs_ParseInt( C, 0, &onecode ) ) ) )
		STDLIB_WARN( "string_frombytes() - unexpected arguments; function expects 1 argument: array|int" )

	if( hasone )
	{
		if( onecode < 0 || onecode > 255 )
			STDLIB_WARN( "string_frombytes() - invalid byte value" )
		else
		{
			char c = (char) onecode;
			sgs_PushStringBuf( C, &c, 1 );
			return 1;
		}
	}

	size = sgs_ArraySize( C, &arr );
	if( size < 0 )
		goto fail;

	sgs_PushStringBuf( C, NULL, size );
	buf = sgs_ToString( C, -1 );
	if( sgs_PushIterator( C, 0 ) < 0 )
		goto fail;

	while( sgs_PushNextKey( C, -1 ) > 0 )
	{
		sgs_Integer b;
		sgs_Variable idx;
		sgs_GetStackItem( C, -1, &idx );
		if( sgs_PushIndex( C, &arr, &idx ) < 0 )
			goto fail;
		b = sgs_GetInt( C, -1 );
		if( b < 0 || b > 255 )
			STDLIB_WARN( "string_frombytes() - invalid byte value" )
		buf[ i++ ] = b;
		sgs_Pop( C, 2 );
	}
	sgs_Pop( C, 1 );
	return 1;

fail:
	STDLIB_WARN( "string_frombytes() - failed to read the array" )
}



#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegIntConst s_iconsts[] =
{
	{ "NO_REV_INDEX", sgsNO_REV_INDEX },
	{ "STRICT_RANGES", sgsSTRICT_RANGES },
	{ "LEFT", sgsLEFT },
	{ "RIGHT", sgsRIGHT },
};

static const sgs_RegFuncConst s_fconsts[] =
{
	FN( string_cut ), FN( string_part ), FN( string_reverse ), FN( string_pad ),
	FN( string_repeat ), FN( string_count ), FN( string_find ), FN( string_find_rev ),
	FN( string_replace ), FN( string_trim ),
	FN( string_implode ), FN( string_explode ),
	FN( string_charcode ), FN( string_frombytes ),
};

SGSRESULT sgs_LoadLib_String( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, s_iconsts, ARRAY_SIZE( s_iconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, s_fconsts, ARRAY_SIZE( s_fconsts ) );
	return ret;
}


#define EXPECT_ONEARG( N ) \
	if( sgs_StackSize( C ) != 1 ){ \
		sgs_Printf( C, SGS_WARNING, -1, "type_" #N ": expected 1 argument" ); \
		return 0;}

static int sgsstd_type_get( SGS_CTX )
{
	EXPECT_ONEARG( type_get )
	sgs_PushInt( C, sgs_ItemType( C, 0 ) );
	return 1;
}

static int sgsstd_typeof( SGS_CTX )
{
	EXPECT_ONEARG( typeof )
	sgs_TypeOf( C );
	return 1;
}

static int sgsstd_type_cast( SGS_CTX )
{
	int argc;
	sgs_Integer ty;

	argc = sgs_StackSize( C );
	if( argc < 1 || argc > 2 ||
		!sgs_ParseInt( C, 1, &ty ) )
		STDLIB_WARN( "type_cast() - unexpected arguments; function expects 2 arguments: any, int" )

	sgs_Convert( C, 0, ty );
	sgs_Pop( C, 1 );
	return 1;
}


static int sgsstd_type_is_numeric( SGS_CTX )
{
	int res, ty = sgs_ItemType( C, 0 );
	EXPECT_ONEARG( is_numeric )

	if( ty == SVT_NULL || ty == SVT_FUNC || ty == SVT_CFUNC || ty == SVT_OBJECT )
		res = FALSE;

	else
		res = ty != SVT_STRING || sgs_IsNumericString(
			sgs_GetStringPtr( C, 0 ), sgs_GetStringSize( C, 0 ) );

	sgs_PushBool( C, res );
	return 1;
}

#define OBJECT_HAS_IFACE( outVar, O, iFace ) \
	{ void** ptr = O->iface; outVar = 0; \
	while( *ptr ){ if( *ptr == iFace ){ outVar = 1; \
		break; } ptr += 2; } }

static int sgsstd_type_is_callable( SGS_CTX )
{
	int res, ty = sgs_ItemType( C, 0 );
	EXPECT_ONEARG( is_callable )

	if( ty != SVT_FUNC && ty != SVT_CFUNC && ty != SVT_OBJECT )
		res = FALSE;

	else if( ty == SVT_OBJECT )
	{
		sgs_VarObj* O = sgs_GetObjectData( C, 0 );
		OBJECT_HAS_IFACE( res, O, SOP_CALL )
	}
	else
		res = TRUE;

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_type_is_switch( SGS_CTX )
{
	int res, ty = sgs_ItemType( C, 0 );
	EXPECT_ONEARG( is_switch )

	if( ty == SVT_FUNC || ty == SVT_CFUNC )
		res = FALSE;

	else if( ty == SVT_STRING )
		res = sgs_IsNumericString(
			sgs_GetStringPtr( C, 0 ), sgs_GetStringSize( C, 0 ) );

	else if( ty == SVT_OBJECT )
	{
		sgs_VarObj* O = sgs_GetObjectData( C, 0 );
		OBJECT_HAS_IFACE( res, O, SOP_TOBOOL )
	}
	else
		res = TRUE;

	sgs_PushBool( C, res );
	return 1;
}

static int sgsstd_type_is_printable( SGS_CTX )
{
	int res, ty = sgs_ItemType( C, 0 );
	EXPECT_ONEARG( is_printable )

	if( ty == SVT_NULL || ty == SVT_FUNC || ty == SVT_CFUNC )
		res = FALSE;

	else if( ty == SVT_OBJECT )
	{
		sgs_VarObj* O = sgs_GetObjectData( C, 0 );
		OBJECT_HAS_IFACE( res, O, SOP_TOSTRING )
	}
	else
		res = TRUE;

	sgs_PushBool( C, res );
	return 1;
}


#define FN( x ) { #x, sgsstd_##x }

static const sgs_RegFuncConst t_fconsts[] =
{
	FN( type_get ), FN( typeof ), FN( type_cast ),
	FN( type_is_numeric ), FN( type_is_callable ),
	FN( type_is_switch ), FN( type_is_printable ),
};

static const sgs_RegIntConst t_iconsts[] =
{
	{ "TYPE_NULL", SVT_NULL },
	{ "TYPE_BOOL", SVT_BOOL },
	{ "TYPE_INT", SVT_INT },
	{ "TYPE_REAL", SVT_REAL },
	{ "TYPE_STRING", SVT_STRING },
	{ "TYPE_FUNC", SVT_FUNC },
	{ "TYPE_CFUNC", SVT_CFUNC },
	{ "TYPE_OBJECT", SVT_OBJECT },
	{ "TYPE_COUNT", SVT__COUNT },
};


SGSRESULT sgs_LoadLib_Type( SGS_CTX )
{
	int ret;
	ret = sgs_RegIntConsts( C, t_iconsts, ARRAY_SIZE( t_iconsts ) );
	if( ret != SGS_SUCCESS ) return ret;
	ret = sgs_RegFuncConsts( C, t_fconsts, ARRAY_SIZE( t_fconsts ) );
	return ret;
}

